#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# The model on the chip: rs_boot_check (rs_selfcheck() at boot, firmware/src/roboseq.c) read from fm1-emu's RAM dump,
# against the same fingerprint computed on this computer (build/test/trace_test); and the audio interrupt's worst
# block (rs_audio_us_worst), which must stay under half its 5805 us (docs/RULES.md).
#   tools/emu_selfcheck.py build/roboseq.dis build/emu/ram.bin build/test/trace_test
import re, struct, subprocess, sys
dis, ram, test = sys.argv[1:4]
text, mem = open(dis).read(), open(ram, "rb").read()


def word(sym):
    m = re.search(r"<" + sym + r" : ([0-9a-f]+) >", text)
    if not m:
        sys.exit(sym + " not in " + dis)
    return struct.unpack_from("<I", mem, int(m.group(1), 16) - 0x1C00000)[0]


chip = word("rs_boot_check")
worst = word("rs_audio_us_worst")
out = subprocess.run([test, "tests/golden"], capture_output=True, text=True).stdout
host = int(re.search(r"selfcheck: ([0-9a-f]+)", out).group(1), 16)
print(f"model on the chip {chip:08x}, on this computer {host:08x}: {'the same' if chip == host else 'DIFFERENT'}")
print(f"audio interrupt: the worst block {worst} us of 5805 ({100 * worst / 5805:.0f} %; under half: "
      f"{'yes' if worst < 5805 // 2 else 'NO'})")
sys.exit(chip != host or worst >= 5805 // 2)
