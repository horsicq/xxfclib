/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZPAQ (levels 1 and 2), written from the public ZPAQ specification.
 *
 * Block:
 *   [13-byte locator tag 37 6B 53 74 A0 31 83 D3 8C B2 28 B0 D3]
 *   "zPQ" level(1|2) 1 hsize(u16 LE)
 *   hsize bytes: hh hm ph pm n, n components, 0, HCOMP code ..., 0
 *   segments:  1 filename 0 comment 0 0 <coded data> (253 sha1[20] | 254)
 *   255
 *
 * Coded data: with n = 0 components it is stored as big-endian u32 length
 * prefixed runs ending with a zero length; otherwise it is binary arithmetic
 * coded, every byte preceded by an end-of-segment bit, with the prediction
 * mixed from the components whose contexts HCOMP computes after every byte.
 * The segment ends in four zero bytes.
 *
 * The decoded bytes of a block's first segment start with 0 (no
 * post-processing) or 1, u16 LE length and a PCOMP program; with PCOMP every
 * decoded byte, and -1 at the end of each segment, is fed to that program and
 * what it OUTputs is the segment's content. The model and the post-processor
 * carry across the segments of a block and are reset per block.
 *
 * Streaming archives name a file per segment (an unnamed segment continues
 * the previous file). Journaling archives (zpaq 6/7) consist of transactions
 * of blocks whose single segment is named "jDC" + YYYYMMDDHHMMSS + type +
 * 10-digit number:
 *   c  i64 csize: compressed size of the d blocks that follow (-1: the
 *      transaction was never completed and everything after is ignored)
 *   d  fragments, then u32 sizes and 8 more bytes; located through h
 *   h  u32 compressed size of the matching d block, then per fragment
 *      sha1[20] and u32 size; the number is the first fragment id
 *   i  records: i64 date (0 = deleted), name 0, and for a live file u32 na,
 *      na attribute bytes, u32 ni, ni u32 fragment ids
 * The d blocks of a transaction are skipped with csize and the h blocks'
 * sizes, so listing decodes only c, h and i blocks.
 *
 * Hostile input: every length and count is bounded, model and ZPAQL memory
 * are capped per block, and both ZPAQL machines run on an instruction budget
 * that grows only with the bytes they consume and produce, so a program
 * cannot loop forever. Every segment with a stored SHA-1 is verified, and
 * every journaling fragment is checked against the SHA-1 in its h block.
 *
 * Not handled: encrypted archives (salt + AES-CTR, no plaintext signature),
 * multi-part archives, and zpaq 7's "-until" roll-back (the newest version
 * is listed).
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zpaq/xx_zpaq.h"

#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_ZPAQ_TAG_SIZE 13
#define XX_ZPAQ_BLOCK_PREFIX_SIZE 7
#define XX_ZPAQ_MIN_HEADER_SIZE 7U
#define XX_ZPAQ_BLOCK_TYPE 1U

static const uint8_t XX_ZPAQ_TAG[XX_ZPAQ_TAG_SIZE] = {
    0x37U, 0x6BU, 0x53U, 0x74U, 0xA0U, 0x31U, 0x83U,
    0xD3U, 0x8CU, 0xB2U, 0x28U, 0xB0U, 0xD3U};

/* Model arrays of one block: components plus both ZPAQL machines. */
#define ZP_MAX_MODEL_BYTES ((uint64_t)1U << 30)
/* One c/h/i block, or one d block, decoded into memory. */
#define ZP_MAX_MEMBLOCK ((uint64_t)1U << 30)
/* Output of one segment whose size is not declared in its comment. */
#define ZP_MAX_UNKNOWN_OUTPUT ((uint64_t)1U << 30)
/* ZPAQL budget: a start allowance, then per run and per byte OUTput. */
#define ZP_VM_START_BUDGET ((uint64_t)1U << 24)
#define ZP_VM_RUN_GRANT 4096U
#define ZP_VM_OUT_GRANT 64U
/* PCOMP OUTput allowance: a start amount plus this much per byte fed in.
 * LZ77 expands one code byte into at most a few ten thousand bytes. */
#define ZP_VM_OUT_START ((uint64_t)1U << 24)
#define ZP_VM_OUT_PER_RUN 65536U
#define ZP_MAX_FRAGMENTS ((uint32_t)1U << 24)
#define ZP_MAX_ENTRIES ((uint32_t)1U << 22)
#define ZP_MAX_NAME 65535U
#define ZP_IN_BUFFER 65536U
#define ZP_OUT_BUFFER 65536U

/* ---------------------------------------------------------------------- */
/* Tables                                                                  */

/* squash(x) = floor(32768 / (1 + e^(-x/64))) for x = -665..665 (index x + 2048);
 * below that range it is 0, above it 32767. */
#define ZP_SQUASH_LO 1383
#define ZP_SQUASH_HI 2713
static const uint16_t zp_squash_mid[1331] = {
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     2,     2,     2,     2,
        2,     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
        2,     2,     2,     2,     2,     2,     2,     2,     2,     2,     3,     3,
        3,     3,     3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
        3,     3,     3,     3,     3,     4,     4,     4,     4,     4,     4,     4,
        4,     4,     4,     4,     4,     4,     4,     5,     5,     5,     5,     5,
        5,     5,     5,     5,     5,     5,     5,     6,     6,     6,     6,     6,
        6,     6,     6,     6,     6,     7,     7,     7,     7,     7,     7,     7,
        7,     8,     8,     8,     8,     8,     8,     8,     8,     9,     9,     9,
        9,     9,     9,    10,    10,    10,    10,    10,    10,    10,    11,    11,
       11,    11,    11,    12,    12,    12,    12,    12,    13,    13,    13,    13,
       13,    14,    14,    14,    14,    15,    15,    15,    15,    15,    16,    16,
       16,    17,    17,    17,    17,    18,    18,    18,    18,    19,    19,    19,
       20,    20,    20,    21,    21,    21,    22,    22,    22,    23,    23,    23,
       24,    24,    25,    25,    25,    26,    26,    27,    27,    28,    28,    28,
       29,    29,    30,    30,    31,    31,    32,    32,    33,    33,    34,    34,
       35,    36,    36,    37,    37,    38,    38,    39,    40,    40,    41,    42,
       42,    43,    44,    44,    45,    46,    46,    47,    48,    49,    49,    50,
       51,    52,    53,    54,    54,    55,    56,    57,    58,    59,    60,    61,
       62,    63,    64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
       74,    76,    77,    78,    79,    81,    82,    83,    84,    86,    87,    88,
       90,    91,    93,    94,    96,    97,    99,   100,   102,   103,   105,   107,
      108,   110,   112,   114,   115,   117,   119,   121,   123,   125,   127,   129,
      131,   133,   135,   137,   139,   141,   144,   146,   148,   151,   153,   155,
      158,   160,   163,   165,   168,   171,   173,   176,   179,   182,   184,   187,
      190,   193,   196,   199,   202,   206,   209,   212,   215,   219,   222,   226,
      229,   233,   237,   240,   244,   248,   252,   256,   260,   264,   268,   272,
      276,   281,   285,   289,   294,   299,   303,   308,   313,   318,   323,   328,
      333,   338,   343,   349,   354,   360,   365,   371,   377,   382,   388,   394,
      401,   407,   413,   420,   426,   433,   440,   446,   453,   460,   467,   475,
      482,   490,   497,   505,   513,   521,   529,   537,   545,   554,   562,   571,
      580,   589,   598,   607,   617,   626,   636,   646,   656,   666,   676,   686,
      697,   708,   719,   730,   741,   752,   764,   776,   788,   800,   812,   825,
      837,   850,   863,   876,   890,   903,   917,   931,   946,   960,   975,   990,
     1005,  1020,  1036,  1051,  1067,  1084,  1100,  1117,  1134,  1151,  1169,  1186,
     1204,  1223,  1241,  1260,  1279,  1298,  1318,  1338,  1358,  1379,  1399,  1421,
     1442,  1464,  1486,  1508,  1531,  1554,  1577,  1600,  1624,  1649,  1673,  1698,
     1724,  1749,  1775,  1802,  1829,  1856,  1883,  1911,  1940,  1968,  1998,  2027,
     2057,  2087,  2118,  2149,  2181,  2213,  2245,  2278,  2312,  2345,  2380,  2414,
     2450,  2485,  2521,  2558,  2595,  2633,  2671,  2709,  2748,  2788,  2828,  2869,
     2910,  2952,  2994,  3037,  3080,  3124,  3168,  3213,  3259,  3305,  3352,  3399,
     3447,  3496,  3545,  3594,  3645,  3696,  3747,  3799,  3852,  3906,  3960,  4014,
     4070,  4126,  4182,  4240,  4298,  4356,  4416,  4476,  4537,  4598,  4660,  4723,
     4786,  4851,  4916,  4981,  5048,  5115,  5183,  5251,  5320,  5390,  5461,  5533,
     5605,  5678,  5752,  5826,  5901,  5977,  6054,  6131,  6210,  6289,  6369,  6449,
     6530,  6613,  6695,  6779,  6863,  6949,  7035,  7121,  7209,  7297,  7386,  7476,
     7566,  7658,  7750,  7842,  7936,  8030,  8126,  8221,  8318,  8415,  8513,  8612,
     8712,  8812,  8913,  9015,  9117,  9221,  9324,  9429,  9534,  9640,  9747,  9854,
     9962, 10071, 10180, 10290, 10401, 10512, 10624, 10737, 10850, 10963, 11078, 11192,
    11308, 11424, 11540, 11658, 11775, 11893, 12012, 12131, 12251, 12371, 12491, 12612,
    12734, 12856, 12978, 13101, 13224, 13347, 13471, 13595, 13719, 13844, 13969, 14095,
    14220, 14346, 14472, 14599, 14725, 14852, 14979, 15106, 15233, 15361, 15488, 15616,
    15744, 15872, 16000, 16128, 16256, 16384, 16511, 16639, 16767, 16895, 17023, 17151,
    17279, 17406, 17534, 17661, 17788, 17915, 18042, 18168, 18295, 18421, 18547, 18672,
    18798, 18923, 19048, 19172, 19296, 19420, 19543, 19666, 19789, 19911, 20033, 20155,
    20276, 20396, 20516, 20636, 20755, 20874, 20992, 21109, 21227, 21343, 21459, 21575,
    21689, 21804, 21917, 22030, 22143, 22255, 22366, 22477, 22587, 22696, 22805, 22913,
    23020, 23127, 23233, 23338, 23443, 23546, 23650, 23752, 23854, 23955, 24055, 24155,
    24254, 24352, 24449, 24546, 24641, 24737, 24831, 24925, 25017, 25109, 25201, 25291,
    25381, 25470, 25558, 25646, 25732, 25818, 25904, 25988, 26072, 26154, 26237, 26318,
    26398, 26478, 26557, 26636, 26713, 26790, 26866, 26941, 27015, 27089, 27162, 27234,
    27306, 27377, 27447, 27516, 27584, 27652, 27719, 27786, 27851, 27916, 27981, 28044,
    28107, 28169, 28230, 28291, 28351, 28411, 28469, 28527, 28585, 28641, 28697, 28753,
    28807, 28861, 28915, 28968, 29020, 29071, 29122, 29173, 29222, 29271, 29320, 29368,
    29415, 29462, 29508, 29554, 29599, 29643, 29687, 29730, 29773, 29815, 29857, 29898,
    29939, 29979, 30019, 30058, 30096, 30134, 30172, 30209, 30246, 30282, 30317, 30353,
    30387, 30422, 30455, 30489, 30522, 30554, 30586, 30618, 30649, 30680, 30710, 30740,
    30769, 30799, 30827, 30856, 30884, 30911, 30938, 30965, 30992, 31018, 31043, 31069,
    31094, 31118, 31143, 31167, 31190, 31213, 31236, 31259, 31281, 31303, 31325, 31346,
    31368, 31388, 31409, 31429, 31449, 31469, 31488, 31507, 31526, 31544, 31563, 31581,
    31598, 31616, 31633, 31650, 31667, 31683, 31700, 31716, 31731, 31747, 31762, 31777,
    31792, 31807, 31821, 31836, 31850, 31864, 31877, 31891, 31904, 31917, 31930, 31942,
    31955, 31967, 31979, 31991, 32003, 32015, 32026, 32037, 32048, 32059, 32070, 32081,
    32091, 32101, 32111, 32121, 32131, 32141, 32150, 32160, 32169, 32178, 32187, 32196,
    32205, 32213, 32222, 32230, 32238, 32246, 32254, 32262, 32270, 32277, 32285, 32292,
    32300, 32307, 32314, 32321, 32327, 32334, 32341, 32347, 32354, 32360, 32366, 32373,
    32379, 32385, 32390, 32396, 32402, 32407, 32413, 32418, 32424, 32429, 32434, 32439,
    32444, 32449, 32454, 32459, 32464, 32468, 32473, 32478, 32482, 32486, 32491, 32495,
    32499, 32503, 32507, 32511, 32515, 32519, 32523, 32527, 32530, 32534, 32538, 32541,
    32545, 32548, 32552, 32555, 32558, 32561, 32565, 32568, 32571, 32574, 32577, 32580,
    32583, 32585, 32588, 32591, 32594, 32596, 32599, 32602, 32604, 32607, 32609, 32612,
    32614, 32616, 32619, 32621, 32623, 32626, 32628, 32630, 32632, 32634, 32636, 32638,
    32640, 32642, 32644, 32646, 32648, 32650, 32652, 32653, 32655, 32657, 32659, 32660,
    32662, 32664, 32665, 32667, 32668, 32670, 32671, 32673, 32674, 32676, 32677, 32679,
    32680, 32681, 32683, 32684, 32685, 32686, 32688, 32689, 32690, 32691, 32693, 32694,
    32695, 32696, 32697, 32698, 32699, 32700, 32701, 32702, 32703, 32704, 32705, 32706,
    32707, 32708, 32709, 32710, 32711, 32712, 32713, 32713, 32714, 32715, 32716, 32717,
    32718, 32718, 32719, 32720, 32721, 32721, 32722, 32723, 32723, 32724, 32725, 32725,
    32726, 32727, 32727, 32728, 32729, 32729, 32730, 32730, 32731, 32731, 32732, 32733,
    32733, 32734, 32734, 32735, 32735, 32736, 32736, 32737, 32737, 32738, 32738, 32739,
    32739, 32739, 32740, 32740, 32741, 32741, 32742, 32742, 32742, 32743, 32743, 32744,
    32744, 32744, 32745, 32745, 32745, 32746, 32746, 32746, 32747, 32747, 32747, 32748,
    32748, 32748, 32749, 32749, 32749, 32749, 32750, 32750, 32750, 32750, 32751, 32751,
    32751, 32752, 32752, 32752, 32752, 32752, 32753, 32753, 32753, 32753, 32754, 32754,
    32754, 32754, 32754, 32755, 32755, 32755, 32755, 32755, 32756, 32756, 32756, 32756,
    32756, 32757, 32757, 32757, 32757, 32757, 32757, 32757, 32758, 32758, 32758, 32758,
    32758, 32758, 32759, 32759, 32759, 32759, 32759, 32759, 32759, 32759, 32760, 32760,
    32760, 32760, 32760, 32760, 32760, 32760, 32761, 32761, 32761, 32761, 32761, 32761,
    32761, 32761, 32761, 32761, 32762, 32762, 32762, 32762, 32762, 32762, 32762, 32762,
    32762, 32762, 32762, 32762, 32763, 32763, 32763, 32763, 32763, 32763, 32763, 32763,
    32763, 32763, 32763, 32763, 32763, 32763, 32764, 32764, 32764, 32764, 32764, 32764,
    32764, 32764, 32764, 32764, 32764, 32764, 32764, 32764, 32764, 32764, 32764, 32764,
    32764, 32765, 32765, 32765, 32765, 32765, 32765, 32765, 32765, 32765, 32765, 32765,
    32765, 32765, 32765, 32765, 32765, 32765, 32765, 32765, 32765, 32765, 32765, 32765,
    32765, 32765, 32765, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766,
    32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766,
    32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766,
    32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32766,
};
/* stretch(p) = round(64 * ln((p + 0.5) / (32767.5 - p))): for the upper
 * half, the number of p in 16384..32767 that map to each value 0..710. */
static const uint8_t zp_stretch_runs[711] = {
     64, 128, 128, 128, 128, 128, 127, 128, 127, 128, 127, 127, 127, 127, 126, 126,
    126, 126, 126, 125, 125, 124, 125, 124, 123, 123, 123, 123, 122, 122, 121, 121,
    120, 120, 119, 119, 118, 118, 118, 116, 117, 115, 116, 114, 114, 113, 113, 112,
    112, 111, 110, 110, 109, 108, 108, 107, 106, 106, 105, 104, 104, 102, 103, 101,
    101, 100,  99,  98,  98,  97,  96,  96,  94,  94,  94,  92,  92,  91,  90,  89,
     89,  88,  87,  86,  86,  84,  84,  84,  82,  82,  81,  80,  79,  79,  78,  77,
     76,  76,  75,  74,  73,  73,  72,  71,  70,  70,  69,  68,  67,  67,  66,  65,
     65,  64,  63,  62,  62,  61,  61,  59,  59,  59,  57,  58,  56,  56,  55,  54,
     54,  53,  52,  52,  51,  51,  50,  49,  49,  48,  48,  47,  47,  45,  46,  44,
     45,  43,  43,  43,  42,  41,  41,  40,  40,  40,  39,  38,  38,  37,  37,  36,
     36,  36,  35,  34,  34,  34,  33,  32,  33,  32,  31,  31,  30,  31,  29,  30,
     28,  29,  28,  28,  27,  27,  27,  26,  26,  25,  26,  24,  25,  24,  24,  23,
     23,  23,  23,  22,  22,  21,  22,  21,  20,  21,  20,  19,  20,  19,  19,  19,
     18,  18,  18,  18,  17,  17,  17,  17,  16,  16,  16,  16,  15,  15,  15,  15,
     15,  14,  14,  14,  14,  13,  14,  13,  13,  13,  12,  13,  12,  12,  12,  11,
     12,  11,  11,  11,  11,  11,  10,  11,  10,  10,  10,  10,   9,  10,   9,   9,
      9,   9,   9,   8,   9,   8,   9,   8,   8,   8,   7,   8,   8,   7,   7,   8,
      7,   7,   7,   6,   7,   7,   6,   6,   7,   6,   6,   6,   6,   6,   6,   5,
      6,   5,   6,   5,   5,   5,   5,   5,   5,   5,   5,   5,   4,   5,   4,   5,
      4,   4,   5,   4,   4,   4,   4,   4,   4,   3,   4,   4,   3,   4,   4,   3,
      3,   4,   3,   3,   3,   4,   3,   3,   3,   3,   3,   3,   2,   3,   3,   3,
      2,   3,   2,   3,   3,   2,   2,   3,   2,   2,   3,   2,   2,   2,   2,   3,
      2,   2,   2,   2,   2,   2,   1,   2,   2,   2,   2,   1,   2,   2,   2,   1,
      2,   1,   2,   2,   1,   2,   1,   2,   1,   1,   2,   1,   1,   2,   1,   1,
      2,   1,   1,   1,   1,   2,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   0,   1,   1,   1,   1,   0,
      1,   1,   1,   0,   1,   1,   1,   0,   1,   1,   0,   1,   1,   0,   1,   0,
      1,   1,   0,   1,   0,   1,   0,   1,   0,   1,   0,   1,   0,   1,   0,   1,
      0,   1,   0,   1,   0,   1,   0,   0,   1,   0,   1,   0,   0,   1,   0,   1,
      0,   0,   1,   0,   0,   1,   0,   0,   1,   0,   0,   1,   0,   0,   0,   1,
      0,   0,   1,   0,   0,   0,   1,   0,   0,   0,   1,   0,   0,   0,   1,   0,
      0,   0,   0,   1,   0,   0,   0,   0,   1,   0,   0,   0,   0,   1,   0,   0,
      0,   0,   0,   1,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   0,
      1,   0,   0,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   1,
};

typedef struct zp_tables_s {
    uint16_t squash[4096];
    int16_t stretch[32768];
    uint8_t ns[1024];   /* state*4: next if 0, next if 1, n0, n1 */
    int32_t dt2k[256];
    int32_t dt[1024];
} zp_tables;

/* Bit-history states: counts (n0, n1) of zeros and ones, bounded so that
 * the whole table fits a byte; states are numbered by increasing n0 + n1. */
static int zp_num_states(int n0, int n1) {
    static const int bound[6] = {20, 48, 15, 8, 6, 5};
    if (n0 < n1) return zp_num_states(n1, n0);
    if (n0 < 0 || n1 < 0 || n1 >= 6 || n0 > bound[n1]) return 0;
    return 1 + (n1 > 0 && n0 + n1 <= 17);
}

static int zp_discount(int n) {
    return (n >= 1) + (n >= 2) + (n >= 3) + (n >= 4) + (n >= 5) + (n >= 7) +
           (n >= 8);
}

static void zp_next_state(int *n0, int *n1, int y) {
    int guard;
    if (*n0 < *n1) {
        zp_next_state(n1, n0, 1 - y);
        return;
    }
    if (y) {
        ++*n1;
        *n0 = zp_discount(*n0);
    } else {
        ++*n0;
        *n1 = zp_discount(*n1);
    }
    for (guard = 0; guard < 200 && !zp_num_states(*n0, *n1); ++guard) {
        if (*n1 < 2) {
            --*n0;
        } else {
            *n0 = (*n0 * (*n1 - 1) + (*n1 / 2)) / *n1;
            --*n1;
        }
    }
}

static void zp_tables_init(zp_tables *t) {
    uint8_t (*st)[50][2];
    int i, j, k, state = 0, n0, n1, y;
    for (i = 0; i < 4096; ++i) {
        t->squash[i] = i < ZP_SQUASH_LO ? 0U
                       : i > ZP_SQUASH_HI ? 32767U
                                          : zp_squash_mid[i - ZP_SQUASH_LO];
    }
    k = 16384;
    for (i = 0; i < 711; ++i)
        for (j = zp_stretch_runs[i]; j > 0 && k < 32768; --j)
            t->stretch[k++] = (int16_t)i;
    for (i = 0; i < 16384; ++i) t->stretch[i] = (int16_t)-t->stretch[32767 - i];
    t->dt2k[0] = 0;
    for (i = 1; i < 256; ++i) t->dt2k[i] = 2048 / i;
    for (i = 0; i < 1024; ++i) t->dt[i] = (1 << 17) / (i * 2 + 3) * 2;

    xx_mem_zero(t->ns, sizeof(t->ns));
    st = (uint8_t (*)[50][2])xx_mem_calloc(50U * 50U * 2U, 1U);
    if (!st) return;
    for (i = 0; i < 50; ++i) {
        for (n1 = 0; n1 <= i; ++n1) {
            int n;
            n0 = i - n1;
            n = zp_num_states(n0, n1);
            if (n) {
                st[n0][n1][0] = (uint8_t)state;
                st[n0][n1][1] = (uint8_t)(state + n - 1);
                state += n;
            }
        }
    }
    for (n0 = 0; n0 < 50; ++n0) {
        for (n1 = 0; n1 < 50; ++n1) {
            for (y = 0; y < zp_num_states(n0, n1); ++y) {
                int s = st[n0][n1][y], s0 = n0, s1 = n1;
                zp_next_state(&s0, &s1, 0);
                t->ns[s * 4 + 0] = st[s0][s1][0];
                s0 = n0;
                s1 = n1;
                zp_next_state(&s0, &s1, 1);
                t->ns[s * 4 + 1] = st[s0][s1][1];
                t->ns[s * 4 + 2] = (uint8_t)n0;
                t->ns[s * 4 + 3] = (uint8_t)n1;
            }
        }
    }
    xx_mem_free(st);
}

static int zp_squash(const zp_tables *t, int x) {
    if (x > 2047) x = 2047;
    if (x < -2047) x = -2047;
    return (int)t->squash[x + 2048];
}

static int zp_stretch(const zp_tables *t, uint32_t p) {
    return (int)t->stretch[p & 32767U];
}

static int zp_clamp2k(int64_t x) {
    return x < -2048 ? -2048 : x > 2047 ? 2047 : (int)x;
}

static int32_t zp_clamp512k(int64_t x) {
    return x < -(1 << 19) ? -(1 << 19)
           : x >= (1 << 19) ? (1 << 19) - 1
                            : (int32_t)x;
}

static uint32_t zp_cminit(const zp_tables *t, int state) {
    uint32_t n0 = t->ns[state * 4 + 2], n1 = t->ns[state * 4 + 3];
    return ((n1 * 2U + 1U) << 22) / (n0 + n1 + 1U);
}

/* ---------------------------------------------------------------------- */
/* Buffered input over a device window                                     */

typedef struct zp_in_s {
    xx_io_device *device;
    int64_t base;   /* device offset of buffer[0] */
    int64_t end;    /* first offset not to read */
    size_t length;
    size_t at;
    uint8_t buffer[ZP_IN_BUFFER];
} zp_in;

static void zp_in_seek(zp_in *in, int64_t offset) {
    in->base = offset;
    in->length = 0U;
    in->at = 0U;
}

static int64_t zp_in_tell(const zp_in *in) {
    return in->base + (int64_t)in->at;
}

static int zp_in_refill(zp_in *in) {
    int64_t offset = in->base + (int64_t)in->length;
    size_t want = ZP_IN_BUFFER;
    size_t done = 0U;
    if (offset >= in->end || offset < 0) return -1;
    if ((int64_t)want > in->end - offset) want = (size_t)(in->end - offset);
    if (xx_io_seek64(in->device, offset, SEEK_SET) != 0) return -1;
    while (done < want) {
        ssize_t got = xx_io_read(in->device, in->buffer + done, want - done);
        if (got <= 0 || (size_t)got > want - done) break;
        done += (size_t)got;
    }
    if (done == 0U) return -1;
    in->base = offset;
    in->length = done;
    in->at = 0U;
    return 0;
}

static int zp_get(zp_in *in) {
    if (in->at >= in->length && zp_in_refill(in) != 0) return -1;
    return in->buffer[in->at++];
}

static bool zp_read(zp_in *in, uint8_t *out, size_t size) {
    size_t i;
    for (i = 0U; i < size; ++i) {
        int c = zp_get(in);
        if (c < 0) return false;
        out[i] = (uint8_t)c;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Output sink: SHA-1 and byte count over what a segment produces          */

typedef bool (*zp_write_fn)(void *context, const uint8_t *data, size_t size);

typedef struct zp_sink_s {
    zp_write_fn write;  /* NULL discards */
    void *context;
    xx_hash_context sha;
    uint64_t count;
    uint64_t limit;
    size_t used;
    bool bad;
    uint8_t buffer[ZP_OUT_BUFFER];
} zp_sink;

static void zp_sink_start(zp_sink *sink, zp_write_fn write, void *context,
                          uint64_t limit) {
    sink->write = write;
    sink->context = context;
    sink->count = 0U;
    sink->limit = limit;
    sink->used = 0U;
    sink->bad = false;
    (void)xx_hash_init(&sink->sha, XX_HASH_SHA1);
}

static void zp_sink_flush(zp_sink *sink) {
    if (sink->used == 0U) return;
    xx_hash_update(&sink->sha, sink->buffer, sink->used);
    if (!sink->bad && sink->write &&
        !sink->write(sink->context, sink->buffer, sink->used))
        sink->bad = true;
    sink->used = 0U;
}

static void zp_sink_put(zp_sink *sink, int c) {
    if (sink->count >= sink->limit) {
        sink->bad = true;
        return;
    }
    ++sink->count;
    sink->buffer[sink->used++] = (uint8_t)c;
    if (sink->used == ZP_OUT_BUFFER) zp_sink_flush(sink);
}

/* ---------------------------------------------------------------------- */
/* ZPAQL virtual machine                                                   */

typedef struct zp_vm_s {
    uint8_t *code;      /* program, ending in a 0 byte */
    uint32_t length;
    uint32_t *h;
    uint32_t hmask;
    uint8_t *m;
    uint32_t mmask;
    uint32_t a, b, c, d;
    bool f;
    uint32_t r[256];
    uint64_t budget;
    uint64_t out_allow;
    zp_sink *out;       /* PCOMP output; NULL for HCOMP */
} zp_vm;

static void zp_vm_free(zp_vm *z) {
    if (z->h) xx_mem_free(z->h);
    if (z->m) xx_mem_free(z->m);
    z->h = NULL;
    z->m = NULL;
}

/* Sizes 2^hbits words and 2^mbits bytes; charged to *used. */
static bool zp_vm_alloc(zp_vm *z, int hbits, int mbits, uint64_t *used) {
    uint64_t hbytes, mbytes;
    if (hbits > 32 || mbits > 32) return false;
    hbytes = (uint64_t)4U << hbits;
    mbytes = (uint64_t)1U << mbits;
    if (hbytes > ZP_MAX_MODEL_BYTES || mbytes > ZP_MAX_MODEL_BYTES ||
        *used + hbytes + mbytes > ZP_MAX_MODEL_BYTES)
        return false;
    z->h = (uint32_t *)xx_mem_calloc((size_t)(hbytes / 4U), 4U);
    z->m = (uint8_t *)xx_mem_calloc((size_t)mbytes, 1U);
    if (!z->h || !z->m) {
        zp_vm_free(z);
        return false;
    }
    *used += hbytes + mbytes;
    z->hmask = (uint32_t)(hbytes / 4U - 1U);
    z->mmask = (uint32_t)(mbytes - 1U);
    z->a = z->b = z->c = z->d = 0U;
    z->f = false;
    xx_mem_zero(z->r, sizeof(z->r));
    z->budget = ZP_VM_START_BUDGET;
    z->out_allow = ZP_VM_OUT_START;
    return true;
}

/* Operand sources: A B C D *B *C *D; 7 is the immediate. */
static uint32_t zp_vm_get(const zp_vm *z, int which, uint32_t immediate) {
    switch (which) {
        case 0: return z->a;
        case 1: return z->b;
        case 2: return z->c;
        case 3: return z->d;
        case 4: return z->m[z->b & z->mmask];
        case 5: return z->m[z->c & z->mmask];
        case 6: return z->h[z->d & z->hmask];
        default: return immediate;
    }
}

static void zp_vm_set(zp_vm *z, int which, uint32_t value) {
    switch (which) {
        case 0: z->a = value; break;
        case 1: z->b = value; break;
        case 2: z->c = value; break;
        case 3: z->d = value; break;
        case 4: z->m[z->b & z->mmask] = (uint8_t)value; break;
        case 5: z->m[z->c & z->mmask] = (uint8_t)value; break;
        default: z->h[z->d & z->hmask] = value; break;
    }
}

/* Run the program from its start with A = input until HALT. */
static bool zp_vm_run(zp_vm *z, uint32_t input) {
    uint32_t pc = 0U;
    if (!z->code || z->length == 0U) return false;
    z->budget += ZP_VM_RUN_GRANT;
    z->out_allow += ZP_VM_OUT_PER_RUN;
    z->a = input;
    for (;;) {
        uint32_t op, operand = 0U, x;
        int group, sub;
        if (z->budget == 0U || pc >= z->length) return false;
        --z->budget;
        op = z->code[pc++];
        if (op == 255U) {
            if (pc + 2U > z->length) return false;
            pc = (uint32_t)z->code[pc] + 256U * (uint32_t)z->code[pc + 1U];
            continue;
        }
        if ((op & 7U) == 7U) {
            if (pc >= z->length) return false;
            operand = z->code[pc++];
        }
        if (op < 64U) {
            group = (int)(op >> 3);
            sub = (int)(op & 7U);
            if (group < 7) {
                switch (sub) {
                    case 0: /* X<>A */
                        if (group == 0) return false;
                        if (group == 4 || group == 5) {
                            uint8_t *cell = &z->m[(group == 4 ? z->b : z->c) &
                                                  z->mmask];
                            uint8_t old = *cell;
                            *cell = (uint8_t)z->a;
                            z->a = (z->a & ~0xFFU) | old;
                        } else {
                            x = zp_vm_get(z, group, 0U);
                            zp_vm_set(z, group, z->a);
                            z->a = x;
                        }
                        break;
                    case 1: zp_vm_set(z, group, zp_vm_get(z, group, 0U) + 1U); break;
                    case 2: zp_vm_set(z, group, zp_vm_get(z, group, 0U) - 1U); break;
                    case 3: zp_vm_set(z, group, ~zp_vm_get(z, group, 0U)); break;
                    case 4: zp_vm_set(z, group, 0U); break;
                    case 7:
                        if (group < 4) {
                            zp_vm_set(z, group, z->r[operand]);
                        } else if (group == 6) {
                            z->r[operand] = z->a;
                        } else if ((group == 4) == z->f) { /* JT / JF */
                            pc = (uint32_t)((int64_t)pc +
                                            (int64_t)(int8_t)operand);
                        }
                        break;
                    default: return false;
                }
            } else {
                switch (op) {
                    case 56: return true; /* HALT */
                    case 57:              /* OUT */
                        if (z->out) {
                            if (z->out_allow == 0U) return false;
                            --z->out_allow;
                            zp_sink_put(z->out, (int)(z->a & 255U));
                            if (z->out->bad) return false;
                            z->budget += ZP_VM_OUT_GRANT;
                        }
                        break;
                    case 59: /* HASH */
                        z->a = (z->a + z->m[z->b & z->mmask] + 512U) * 773U;
                        break;
                    case 60: /* HASHD */
                        z->h[z->d & z->hmask] =
                            (z->h[z->d & z->hmask] + z->a + 512U) * 773U;
                        break;
                    case 63: /* JMP */
                        pc = (uint32_t)((int64_t)pc + (int64_t)(int8_t)operand);
                        break;
                    default: return false;
                }
            }
        } else if (op < 128U) {
            int dst = (int)((op - 64U) >> 3);
            if (dst > 6) return false;
            zp_vm_set(z, dst, zp_vm_get(z, (int)(op & 7U), operand));
        } else if (op < 240U) {
            x = zp_vm_get(z, (int)(op & 7U), operand);
            switch ((op - 128U) >> 3) {
                case 0: z->a += x; break;
                case 1: z->a -= x; break;
                case 2: z->a *= x; break;
                case 3: z->a = x ? z->a / x : 0U; break;
                case 4: z->a = x ? z->a % x : 0U; break;
                case 5: z->a &= x; break;
                case 6: z->a &= ~x; break;
                case 7: z->a |= x; break;
                case 8: z->a ^= x; break;
                case 9: z->a <<= (x & 31U); break;
                case 10: z->a >>= (x & 31U); break;
                case 11: z->f = z->a == x; break;
                case 12: z->f = z->a < x; break;
                default: z->f = z->a > x; break;
            }
        } else {
            return false;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Predictor                                                               */

enum { ZP_CONS = 1, ZP_CM, ZP_ICM, ZP_MATCH, ZP_AVG, ZP_MIX2, ZP_MIX, ZP_ISSE,
       ZP_SSE };
static const uint8_t zp_compsize[10] = {0, 2, 3, 2, 3, 4, 6, 6, 3, 5};

typedef struct zp_comp_s {
    uint32_t limit, cxt, a, b, c;
    uint32_t *cm;
    uint32_t cm_mask;
    uint8_t *ht;
    uint32_t ht_mask;
    uint32_t ht_size;
    uint16_t *a16;
} zp_comp;

typedef struct zp_pred_s {
    int n;
    const uint8_t *cp[256];
    zp_comp comp[256];
    int p[256];
    uint32_t h[256];
    uint32_t c8, hmap4;
    zp_vm hcomp;
    zp_tables tables;
} zp_pred;

static void zp_pred_free(zp_pred *pr) {
    int i;
    if (!pr) return;
    for (i = 0; i < 256; ++i) {
        if (pr->comp[i].cm) xx_mem_free(pr->comp[i].cm);
        if (pr->comp[i].ht) xx_mem_free(pr->comp[i].ht);
        if (pr->comp[i].a16) xx_mem_free(pr->comp[i].a16);
    }
    zp_vm_free(&pr->hcomp);
    xx_mem_free(pr);
}

/* count << bits elements of element_size bytes, charged to *used. */
static void *zp_model_alloc(uint32_t count, int bits, size_t element_size,
                            uint64_t *used, uint32_t *elements) {
    uint64_t n, bytes;
    if (bits > 32) return NULL;
    n = (uint64_t)count << bits;
    if (n == 0U || n > ZP_MAX_MODEL_BYTES) return NULL;
    bytes = n * element_size;
    if (bytes > ZP_MAX_MODEL_BYTES || *used + bytes > ZP_MAX_MODEL_BYTES)
        return NULL;
    *used += bytes;
    *elements = (uint32_t)n;
    return xx_mem_calloc((size_t)n, element_size);
}

/* hdr: hh hm ph pm n components...; parsed and bounds-checked already. */
static zp_pred *zp_pred_create(const uint8_t *hdr, const uint8_t *hcomp,
                               uint32_t hcomp_length, uint64_t *used) {
    zp_pred *pr = (zp_pred *)xx_mem_calloc(1U, sizeof(zp_pred));
    const uint8_t *cp;
    int i;
    uint32_t j, count;
    if (!pr) return NULL;
    zp_tables_init(&pr->tables);
    pr->n = hdr[4];
    pr->c8 = 1U;
    pr->hmap4 = 1U;
    pr->hcomp.code = (uint8_t *)hcomp;
    pr->hcomp.length = hcomp_length;
    if (!zp_vm_alloc(&pr->hcomp, hdr[0], hdr[1], used)) goto fail;
    cp = hdr + 5;
    for (i = 0; i < pr->n; ++i) {
        zp_comp *cr = &pr->comp[i];
        pr->cp[i] = cp;
        switch (cp[0]) {
            case ZP_CONS:
                pr->p[i] = (cp[1] - 128) * 4;
                break;
            case ZP_CM:
                cr->cm = (uint32_t *)zp_model_alloc(1U, cp[1], 4U, used, &count);
                if (!cr->cm) goto fail;
                cr->cm_mask = count - 1U;
                cr->limit = cp[2] * 4U;
                for (j = 0; j < count; ++j) cr->cm[j] = 0x80000000U;
                break;
            case ZP_ICM:
                cr->limit = 1023U;
                cr->cm = (uint32_t *)zp_model_alloc(256U, 0, 4U, used, &count);
                cr->ht = (uint8_t *)zp_model_alloc(64U, cp[1], 1U, used,
                                                   &cr->ht_size);
                if (!cr->cm || !cr->ht) goto fail;
                cr->cm_mask = 255U;
                cr->ht_mask = cr->ht_size - 1U;
                for (j = 0; j < 256U; ++j)
                    cr->cm[j] = zp_cminit(&pr->tables, (int)j);
                break;
            case ZP_MATCH:
                cr->cm = (uint32_t *)zp_model_alloc(1U, cp[1], 4U, used, &count);
                cr->ht = (uint8_t *)zp_model_alloc(1U, cp[2], 1U, used,
                                                   &cr->ht_size);
                if (!cr->cm || !cr->ht) goto fail;
                cr->cm_mask = count - 1U;
                cr->ht_mask = cr->ht_size - 1U;
                cr->ht[0] = 1U;
                break;
            case ZP_AVG:
                if (cp[1] >= i || cp[2] >= i) goto fail;
                break;
            case ZP_MIX2:
                if (cp[2] >= i || cp[3] >= i) goto fail;
                cr->a16 = (uint16_t *)zp_model_alloc(1U, cp[1], 2U, used, &count);
                if (!cr->a16) goto fail;
                cr->c = count;
                for (j = 0; j < count; ++j) cr->a16[j] = 32768U;
                break;
            case ZP_MIX: {
                uint32_t m = cp[3];
                if (cp[2] >= i || m < 1U || m > (uint32_t)(i - cp[2])) goto fail;
                cr->cm = (uint32_t *)zp_model_alloc(m, cp[1], 4U, used, &count);
                if (!cr->cm) goto fail;
                cr->c = count / m;
                cr->cm_mask = count - 1U;
                for (j = 0; j < count; ++j) cr->cm[j] = 65536U / m;
                break;
            }
            case ZP_ISSE:
                if (cp[2] >= i) goto fail;
                cr->ht = (uint8_t *)zp_model_alloc(64U, cp[1], 1U, used,
                                                   &cr->ht_size);
                cr->cm = (uint32_t *)zp_model_alloc(512U, 0, 4U, used, &count);
                if (!cr->ht || !cr->cm) goto fail;
                cr->ht_mask = cr->ht_size - 1U;
                cr->cm_mask = 511U;
                for (j = 0; j < 256U; ++j) {
                    cr->cm[j * 2U] = 1U << 15;
                    cr->cm[j * 2U + 1U] = (uint32_t)zp_clamp512k(
                        (int64_t)zp_stretch(&pr->tables,
                                            zp_cminit(&pr->tables, (int)j) >> 8) *
                        1024);
                }
                break;
            case ZP_SSE:
                if (cp[2] >= i || cp[3] > cp[4] * 4) goto fail;
                cr->cm = (uint32_t *)zp_model_alloc(32U, cp[1], 4U, used, &count);
                if (!cr->cm) goto fail;
                cr->cm_mask = count - 1U;
                cr->limit = cp[4] * 4U;
                for (j = 0; j < count; ++j)
                    cr->cm[j] = ((uint32_t)zp_squash(&pr->tables,
                                                     (int)(j & 31U) * 64 - 992)
                                 << 17) |
                                cp[3];
                break;
            default:
                goto fail;
        }
        cp += zp_compsize[cp[0]];
    }
    return pr;
fail:
    zp_pred_free(pr);
    return NULL;
}

/* Row of 16 bytes for context cxt in a bit-history table: slot 0 checks the
 * next 8 context bits; after 3 misses the lowest-priority row is reused. */
static uint32_t zp_find(zp_comp *cr, int sizebits, uint32_t cxt) {
    uint8_t *ht = cr->ht;
    uint32_t chk = (sizebits < 32 ? cxt >> sizebits : 0U) & 255U;
    uint32_t h0 = (cxt * 16U) & (cr->ht_size - 16U);
    uint32_t h1 = h0 ^ 16U, h2 = h0 ^ 32U, pick;
    if (ht[h0] == chk) return h0;
    if (ht[h1] == chk) return h1;
    if (ht[h2] == chk) return h2;
    if (ht[h0 + 1U] <= ht[h1 + 1U] && ht[h0 + 1U] <= ht[h2 + 1U])
        pick = h0;
    else if (ht[h1 + 1U] < ht[h2 + 1U])
        pick = h1;
    else
        pick = h2;
    xx_mem_zero(ht + pick, 16U);
    ht[pick] = (uint8_t)chk;
    return pick;
}

static int zp_predict(zp_pred *pr) {
    const zp_tables *t = &pr->tables;
    int i;
    for (i = 0; i < pr->n; ++i) {
        const uint8_t *cp = pr->cp[i];
        zp_comp *cr = &pr->comp[i];
        switch (cp[0]) {
            case ZP_CONS:
                break;
            case ZP_CM:
                cr->cxt = pr->h[i] ^ pr->hmap4;
                pr->p[i] = zp_stretch(t, cr->cm[cr->cxt & cr->cm_mask] >> 17);
                break;
            case ZP_ICM:
                if (pr->c8 == 1U || (pr->c8 & 0xF0U) == 16U)
                    cr->c = zp_find(cr, cp[1] + 2, pr->h[i] + 16U * pr->c8);
                cr->cxt = cr->ht[cr->c + (pr->hmap4 & 15U)];
                pr->p[i] = zp_stretch(t, cr->cm[cr->cxt] >> 8);
                break;
            case ZP_MATCH:
                if (cr->a == 0U) {
                    pr->p[i] = 0;
                } else {
                    cr->c = (cr->ht[(cr->limit - cr->b) & cr->ht_mask] >>
                             (7U - cr->cxt)) & 1U;
                    pr->p[i] = zp_stretch(
                        t, (uint32_t)(t->dt2k[cr->a] * (cr->c ? -1 : 1)) &
                               32767U);
                }
                break;
            case ZP_AVG:
                pr->p[i] = (pr->p[cp[1]] * cp[3] + pr->p[cp[2]] * (256 - cp[3])) >> 8;
                break;
            case ZP_MIX2: {
                int w;
                cr->cxt = (pr->h[i] + (pr->c8 & cp[5])) & (cr->c - 1U);
                w = cr->a16[cr->cxt];
                pr->p[i] = (int)(((int64_t)w * pr->p[cp[2]] +
                                  (int64_t)(65536 - w) * pr->p[cp[3]]) >> 16);
                break;
            }
            case ZP_MIX: {
                uint32_t m = cp[3], j;
                const int32_t *wt;
                int64_t sum = 0;
                cr->cxt = pr->h[i] + (pr->c8 & cp[5]);
                cr->cxt = (cr->cxt & (cr->c - 1U)) * m;
                wt = (const int32_t *)&cr->cm[cr->cxt];
                for (j = 0; j < m; ++j)
                    sum += (int64_t)(wt[j] >> 8) * pr->p[cp[2] + j];
                pr->p[i] = zp_clamp2k(sum >> 8);
                break;
            }
            case ZP_ISSE: {
                const int32_t *wt;
                if (pr->c8 == 1U || (pr->c8 & 0xF0U) == 16U)
                    cr->c = zp_find(cr, cp[1] + 2, pr->h[i] + 16U * pr->c8);
                cr->cxt = cr->ht[cr->c + (pr->hmap4 & 15U)];
                wt = (const int32_t *)&cr->cm[cr->cxt * 2U];
                pr->p[i] = zp_clamp2k(((int64_t)wt[0] * pr->p[cp[2]] +
                                       (int64_t)wt[1] * 64) >> 16);
                break;
            }
            case ZP_SSE: {
                int pq = pr->p[cp[2]] + 992, wt;
                if (pq < 0) pq = 0;
                if (pq > 1983) pq = 1983;
                wt = pq & 63;
                pq >>= 6;
                cr->cxt = (pr->h[i] + pr->c8) * 32U + (uint32_t)pq;
                pr->p[i] = zp_stretch(
                    t, (uint32_t)(((int64_t)(cr->cm[cr->cxt & cr->cm_mask] >> 10) *
                                       (64 - wt) +
                                   (int64_t)(cr->cm[(cr->cxt + 1U) & cr->cm_mask] >>
                                             10) *
                                       wt) >> 13));
                cr->cxt += (uint32_t)(wt >> 5);
                break;
            }
            default:
                break;
        }
    }
    return zp_squash(t, pr->p[pr->n - 1]);
}

static void zp_train(const zp_tables *t, zp_comp *cr, int y) {
    uint32_t *pn = &cr->cm[cr->cxt & cr->cm_mask];
    uint32_t count = *pn & 0x3FFU;
    int32_t error = y * 32767 - (int32_t)(*pn >> 17);
    *pn += (((uint32_t)error * (uint32_t)t->dt[count]) & 0xFFFFFC00U) +
           (count < cr->limit ? 1U : 0U);
}

static bool zp_update(zp_pred *pr, int y) {
    const zp_tables *t = &pr->tables;
    int i;
    for (i = 0; i < pr->n; ++i) {
        const uint8_t *cp = pr->cp[i];
        zp_comp *cr = &pr->comp[i];
        switch (cp[0]) {
            case ZP_CM:
            case ZP_SSE:
                zp_train(t, cr, y);
                break;
            case ZP_ICM: {
                uint32_t slot = cr->c + (pr->hmap4 & 15U);
                uint32_t *pn = &cr->cm[cr->cxt];
                cr->ht[slot] = t->ns[cr->ht[slot] * 4U + (uint32_t)y];
                *pn += (uint32_t)((y * 32767 - (int32_t)(*pn >> 8)) >> 2);
                break;
            }
            case ZP_MATCH:
                if ((int)cr->c != y) cr->a = 0U;
                cr->ht[cr->limit & cr->ht_mask] =
                    (uint8_t)(cr->ht[cr->limit & cr->ht_mask] * 2U + (uint32_t)y);
                if (++cr->cxt == 8U) {
                    cr->cxt = 0U;
                    cr->limit = (cr->limit + 1U) & cr->ht_mask;
                    if (cr->a == 0U) {
                        cr->b = cr->limit - cr->cm[pr->h[i] & cr->cm_mask];
                        if (cr->b & cr->ht_mask) {
                            while (cr->a < 255U &&
                                   cr->ht[(cr->limit - cr->a - 1U) & cr->ht_mask] ==
                                       cr->ht[(cr->limit - cr->a - cr->b - 1U) &
                                              cr->ht_mask])
                                ++cr->a;
                        }
                    } else if (cr->a < 255U) {
                        ++cr->a;
                    }
                    cr->cm[pr->h[i] & cr->cm_mask] = cr->limit;
                }
                break;
            case ZP_MIX2: {
                int32_t err = ((y * 32767 - zp_squash(t, pr->p[i])) * cp[4]) >> 5;
                int64_t w = cr->a16[cr->cxt];
                w += ((int64_t)err * (pr->p[cp[2]] - pr->p[cp[3]]) + (1 << 12)) >> 13;
                if (w < 0) w = 0;
                if (w > 65535) w = 65535;
                cr->a16[cr->cxt] = (uint16_t)w;
                break;
            }
            case ZP_MIX: {
                uint32_t m = cp[3], j;
                int32_t err = ((y * 32767 - zp_squash(t, pr->p[i])) * cp[4]) >> 4;
                int32_t *wt = (int32_t *)&cr->cm[cr->cxt];
                for (j = 0; j < m; ++j)
                    wt[j] = zp_clamp512k(
                        (int64_t)wt[j] +
                        (((int64_t)err * pr->p[cp[2] + j] + (1 << 12)) >> 13));
                break;
            }
            case ZP_ISSE: {
                int32_t err = y * 32767 - zp_squash(t, pr->p[i]);
                int32_t *wt = (int32_t *)&cr->cm[cr->cxt * 2U];
                wt[0] = zp_clamp512k((int64_t)wt[0] +
                                     (((int64_t)err * pr->p[cp[2]] + (1 << 12)) >> 13));
                wt[1] = zp_clamp512k((int64_t)wt[1] + ((err + 16) >> 5));
                cr->ht[cr->c + (pr->hmap4 & 15U)] =
                    t->ns[cr->cxt * 4U + (uint32_t)y];
                break;
            }
            default:
                break;
        }
    }
    pr->c8 += pr->c8 + (uint32_t)y;
    if (pr->c8 >= 256U) {
        if (!zp_vm_run(&pr->hcomp, pr->c8 - 256U)) return false;
        pr->hmap4 = 1U;
        pr->c8 = 1U;
        for (i = 0; i < pr->n; ++i)
            pr->h[i] = pr->hcomp.h[(uint32_t)i & pr->hcomp.hmask];
    } else if (pr->c8 >= 16U && pr->c8 < 32U) {
        pr->hmap4 = ((pr->hmap4 & 15U) << 5) | ((uint32_t)y << 4) | 1U;
    } else {
        pr->hmap4 = (pr->hmap4 & 0x1F0U) | (((pr->hmap4 & 15U) * 2U + (uint32_t)y) & 15U);
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Post-processor                                                          */

typedef struct zp_pp_s {
    int state;       /* 0 start, 1 pass, 2..4 loading PCOMP, 5 running */
    uint32_t size, got;
    uint8_t *code;
    zp_vm vm;
    int ph, pm;
    bool has_vm;
} zp_pp;

static void zp_pp_free(zp_pp *pp) {
    if (pp->code) xx_mem_free(pp->code);
    pp->code = NULL;
    zp_vm_free(&pp->vm);
    pp->has_vm = false;
}

/* c is a decoded byte, or -1 at the end of a segment. */
static bool zp_pp_write(zp_pp *pp, int c, zp_sink *sink, uint64_t *used) {
    switch (pp->state) {
        case 0:
            if (c < 0) return true; /* empty first segment */
            if (c > 1) return false;
            pp->state = c + 1;
            return true;
        case 1:
            if (c >= 0) zp_sink_put(sink, c);
            return !sink->bad;
        case 2:
            if (c < 0) return false;
            pp->size = (uint32_t)c;
            pp->state = 3;
            return true;
        case 3:
            if (c < 0) return false;
            pp->size += (uint32_t)c * 256U;
            if (pp->size < 1U) return false;
            pp->code = (uint8_t *)xx_mem_alloc(pp->size + 1U);
            if (!pp->code) return false;
            pp->got = 0U;
            pp->state = 4;
            return true;
        case 4:
            if (c < 0) return false;
            pp->code[pp->got++] = (uint8_t)c;
            if (pp->got == pp->size) {
                pp->code[pp->size] = 0U;
                pp->vm.code = pp->code;
                pp->vm.length = pp->size + 1U;
                if (!zp_vm_alloc(&pp->vm, pp->ph, pp->pm, used)) return false;
                pp->has_vm = true;
                pp->state = 5;
            }
            return true;
        default:
            pp->vm.out = sink;
            if (!zp_vm_run(&pp->vm, (uint32_t)c)) return false;
            return !sink->bad;
    }
}

/* ---------------------------------------------------------------------- */
/* Block and segment decoding                                              */

typedef struct zp_block_s {
    zp_in *in;
    int64_t start;        /* tag, or "zPQ" */
    int level;
    uint32_t hsize;
    uint8_t *hdr;         /* hsize bytes: hh hm ph pm n ... */
    uint8_t *hcomp;       /* HCOMP code, 0-terminated */
    uint32_t hcomp_length;
    zp_pred *pred;        /* NULL when stored (n = 0) */
    zp_pp pp;
    uint32_t low, high, curr;
    uint64_t used;        /* model bytes */
    uint32_t segment;     /* segments begun so far */
    bool ended;           /* 255 seen */
    bool bad;
    bool sha_mismatch;    /* last segment decoded, but its SHA-1 differs */
} zp_block;

static void zp_block_close(zp_block *b) {
    if (b->pred) zp_pred_free(b->pred);
    b->pred = NULL;
    zp_pp_free(&b->pp);
    if (b->hdr) xx_mem_free(b->hdr);
    b->hdr = NULL;
    b->hcomp = NULL;
}

/* Validate the component list and split off the HCOMP code. */
static bool zp_parse_header(const uint8_t *hdr, uint32_t hsize,
                            uint32_t *hcomp_at) {
    uint32_t at = 5U;
    int i, n;
    if (hsize < XX_ZPAQ_MIN_HEADER_SIZE || hdr[hsize - 1U] != 0U) return false;
    n = hdr[4];
    for (i = 0; i < n; ++i) {
        uint8_t type;
        if (at >= hsize) return false;
        type = hdr[at];
        if (type < ZP_CONS || type > ZP_SSE) return false;
        if (at + zp_compsize[type] > hsize) return false;
        at += zp_compsize[type];
    }
    if (at >= hsize || hdr[at] != 0U) return false;
    ++at;
    if (at >= hsize) return false;
    *hcomp_at = at;
    return true;
}

static bool zp_block_open(zp_block *b, zp_in *in, int64_t offset) {
    uint8_t prefix[XX_ZPAQ_TAG_SIZE];
    uint32_t hcomp_at;
    xx_mem_zero(b, sizeof(*b));
    b->in = in;
    b->start = offset;
    zp_in_seek(in, offset);
    if (offset > in->end - 7) return false;
    if (offset <= in->end - (XX_ZPAQ_TAG_SIZE + 7) &&
        zp_read(in, prefix, XX_ZPAQ_TAG_SIZE) &&
        xx_rt_memcmp(prefix, XX_ZPAQ_TAG, XX_ZPAQ_TAG_SIZE) == 0) {
        /* tagged */
    } else {
        zp_in_seek(in, offset);
    }
    if (!zp_read(in, prefix, 7U) || prefix[0] != 'z' || prefix[1] != 'P' ||
        prefix[2] != 'Q' || (prefix[3] != 1U && prefix[3] != 2U) ||
        prefix[4] != XX_ZPAQ_BLOCK_TYPE)
        return false;
    b->level = prefix[3];
    b->hsize = (uint32_t)prefix[5] | ((uint32_t)prefix[6] << 8);
    if (b->hsize < XX_ZPAQ_MIN_HEADER_SIZE) return false;
    b->hdr = (uint8_t *)xx_mem_alloc(b->hsize);
    if (!b->hdr) return false;
    if (!zp_read(in, b->hdr, b->hsize) ||
        !zp_parse_header(b->hdr, b->hsize, &hcomp_at)) {
        zp_block_close(b);
        return false;
    }
    b->hcomp = b->hdr + hcomp_at;
    b->hcomp_length = b->hsize - hcomp_at;
    if (b->hdr[4] != 0U) {
        b->pred = zp_pred_create(b->hdr, b->hcomp, b->hcomp_length, &b->used);
        if (!b->pred) {
            zp_block_close(b);
            return false;
        }
    }
    b->pp.ph = b->hdr[2];
    b->pp.pm = b->hdr[3];
    b->low = b->pred ? 1U : 0U;
    b->high = b->pred ? 0xFFFFFFFFU : 0U;
    b->curr = 0U;
    return true;
}

static int zp_decode_bit(zp_block *b, int p) {
    uint32_t mid;
    int y;
    if (b->curr < b->low || b->curr > b->high) return -1;
    mid = b->low + (uint32_t)(((uint64_t)(b->high - b->low) * (uint32_t)p) >> 16);
    if (b->curr <= mid) {
        y = 1;
        b->high = mid;
    } else {
        y = 0;
        b->low = mid + 1U;
    }
    while ((b->high ^ b->low) < 0x1000000U) {
        int c;
        b->high = (b->high << 8) | 255U;
        b->low <<= 8;
        b->low += (b->low == 0U);
        c = zp_get(b->in);
        if (c < 0) return -1;
        b->curr = (b->curr << 8) | (uint32_t)c;
    }
    return y;
}

/* Next decoded byte, -1 at the end of the segment, -2 on error. */
static int zp_decompress(zp_block *b) {
    if (b->pred) {
        int y, c = 1, i;
        if (b->curr == 0U) {
            for (i = 0; i < 4; ++i) {
                int v = zp_get(b->in);
                if (v < 0) return -2;
                b->curr = (b->curr << 8) | (uint32_t)v;
            }
        }
        y = zp_decode_bit(b, 0);
        if (y < 0) return -2;
        if (y) return b->curr == 0U ? -1 : -2;
        while (c < 256) {
            int p = zp_predict(b->pred) * 2 + 1;
            y = zp_decode_bit(b, p);
            if (y < 0) return -2;
            c += c + y;
            if (!zp_update(b->pred, y)) return -2;
        }
        return c - 256;
    } else {
        int v;
        if (b->curr == 0U) {
            int i;
            for (i = 0; i < 4; ++i) {
                v = zp_get(b->in);
                if (v < 0) return -2;
                b->curr = (b->curr << 8) | (uint32_t)v;
            }
            if (b->curr == 0U) return -1;
        }
        --b->curr;
        v = zp_get(b->in);
        return v < 0 ? -2 : v;
    }
}

typedef struct zp_seghead_s {
    char *name;
    size_t name_length;
    char *comment;
    size_t comment_length;
} zp_seghead;

static void zp_seghead_free(zp_seghead *s) {
    if (s->name) xx_mem_free(s->name);
    if (s->comment) xx_mem_free(s->comment);
    s->name = s->comment = NULL;
}

static char *zp_read_string(zp_in *in, size_t *length) {
    size_t capacity = 64U, used = 0U;
    char *text = (char *)xx_mem_alloc(capacity);
    if (!text) return NULL;
    for (;;) {
        int c = zp_get(in);
        if (c < 0 || used >= ZP_MAX_NAME) {
            xx_mem_free(text);
            return NULL;
        }
        if (used + 1U >= capacity) {
            char *grown = (char *)xx_mem_realloc(text, capacity * 2U);
            if (!grown) {
                xx_mem_free(text);
                return NULL;
            }
            text = grown;
            capacity *= 2U;
        }
        text[used] = (char)c;
        if (c == 0) break;
        ++used;
    }
    *length = used;
    return text;
}

/* 1 = a segment follows (header read), 0 = end of block, -1 = error. */
static int zp_segment_begin(zp_block *b, zp_seghead *head) {
    int c;
    xx_mem_zero(head, sizeof(*head));
    if (b->ended || b->bad) return b->ended ? 0 : -1;
    c = zp_get(b->in);
    if (c == 255) {
        b->ended = true;
        return 0;
    }
    if (c != 1) {
        b->bad = true;
        return -1;
    }
    head->name = zp_read_string(b->in, &head->name_length);
    head->comment = head->name ? zp_read_string(b->in, &head->comment_length)
                               : NULL;
    if (!head->name || !head->comment || zp_get(b->in) != 0) {
        zp_seghead_free(head);
        b->bad = true;
        return -1;
    }
    ++b->segment;
    return 1;
}

/* Decode the current segment's data into sink (limit = most bytes it may
 * produce) and check its end marker and SHA-1. */
static bool zp_segment_decode(zp_block *b, zp_sink *sink, xx_pd_struct *pd) {
    uint64_t decoded = 0U, max_decoded;
    uint8_t digest[20], stored[20];
    int c;
    max_decoded = sink->limit + ((uint64_t)1U << 20);
    for (;;) {
        c = zp_decompress(b);
        if (c == -1) break;
        if (c < 0 || ++decoded > max_decoded) goto fail;
        if (!zp_pp_write(&b->pp, c, sink, &b->used)) goto fail;
        if ((decoded & 0xFFFFU) == 0U && pd && xx_pd_is_stopped(pd)) goto fail;
    }
    if (!zp_pp_write(&b->pp, -1, sink, &b->used)) goto fail;
    zp_sink_flush(sink);
    if (sink->bad) goto fail;
    c = zp_get(b->in);
    if (c == 253) {
        if (!zp_read(b->in, stored, 20U) ||
            !xx_hash_final(&sink->sha, digest, sizeof(digest)) ||
            xx_rt_memcmp(digest, stored, 20U) != 0) {
            /* The stream itself is intact: only this segment is wrong. */
            b->sha_mismatch = true;
            return false;
        }
    } else if (c != 254) {
        goto fail;
    }
    return true;
fail:
    b->bad = true;
    return false;
}

/* Skip one whole segment's data. */
static bool zp_segment_skip(zp_block *b, uint64_t *size, uint64_t limit,
                            xx_pd_struct *pd) {
    zp_sink *sink = (zp_sink *)xx_mem_alloc(sizeof(zp_sink));
    bool ok;
    if (!sink) return false;
    zp_sink_start(sink, NULL, NULL, limit);
    ok = zp_segment_decode(b, sink, pd);
    if (size) *size = sink->count;
    xx_mem_free(sink);
    return ok;
}

/* Memory sink */
typedef struct zp_membuf_s {
    uint8_t *data;
    size_t size, capacity, limit;
} zp_membuf;

static bool zp_membuf_write(void *context, const uint8_t *data, size_t size) {
    zp_membuf *mb = (zp_membuf *)context;
    if (size > mb->limit - mb->size) return false;
    if (mb->size + size > mb->capacity) {
        size_t want = mb->capacity ? mb->capacity : 65536U;
        uint8_t *grown;
        while (want < mb->size + size) want *= 2U;
        if (want > mb->limit) want = mb->limit;
        grown = (uint8_t *)xx_mem_realloc(mb->data, want);
        if (!grown) return false;
        mb->data = grown;
        mb->capacity = want;
    }
    xx_mem_copy(mb->data + mb->size, data, size);
    mb->size += size;
    return true;
}

/* Decode the current segment into memory, at most limit bytes. */
static bool zp_segment_to_memory(zp_block *b, uint64_t limit, zp_membuf *mb,
                                 xx_pd_struct *pd) {
    zp_sink *sink = (zp_sink *)xx_mem_alloc(sizeof(zp_sink));
    bool ok;
    if (!sink) return false;
    xx_mem_zero(mb, sizeof(*mb));
    mb->limit = (size_t)limit;
    zp_sink_start(sink, zp_membuf_write, mb, limit);
    ok = zp_segment_decode(b, sink, pd);
    xx_mem_free(sink);
    if (!ok) {
        if (mb->data) xx_mem_free(mb->data);
        xx_mem_zero(mb, sizeof(*mb));
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Extraction names                                                        */
/*
 * Adapted from the member-name handling of src/formats/ar/xx_ar.c (this
 * library, MIT): the same device-name, UTF-8 and case-fold rules, so that
 * two members are never written to one file. Unlike ar, a ".." component
 * refuses the member outright.
 */

static char zp_upper(char ch) {
    return (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
}

static bool zp_equal_ci(const char *text, const char *word, size_t length) {
    size_t i;
    for (i = 0; i < length; ++i)
        if (zp_upper(text[i]) != word[i]) return false;
    return true;
}

static bool zp_is_device_name(const char *component, size_t length) {
    static const char *const k_devices[] = {"CON",    "PRN",     "AUX", "NUL",
                                            "CONIN$", "CONOUT$", "CLOCK$"};
    size_t base = 0U, i;
    while (base < length && component[base] != '.') ++base;
    while (base > 0U && component[base - 1U] == ' ') --base;
    for (i = 0; i < sizeof(k_devices) / sizeof(k_devices[0]); ++i)
        if (xx_str_len(k_devices[i]) == base &&
            zp_equal_ci(component, k_devices[i], base))
            return true;
    if (base >= 4U &&
        (zp_equal_ci(component, "COM", 3U) || zp_equal_ci(component, "LPT", 3U))) {
        const unsigned char *tail = (const unsigned char *)component + 3;
        if (base == 4U && tail[0] >= '0' && tail[0] <= '9') return true;
        if (base == 5U && tail[0] == 0xC2U &&
            (tail[1] == 0xB9U || tail[1] == 0xB2U || tail[1] == 0xB3U))
            return true;
    }
    return false;
}

static size_t zp_utf8_length(const unsigned char *text) {
    unsigned char lead = text[0], low = 0x80U, high = 0xBFU;
    size_t length, i;
    if (lead < 0x80U) return 1U;
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2U;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3U;
        if (lead == 0xE0U) low = 0xA0U;
        else if (lead == 0xEDU) high = 0x9FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4U;
        if (lead == 0xF0U) low = 0x90U;
        else if (lead == 0xF4U) high = 0x8FU;
    } else {
        return 0U;
    }
    if (text[1] < low || text[1] > high) return 0U;
    for (i = 2U; i < length; ++i)
        if (text[i] < 0x80U || text[i] > 0xBFU) return 0U;
    return length;
}

/* Relative path a member is written to: a drive prefix and leading
 * separators are dropped (zpaq stores paths as given, often absolute),
 * empty and "." components are dropped, trailing dots and spaces trimmed,
 * bytes that are not UTF-8 and the characters :<>"|?* written as %XX (zpaq
 * on Windows stores alternate data streams as "name:stream:$DATA"; they
 * become ordinary files). A ".." component, a control character, a device
 * name or an empty result: NULL. */
static char *zp_make_extract_name(const char *name) {
    const char *cursor = name;
    char *result;
    size_t out = 0U, length_in = 0U;
    const unsigned char *at;
    if (!name) return NULL;
    if (((cursor[0] >= 'A' && cursor[0] <= 'Z') ||
         (cursor[0] >= 'a' && cursor[0] <= 'z')) && cursor[1] == ':')
        cursor += 2;
    while (*cursor == '/' || *cursor == '\\') ++cursor;
    for (at = (const unsigned char *)cursor; *at != 0U;) {
        size_t sequence = zp_utf8_length(at);
        length_in += (sequence == 0U || sequence == 1U) ? 3U : sequence;
        at += sequence == 0U ? 1U : sequence;
    }
    result = xx_str_create_len(length_in);
    if (!result) return NULL;
    while (*cursor != '\0') {
        const char *component = cursor;
        size_t length, kept, i;
        while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
            unsigned char ch = (unsigned char)*cursor;
            if (ch < 32U || ch == 127U) {
                xx_str_free(result);
                return NULL;
            }
            ++cursor;
        }
        length = (size_t)(cursor - component);
        if (*cursor != '\0') ++cursor;
        if (length == 0U || (length == 1U && component[0] == '.')) continue;
        if (length == 2U && component[0] == '.' && component[1] == '.') {
            xx_str_free(result);
            return NULL;
        }
        kept = length;
        while (kept > 0U &&
               (component[kept - 1U] == '.' || component[kept - 1U] == ' '))
            --kept;
        if (kept == 0U || zp_is_device_name(component, kept)) {
            xx_str_free(result);
            return NULL;
        }
        if (out != 0U) result[out++] = '/';
        for (i = 0U; i < kept;) {
            const unsigned char *p = (const unsigned char *)component + i;
            size_t sequence = zp_utf8_length(p);
            if (sequence == 0U || p[0] == ':' || p[0] == '<' || p[0] == '>' ||
                p[0] == '"' || p[0] == '|' || p[0] == '?' || p[0] == '*') {
                static const char k_hex[] = "0123456789ABCDEF";
                result[out++] = '%';
                result[out++] = k_hex[p[0] >> 4];
                result[out++] = k_hex[p[0] & 15U];
                ++i;
                continue;
            }
            xx_mem_copy(result + out, p, sequence);
            out += sequence;
            i += sequence;
        }
    }
    if (out == 0U) {
        xx_str_free(result);
        return NULL;
    }
    result[out] = '\0';
    return result;
}

#define ZP_FOLD_CASED 0x110000U
#define ZP_FOLD_BAD_BYTE 0x110100U

static uint32_t zp_fold_code_point(uint32_t cp) {
    static const uint32_t k_cased[][2] = {
        {0x0180U, 0x02AFU}, {0x0345U, 0x0345U}, {0x0370U, 0x052FU},
        {0x0531U, 0x058FU}, {0x10A0U, 0x10FFU}, {0x13A0U, 0x13FFU},
        {0x1C80U, 0x1CBFU}, {0x1D00U, 0x1DBFU}, {0x1E00U, 0x1FFFU},
        {0x2100U, 0x218FU}, {0x24B6U, 0x24E9U}, {0x2C00U, 0x2D2FU},
        {0xA640U, 0xA69FU}, {0xA720U, 0xA7FFU}, {0xAB30U, 0xABBFU},
        {0xFF21U, 0xFF5AU}, {0x10400U, 0x104FFU}, {0x10570U, 0x105BFU},
        {0x10C80U, 0x10CFFU}, {0x118A0U, 0x118FFU}, {0x16E40U, 0x16E9FU},
        {0x1E900U, 0x1E95FU}};
    size_t i;
    if (cp < 0x80U) return (cp >= 'a' && cp <= 'z') ? cp - 0x20U : cp;
    if (cp < 0x100U) {
        if (cp == 0xB5U || cp == 0xDFU) return ZP_FOLD_CASED;
        if (cp >= 0xE0U && cp <= 0xFEU && cp != 0xF7U) return cp - 0x20U;
        return cp == 0xFFU ? 0x178U : cp;
    }
    if (cp < 0x180U) {
        if (cp == 0x130U || cp == 0x131U) return 'I';
        if (cp == 0x17FU) return 'S';
        if (cp == 0x138U || cp == 0x149U || cp == 0x178U) return cp;
        if ((cp >= 0x139U && cp <= 0x148U) || cp >= 0x179U)
            return (cp & 1U) != 0U ? cp : cp - 1U;
        return cp & ~1U;
    }
    for (i = 0; i < sizeof(k_cased) / sizeof(k_cased[0]); ++i)
        if (cp >= k_cased[i][0] && cp <= k_cased[i][1]) return ZP_FOLD_CASED;
    return cp;
}

static uint32_t zp_fold_next(const unsigned char **cursor) {
    const unsigned char *at = *cursor;
    size_t length;
    uint32_t cp;
    if (at[0] == 0U) return 0U;
    length = zp_utf8_length(at);
    if (length == 0U) {
        *cursor = at + 1;
        return ZP_FOLD_BAD_BYTE + at[0];
    }
    if (length == 1U) {
        cp = at[0];
    } else if (length == 2U) {
        cp = ((uint32_t)(at[0] & 0x1FU) << 6) | (uint32_t)(at[1] & 0x3FU);
    } else if (length == 3U) {
        cp = ((uint32_t)(at[0] & 0x0FU) << 12) | ((uint32_t)(at[1] & 0x3FU) << 6) |
             (uint32_t)(at[2] & 0x3FU);
    } else {
        cp = ((uint32_t)(at[0] & 0x07U) << 18) | ((uint32_t)(at[1] & 0x3FU) << 12) |
             ((uint32_t)(at[2] & 0x3FU) << 6) | (uint32_t)(at[3] & 0x3FU);
    }
    *cursor = at + length;
    return zp_fold_code_point(cp);
}

static int zp_compare_fold(const char *x, const char *y, bool prefix_only) {
    const unsigned char *a = (const unsigned char *)x;
    const unsigned char *b = (const unsigned char *)y;
    for (;;) {
        uint32_t ua = zp_fold_next(&a), ub = zp_fold_next(&b);
        if (prefix_only && ub == 0U) return 0;
        if (ua != ub) return ua < ub ? -1 : 1;
        if (ua == 0U) return 0;
    }
}

typedef struct zp_name_key_s {
    const char *key;
    size_t item;
} zp_name_key;

static int zp_compare_keys(const void *left, const void *right) {
    const zp_name_key *a = (const zp_name_key *)left;
    const zp_name_key *b = (const zp_name_key *)right;
    int order = zp_compare_fold(a->key, b->key, false);
    if (order != 0) return order;
    return a->item < b->item ? -1 : (a->item > b->item ? 1 : 0);
}

static char *zp_suffixed_name(const char *name, uint32_t number) {
    char digits[12];
    size_t digit_count = 0U, length = xx_str_len(name), start = 0U, dot, i,
           at = 0U;
    char *result;
    for (i = 0; i < length; ++i)
        if (name[i] == '/' || name[i] == '\\') start = i + 1U;
    dot = length;
    for (i = start; i < length; ++i)
        if (name[i] == '.') dot = i;
    if (dot == start) dot = length;
    do {
        digits[digit_count++] = (char)('0' + (number % 10U));
        number /= 10U;
    } while (number != 0U && digit_count < sizeof(digits));
    result = xx_str_create_len(length + digit_count + 1U);
    if (!result) return NULL;
    for (i = 0; i < dot; ++i) result[at++] = name[i];
    result[at++] = '_';
    while (digit_count != 0U) result[at++] = digits[--digit_count];
    for (i = dot; i < length; ++i) result[at++] = name[i];
    result[at] = '\0';
    return result;
}

static size_t zp_lower_bound(const zp_name_key *keys, size_t used,
                             const char *text, bool prefix_only) {
    size_t low = 0U, high = used;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (zp_compare_fold(keys[middle].key, text, prefix_only) < 0)
            low = middle + 1U;
        else
            high = middle;
    }
    return low;
}

static bool zp_key_exists(const zp_name_key *keys, size_t used,
                          const char *text) {
    size_t at = zp_lower_bound(keys, used, text, false);
    return at < used && zp_compare_fold(keys[at].key, text, false) == 0;
}

static int zp_key_is_directory(const zp_name_key *keys, size_t used,
                               const char *text) {
    char *probe = xx_str_concat(text, "/");
    size_t at;
    int found;
    if (!probe) return -1;
    at = zp_lower_bound(keys, used, probe, true);
    found = at < used && zp_compare_fold(keys[at].key, probe, true) == 0;
    xx_str_free(probe);
    return found;
}

/* ---------------------------------------------------------------------- */
/* Archive walk                                                            */

typedef struct zp_dblock_s {
    int64_t offset;     /* -1: not locatable */
    uint64_t usize;     /* decoded size: fragments, sizes, 8 bytes */
    uint32_t first, count;
} zp_dblock;

typedef struct zp_frag_s {
    uint32_t dblock;    /* index + 1; 0 = undefined */
    uint32_t offset;
    uint32_t size;
    uint8_t sha[20];
} zp_frag;

typedef struct zp_piece_s {
    int64_t block;
    uint32_t segment;
    uint64_t size;
} zp_piece;

typedef struct zp_entry_s {
    char *name;
    char *extract_name;
    char *record_name;
    uint64_t size;
    uint32_t attr;
    bool has_attr;
    bool is_dir;
    bool deleted;
    bool broken;
    bool journal;
    uint32_t *ids;
    uint32_t nids;
    zp_piece *pieces;
    uint32_t npieces, cpieces;
} zp_entry;

struct xx_zpaq_scan {
    bool ok;
    bool journaling;
    int64_t end;
    zp_entry *entries;
    uint32_t count, capacity;
    uint32_t *table;       /* journaling name hash: entry index + 1 */
    uint32_t table_size;
    zp_frag *frags;
    uint32_t nfrags;
    zp_dblock *dblocks;
    uint32_t ndblocks, cdblocks;
    uint32_t *visible;
    uint32_t nvisible;
    int64_t last_stream;   /* entry continued by an unnamed segment, or -1 */
    int64_t d_cursor, d_limit;
    bool stop;
};

static void zp_scan_free(struct xx_zpaq_scan *s) {
    uint32_t i;
    if (!s) return;
    for (i = 0; i < s->count; ++i) {
        zp_entry *e = &s->entries[i];
        if (e->name) xx_mem_free(e->name);
        if (e->extract_name) xx_str_free(e->extract_name);
        if (e->record_name) xx_str_free(e->record_name);
        if (e->ids) xx_mem_free(e->ids);
        if (e->pieces) xx_mem_free(e->pieces);
    }
    if (s->entries) xx_mem_free(s->entries);
    if (s->table) xx_mem_free(s->table);
    if (s->frags) xx_mem_free(s->frags);
    if (s->dblocks) xx_mem_free(s->dblocks);
    if (s->visible) xx_mem_free(s->visible);
    xx_mem_free(s);
}

static uint32_t zp_hash_name(const char *name) {
    uint32_t h = 2166136261U;
    while (*name) h = (h ^ (uint8_t)*name++) * 16777619U;
    return h;
}

static bool zp_table_grow(struct xx_zpaq_scan *s) {
    uint32_t size = s->table_size ? s->table_size * 2U : 1024U, i;
    uint32_t *table = (uint32_t *)xx_mem_calloc(size, sizeof(uint32_t));
    if (!table) return false;
    for (i = 0; i < s->count; ++i) {
        uint32_t at;
        if (!s->entries[i].journal) continue;
        at = zp_hash_name(s->entries[i].name) & (size - 1U);
        while (table[at]) at = (at + 1U) & (size - 1U);
        table[at] = i + 1U;
    }
    if (s->table) xx_mem_free(s->table);
    s->table = table;
    s->table_size = size;
    return true;
}

static zp_entry *zp_new_entry(struct xx_zpaq_scan *s, const char *name,
                              size_t length) {
    zp_entry *e;
    if (s->count >= ZP_MAX_ENTRIES) return NULL;
    if (s->count == s->capacity) {
        uint32_t capacity = s->capacity ? s->capacity * 2U : 64U;
        zp_entry *grown = (zp_entry *)xx_mem_realloc(
            s->entries, (size_t)capacity * sizeof(zp_entry));
        if (!grown) return NULL;
        s->entries = grown;
        s->capacity = capacity;
    }
    e = &s->entries[s->count];
    xx_mem_zero(e, sizeof(*e));
    e->name = (char *)xx_mem_alloc(length + 1U);
    if (!e->name) return NULL;
    xx_mem_copy(e->name, name, length);
    e->name[length] = '\0';
    ++s->count;
    return e;
}

/* Journaling entry by name, created when create is set. */
static zp_entry *zp_journal_entry(struct xx_zpaq_scan *s, const char *name,
                                  size_t length, bool create) {
    uint32_t at;
    zp_entry *e;
    if (!s->table && !zp_table_grow(s)) return NULL;
    at = zp_hash_name(name) & (s->table_size - 1U);
    while (s->table[at]) {
        e = &s->entries[s->table[at] - 1U];
        if (xx_str_len(e->name) == length &&
            xx_rt_memcmp(e->name, name, length) == 0)
            return e;
        at = (at + 1U) & (s->table_size - 1U);
    }
    if (!create) return NULL;
    if ((s->count + 1U) * 2U > s->table_size) {
        if (!zp_table_grow(s)) return NULL;
        return zp_journal_entry(s, name, length, create);
    }
    e = zp_new_entry(s, name, length);
    if (!e) return NULL;
    e->journal = true;
    s->table[at] = s->count;
    return e;
}

static uint32_t zp_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t zp_le64(const uint8_t *p) {
    return (uint64_t)zp_le32(p) | ((uint64_t)zp_le32(p + 4) << 32);
}

/* "jDC" YYYYMMDDHHMMSS type NNNNNNNNNN */
static bool zp_parse_jdc(const zp_seghead *head, char *type, uint32_t *number) {
    const char *n = head->name;
    uint64_t value = 0U;
    size_t i;
    if (head->name_length != 28U || n[0] != 'j' || n[1] != 'D' || n[2] != 'C')
        return false;
    for (i = 3U; i < 17U; ++i)
        if (n[i] < '0' || n[i] > '9') return false;
    if (n[17] != 'c' && n[17] != 'd' && n[17] != 'h' && n[17] != 'i')
        return false;
    for (i = 18U; i < 28U; ++i) {
        if (n[i] < '0' || n[i] > '9') return false;
        value = value * 10U + (uint64_t)(n[i] - '0');
    }
    if (value > 0xFFFFFFFFU) return false;
    *type = n[17];
    *number = (uint32_t)value;
    return true;
}

/* Leading decimal number of a segment comment (the size), or -1. */
static int64_t zp_comment_size(const zp_seghead *head) {
    uint64_t value = 0U;
    size_t i = 0U;
    while (i < head->comment_length && head->comment[i] == ' ') ++i;
    if (i >= head->comment_length || head->comment[i] < '0' ||
        head->comment[i] > '9')
        return -1;
    while (i < head->comment_length && head->comment[i] >= '0' &&
           head->comment[i] <= '9') {
        value = value * 10U + (uint64_t)(head->comment[i] - '0');
        if (value > ((uint64_t)1U << 60)) return -1;
        ++i;
    }
    return (int64_t)value;
}

/* Consume the rest of a block after its first segment. */
static bool zp_block_finish(zp_block *b, xx_pd_struct *pd) {
    int guard;
    for (guard = 0; guard < 1000000; ++guard) {
        zp_seghead head;
        int r = zp_segment_begin(b, &head);
        if (r <= 0) return r == 0;
        zp_seghead_free(&head);
        if (!zp_segment_skip(b, NULL, ZP_MAX_UNKNOWN_OUTPUT, pd)) return false;
    }
    return false;
}

static bool zp_parse_h(struct xx_zpaq_scan *s, uint32_t first,
                       const zp_membuf *mb) {
    uint32_t k, i;
    uint64_t sum = 0U, csize;
    zp_dblock *d;
    if (mb->size < 4U || (mb->size - 4U) % 24U != 0U) return false;
    k = (uint32_t)((mb->size - 4U) / 24U);
    csize = zp_le32(mb->data);
    if (first == 0U || (uint64_t)first + k > ZP_MAX_FRAGMENTS) return false;
    if (s->ndblocks == s->cdblocks) {
        uint32_t capacity = s->cdblocks ? s->cdblocks * 2U : 64U;
        zp_dblock *grown = (zp_dblock *)xx_mem_realloc(
            s->dblocks, (size_t)capacity * sizeof(zp_dblock));
        if (!grown) return false;
        s->dblocks = grown;
        s->cdblocks = capacity;
    }
    if (first + k > s->nfrags) {
        uint32_t size = first + k;
        zp_frag *grown =
            (zp_frag *)xx_mem_realloc(s->frags, (size_t)size * sizeof(zp_frag));
        if (!grown) return false;
        xx_mem_zero(grown + s->nfrags, (size_t)(size - s->nfrags) * sizeof(zp_frag));
        s->frags = grown;
        s->nfrags = size;
    }
    d = &s->dblocks[s->ndblocks];
    d->first = first;
    d->count = k;
    d->offset = -1;
    if (s->d_cursor >= 0 && csize > 0U &&
        (int64_t)csize <= s->d_limit - s->d_cursor) {
        d->offset = s->d_cursor;
        s->d_cursor += (int64_t)csize;
    } else {
        s->d_cursor = -1;
    }
    for (i = 0; i < k; ++i) {
        const uint8_t *row = mb->data + 4U + (size_t)i * 24U;
        zp_frag *f = &s->frags[first + i];
        xx_mem_copy(f->sha, row, 20U);
        f->size = zp_le32(row + 20);
        f->offset = (uint32_t)sum;
        f->dblock = s->ndblocks + 1U;
        sum += f->size;
        if (sum > ZP_MAX_MEMBLOCK) return false;
    }
    d->usize = sum + 4U * (uint64_t)k + 8U;
    if (d->usize > ZP_MAX_MEMBLOCK) return false;
    ++s->ndblocks;
    return true;
}

static bool zp_parse_i(struct xx_zpaq_scan *s, const zp_membuf *mb) {
    size_t p = 0U;
    const uint8_t *d = mb->data;
    while (p < mb->size) {
        uint64_t date;
        size_t start, length;
        zp_entry *e;
        if (mb->size - p < 9U) return false;
        date = zp_le64(d + p);
        p += 8U;
        start = p;
        while (p < mb->size && d[p] != 0U) ++p;
        if (p >= mb->size) return false;
        length = p - start;
        ++p;
        if (length == 0U) return false;
        if (date == 0U) {
            e = zp_journal_entry(s, (const char *)d + start, length, false);
            if (e) e->deleted = true;
        } else {
            uint32_t na, ni, j;
            const uint8_t *attr;
            if (mb->size - p < 4U) return false;
            na = zp_le32(d + p);
            p += 4U;
            if (na > mb->size - p) return false;
            attr = d + p;
            p += na;
            if (mb->size - p < 4U) return false;
            ni = zp_le32(d + p);
            p += 4U;
            if (ni > (mb->size - p) / 4U) return false;
            e = zp_journal_entry(s, (const char *)d + start, length, true);
            if (!e) return false;
            if (e->ids) xx_mem_free(e->ids);
            e->ids = NULL;
            e->nids = 0U;
            e->deleted = false;
            e->has_attr = false;
            if (na >= 5U && (attr[0] == 'w' || attr[0] == 'u')) {
                e->attr = zp_le32(attr + 1);
                e->has_attr = true;
            }
            if (ni) {
                e->ids = (uint32_t *)xx_mem_alloc((size_t)ni * sizeof(uint32_t));
                if (!e->ids) return false;
                for (j = 0; j < ni; ++j) e->ids[j] = zp_le32(d + p + 4U * j);
                e->nids = ni;
            }
            p += 4U * (size_t)ni;
        }
    }
    return true;
}

static bool zp_add_piece(zp_entry *e, int64_t block, uint32_t segment,
                         uint64_t size) {
    if (e->npieces == e->cpieces) {
        uint32_t capacity = e->cpieces ? e->cpieces * 2U : 4U;
        zp_piece *grown;
        if (capacity > 0x1000000U) return false;
        grown = (zp_piece *)xx_mem_realloc(e->pieces,
                                            (size_t)capacity * sizeof(zp_piece));
        if (!grown) return false;
        e->pieces = grown;
        e->cpieces = capacity;
    }
    e->pieces[e->npieces].block = block;
    e->pieces[e->npieces].segment = segment;
    e->pieces[e->npieces].size = size;
    ++e->npieces;
    e->size += size;
    return true;
}

/* Walk one block at pos. false: not a (complete) block, stop there. */
static bool zp_scan_block(struct xx_zpaq_scan *s, zp_in *in, int64_t pos,
                          int64_t *next, xx_pd_struct *pd) {
    zp_block *b = (zp_block *)xx_mem_alloc(sizeof(zp_block));
    zp_seghead head;
    zp_membuf mb;
    char type;
    uint32_t number;
    int r;
    bool ok = false;
    if (!b) return false;
    xx_mem_zero(&mb, sizeof(mb));
    if (!zp_block_open(b, in, pos)) {
        xx_mem_free(b);
        return false;
    }
    r = zp_segment_begin(b, &head);
    if (r < 0) goto done;
    if (r == 0) {
        ok = true;
        goto done;
    }
    if (zp_parse_jdc(&head, &type, &number)) {
        int64_t expected = zp_comment_size(&head);
        uint64_t limit = expected >= 0 && (uint64_t)expected < ZP_MAX_MEMBLOCK
                             ? (uint64_t)expected : ZP_MAX_MEMBLOCK;
        zp_seghead_free(&head);
        if (type == 'd') goto done; /* only reached through a bad csize */
        if (type == 'c' && limit > 4096U) limit = 4096U;
        if (!zp_segment_to_memory(b, limit, &mb, pd) || !zp_block_finish(b, pd))
            goto done;
        s->journaling = true;
        if (type == 'c') {
            int64_t csize, block_end = zp_in_tell(in);
            if (mb.size < 8U) goto done;
            csize = (int64_t)zp_le64(mb.data);
            *next = block_end;
            if (csize < 0 || csize > in->end - block_end) {
                /* An unfinished (or cut) transaction: zpaq ignores it and
                 * everything after it. */
                s->stop = true;
            } else {
                s->d_cursor = block_end;
                s->d_limit = block_end + csize;
                *next = s->d_limit;
            }
            ok = true;
            goto done;
        }
        if (type == 'h') ok = zp_parse_h(s, number, &mb);
        else ok = zp_parse_i(s, &mb);
        *next = zp_in_tell(in);
        goto done;
    }
    /* Streaming: every segment is a file or continues one. */
    for (;;) {
        uint64_t size = 0U;
        zp_entry *e;
        uint32_t segment = b->segment - 1U;
        int64_t declared = zp_comment_size(&head);
        uint64_t limit = declared >= 0 && (uint64_t)declared < ZP_MAX_UNKNOWN_OUTPUT
                             ? (uint64_t)declared : ZP_MAX_UNKNOWN_OUTPUT;
        if (head.name_length != 0U) {
            e = zp_new_entry(s, head.name, head.name_length);
            if (!e) {
                zp_seghead_free(&head);
                goto done;
            }
            s->last_stream = (int64_t)(s->count - 1U);
        } else if (s->last_stream >= 0) {
            e = &s->entries[s->last_stream];
        } else {
            e = zp_new_entry(s, "zpaq_stream", 11U);
            if (!e) {
                zp_seghead_free(&head);
                goto done;
            }
            s->last_stream = (int64_t)(s->count - 1U);
        }
        zp_seghead_free(&head);
        b->sha_mismatch = false;
        if (!zp_segment_skip(b, &size, limit, pd)) {
            if (b->bad || !b->sha_mismatch) goto done;
            e->broken = true;
        }
        if (!zp_add_piece(e, pos, segment, size)) goto done;
        r = zp_segment_begin(b, &head);
        if (r < 0) goto done;
        if (r == 0) break;
    }
    *next = zp_in_tell(in);
    ok = true;
done:
    if (mb.data) xx_mem_free(mb.data);
    zp_block_close(b);
    xx_mem_free(b);
    return ok;
}

static bool zp_finalize(struct xx_zpaq_scan *s) {
    zp_name_key *keys;
    uint32_t i, used = 0U;
    s->visible = (uint32_t *)xx_mem_alloc(((size_t)s->count + 1U) * sizeof(uint32_t));
    if (!s->visible) return false;
    for (i = 0; i < s->count; ++i) {
        zp_entry *e = &s->entries[i];
        size_t length = xx_str_len(e->name);
        if (e->deleted) continue;
        if (e->journal) {
            uint32_t j;
            e->size = 0U;
            e->is_dir = length > 0U && (e->name[length - 1U] == '/' ||
                                        e->name[length - 1U] == '\\');
            for (j = 0; j < e->nids; ++j) {
                uint32_t id = e->ids[j];
                if (id == 0U || id >= s->nfrags || s->frags[id].dblock == 0U ||
                    s->dblocks[s->frags[id].dblock - 1U].offset < 0) {
                    e->broken = true;
                    continue;
                }
                e->size += s->frags[id].size;
            }
        }
        e->extract_name = zp_make_extract_name(e->name);
        s->visible[s->nvisible++] = i;
    }
    /* Two files are never written to one path, nor a file where another
     * member needs a directory (see xx_ar.c for the scheme). */
    keys = (zp_name_key *)xx_mem_alloc(((size_t)s->nvisible + 1U) * sizeof(zp_name_key));
    if (!keys) return false;
    for (i = 0; i < s->nvisible; ++i) {
        zp_entry *e = &s->entries[s->visible[i]];
        if (e->extract_name && !e->is_dir) {
            keys[used].key = e->extract_name;
            keys[used].item = s->visible[i];
            ++used;
        }
    }
    if (used >= 2U) {
        size_t group = 0U;
        char **fresh;
        uint32_t *numbers;
        xx_rt_qsort(keys, used, sizeof(*keys), zp_compare_keys);
        fresh = (char **)xx_mem_calloc(used, sizeof(char *));
        numbers = (uint32_t *)xx_mem_calloc(used, sizeof(uint32_t));
        if (!fresh || !numbers) {
            if (fresh) xx_mem_free(fresh);
            if (numbers) xx_mem_free(numbers);
            xx_mem_free(keys);
            return false;
        }
        while (group < used) {
            size_t end = group + 1U, index;
            uint32_t number = 0U;
            int is_directory;
            while (end < used &&
                   zp_compare_fold(keys[end].key, keys[group].key, false) == 0)
                ++end;
            is_directory = zp_key_is_directory(keys, used, keys[group].key);
            if (is_directory < 0) break;
            for (index = is_directory ? group : group + 1U; index < end; ++index) {
                char *candidate = NULL;
                while (number < 0xFFFFFFF0U) {
                    int taken;
                    ++number;
                    candidate = zp_suffixed_name(keys[index].key, number);
                    if (!candidate) break;
                    taken = zp_key_exists(keys, used, candidate)
                                ? 1
                                : zp_key_is_directory(keys, used, candidate);
                    if (taken == 0) break;
                    xx_str_free(candidate);
                    candidate = NULL;
                    if (taken < 0) break;
                }
                fresh[index] = candidate;
                numbers[index] = number;
            }
            group = end;
        }
        for (i = 0; i < used; ++i) {
            zp_entry *e = &s->entries[keys[i].item];
            if (!fresh[i]) continue;
            e->record_name = zp_suffixed_name(e->name, numbers[i]);
            xx_str_free(e->extract_name);
            e->extract_name = fresh[i];
        }
        xx_mem_free(numbers);
        xx_mem_free(fresh);
    }
    xx_mem_free(keys);
    return true;
}

static struct xx_zpaq_scan *zp_scan_archive(Abstractformat *self,
                                            xx_pd_struct *pd) {
    struct xx_zpaq_scan *s;
    zp_in *in;
    int64_t pos, total;
    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total <= self->base_address) return NULL;
    s = (struct xx_zpaq_scan *)xx_mem_calloc(1U, sizeof(*s));
    in = (zp_in *)xx_mem_alloc(sizeof(zp_in));
    if (!s || !in) {
        if (s) xx_mem_free(s);
        if (in) xx_mem_free(in);
        return NULL;
    }
    in->device = self->device;
    in->end = total;
    s->last_stream = -1;
    s->d_cursor = -1;
    s->d_limit = -1;
    pos = self->base_address;
    s->end = pos;
    while (pos < total && !s->stop) {
        int64_t next = pos;
        if (pd && xx_pd_is_stopped(pd)) break;
        if (!zp_scan_block(s, in, pos, &next, pd) || next <= pos) break;
        s->ok = true;
        s->end = next;
        pos = next;
    }
    xx_mem_free(in);
    if (s->ok && !zp_finalize(s)) s->ok = false;
    return s;
}

static struct xx_zpaq_scan *zp_get_scan(xx_zpaq *archive, xx_pd_struct *pd) {
    if (!archive) return NULL;
    if (!archive->scan) {
        if (!archive->format.base_info_handled &&
            !xx_zpaq_handle_base_info(&archive->format, pd))
            return NULL;
        if (!archive->format.is_valid) return NULL;
        archive->scan = zp_scan_archive(&archive->format, pd);
    }
    return archive->scan && archive->scan->ok ? archive->scan : NULL;
}

/* ---------------------------------------------------------------------- */
/* First-block probe (detection)                                           */

static void xx_zpaq_vtable_destroy(Abstractformat *self);

static bool xx_zpaq_read_at(Abstractformat *self, int64_t offset,
                            uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

/*
 * Returns the offset of the first block relative to the base address, or -1.
 * The tagged form is tested first: a tagged stream also contains "zPQ", just
 * not at offset 0, so testing the bare form first would mislocate the block.
 */
static int64_t xx_zpaq_find_block(Abstractformat *self, int64_t span,
                                  bool *has_tag) {
    uint8_t prefix[XX_ZPAQ_TAG_SIZE + 3];

    if (span >= XX_ZPAQ_TAG_SIZE + 3 &&
        xx_zpaq_read_at(self, self->base_address, prefix, sizeof(prefix)) &&
        xx_rt_memcmp(prefix, XX_ZPAQ_TAG, XX_ZPAQ_TAG_SIZE) == 0 &&
        prefix[XX_ZPAQ_TAG_SIZE] == 'z' &&
        prefix[XX_ZPAQ_TAG_SIZE + 1] == 'P' &&
        prefix[XX_ZPAQ_TAG_SIZE + 2] == 'Q') {
        if (has_tag) *has_tag = true;
        return XX_ZPAQ_TAG_SIZE;
    }
    if (span >= 3 &&
        xx_zpaq_read_at(self, self->base_address, prefix, 3U) &&
        prefix[0] == 'z' && prefix[1] == 'P' && prefix[2] == 'Q') {
        if (has_tag) *has_tag = false;
        return 0;
    }
    return -1;
}

/* The block prefix, a plausible header size and the zero byte that ends the
 * header's bytecode: this ties the size field to the content, so "zPQ" alone
 * cannot pass. Deeper checks happen when the archive is walked. */
static bool xx_zpaq_probe(Abstractformat *self, xx_zpaq *out) {
    uint8_t block[XX_ZPAQ_BLOCK_PREFIX_SIZE];
    uint8_t terminator;
    bool has_tag = false;
    int64_t block_offset;
    int64_t total;
    int64_t span;
    int64_t remaining;
    uint16_t header_size;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;

    block_offset = xx_zpaq_find_block(self, span, &has_tag);
    if (block_offset < 0 || block_offset > span - XX_ZPAQ_BLOCK_PREFIX_SIZE) {
        return false;
    }
    if (!xx_zpaq_read_at(self, self->base_address + block_offset, block,
                         sizeof(block))) {
        return false;
    }
    if ((block[3] != 1U && block[3] != 2U) ||
        block[4] != XX_ZPAQ_BLOCK_TYPE) {
        return false;
    }
    header_size = (uint16_t)((uint16_t)block[5] | ((uint16_t)block[6] << 8));
    remaining = span - block_offset - XX_ZPAQ_BLOCK_PREFIX_SIZE;
    if (header_size < XX_ZPAQ_MIN_HEADER_SIZE ||
        (int64_t)header_size > remaining) {
        return false;
    }
    if (!xx_zpaq_read_at(self,
                         self->base_address + block_offset +
                             XX_ZPAQ_BLOCK_PREFIX_SIZE + header_size - 1,
                         &terminator, 1U) ||
        terminator != 0U) {
        return false;
    }

    if (out) {
        out->block_offset = block_offset;
        out->has_tag = has_tag;
        out->level = block[3];
        out->header_size = header_size;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Format object                                                           */

void xx_zpaq_init(xx_zpaq *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZPAQ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    archive->block_offset = -1;
    xx_format_set_mime_type(&archive->format, "application/x-zpaq");
    xx_format_set_extension(&archive->format, "zpaq");
    archive->format.check_is_valid = xx_zpaq_check_is_valid;
    archive->format.handle_base_info = xx_zpaq_handle_base_info;
    archive->format.get_format_size = xx_zpaq_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zpaq_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zpaq_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zpaq_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zpaq_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zpaq_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zpaq_free_archive_records_reading;
    archive->format.destroy = xx_zpaq_vtable_destroy;
}

xx_zpaq *xx_zpaq_create(xx_io_device *device, int64_t base_address) {
    xx_zpaq *archive = (xx_zpaq *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zpaq_init(archive, device, base_address);
    return archive;
}

void xx_zpaq_destroy(xx_zpaq *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    zp_scan_free(archive->scan);
    archive->scan = NULL;
    archive->block_offset = -1;
}

void xx_zpaq_free(xx_zpaq *archive) {
    if (!archive) return;
    xx_zpaq_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zpaq_vtable_destroy(Abstractformat *self) {
    xx_zpaq_destroy((xx_zpaq *)self);
}

bool xx_zpaq_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_zpaq_probe(self, NULL);
}

bool xx_zpaq_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zpaq *archive = (xx_zpaq *)self;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_zpaq_probe(self, archive);
    if (!self->is_valid) {
        self->format_size = 0;
        return false;
    }
    /* Detection stops at the first block; the exact extent is measured by
     * xx_zpaq_get_format_size, which walks the archive. */
    self->format_size = xx_io_total_size(self->device) - self->base_address;
    return true;
}

int64_t xx_zpaq_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    struct xx_zpaq_scan *scan;
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    if (!self->is_valid) return 0;
    scan = zp_get_scan((xx_zpaq *)self, pd);
    if (scan && scan->end > self->base_address)
        self->format_size = scan->end - self->base_address;
    return self->format_size;
}

uint64_t xx_zpaq_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    struct xx_zpaq_scan *scan = zp_get_scan((xx_zpaq *)self, pd);
    return scan ? scan->nvisible : 0U;
}

int64_t xx_zpaq_get_block_offset(const xx_zpaq *archive) {
    return archive ? archive->block_offset : -1;
}

bool xx_zpaq_has_tag(const xx_zpaq *archive) {
    return archive ? archive->has_tag : false;
}

uint8_t xx_zpaq_get_level(const xx_zpaq *archive) {
    return archive ? archive->level : 0U;
}

bool xx_zpaq_is_journaling(xx_zpaq *archive, xx_pd_struct *pd) {
    struct xx_zpaq_scan *scan = zp_get_scan(archive, pd);
    return scan ? scan->journaling : false;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

#define ZP_CACHE_SLOTS 4U

typedef struct zp_cache_slot_s {
    uint32_t dblock;         /* index + 1, 0 = empty */
    bool bad;
    uint64_t used;           /* last use */
    zp_membuf data;
} zp_cache_slot;

typedef struct zp_reader_s {
    xx_zpaq *archive;
    uint32_t index;          /* into scan->visible */
    zp_in *in;
    /* journaling: the most recently used decoded d blocks. Deduplicated
     * files interleave fragments of several blocks, so one slot would
     * decode the same block again and again. */
    zp_cache_slot slots[ZP_CACHE_SLOTS];
    uint64_t clock;
    /* streaming: an open block positioned after its last decoded segment */
    zp_block *block;
    bool block_open;
} zp_reader;

static void zp_reader_free(void *opaque) {
    zp_reader *r = (zp_reader *)opaque;
    if (!r) return;
    if (r->block) {
        if (r->block_open) zp_block_close(r->block);
        xx_mem_free(r->block);
    }
    {
        uint32_t i;
        for (i = 0; i < ZP_CACHE_SLOTS; ++i)
            if (r->slots[i].data.data) xx_mem_free(r->slots[i].data.data);
    }
    if (r->in) xx_mem_free(r->in);
    xx_mem_free(r);
}

static bool zp_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *zp_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool zp_set_record(xx_archive_record *record, const zp_entry *e) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!xx_archive_record_set_original_name(
            record, e->record_name ? e->record_name : e->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        e->size) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         e->is_dir))
        return false;
    if (e->has_attr &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, e->attr))
        return false;
    return true;
}

static bool zp_load_record(xx_archive_record_state *state) {
    zp_reader *r = (zp_reader *)state->internal_state;
    struct xx_zpaq_scan *s = r->archive->scan;
    if (r->index >= s->nvisible) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)r->index;
    state->has_record =
        zp_set_record(&state->current_record, &s->entries[s->visible[r->index]]);
    return state->has_record;
}

xx_archive_record_state *xx_zpaq_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    struct xx_zpaq_scan *scan = zp_get_scan((xx_zpaq *)self, pd);
    xx_archive_record_state *state;
    zp_reader *r;
    if (!scan) return NULL;
    r = (zp_reader *)xx_mem_calloc(1U, sizeof(*r));
    if (!r) return NULL;
    r->archive = (xx_zpaq *)self;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(r);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = r;
    state->free_internal = zp_reader_free;
    state->total_records = (int64_t)scan->nvisible;
    if (!zp_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (scan->nvisible) (void)zp_load_record(state);
    return state;
}

const xx_archive_record *xx_zpaq_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_zpaq_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    zp_reader *r;
    (void)pd;
    if (!self || !state || state->format != self ||
        !(r = (zp_reader *)state->internal_state)) {
        if (state) state->has_record = false;
        return false;
    }
    ++r->index;
    return zp_load_record(state);
}

void xx_zpaq_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

static bool zp_device_write(void *context, const uint8_t *data, size_t size) {
    xx_io_device *device = (xx_io_device *)context;
    size_t done = 0U;
    if (!device) return true;
    while (done < size) {
        ssize_t wrote = xx_io_write(device, data + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return false;
        done += (size_t)wrote;
    }
    return true;
}

static bool zp_ensure_in(zp_reader *r) {
    if (r->in) return true;
    r->in = (zp_in *)xx_mem_alloc(sizeof(zp_in));
    if (!r->in) return false;
    r->in->device = r->archive->format.device;
    r->in->end = xx_io_total_size(r->archive->format.device);
    zp_in_seek(r->in, 0);
    return true;
}

/* Decoded d block number index (0-based), from the cache or decoded now,
 * with every fragment it holds checked against the SHA-1 of its h block;
 * NULL when it cannot be had. The cache holds at most ZP_CACHE_SLOTS blocks
 * and ZP_MAX_MEMBLOCK bytes; the least recently used block goes first. */
static const uint8_t *zp_load_dblock(zp_reader *r, uint32_t index,
                                     xx_pd_struct *pd) {
    struct xx_zpaq_scan *s = r->archive->scan;
    const zp_dblock *d = &s->dblocks[index];
    zp_cache_slot *slot = NULL;
    zp_block *b;
    zp_seghead head;
    char type;
    uint32_t number, i;
    uint64_t held = 0U;
    bool ok = false;
    for (i = 0; i < ZP_CACHE_SLOTS; ++i) {
        if (r->slots[i].dblock == index + 1U) {
            r->slots[i].used = ++r->clock;
            return r->slots[i].bad ? NULL : r->slots[i].data.data;
        }
    }
    /* Evict until a slot is free and the new block fits the budget. */
    for (;;) {
        zp_cache_slot *oldest = NULL;
        held = 0U;
        slot = NULL;
        for (i = 0; i < ZP_CACHE_SLOTS; ++i) {
            zp_cache_slot *c = &r->slots[i];
            if (c->dblock == 0U) {
                if (!slot) slot = c;
                continue;
            }
            held += c->data.size;
            if (!oldest || c->used < oldest->used) oldest = c;
        }
        if (slot && held + d->usize <= ZP_MAX_MEMBLOCK) break;
        if (!oldest) break;
        if (oldest->data.data) xx_mem_free(oldest->data.data);
        xx_mem_zero(oldest, sizeof(*oldest));
    }
    if (!slot) return NULL;
    slot->dblock = index + 1U;
    slot->used = ++r->clock;
    slot->bad = true;
    if (d->offset < 0 || !zp_ensure_in(r)) return NULL;
    b = (zp_block *)xx_mem_alloc(sizeof(zp_block));
    if (!b) return NULL;
    if (!zp_block_open(b, r->in, d->offset)) {
        xx_mem_free(b);
        return NULL;
    }
    if (zp_segment_begin(b, &head) == 1) {
        bool named = zp_parse_jdc(&head, &type, &number) && type == 'd' &&
                     number == d->first;
        zp_seghead_free(&head);
        if (named && zp_segment_to_memory(b, d->usize, &slot->data, pd) &&
            slot->data.size == d->usize) {
            ok = true;
            for (i = 0; ok && i < d->count; ++i) {
                const zp_frag *f = &s->frags[d->first + i];
                uint8_t digest[20];
                if (!xx_sha1_memory(slot->data.data + f->offset, f->size, digest) ||
                    xx_rt_memcmp(digest, f->sha, 20U) != 0)
                    ok = false;
            }
        }
    }
    zp_block_close(b);
    xx_mem_free(b);
    if (!ok && slot->data.data) {
        /* Keep the verdict, not the bytes. */
        xx_mem_free(slot->data.data);
        xx_mem_zero(&slot->data, sizeof(slot->data));
    }
    slot->bad = !ok;
    return ok ? slot->data.data : NULL;
}

static bool zp_extract_journal(zp_reader *r, const zp_entry *e,
                               xx_io_device *out, xx_pd_struct *pd) {
    struct xx_zpaq_scan *s = r->archive->scan;
    uint32_t j;
    if (e->broken) return false;
    for (j = 0; j < e->nids; ++j) {
        const zp_frag *f = &s->frags[e->ids[j]];
        const uint8_t *data;
        if (pd && xx_pd_is_stopped(pd)) return false;
        data = zp_load_dblock(r, f->dblock - 1U, pd);
        if (!data) return false;
        if (out && !zp_device_write(out, data + f->offset, f->size))
            return false;
    }
    return true;
}

static bool zp_extract_stream(zp_reader *r, const zp_entry *e,
                              xx_io_device *out, xx_pd_struct *pd) {
    zp_sink *sink;
    uint32_t j;
    bool ok = true;
    if (e->broken || !zp_ensure_in(r)) return false;
    if (!r->block) {
        r->block = (zp_block *)xx_mem_calloc(1U, sizeof(zp_block));
        if (!r->block) return false;
    }
    sink = (zp_sink *)xx_mem_alloc(sizeof(zp_sink));
    if (!sink) return false;
    for (j = 0; ok && j < e->npieces; ++j) {
        const zp_piece *piece = &e->pieces[j];
        zp_block *b = r->block;
        if (!r->block_open || b->start != piece->block || b->bad ||
            b->segment > piece->segment) {
            if (r->block_open) zp_block_close(b);
            r->block_open = zp_block_open(b, r->in, piece->block);
            if (!r->block_open) {
                ok = false;
                break;
            }
        }
        while (ok) {
            zp_seghead head;
            if (zp_segment_begin(b, &head) != 1) {
                ok = false;
                break;
            }
            zp_seghead_free(&head);
            if (b->segment - 1U < piece->segment) {
                ok = zp_segment_skip(b, NULL, ZP_MAX_UNKNOWN_OUTPUT, pd);
                continue;
            }
            zp_sink_start(sink, zp_device_write, out, piece->size);
            ok = zp_segment_decode(b, sink, pd) && sink->count == piece->size;
            break;
        }
    }
    xx_mem_free(sink);
    return ok;
}

bool xx_zpaq_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    zp_reader *r;
    struct xx_zpaq_scan *s;
    const zp_entry *e;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *out = NULL;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(r = (zp_reader *)state->internal_state) ||
        !(s = r->archive->scan) || r->index >= s->nvisible ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    e = &s->entries[s->visible[r->index]];
    path_option = zp_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        if (e->is_dir) return true;
        return e->journal ? zp_extract_journal(r, e, NULL, pd)
                          : zp_extract_stream(r, e, NULL, pd);
    }
    if (!e->extract_name) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", e->extract_name)
               : xx_str_concat(base, e->extract_name);
    if (!path) goto done;
    if (e->is_dir) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    out = xx_io_file_open(path, "wb");
    if (!out) goto done;
    created = true;
    result = e->journal ? zp_extract_journal(r, e, out, pd)
                        : zp_extract_stream(r, e, out, pd);
    if (xx_io_close(out) != 0) result = false;
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}
