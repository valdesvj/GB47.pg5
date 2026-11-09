/*
 * FPS47 for the SwissMicros DM42n (DMCP5 native program, .pg5): how fast can the LCD show moving pictures?
 * (C) 2026 Victor Valdes (valdes.vj@gmail.com) - GPL-3.0-or-later
 *
 * The question before a Game Boy (or C64) emulator: how many whole frames a second the DM42n's 400x240 LCD can
 * take, and how long the drawing itself takes. Seven tests, about 3 seconds each:
 *
 *   1 FULL forced    all 240 lines change (inverted in the buffer), lcd_forced_refresh
 *   2 FULL fill      the whole screen black / white with lcd_fill_rect, lcd_refresh (DMCP's own way)
 *   3 GB lines       a Game Boy frame (160x144 at 1.5x = 240x216, 4 greys dithered) written straight into the
 *                    line buffer, lcd_refresh_lines on its 216 lines        <- the emulator's way
 *   4 GB forced      the same frame, lcd_forced_refresh (all 240 lines)
 *   5 GB rects       the same frame drawn with lcd_fill_rect runs (as NAV47 and TERM47 draw), lcd_refresh
 *   6 GB draw only   the drawing alone, no refresh: the CPU's part
 *   7 lines only     lcd_refresh_lines of 216 unchanged lines: the LCD's part
 *
 * Each frame waits for the LCD (lcd_refresh_wait), so the numbers are what the eye gets. The results show on the
 * screen and go to /FPS47/RESULT.TXT (copy it over USB). Run once on battery (80 MHz) and once on USB power
 * (160 MHz). F1 (WATCH) runs test 3 without end, with the frame rate on the left: look for smearing.
 *
 * The line buffer's layout (byte and bit order, which value is ink) is learnt at start by setting single
 * pixels, as Chess 47's and NAV47's screen mirror do, so no undocumented detail is assumed.
 */
#include <main.h>
#include <dmcp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GBW 160
#define GBH 144
#define OX  80                              /* the Game Boy picture on the screen: 240x216 at (80, 12) */
#define OY  12
#define OW  240
#define OH  216

/* ------------------------------------------------------------------ */
/*  The line buffer's layout                                          */
/* ------------------------------------------------------------------ */
static uint8_t xbyte[LCD_X];                /* pixel x -> byte in lcd_line_addr(y), and its bit */
static uint8_t xbit[LCD_X];
static int ink;                             /* the bit value of a black pixel */
static int learnt;

static int find_bit(int x, int y, int *byte, int *bit, int *val) {
  uint8_t a[LCD_LINE_BUF_SIZE], b[LCD_LINE_BUF_SIZE];
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 1);
  memcpy(a, lcd_line_addr(y), sizeof a);
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 0);
  memcpy(b, lcd_line_addr(y), sizeof b);
  for (int i = 0; i < (int)sizeof a; i++) {
    uint8_t d = (uint8_t)(a[i] ^ b[i]);
    if (!d) continue;
    for (int k = 0; k < 8; k++)
      if (d == (1u << k)) { *byte = i; *bit = k; *val = (a[i] >> k) & 1; return 1; }
    return 0;
  }
  return 0;
}

static void learn(void) {
  int y = LCD_Y - 1, byte, bit, val;
  learnt = 1;
  for (int x = 0; x < LCD_X; x++) {          /* every x: no guess about the order */
    if (!find_bit(x, y, &byte, &bit, &val)) { learnt = 0; return; }
    xbyte[x] = (uint8_t)byte;
    xbit[x] = (uint8_t)(1u << bit);
    if (x == 0) ink = val;
    else if (val != ink) { learnt = 0; return; }
  }
  lcd_clear_buf();
}

/* ------------------------------------------------------------------ */
/*  A Game Boy frame: 160x144, greys 0..3, moving                     */
/* ------------------------------------------------------------------ */
static uint8_t (*gb)[GBW];                  /* GBH rows, on the heap (8 KB of static RAM) */

static void make_gb(int t) {
  for (int y = 0; y < GBH; y++)
    for (int x = 0; x < GBW; x++) {
      int g;
      int sx = x + t, sy = y;                                   /* the background scrolls left */
      if (y >= 120) g = ((sx >> 3) + (y >> 3)) & 1 ? 2 : 1;      /* the ground: tiles */
      else g = ((sx / 24) & 1) && y > 60 + ((sx / 24) % 5) * 8 ? 3 : 0;   /* hills */
      int bx = 40 + (t * 2) % 100, by = 60 + ((t * 3) % 40);    /* a ball moving over it */
      int dx = x - bx, dy = y - by;
      if (dx * dx + dy * dy < 81) g = dx * dx + dy * dy < 25 ? 3 : 2;
      (void)sy;
      gb[y][x] = (uint8_t)g;
    }
}

/* 4 greys as 2x2 patterns: 0 white, 1 one dot, 2 checker, 3 black */
static inline int dither(int g, int x, int y) {
  static const uint8_t pat[4] = { 0x0, 0x1, 0x9, 0xf };          /* bits: (0,0) (1,0) (0,1) (1,1) */
  return (pat[g] >> ((x & 1) | ((y & 1) << 1))) & 1;
}

static uint8_t sx_of[OW], sy_of[OH];

/* straight into the line buffer */
static void draw_gb_direct(void) {
  for (int y = 0; y < OH; y++) {
    uint8_t *p = lcd_line_addr(OY + y);
    const uint8_t *row = gb[sy_of[y]];
    for (int x = 0; x < OW; x++) {
      int X = OX + x;
      int on = dither(row[sx_of[x]], X, OY + y);
      if (on == ink) p[xbyte[X]] |= xbit[X]; else p[xbyte[X]] &= (uint8_t)~xbit[X];
    }
  }
}

/* with lcd_fill_rect: white background, a rectangle for each run of black pixels */
static void draw_gb_rects(void) {
  lcd_fill_rect(OX, OY, OW, OH, 0);
  for (int y = 0; y < OH; y++) {
    const uint8_t *row = gb[sy_of[y]];
    int x = 0;
    while (x < OW) {
      while (x < OW && !dither(row[sx_of[x]], OX + x, OY + y)) x++;
      int s = x;
      while (x < OW && dither(row[sx_of[x]], OX + x, OY + y)) x++;
      if (x > s) lcd_fill_rect((uint32_t)(OX + s), (uint32_t)(OY + y), (uint32_t)(x - s), 1, 1);
    }
  }
}

static void invert_all(void) {
  for (int y = 0; y < LCD_Y; y++) {
    uint8_t *p = lcd_line_addr(y);
    for (int x = 0; x < LCD_X; x += 8) {                       /* each byte once: every 8th x is enough */
      uint8_t m = 0;
      for (int k = 0; k < 8; k++) if (xbyte[x + k] == xbyte[x]) m |= xbit[x + k];
      p[xbyte[x]] ^= m;
    }
  }
}

/* ------------------------------------------------------------------ */
/*  Text                                                              */
/* ------------------------------------------------------------------ */
static void text(int x, int y, const char *s, int inv) {
  t20->inv = inv;
  lcd_setXY(t20, x, y);
  lcd_writeText(t20, s);
  t20->inv = 0;
}

/* a title bar: white text on black across the screen */
static void bar(const char *s) {
  lcd_fill_rect(0, 0, LCD_X, 22, 1);
  text(4, 1, s, 1);
}

static void show(void) { lcd_refresh(); lcd_refresh_wait(); }

/* ------------------------------------------------------------------ */
/*  The tests                                                         */
/* ------------------------------------------------------------------ */
typedef struct { const char *name; int frames; uint32_t ms, draw_ms, lcd_ms; } Result;
enum { T_FULL_FORCED, T_FULL_FILL, T_GB_LINES, T_GB_FORCED, T_GB_RECTS, T_GB_DRAW, T_LINES, T_CNT };
static const char *TNAME[T_CNT] = { "FULL forced", "FULL fill", "GB lines", "GB forced", "GB rects",
                                    "GB draw only", "lines only" };
static Result res[T_CNT];
#define RUN_MS 3000

static int aborted(void) {
  int k = key_empty() ? -1 : key_pop();
  return k == KEY_EXIT;
}

/* one frame of a test: the drawing, then the LCD; the times added to r */
static void frame(int t, int n, Result *r) {
  uint32_t a = sys_current_ms();
  switch (t) {
  case T_FULL_FORCED: invert_all(); break;
  case T_FULL_FILL: lcd_fill_rect(0, 0, LCD_X, LCD_Y, n & 1); break;
  case T_GB_LINES: case T_GB_FORCED: case T_GB_DRAW: make_gb(n); draw_gb_direct(); break;
  case T_GB_RECTS: make_gb(n); draw_gb_rects(); break;
  case T_LINES: break;
  }
  uint32_t b = sys_current_ms();
  switch (t) {
  case T_FULL_FORCED: case T_GB_FORCED: lcd_forced_refresh(); lcd_refresh_wait(); break;
  case T_FULL_FILL: case T_GB_RECTS: lcd_refresh(); lcd_refresh_wait(); break;
  case T_GB_LINES: case T_LINES: lcd_refresh_lines(OY, OH); lcd_refresh_wait(); break;
  case T_GB_DRAW: break;
  }
  uint32_t c = sys_current_ms();
  r->draw_ms += b - a;
  r->lcd_ms += c - b;
  r->frames++;
}

static int run_test(int t) {
  Result *r = &res[t];
  memset(r, 0, sizeof *r);
  r->name = TNAME[t];
  lcd_clear_buf();
  char s[48];
  sprintf(s, "Test %d/%d: %s   (EXIT stops)", t + 1, T_CNT, TNAME[t]);
  bar(s);
  if (t == T_LINES) { make_gb(0); draw_gb_direct(); }
  lcd_forced_refresh(); lcd_refresh_wait();
  uint32_t start = sys_current_ms();
  int n = 0;
  while (sys_current_ms() - start < RUN_MS || r->frames < 10) {
    frame(t, n++, r);
    if (aborted()) return 0;
    if (r->frames >= 2000) break;
  }
  r->ms = sys_current_ms() - start;
  reset_auto_off();
  return 1;
}

static void fmt_fps(char *s, const Result *r) {             /* frames a second with one decimal, no float printf */
  if (!r->ms) { strcpy(s, " >999"); return; }                 /* too fast to time */
  uint32_t f10 = (uint32_t)r->frames * 10000u / r->ms;
  sprintf(s, "%3lu.%lu", (unsigned long)(f10 / 10), (unsigned long)(f10 % 10));
}

static void ms_per(char *s, uint32_t total, int frames) {   /* ms a frame with one decimal */
  uint32_t m10 = frames ? total * 10u / (uint32_t)frames : 0;
  sprintf(s, "%3lu.%lu", (unsigned long)(m10 / 10), (unsigned long)(m10 % 10));
}

static void save_results(void) {
  if (!sys_disk_ok()) return;
  char buf[1024], a[16], b[16], c[16];
  int n = sprintf(buf, "FPS47 %s  DM42n LCD frame rate\r\nUSB power: %s   power voltage: %lu\r\n"
                       "layout learnt: %s (ink=%d)\r\n\r\n%-13s %7s %7s %8s %8s\r\n",
                  PROGRAM_VERSION, usb_powered() ? "yes (160 MHz)" : "no (battery, 80 MHz)",
                  (unsigned long)read_power_voltage(), learnt ? "yes" : "NO", ink,
                  "test", "fps", "frames", "draw ms", "lcd ms");
  for (int t = 0; t < T_CNT; t++) {
    fmt_fps(a, &res[t]); ms_per(b, res[t].draw_ms, res[t].frames); ms_per(c, res[t].lcd_ms, res[t].frames);
    n += sprintf(buf + n, "%-13s %7s %7d %8s %8s\r\n", TNAME[t], a, res[t].frames, b, c);
  }
  sys_disk_write_enable(1);
  check_create_dir("/FPS47");
  const char *name = usb_powered() ? "/FPS47/RESULT_USB.TXT" : "/FPS47/RESULT_BAT.TXT";
  FIL f;
  UINT bw;
  if (f_open(&f, name, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, buf, (UINT)n, &bw); f_close(&f); }
  sys_disk_write_enable(0);
}

static void show_results(int done) {
  char s[64], a[16], b[16], c[16];
  lcd_clear_buf();
  sprintf(s, "FPS47 %s   %s", PROGRAM_VERSION, usb_powered() ? "USB power, 160 MHz" : "battery, 80 MHz");
  bar(s);
  int y = 24;
  text(4, y, "test", 0); text(150, y, "fps", 0); text(220, y, "draw", 0); text(310, y, "lcd ms", 0);
  y += 22;
  for (int t = 0; t < T_CNT; t++) {
    if (!res[t].frames) continue;
    fmt_fps(a, &res[t]); ms_per(b, res[t].draw_ms, res[t].frames); ms_per(c, res[t].lcd_ms, res[t].frames);
    text(4, y, TNAME[t], 0); text(140, y, a, 0); text(215, y, b, 0); text(310, y, c, 0);
    y += 20;
  }
  if (!done) text(4, y + 2, "Stopped (EXIT)", 0);
  else text(4, y + 2, usb_powered() ? "Saved: /FPS47/RESULT_USB.TXT" : "Saved: /FPS47/RESULT_BAT.TXT", 0);
  static const char *K[6] = { "WATCH", "", "", "", "RUN", "OFF" };
  lcd_draw_menu_keys(K);
  show();
}

static void intro(void) {
  lcd_clear_buf();
  bar("FPS47: how fast is the DM42n's LCD?");
  static const char *L[] = {
    "Seven tests, about 3 s each (21 s).",
    "Run on battery, then on USB power.",
    "",
    "RUN (F5) or ENTER: start the tests",
    "WATCH (F1): the Game Boy picture",
    "   moving, frame rate on the left",
    "EXIT: stop a test     OFF (F6): off",
  };
  for (unsigned i = 0; i < sizeof L / sizeof *L; i++) text(4, 26 + (int)i * 22, L[i], 0);
  if (!learnt) text(4, 186, "LCD layout NOT learnt: direct tests off", 0);
  static const char *K[6] = { "WATCH", "", "", "", "RUN", "OFF" };
  lcd_draw_menu_keys(K);
  show();
}

/* the Game Boy picture without end, as an emulator would draw it */
static void watch(void) {
  Result r;
  memset(&r, 0, sizeof r);
  lcd_clear_buf();
  lcd_forced_refresh(); lcd_refresh_wait();
  uint32_t t0 = sys_current_ms(), last = t0;
  int n = 0, shown = 0;
  for (;;) {
    frame(T_GB_LINES, n++, &r);
    uint32_t now = sys_current_ms();
    if (now - last >= 1000) {                                 /* the frame rate, once a second */
      char s[16], a[16];
      Result w = r;
      w.frames = r.frames - shown;
      w.ms = now - last;
      fmt_fps(a, &w);
      sprintf(s, "%s", a);
      lcd_fill_rect(0, 30, OX - 4, 70, 0);                  /* the left margin, beside the picture */
      text(4, 30, "fps", 0);
      text(4, 52, s, 0);
      text(4, 74, "EXIT", 0);
      lcd_refresh_lines(30, 70);
      lcd_refresh_wait();
      shown = r.frames;
      last = now;
      reset_auto_off();
    }
    if (aborted()) break;
  }
}

void _exit(int code) { (void)code; sys_reset(); for (;;) {} }

void program_main(void) {
  switch_usb_powered_freq();
  gb = malloc(GBH * GBW);
  if (!gb) {
    lcd_clear_buf();
    text(4, 40, "FPS47: not enough memory", 0);
    show();
    for (;;) wait_for_key_press();
  }
  for (int i = 0; i < OW; i++) sx_of[i] = (uint8_t)(i * 2 / 3);
  for (int i = 0; i < OH; i++) sy_of[i] = (uint8_t)(i * 2 / 3);
  learn();
  intro();
  int have = 0;

  for (;;) {
    if (key_empty() && !ST(STAT_PGM_END)) {
      CLR_ST(STAT_RUNNING);
      sys_sleep();
    }
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) {
      if (!ST(STAT_SUSPENDED)) {
        lcd_set_buf_cleared(0);
        draw_power_off_image(1);
        LCD_power_off(0);
        SET_ST(STAT_SUSPENDED);
        SET_ST(STAT_OFF);
      }
      continue;
    }
    SET_ST(STAT_RUNNING);
    CLR_ST(STAT_SUSPENDED);
    if (ST(STAT_OFF)) {
      LCD_power_on();
      rtc_wakeup_delay();
      CLR_ST(STAT_OFF);
      if (have) show_results(1); else intro();
    }
    if (ST(STAT_POWER_CHANGE)) {                               /* the cable in or out: the clock changes */
      switch_usb_powered_freq();
      CLR_ST(STAT_POWER_CHANGE);
    }

    int key = key_pop();
    if (key <= 0 || key == KEY_DOUBLE_RELEASE) continue;
    if (key == KEY_F6 || key == KEY_EXIT) { SET_ST(STAT_PGM_END); continue; }
    if (key == KEY_F1 && learnt) { watch(); if (have) show_results(1); else intro(); continue; }
    if (key == KEY_F5 || key == KEY_ENTER) {
      switch_usb_powered_freq();
      int done = 1;
      memset(res, 0, sizeof res);
      for (int t = 0; t < T_CNT && done; t++) {
        if (!learnt && t != T_FULL_FILL && t != T_GB_RECTS) continue;
        done = run_test(t);
      }
      if (done) save_results();
      have = 1;
      show_results(done);
    }
  }
}
