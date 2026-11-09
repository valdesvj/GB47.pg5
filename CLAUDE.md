# GB47: working rules

- Work on a new branch; Victor creates and merges the PRs. Never force-push or rewrite history.
- Commit author: "Victor Valdes" <valdesvj@users.noreply.github.com>.
- The same base as Chess 47, NAV47 and TERM47 (dmcp/, tools/pg5tool.py, test/sim): keep them in step.
- Tools (tests, probes) live in tools/<name>/ with their own main.h, and build with their own make target.
- DMCP5: 8 KB of static RAM. Big buffers go on the heap; check .bss in arm-none-eabi-size.
- Check every screen in the simulator before committing. Its timings mean nothing: speed is measured on the DM42n.
- No Nintendo code, BIOS or games in the repo.
- Ask before anything that can't be undone. Pull requests only for gb47.pg5.
