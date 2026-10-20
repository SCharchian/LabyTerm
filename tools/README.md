# tools/

Developer scripts. None of them ship with the game.

| Script | Use |
|---|---|
| `build.sh [--target X]` | configure (Release) and build into `build/`; `--target labyterm` builds only the game |
| `sanitize.sh` | build with AddressSanitizer and UBSan in `build-san/` and run `ctest` |
| `gen_tuning.py` | generate `src/core/TuningParams.hpp` from `data/gameplay.json` (run after adding or removing a knob) |
| `balance_report.py`, `balance_survival_goals.txt` | run the balance bot (`tests/balance_bot`) over many seeds and write `build/balance/REPORT.md`; the text file holds the survival targets |
| `art.sh`, `theme_shots.sh`, `make_icons.py` | render the procedural art, per-theme screenshots, and the icons |
| `measure_cpu.sh`, `record_cpu.py` | measure a running game's CPU use (idle goal: about 0%) |
| `check_version.sh` | fail unless `CMakeLists.txt`, the plugin manifest, the PKGBUILD and the top CHANGELOG entry agree |
| `test_release_compile.sh` | syntax-check the sources with the debug hotkeys compiled out |
| `serve_test_repos.sh` | serve the committed repo over `git://` to a test machine (release rehearsals) |
