# PLATFORM

RoboSeq runs on **Dinghy 0.4**, imported as source (the first commit of this repo, unchanged).

- upstream: https://github.com/zvenson/dinghy
- commit: `22acb9165ebc8baa712af3aff8c507e230c1e028` (2026-10-23, "Dinghy 0.4")
- and upstream's `f77bf032` (2026-11-03): `firmware/hal/fm1_flash_map.h` accepts the UC25HQ80 (`0xB36014`) of
  FM-1s sold since autumn 2026 beside the Puya `0x856014` (without it the update loader refuses to install on them);
  `tests/roboseq/flash_id_test.c` holds it
- Dinghy's docs stay in `docs/` (`API.md`, `RULES.md`, `MAKE-IT-YOURS.md`, `SMK37.md`); its README is
  `docs/DINGHY_README.md` (the toolchain, the SDK, the host mode)

## What differs from upstream

Kept small, so a later Dinghy can be merged by diffing these files.

| file | change |
| --- | --- |
| `firmware/src/dinghy.c` | renamed `roboseq.c`: the app; `APP_NAME "ROBOSEQ"`, `STORE_MAGIC` "RSEQ" |
| `tools/build.py` | compiles `roboseq.c`; outputs `roboseq.*`; identity `FM-1_96N`; loader screen "ROBOSEQ" in mint `0x8FFA`; data family `ROBOSEQ` |
| `build.sh` | a comment |
| `tests/run_host.sh` | builds `roboseq.c` as `build/host/roboseq` |
| `tests/run_tests.sh` | `roboseq.*` names; the SMK-37 packages' identities `_96N` |
| `tests/install_test.py` | `roboseq.*` names |
| `README.md` | moved to `docs/DINGHY_README.md`; RoboSeq's README in its place |
| `.github/workflows/release.yml` | replaced by `build.yml` (zvenson/fm1-build, as SkurkyMello) |

Nothing in `firmware/hal/`, `firmware/loader/`, `firmware/app.ld`, `platform.c` or `tests/host.c` is changed but
upstream's own `f77bf032` above.

## Updating Dinghy

1. Diff the new upstream against the commit above for the imported paths.
2. Apply upstream changes; re-apply the table above.
3. `make test`, CI's firmware job, a boot in fm1-emu.
4. Update the commit here.
