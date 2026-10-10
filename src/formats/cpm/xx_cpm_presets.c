/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original DPB mapping of selected cpmtools 2.23 public diskdefs.
 */
#include "xxfclib/formats/cpm/xx_cpm_presets.h"
#include "xxfclib/memory/xx_memory.h"
#include <string.h>
static void cpm_base(xx_cpm_geometry *g, uint32_t tracks, uint16_t sectors, uint16_t sector_size, uint16_t block_size, uint16_t directory_entries, uint16_t boot_tracks,
                     xx_cpm_version version)
{
    uint32_t blocks;
    uint16_t dir_blocks;
    unsigned i;
    xx_mem_zero(g, sizeof(*g));
    g->tracks = tracks;
    g->physical_sectors_per_track = sectors;
    g->physical_sector_size = sector_size;
    g->spt = (uint16_t)(sectors * (sector_size / 128U));
    g->off = boot_tracks;
    g->drm = (uint16_t)(directory_entries - 1U);
    g->version = version;
    g->length_mode = version == XX_CPM_VERSION_3 ? XX_CPM_LENGTH_LAST_RECORD_USED : XX_CPM_LENGTH_RECORDS;
    for (i = block_size / 128U; i > 1U; i >>= 1U) ++g->bsh;
    g->blm = (uint8_t)(block_size / 128U - 1U);
    blocks = (tracks - boot_tracks) * sectors * sector_size / block_size;
    g->dsm = (uint16_t)(blocks - 1U);
    g->exm = (uint8_t)(((g->dsm >= 256U ? 8U : 16U) * block_size / 16384U) - 1U);
    dir_blocks = (uint16_t)((directory_entries * 32U + block_size - 1U) / block_size);
    for (i = 0U; i < dir_blocks; ++i) {
        if (i < 8U) g->al0 |= (uint8_t)(0x80U >> i);
        else g->al1 |= (uint8_t)(0x80U >> (i - 8U));
    }
    for (i = 0U; i < sectors; ++i) g->sector_order[i] = (uint16_t)i;
}
typedef struct xx_cpm_diskdef_preset_s {
    const char *name;
    uint32_t tracks;
    uint16_t sectors, sector_size, block_size, directory_entries;
    uint16_t boot_tracks, directory_blocks;
    xx_cpm_version version;
    uint8_t exm;
    uint16_t order[256];
} xx_cpm_diskdef_preset;
/* BEGIN GENERATED CP/M DISKDEFS TABLE */
static const xx_cpm_diskdef_preset cpm_diskdefs[] = {
    {"gide-cfa", 1000U, 16U, 512U, 4096U, 1024U, 2U, 8U, XX_CPM_VERSION_3, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U}},
    {"attwp", 80U, 32U, 256U, 2048U, 128U, 1U, 2U, XX_CPM_VERSION_22, 0U, {0U,  2U,  4U,  6U,  8U,  10U, 12U, 14U, 1U,  3U,  5U,  7U,  9U,  11U, 13U, 15U,
                                                                           16U, 18U, 20U, 22U, 24U, 26U, 28U, 30U, 17U, 19U, 21U, 23U, 25U, 27U, 29U, 31U}},
    {"kpii", 40U, 10U, 512U, 1024U, 64U, 1U, 4U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U}},
    {"kpiv", 80U, 10U, 512U, 2048U, 64U, 1U, 2U, XX_CPM_VERSION_22, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U}},
    {"interak", 80U, 20U, 512U, 4096U, 256U, 2U, 2U, XX_CPM_VERSION_22, 3U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U}},
    {"fdd3000", 40U, 16U, 256U, 1024U, 128U, 4U, 4U, XX_CPM_VERSION_22, 0U, {0U, 7U, 14U, 5U, 12U, 3U, 10U, 1U, 8U, 15U, 6U, 13U, 4U, 11U, 2U, 9U}},
    {"fdd3000_2", 40U, 16U, 256U, 1024U, 128U, 2U, 4U, XX_CPM_VERSION_22, 0U, {0U, 5U, 10U, 15U, 4U, 9U, 14U, 3U, 8U, 13U, 2U, 7U, 12U, 1U, 6U, 11U}},
    {"1715", 40U, 5U, 1024U, 1024U, 64U, 3U, 2U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U}},
    {"17153", 160U, 5U, 1024U, 2048U, 128U, 4U, 2U, XX_CPM_VERSION_3, 0U, {0U, 1U, 2U, 3U, 4U}},
    {"scp624", 160U, 16U, 256U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U}},
    {"scp640", 160U, 16U, 256U, 2048U, 128U, 0U, 2U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U}},
    {"scp780", 160U, 5U, 1024U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U}},
    {"scp800", 160U, 5U, 1024U, 2048U, 128U, 0U, 2U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U}},
    {"z9001", 160U, 5U, 1024U, 2048U, 192U, 0U, 3U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U}},
    {"v1050", 80U, 10U, 512U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_3, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U}},
    {"microbee40", 80U, 10U, 512U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {1U, 4U, 7U, 0U, 3U, 6U, 9U, 2U, 5U, 8U}},
    {"dreamdisk80", 160U, 10U, 512U, 2048U, 256U, 2U, 4U, XX_CPM_VERSION_22, 0U, {1U, 4U, 7U, 0U, 3U, 6U, 9U, 2U, 5U, 8U}},
    {"icl-comet-525ss", 40U, 10U, 512U, 1024U, 64U, 2U, 2U, XX_CPM_VERSION_22, 0U, {0U, 3U, 6U, 9U, 2U, 5U, 8U, 1U, 4U, 7U}},
    {"z80pack-hd", 255U, 128U, 128U, 2048U, 1024U, 0U, 16U, XX_CPM_VERSION_22, 0U, {0U,   1U,   2U,   3U,   4U,   5U,   6U,   7U,   8U,   9U,   10U,  11U,  12U,
                                                                                    13U,  14U,  15U,  16U,  17U,  18U,  19U,  20U,  21U,  22U,  23U,  24U,  25U,
                                                                                    26U,  27U,  28U,  29U,  30U,  31U,  32U,  33U,  34U,  35U,  36U,  37U,  38U,
                                                                                    39U,  40U,  41U,  42U,  43U,  44U,  45U,  46U,  47U,  48U,  49U,  50U,  51U,
                                                                                    52U,  53U,  54U,  55U,  56U,  57U,  58U,  59U,  60U,  61U,  62U,  63U,  64U,
                                                                                    65U,  66U,  67U,  68U,  69U,  70U,  71U,  72U,  73U,  74U,  75U,  76U,  77U,
                                                                                    78U,  79U,  80U,  81U,  82U,  83U,  84U,  85U,  86U,  87U,  88U,  89U,  90U,
                                                                                    91U,  92U,  93U,  94U,  95U,  96U,  97U,  98U,  99U,  100U, 101U, 102U, 103U,
                                                                                    104U, 105U, 106U, 107U, 108U, 109U, 110U, 111U, 112U, 113U, 114U, 115U, 116U,
                                                                                    117U, 118U, 119U, 120U, 121U, 122U, 123U, 124U, 125U, 126U, 127U}},
    {"bw12", 40U, 18U, 256U, 2048U, 64U, 2U, 1U, XX_CPM_VERSION_22, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U}},
    {"bw14", 80U, 18U, 256U, 2048U, 64U, 2U, 1U, XX_CPM_VERSION_22, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U}},
    {"nsfd", 70U, 10U, 512U, 2048U, 64U, 2U, 1U, XX_CPM_VERSION_22, 1U, {0U, 5U, 1U, 6U, 2U, 7U, 3U, 8U, 4U, 9U}},
    {"nshd4", 512U, 16U, 512U, 4096U, 256U, 0U, 2U, XX_CPM_VERSION_22, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U}},
    {"nshd8", 1024U, 16U, 512U, 8192U, 256U, 0U, 1U, XX_CPM_VERSION_22, 3U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U}},
    {"mdsad175", 35U, 10U, 512U, 1024U, 64U, 2U, 2U, XX_CPM_VERSION_22, 0U, {0U, 5U, 1U, 6U, 2U, 7U, 3U, 8U, 4U, 9U}},
    {"osborne4", 80U, 5U, 1024U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U, 2U, 4U, 1U, 3U}},
    {"lobo2", 77U, 30U, 256U, 2048U, 64U, 2U, 1U, XX_CPM_VERSION_22, 0U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U, 13U, 14U,
                                                                          15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U, 26U, 27U, 28U, 29U}},
    {"lobo3", 77U, 17U, 512U, 2048U, 64U, 2U, 1U, XX_CPM_VERSION_3, 0U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U}},
    {"dec_pro", 80U, 10U, 512U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U, 2U, 4U, 6U, 8U, 1U, 3U, 5U, 7U, 9U}},
    {"pmc101", 80U, 5U, 1024U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_3, 1U, {0U, 1U, 2U, 3U, 4U}},
    {"heassdd8", 77U, 16U, 512U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U}},
    {"zen7", 40U, 8U, 512U, 1024U, 128U, 2U, 4U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}},
    {"zen8", 80U, 8U, 512U, 2048U, 256U, 2U, 4U, XX_CPM_VERSION_22, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}},
    {"zena", 77U, 26U, 256U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U,  9U,  18U, 1U,  10U, 19U, 2U,  11U, 20U, 3U,  12U, 21U, 4U,
                                                                          13U, 22U, 5U,  14U, 23U, 6U,  15U, 24U, 7U,  16U, 25U, 8U,  17U}},
    {"morsddd", 40U, 5U, 1024U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U, 3U, 1U, 4U, 2U}},
    {"osb1sssd", 40U, 10U, 256U, 2048U, 64U, 3U, 1U, XX_CPM_VERSION_22, 1U, {0U, 2U, 4U, 6U, 8U, 1U, 3U, 5U, 7U, 9U}},
    {"altdsdd", 160U, 9U, 512U, 4096U, 177U, 2U, 2U, XX_CPM_VERSION_3, 3U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U}},
    {"trsomsssd", 35U, 18U, 128U, 1024U, 64U, 3U, 2U, XX_CPM_VERSION_22, 0U, {0U, 4U, 8U, 12U, 16U, 2U, 6U, 10U, 14U, 1U, 5U, 9U, 13U, 17U, 3U, 7U, 11U, 15U}},
    {"memotech-type03", 98U, 26U, 128U, 2048U, 64U, 2U, 1U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                    13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type07", 196U, 26U, 128U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 0U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                      13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type43", 315U, 26U, 128U, 4096U, 256U, 2U, 2U, XX_CPM_VERSION_22, 3U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                      13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type47", 630U, 26U, 128U, 4096U, 256U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                      13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type4B", 1260U, 26U, 128U, 4096U, 512U, 2U, 4U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                       13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type4F", 2521U, 26U, 128U, 4096U, 512U, 2U, 4U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                       13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type50", 79U, 26U, 128U, 1024U, 64U, 2U, 2U, XX_CPM_VERSION_22, 0U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                    13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type51", 158U, 26U, 128U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                      13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type51-italy", 148U, 26U, 128U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                            13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type51-s2r64", 138U, 26U, 128U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                            13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"memotech-type51-s2r", 44U, 26U, 128U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                                         13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"rm-sd", 40U, 16U, 128U, 1024U, 64U, 3U, 2U, XX_CPM_VERSION_22, 0U, {0U, 3U, 6U, 9U, 12U, 15U, 2U, 5U, 8U, 11U, 14U, 1U, 4U, 7U, 10U, 13U}},
    {"rm-dd", 40U, 9U, 512U, 1024U, 64U, 3U, 2U, XX_CPM_VERSION_22, 0U, {0U, 5U, 1U, 6U, 2U, 7U, 3U, 8U, 4U}},
    {"rm-qd", 80U, 9U, 512U, 2048U, 128U, 3U, 2U, XX_CPM_VERSION_22, 1U, {0U, 5U, 1U, 6U, 2U, 7U, 3U, 8U, 4U}},
    {"amp1", 40U, 10U, 512U, 2048U, 64U, 2U, 1U, XX_CPM_VERSION_22, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U}},
    {"amp2", 80U, 10U, 512U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U}},
    {"amp3", 80U, 5U, 1024U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U, 1U, 2U, 3U, 4U}},
    {"amp4", 160U, 5U, 1024U, 2048U, 256U, 2U, 4U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U}},
    {"8megAltairSIMH", 2048U, 32U, 128U, 4096U, 1024U, 6U, 8U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U, 13U, 14U, 15U,
                                                                                       16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U, 26U, 27U, 28U, 29U, 30U, 31U}},
    {"simh", 254U, 32U, 128U, 2048U, 256U, 6U, 4U, XX_CPM_VERSION_22, 0U, {0U,  17U, 2U,  19U, 4U,  21U, 6U,  23U, 8U,  25U, 10U, 27U, 12U, 29U, 14U, 31U,
                                                                           16U, 1U,  18U, 3U,  20U, 5U,  22U, 7U,  24U, 9U,  26U, 11U, 28U, 13U, 30U, 15U}},
    {"svi707", 40U, 17U, 256U, 2048U, 64U, 3U, 1U, XX_CPM_VERSION_22, 1U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U}},
    {"mds-dd", 77U, 52U, 128U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 1U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U,
                                                                            18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U, 26U, 27U, 28U, 29U, 30U, 31U, 32U, 33U, 34U, 35U,
                                                                            36U, 37U, 38U, 39U, 40U, 41U, 42U, 43U, 44U, 45U, 46U, 47U, 48U, 49U, 50U, 51U}},
    {"mds-sd", 77U, 26U, 128U, 1024U, 64U, 2U, 2U, XX_CPM_VERSION_22, 0U, {0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U, 11U, 12U,
                                                                           13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U}},
    {"zcna_boot", 256U, 1U, 1024U, 1024U, 64U, 13U, 2U, XX_CPM_VERSION_22, 0U, {0U}},
    {"zcna_nonboot", 256U, 1U, 1024U, 1024U, 64U, 1U, 2U, XX_CPM_VERSION_22, 0U, {0U}},
    {"HP25", 66U, 16U, 256U, 1024U, 128U, 3U, 4U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U}},
    {"rc75x", 154U, 8U, 1024U, 2048U, 512U, 4U, 8U, XX_CPM_VERSION_3, 0U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}},
    {"nc200cf",
     256U,
     256U,
     512U,
     16384U,
     512U,
     0U,
     1U,
     XX_CPM_VERSION_22,
     7U,
     {0U,   1U,   2U,   3U,   4U,   5U,   6U,   7U,   8U,   9U,   10U,  11U,  12U,  13U,  14U,  15U,  16U,  17U,  18U,  19U,  20U,  21U,  22U,  23U,  24U,  25U,
      26U,  27U,  28U,  29U,  30U,  31U,  32U,  33U,  34U,  35U,  36U,  37U,  38U,  39U,  40U,  41U,  42U,  43U,  44U,  45U,  46U,  47U,  48U,  49U,  50U,  51U,
      52U,  53U,  54U,  55U,  56U,  57U,  58U,  59U,  60U,  61U,  62U,  63U,  64U,  65U,  66U,  67U,  68U,  69U,  70U,  71U,  72U,  73U,  74U,  75U,  76U,  77U,
      78U,  79U,  80U,  81U,  82U,  83U,  84U,  85U,  86U,  87U,  88U,  89U,  90U,  91U,  92U,  93U,  94U,  95U,  96U,  97U,  98U,  99U,  100U, 101U, 102U, 103U,
      104U, 105U, 106U, 107U, 108U, 109U, 110U, 111U, 112U, 113U, 114U, 115U, 116U, 117U, 118U, 119U, 120U, 121U, 122U, 123U, 124U, 125U, 126U, 127U, 128U, 129U,
      130U, 131U, 132U, 133U, 134U, 135U, 136U, 137U, 138U, 139U, 140U, 141U, 142U, 143U, 144U, 145U, 146U, 147U, 148U, 149U, 150U, 151U, 152U, 153U, 154U, 155U,
      156U, 157U, 158U, 159U, 160U, 161U, 162U, 163U, 164U, 165U, 166U, 167U, 168U, 169U, 170U, 171U, 172U, 173U, 174U, 175U, 176U, 177U, 178U, 179U, 180U, 181U,
      182U, 183U, 184U, 185U, 186U, 187U, 188U, 189U, 190U, 191U, 192U, 193U, 194U, 195U, 196U, 197U, 198U, 199U, 200U, 201U, 202U, 203U, 204U, 205U, 206U, 207U,
      208U, 209U, 210U, 211U, 212U, 213U, 214U, 215U, 216U, 217U, 218U, 219U, 220U, 221U, 222U, 223U, 224U, 225U, 226U, 227U, 228U, 229U, 230U, 231U, 232U, 233U,
      234U, 235U, 236U, 237U, 238U, 239U, 240U, 241U, 242U, 243U, 244U, 245U, 246U, 247U, 248U, 249U, 250U, 251U, 252U, 253U, 254U, 255U}},
    {"nc200cpm", 80U, 18U, 512U, 2048U, 128U, 2U, 2U, XX_CPM_VERSION_22, 0U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U}},
};
/* END GENERATED CP/M DISKDEFS TABLE */
static bool cpm_generated_geometry(xx_cpm_geometry *g, xx_cpm_preset preset)
{
    const xx_cpm_diskdef_preset *d;
    unsigned i;
    if ((unsigned)preset < 18U || (unsigned)preset >= 18U + sizeof(cpm_diskdefs) / sizeof(cpm_diskdefs[0])) return false;
    d = &cpm_diskdefs[(unsigned)preset - 18U];
    cpm_base(g, d->tracks, d->sectors, d->sector_size, d->block_size, d->directory_entries, d->boot_tracks, d->version);
    g->exm = d->exm;
    g->al0 = g->al1 = 0U;
    for (i = 0U; i < d->directory_blocks; ++i) {
        if (i < 8U) g->al0 |= (uint8_t)(0x80U >> i);
        else g->al1 |= (uint8_t)(0x80U >> (i - 8U));
    }
    for (i = 0U; i < d->sectors; ++i) g->sector_order[i] = d->order[i];
    return true;
}
bool xx_cpm_geometry_preset(xx_cpm_geometry *g, xx_cpm_preset preset)
{
    static const uint8_t apple_do[16] = {0, 6, 12, 3, 9, 15, 14, 5, 11, 2, 8, 7, 13, 4, 10, 1};
    static const uint8_t apple_po[16] = {0, 9, 3, 12, 6, 15, 1, 10, 4, 13, 7, 8, 2, 11, 5, 14};
    unsigned i;
    if (!g) return false;
    switch (preset) {
        case XX_CPM_PRESET_APPLE_DO:
        case XX_CPM_PRESET_APPLE_PO:
            cpm_base(g, 35U, 16U, 256U, 1024U, 64U, 3U, XX_CPM_VERSION_22);
            for (i = 0U; i < 16U; ++i) g->sector_order[i] = preset == XX_CPM_PRESET_APPLE_DO ? apple_do[i] : apple_po[i];
            return true;
        case XX_CPM_PRESET_PCW180: cpm_base(g, 40U, 9U, 512U, 1024U, 64U, 1U, XX_CPM_VERSION_3); return true;
        case XX_CPM_PRESET_CPC_SYSTEM: cpm_base(g, 40U, 9U, 512U, 1024U, 64U, 2U, XX_CPM_VERSION_3); return true;
        case XX_CPM_PRESET_CPC_DATA: cpm_base(g, 40U, 9U, 512U, 1024U, 64U, 0U, XX_CPM_VERSION_3); return true;
        case XX_CPM_PRESET_CF2DD: cpm_base(g, 160U, 9U, 512U, 2048U, 256U, 1U, XX_CPM_VERSION_3); return true;
        case XX_CPM_PRESET_ALPHA: cpm_base(g, 40U, 32U, 256U, 2048U, 128U, 2U, XX_CPM_VERSION_22); return true;
        case XX_CPM_PRESET_SDCARD: cpm_base(g, 256U, 64U, 512U, 8192U, 256U, 1U, XX_CPM_VERSION_22); return true;
        case XX_CPM_PRESET_PC1_2M: cpm_base(g, 80U, 30U, 512U, 4096U, 256U, 0U, XX_CPM_VERSION_3); return true;
        case XX_CPM_PRESET_CPM86_144FEAT: cpm_base(g, 160U, 18U, 512U, 4096U, 256U, 2U, XX_CPM_VERSION_3); return true;
        case XX_CPM_PRESET_P112: cpm_base(g, 160U, 18U, 512U, 2048U, 256U, 2U, XX_CPM_VERSION_3); return true;
        case XX_CPM_PRESET_P112_OLD: cpm_base(g, 160U, 18U, 512U, 2048U, 128U, 1U, XX_CPM_VERSION_3); return true;
        case XX_CPM_PRESET_NIGDOS:
            cpm_base(g, 84U, 10U, 512U, 2048U, 128U, 0U, XX_CPM_VERSION_3);
            g->exm = 0U;
            return true; /* stock diskdefs: logicalextents 1 */
        case XX_CPM_PRESET_EPSQX10: cpm_base(g, 40U, 20U, 512U, 2048U, 128U, 2U, XX_CPM_VERSION_22); return true;
        case XX_CPM_PRESET_IBM_8SS: cpm_base(g, 40U, 8U, 512U, 1024U, 64U, 1U, XX_CPM_VERSION_22); return true;
        case XX_CPM_PRESET_ELECTROGLAS: cpm_base(g, 80U, 10U, 512U, 2048U, 256U, 1U, XX_CPM_VERSION_3); return true;
        case XX_CPM_PRESET_IBMPC_514DS: cpm_base(g, 80U, 8U, 512U, 2048U, 64U, 2U, XX_CPM_VERSION_22); return true;
        default:
            if (cpm_generated_geometry(g, preset)) return true;
            xx_mem_zero(g, sizeof(*g));
            return false;
    }
}
bool xx_cpm_init_preset(xx_cpm *volume, xx_io_device *device, int64_t base, xx_cpm_preset preset)
{
    xx_cpm_geometry geometry;
    if (!volume || !xx_cpm_geometry_preset(&geometry, preset)) return false;
    xx_cpm_init_ex(volume, device, base, &geometry);
    return true;
}
xx_cpm *xx_cpm_create_preset(xx_io_device *device, int64_t base, xx_cpm_preset preset)
{
    xx_cpm *volume = (xx_cpm *)xx_mem_alloc(sizeof(*volume));
    if (!volume) return NULL;
    if (!xx_cpm_init_preset(volume, device, base, preset)) {
        xx_mem_free(volume);
        return NULL;
    }
    return volume;
}
unsigned xx_cpm_preset_count(void)
{
    return 17U + (unsigned)(sizeof(cpm_diskdefs) / sizeof(cpm_diskdefs[0]));
}
const char *xx_cpm_preset_name(xx_cpm_preset preset)
{
    static const char *const earlier[18] = {NULL,     "apple-do",      "apple-po", "pcw",      "cpcsys", "cpcdata", "cf2dd",   "alpha",       "sdcard",
                                            "pc1.2m", "cpm86-144feat", "p112",     "p112-old", "nigdos", "epsqx10", "ibm-8ss", "electroglas", "ibmpc-514ds"};
    unsigned id = (unsigned)preset;
    if (id > 0U && id < 18U) return earlier[id];
    if (id >= 18U && id < 18U + sizeof(cpm_diskdefs) / sizeof(cpm_diskdefs[0])) return cpm_diskdefs[id - 18U].name;
    return NULL;
}
bool xx_cpm_preset_from_name(const char *name, xx_cpm_preset *preset)
{
    unsigned id;
    if (!name || !preset) return false;
    for (id = 1U; id <= xx_cpm_preset_count(); ++id) {
        const char *candidate = xx_cpm_preset_name((xx_cpm_preset)id);
        if (candidate && strcmp(name, candidate) == 0) {
            *preset = (xx_cpm_preset)id;
            return true;
        }
    }
    return false;
}
