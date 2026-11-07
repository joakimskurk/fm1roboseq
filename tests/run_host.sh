#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/roboseq.c on this computer (tests/host.c): build/host/roboseq.wav and the screens as PNG; without a
# script also the playability test (tests/roboseq/p4_test.c).
#   tests/run_host.sh [script]               your own script (tests/host.c says what a line can do)
#   BOARD=smk37 tests/run_host.sh [script]   the SMK-37 build (velocity, pads, faders, wheels, outer keys)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
N=roboseq; D=
if [ "${BOARD:-fm1}" = smk37 ]; then N=roboseq-smk37; D=-DBOARD_SMK37=1; fi
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST $D -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/$N firmware/src/roboseq.c
build/host/$N build/host/$N ${1:+"$1"}
if [ -z "$1" ] && [ -z "$D" ]; then             # P4 (ROADMAP): the playability rules, page by page, on the app itself
    cc -O1 -Wall -Wno-unused-function -DDINGHY_HOST -DRS_P4_TEST -DAPP_VER="\"dev\"" -Ifirmware/hal -Ifirmware/src -Ifirmware/gen \
        -o build/host/p4_test firmware/src/roboseq.c
    build/host/p4_test
fi
python3 - "$N" <<'PY'
import sys
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob(sys.argv[1] + "-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
