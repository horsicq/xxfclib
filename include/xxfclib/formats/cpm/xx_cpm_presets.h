/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Explicit, independently tested classic CP/M disk definitions.
 *
 * These named constructors select a precise native geometry/DPB/permutation
 * for the existing CP/M reader. Raw CP/M media do not contain a reliable
 * universal geometry signature; callers must select a preset deliberately.
 * Other cpmtools diskdefs and controller-specific image containers are not
 * implied. Apple DO/PO describe different physical 16-sector orders.
 */
#ifndef XXFCLIB_FORMAT_CPM_PRESETS_H
#define XXFCLIB_FORMAT_CPM_PRESETS_H
#include "xxfclib/formats/cpm/xx_cpm.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_cpm_preset_e {
    XX_CPM_PRESET_APPLE_DO = 1,
    XX_CPM_PRESET_APPLE_PO = 2,
    XX_CPM_PRESET_PCW180 = 3,
    XX_CPM_PRESET_CPC_SYSTEM = 4,
    XX_CPM_PRESET_CPC_DATA = 5,
    XX_CPM_PRESET_CF2DD = 6,
    XX_CPM_PRESET_ALPHA = 7,
    XX_CPM_PRESET_SDCARD = 8,
    XX_CPM_PRESET_PC1_2M = 9,
    XX_CPM_PRESET_CPM86_144FEAT = 10,
    XX_CPM_PRESET_P112 = 11,
    XX_CPM_PRESET_P112_OLD = 12,
    XX_CPM_PRESET_NIGDOS = 13,
    XX_CPM_PRESET_EPSQX10 = 14,
    XX_CPM_PRESET_IBM_8SS = 15,
    XX_CPM_PRESET_ELECTROGLAS = 16,
    XX_CPM_PRESET_IBMPC_514DS = 17,
    /* Generated stock diskdefs presets follow this line. */
    /* BEGIN GENERATED CP/M PRESET IDS */
    XX_CPM_PRESET_GIDE_CFA = 18,
    XX_CPM_PRESET_ATTWP = 19,
    XX_CPM_PRESET_KPII = 20,
    XX_CPM_PRESET_KPIV = 21,
    XX_CPM_PRESET_INTERAK = 22,
    XX_CPM_PRESET_FDD3000 = 23,
    XX_CPM_PRESET_FDD3000_2 = 24,
    XX_CPM_PRESET_1715 = 25,
    XX_CPM_PRESET_17153 = 26,
    XX_CPM_PRESET_SCP624 = 27,
    XX_CPM_PRESET_SCP640 = 28,
    XX_CPM_PRESET_SCP780 = 29,
    XX_CPM_PRESET_SCP800 = 30,
    XX_CPM_PRESET_Z9001 = 31,
    XX_CPM_PRESET_V1050 = 32,
    XX_CPM_PRESET_MICROBEE40 = 33,
    XX_CPM_PRESET_DREAMDISK80 = 34,
    XX_CPM_PRESET_ICL_COMET_525SS = 35,
    XX_CPM_PRESET_Z80PACK_HD = 36,
    XX_CPM_PRESET_BW12 = 37,
    XX_CPM_PRESET_BW14 = 38,
    XX_CPM_PRESET_NSFD = 39,
    XX_CPM_PRESET_NSHD4 = 40,
    XX_CPM_PRESET_NSHD8 = 41,
    XX_CPM_PRESET_MDSAD175 = 42,
    XX_CPM_PRESET_OSBORNE4 = 43,
    XX_CPM_PRESET_LOBO2 = 44,
    XX_CPM_PRESET_LOBO3 = 45,
    XX_CPM_PRESET_DEC_PRO = 46,
    XX_CPM_PRESET_PMC101 = 47,
    XX_CPM_PRESET_HEASSDD8 = 48,
    XX_CPM_PRESET_ZEN7 = 49,
    XX_CPM_PRESET_ZEN8 = 50,
    XX_CPM_PRESET_ZENA = 51,
    XX_CPM_PRESET_MORSDDD = 52,
    XX_CPM_PRESET_OSB1SSSD = 53,
    XX_CPM_PRESET_ALTDSDD = 54,
    XX_CPM_PRESET_TRSOMSSSD = 55,
    XX_CPM_PRESET_MEMOTECH_TYPE03 = 56,
    XX_CPM_PRESET_MEMOTECH_TYPE07 = 57,
    XX_CPM_PRESET_MEMOTECH_TYPE43 = 58,
    XX_CPM_PRESET_MEMOTECH_TYPE47 = 59,
    XX_CPM_PRESET_MEMOTECH_TYPE4B = 60,
    XX_CPM_PRESET_MEMOTECH_TYPE4F = 61,
    XX_CPM_PRESET_MEMOTECH_TYPE50 = 62,
    XX_CPM_PRESET_MEMOTECH_TYPE51 = 63,
    XX_CPM_PRESET_MEMOTECH_TYPE51_ITALY = 64,
    XX_CPM_PRESET_MEMOTECH_TYPE51_S2R64 = 65,
    XX_CPM_PRESET_MEMOTECH_TYPE51_S2R = 66,
    XX_CPM_PRESET_RM_SD = 67,
    XX_CPM_PRESET_RM_DD = 68,
    XX_CPM_PRESET_RM_QD = 69,
    XX_CPM_PRESET_AMP1 = 70,
    XX_CPM_PRESET_AMP2 = 71,
    XX_CPM_PRESET_AMP3 = 72,
    XX_CPM_PRESET_AMP4 = 73,
    XX_CPM_PRESET_8MEGALTAIRSIMH = 74,
    XX_CPM_PRESET_SIMH = 75,
    XX_CPM_PRESET_SVI707 = 76,
    XX_CPM_PRESET_MDS_DD = 77,
    XX_CPM_PRESET_MDS_SD = 78,
    XX_CPM_PRESET_ZCNA_BOOT = 79,
    XX_CPM_PRESET_ZCNA_NONBOOT = 80,
    XX_CPM_PRESET_HP25 = 81,
    XX_CPM_PRESET_RC75X = 82,
    XX_CPM_PRESET_NC200CF = 83,
    XX_CPM_PRESET_NC200CPM = 84,
    /* END GENERATED CP/M PRESET IDS */
} xx_cpm_preset;
XXFC_API bool xx_cpm_geometry_preset(xx_cpm_geometry *, xx_cpm_preset);
XXFC_API bool xx_cpm_init_preset(xx_cpm *, xx_io_device *, int64_t, xx_cpm_preset);
XXFC_API xx_cpm *xx_cpm_create_preset(xx_io_device *, int64_t, xx_cpm_preset);
XXFC_API unsigned xx_cpm_preset_count(void);
XXFC_API const char *xx_cpm_preset_name(xx_cpm_preset);
XXFC_API bool xx_cpm_preset_from_name(const char *, xx_cpm_preset *);
#ifdef __cplusplus
}
#endif
#endif
