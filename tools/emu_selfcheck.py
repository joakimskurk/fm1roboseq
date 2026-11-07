#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# The model on the chip: rs_boot_check (rs_selfcheck() at boot, firmware/src/roboseq.c) read from fm1-emu's RAM dump,
# against the same fingerprint computed on this computer (build/test/trace_test); and the audio interrupt's worst
# block (rs_audio_us_worst), which must stay under half its 5805 us (docs/RULES.md); and how deep the two stacks went
# (build/app.ld's, zeroed at power-on: the untouched bottom is still zero), each under half too.
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
ld = open("build/app.ld").read()
deep = 0
for name, lo, top in (("main loop", "_ustack_lo", "_ustack_top"), ("interrupts", "_sstack_lo", "_sstack_top")):
    a, b = (int(re.search(sym + r" = (0x[0-9A-Fa-f]+);", ld).group(1), 16) - 0x1C00000 for sym in (lo, top))
    seg = mem[a:b]
    used = len(seg.lstrip(b"\0"))                   # (it grows down from the top)
    deep |= used >= len(seg) // 2
    print(f"stack, {name}: about {used} B of {len(seg)} used ({100 * used / len(seg):.0f} %; under half: "
          f"{'yes' if used < len(seg) // 2 else 'NO'})")
sys.exit(chip != host or worst >= 5805 // 2 or deep)
