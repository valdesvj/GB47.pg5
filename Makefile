######################################
# GB47 - DMCP5 programs for the SwissMicros DM42n, on the way to a Game Boy emulator
# Based on the SwissMicros DMCP5_SDK Makefile (BSD-3), as Chess 47, NAV47 and TERM47
######################################
# make fps             tools/fps47: FPS47.pg5, the LCD frame-rate test -> build/fps/FPS47.pg5
# make fps-sim         its PC simulator (build/fps/sim); screens: make fps-shots -> build/fps/run/*.png

BUILD_DIR = build
PYTHON ?= python3

CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

# the DMCP5 interface the .pg5 asks for: 17 loads on DMCP5 3.57 and later (FPS47 needs nothing newer);
# 18 (DMCP5 3.58) only for the serial console, later in the emulators: make IFC=18
IFC ?= 17
C_DEFS   = -D__weak="__attribute__((weak))" -D__packed="__attribute__((__packed__))" -DPLATFORM_IFC_VER=$(IFC)
CPUFLAGS = -mthumb -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16
LDSCRIPT = stm32_program.ld

all: fps

# FPS47: the LCD frame-rate test (tools/fps47), its own program on the DMCP base
FPS_DIR = $(BUILD_DIR)/fps
FPS_CFLAGS = -std=gnu11 $(CPUFLAGS) $(C_DEFS) -Itools/fps47 -Idmcp -Wall -fdata-sections -ffunction-sections -O2 -g
fps:
	mkdir -p $(FPS_DIR)
	$(CC) $(FPS_CFLAGS) -c dmcp/sys/pgm_syscalls.c -o $(FPS_DIR)/pgm_syscalls.o
	$(CC) $(FPS_CFLAGS) -c tools/fps47/fps47.c -o $(FPS_DIR)/fps47.o
	$(AS) -c $(FPS_CFLAGS) dmcp/startup_pgm.s -o $(FPS_DIR)/startup_pgm.o
	$(CC) $(FPS_DIR)/*.o $(CPUFLAGS) -T$(LDSCRIPT) -Wl,-Map=$(FPS_DIR)/FPS47.map,--cref -Wl,--gc-sections \
	    -Wl,--wrap=_malloc_r -g -o $(FPS_DIR)/FPS47.elf
	$(OBJCOPY) --remove-section .qspi -O binary $(FPS_DIR)/FPS47.elf $(FPS_DIR)/FPS47_flash.bin
	$(OBJCOPY) --only-section   .qspi -O binary $(FPS_DIR)/FPS47.elf $(FPS_DIR)/FPS47_qspi.bin
	$(PYTHON) tools/pg5tool.py qspi $(FPS_DIR)/FPS47_qspi.bin tools/fps47/qspi_crc.h
	$(PYTHON) tools/pg5tool.py pg5 $(FPS_DIR)/FPS47_flash.bin $(FPS_DIR)/FPS47.pg5
	$(SIZE) $(FPS_DIR)/FPS47.elf

fps-sim:
	mkdir -p $(FPS_DIR)
	gcc -O2 -w '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' -DGB47_SIM \
	    -Itest/sim -Itools/fps47 -Isrc -o $(FPS_DIR)/sim test/sim/sim.c test/sim/sercon_sim.c tools/fps47/fps47.c

# the simulator's screens: the intro, the results, WATCH GB, WATCH ZX
fps-shots: fps-sim
	rm -rf $(FPS_DIR)/run && mkdir -p $(FPS_DIR)/run
	cd $(FPS_DIR)/run && ../sim SNAP:intro ENTER WAIT:200 SNAP:test1 WAIT:29000 SNAP:results F1 WAIT:2500 SNAP:watch EXIT F2 WAIT:2500 SNAP:watchzx EXIT
	for n in intro test1 results watch watchzx; do $(PYTHON) test/sim/render.py $(FPS_DIR)/run/$$n $(FPS_DIR)/run/$$n.png; done

clean:
	-rm -fR $(BUILD_DIR)

.PHONY: all fps fps-sim fps-shots clean
