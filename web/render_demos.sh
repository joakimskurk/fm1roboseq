#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Joakim (joakimskurk)
# The page's demos: the app itself on the host (tests/host.c), its sound as the FM-1 sends it over USB, into
# build/audio/<id>.wav (web/make_site.py encodes them and draws their spectra). Linux (CI); on a Mac: make site.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/audio
cc -O2 -w -DDINGHY_HOST -DAPP_VER='""' -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/audio/roboseq firmware/src/roboseq.c
for d in band original; do
    build/audio/roboseq "build/audio/$d" "tests/roboseq/demo-$d.txt" >/dev/null
    echo "demo   -> build/audio/$d.wav"
done
