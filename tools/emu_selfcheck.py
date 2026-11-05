#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# The model on the chip: rs_boot_check (rs_selfcheck() at boot, firmware/src/roboseq.c) read from fm1-emu's RAM dump,
# against the same fingerprint computed on this computer (build/test/trace_test).
#   tools/emu_selfcheck.py build/roboseq.dis build/emu/ram.bin build/test/trace_test
import re, struct, subprocess, sys
dis, ram, test = sys.argv[1:4]
m = re.search(r"<rs_boot_check : ([0-9a-f]+) >", open(dis).read())
if not m:
    sys.exit("rs_boot_check not in " + dis)
addr = int(m.group(1), 16)
chip = struct.unpack_from("<I", open(ram, "rb").read(), addr - 0x1C00000)[0]
out = subprocess.run([test, "tests/golden"], capture_output=True, text=True).stdout
host = int(re.search(r"selfcheck: ([0-9a-f]+)", out).group(1), 16)
print(f"model on the chip {chip:08x}, on this computer {host:08x}: {'the same' if chip == host else 'DIFFERENT'}")
sys.exit(chip != host)
