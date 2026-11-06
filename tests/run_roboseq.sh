#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# RoboSeq's own host tests (no FM-1, no toolchain, seconds): the C model against reference/roboseq.html's golden
# traces (tests/golden, made by tools/trace/gen.mjs), its editing, randomizers and undo; plain and with UBSan + ASan
# where the compiler has them.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/test
CC=${CC:-cc}
SRC="tests/roboseq/trace_test.c firmware/src/rs_model.c firmware/src/rs_chance.c firmware/src/rs_score.c firmware/src/rs_voice.c"
$CC -std=c11 -O2 -Wall -Wextra -Werror -Ifirmware/src -o build/test/trace_test $SRC -lm
build/test/trace_test tests/golden --voices
$CC -std=c11 -O2 -Wall -Wextra -Werror -Ifirmware/src -o build/test/edit_test tests/roboseq/edit_test.c firmware/src/rs_model.c firmware/src/rs_chance.c firmware/src/rs_score.c firmware/src/rs_voice.c
build/test/edit_test
$CC -std=c11 -O2 -Wall -Wextra -Werror -Ifirmware/src -o build/test/lead_test tests/roboseq/lead_test.c -lm
build/test/lead_test
$CC -std=c11 -O2 -Wall -Wextra -Werror -Ifirmware/src -o build/test/colour_test tests/roboseq/colour_test.c \
    firmware/src/rs_voice.c firmware/src/rs_score.c firmware/src/rs_model.c firmware/src/rs_chance.c -lm
build/test/colour_test
if $CC -fsanitize=undefined,address -x c -o build/test/san_probe - <<'C' 2>/dev/null; then
int main(void) { return 0; }
C
    $CC -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined,address -fno-sanitize-recover=undefined \
        -Ifirmware/src -o build/test/trace_test_san $SRC -lm
    build/test/trace_test_san tests/golden > /dev/null && echo "trace_test: UBSan + ASan clean"
    $CC -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined,address -fno-sanitize-recover=undefined \
        -Ifirmware/src -o build/test/edit_test_san tests/roboseq/edit_test.c firmware/src/rs_model.c firmware/src/rs_chance.c firmware/src/rs_score.c firmware/src/rs_voice.c
    build/test/edit_test_san > /dev/null && echo "edit_test: UBSan + ASan clean"
fi
# the C voices and mix against the HTML's Tone.Offline renders (tests/golden/html): where python3 has numpy
$CC -std=c11 -O2 -Wall -Wextra -Werror -Ifirmware/src -o build/test/parity_render tests/parity/render.c \
    firmware/src/rs_voice.c firmware/src/rs_score.c firmware/src/rs_model.c firmware/src/rs_chance.c -lm
if python3 -c "import numpy" 2>/dev/null; then
    python3 tools/tone-ref/mix_tables.py --check
    python3 tools/art/tiles.py --check
    python3 tools/art/robots.py --check
    python3 tools/art/icons.py --check
    python3 tools/art/splash.py --check
    python3 tools/tone-ref/compare.py --check > build/test/parity.txt || { cat build/test/parity.txt; exit 1; }
    echo "parity: every voice, the mix, the song, the limiter and the reverb within their tolerances (build/test/parity.txt)"
else
    echo "parity: skipped (python3 without numpy)"
fi
