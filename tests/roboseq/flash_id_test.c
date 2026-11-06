/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's flash chips (hal/fm1_flash_map.h, FL_JEDEC_OK): the app's storage and the update loader accept the Puya
 * P25Q80 (0x856014) and the UC25HQ80 of FM-1s sold since autumn 2026 (0xB36014; Dinghy f77bf032), and nothing else.
 * Without the second, RoboSeq's loader refuses to install on those units. */
#include <stdio.h>
#include "fm1_flash_map.h"
#if BOARD_SMK37
#error "the FM-1's chips: build without BOARD_SMK37"
#endif

int main(void)
{
    int bad = 0;
    bad += !FL_JEDEC_OK(0x856014u);             /* Puya P25Q80 */
    bad += !FL_JEDEC_OK(0xB36014u);             /* UC25HQ80 */
    bad += FL_JEDEC_OK(0xEB6014u);              /* the SMK-37's part: not on an FM-1 */
    bad += FL_JEDEC_OK(0u) || FL_JEDEC_OK(0xFFFFFFu) || FL_JEDEC_OK(0x856015u) || FL_JEDEC_OK(0xB36015u);
    printf("flash_id_test: %s\n", bad ? "FAIL" : "the FM-1's two flash chips accepted, nothing else");
    return bad != 0;
}
