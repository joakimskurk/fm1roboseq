#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# RoboSeq's own host tests (no FM-1, no toolchain, seconds): the C model against reference/roboseq.html's golden
# traces (tests/golden, made by tools/trace/gen.mjs), its editing, randomizers and undo; plain and with UBSan + ASan
# where the compiler has them.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/test
CC=${CC:-cc}
SRC="tests/roboseq/trace_test.c firmware/src/rs_model.c firmware/src/rs_chance.c firmware/src/rs_score.c"
$CC -std=c11 -O2 -Wall -Wextra -Werror -Ifirmware/src -o build/test/trace_test $SRC -lm
build/test/trace_test tests/golden
$CC -std=c11 -O2 -Wall -Wextra -Werror -Ifirmware/src -o build/test/edit_test tests/roboseq/edit_test.c firmware/src/rs_model.c firmware/src/rs_chance.c firmware/src/rs_score.c
build/test/edit_test
if $CC -fsanitize=undefined,address -x c -o build/test/san_probe - <<'C' 2>/dev/null; then
int main(void) { return 0; }
C
    $CC -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined,address -fno-sanitize-recover=undefined \
        -Ifirmware/src -o build/test/trace_test_san $SRC -lm
    build/test/trace_test_san tests/golden > /dev/null && echo "trace_test: UBSan + ASan clean"
    $CC -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined,address -fno-sanitize-recover=undefined \
        -Ifirmware/src -o build/test/edit_test_san tests/roboseq/edit_test.c firmware/src/rs_model.c firmware/src/rs_chance.c firmware/src/rs_score.c
    build/test/edit_test_san > /dev/null && echo "edit_test: UBSan + ASan clean"
fi
