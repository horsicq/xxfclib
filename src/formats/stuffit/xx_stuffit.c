/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for Aladdin StuffIt's original "SIT!" container, as written
 * by StuffIt 1.x through 4.x (StuffIt 5's "StuffIt (c)1997-" container is a
 * different format and is deliberately not claimed here).
 *
 * Layout, taken from XArchive's StuffIt module
 * (Algos/xdearkmodule_stuffit_p.cpp) and confirmed against the corpus:
 *
 *   master header, 22 bytes
 *     0   "SIT!"
 *     4   uint16be  number of top-level members
 *     6   uint32be  total archive size -- exact, and used as the anchor
 *     10  "rLau"
 *     14  uint8     version
 *     15  7 reserved bytes
 *
 *   member header, 112 bytes, then the resource fork's packed bytes and then
 *   the data fork's packed bytes
 *     0   uint8     resource fork method (32 = folder, 33 = end of folder,
 *                   bit 0x10 = encrypted)
 *     1   uint8     data fork method
 *     2   uint8     name length, name field is 63 bytes
 *     66  4         Mac file type
 *     70  4         Mac creator
 *     74  uint16be  Finder flags
 *     76  uint32be  creation time      (Mac epoch)
 *     80  uint32be  modification time  (Mac epoch)
 *     84  uint32be  resource fork unpacked length
 *     88  uint32be  data fork unpacked length
 *     92  uint32be  resource fork packed length
 *     96  uint32be  data fork packed length
 *     100 uint16be  resource fork CRC-16/ARC
 *     102 uint16be  data fork CRC-16/ARC
 *     104 6 reserved
 *     110 uint16be  CRC-16/ARC of the first 110 header bytes
 *
 * Decoding covers method 0 (stored), method 1 (RLE90) and method 13
 * (LZ+Huffman, the method StuffIt 3 and later use for almost everything);
 * the method-13 decoder is a port of XArchive's xdearkstuffit13_p.cpp, which
 * is itself a C port of the MIT-licensed compcol clean-room implementation
 * (Copyright (c) 2026 Karpeles Lab Inc.).  Methods 2 (LZW), 3 (Huffman),
 * 5 (LZAH), 6 (fixed Huffman), 8 (MW), 14 (installer) and 15 (Arsenic), plus
 * every encrypted fork, fail closed.  Every decode is verified against the
 * fork's stored CRC-16/ARC before it is accepted.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stuffit/xx_stuffit.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* The enum entry is added by the coordinator; keep compiling until it is. */
#ifdef STUFFIT
#define XX_STUFFIT_FILE_TYPE XX_FILE_TYPE_STUFFIT
#else
#define XX_STUFFIT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SIT_MASTER_HEADER_SIZE 22
#define SIT_MEMBER_HEADER_SIZE 112
#define SIT_MAX_NAME 63U
#define SIT_MAX_MEMBERS 262144U
#define SIT_MAX_DEPTH 32U
/* Classic Mac forks are 24-bit quantities; nothing legal exceeds this. */
#define SIT_MAX_FORK UINT32_C(0x00FFFFFF)

#define SIT_METHOD_NONE 0U
#define SIT_METHOD_RLE 1U
#define SIT_METHOD_LZHUFF 13U
#define SIT_MARK_FOLDER 32U
#define SIT_MARK_FOLDER_END 33U

/* ------------------------------------------------------------------ */
/* Method 13: LZ+Huffman.                                             */
/* ------------------------------------------------------------------ */

#define SIT13_NONE UINT32_MAX
#define SIT13_MAX_CODE_LENGTH 32U
#define SIT13_LITLEN_SYMBOLS 321U
#define SIT13_EOS 0x140U

/* Method-13 interoperability tables, ported verbatim from XArchive's
 * Algos/xdearkstuffit13tables_p.h, itself derived from the MIT-licensed
 * compcol clean-room implementation (Copyright (c) 2026 Karpeles Lab Inc.). */
static const uint32_t SIT13_META_CODE_VALUES[37] = {
    1496, 88, 64, 192, 0, 120, 43, 20, 12, 28, 27, 11, 16, 32, 56, 24, 216, 3032, 384, 1664, 896, 3968, 1920, 1152, 128, 640, 984, 4056, 2008, 2520, 472, 4, 1, 2, 7, 3, 8,
};

static const uint8_t SIT13_META_CODE_LENGTHS[37] = {
    11, 8, 8, 8, 8, 7, 6, 5, 5, 5, 5, 6, 5, 6, 7, 7, 9, 12, 10, 11, 11, 12, 12, 11, 11, 11, 12, 12, 12, 12, 12, 5, 2, 2, 3, 4, 5,
};

static const uint8_t SIT13_SET1_FIRST[321] = {
    4, 5, 7, 8, 8, 9, 9, 9, 9, 7, 9, 9, 9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 9, 9, 10, 10, 9, 10, 9, 9, 5, 9, 9, 9, 9, 10, 9, 9, 9, 9, 9, 9, 9, 9, 7, 9, 9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 8, 9, 9, 8, 8, 9, 9, 9, 9, 9, 9, 9, 7, 8, 9, 7, 9, 9, 7, 7, 9, 9, 9, 9, 10, 9, 10, 10, 10, 9, 9, 9, 5, 9, 8, 7, 5, 9, 8, 8, 7, 9, 9, 8, 8, 5, 5, 7, 10, 5, 8, 5, 8, 9, 9, 9, 9, 9, 10, 9, 9, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 9, 5, 6, 5, 5, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 9, 9, 9, 10, 9, 10, 9, 10, 9, 10, 9, 10, 10, 10, 9, 10, 9, 10, 10, 9, 9, 9, 6, 9, 9, 10, 9, 5,
};

static const uint8_t SIT13_SET1_SECOND[321] = {
    4, 5, 6, 6, 7, 7, 6, 7, 7, 7, 6, 8, 7, 8, 8, 8, 8, 9, 6, 9, 8, 9, 8, 9, 9, 9, 8, 10, 5, 9, 7, 9, 6, 9, 8, 10, 9, 10, 8, 8, 9, 9, 7, 9, 8, 9, 8, 9, 8, 8, 6, 9, 9, 8, 8, 9, 9, 10, 8, 9, 9, 10, 8, 10, 8, 8, 8, 8, 8, 9, 7, 10, 6, 9, 9, 11, 7, 8, 8, 9, 8, 10, 7, 8, 6, 9, 10, 9, 9, 10, 8, 11, 9, 11, 9, 10, 9, 8, 9, 8, 8, 8, 8, 10, 9, 9, 10, 10, 8, 9, 8, 8, 8, 11, 9, 8, 8, 9, 9, 10, 8, 11, 10, 10, 8, 10, 9, 10, 8, 9, 9, 11, 9, 11, 9, 10, 10, 11, 10, 12, 9, 12, 10, 11, 10, 11, 9, 10, 10, 11, 10, 11, 10, 11, 10, 11, 10, 10, 10, 9, 9, 9, 8, 7, 6, 8, 11, 11, 9, 12, 10, 12, 9, 11, 11, 11, 10, 12, 11, 11, 10, 12, 10, 11, 10, 10, 10, 11, 10, 11, 11, 11, 9, 12, 10, 12, 11, 12, 10, 11, 10, 12, 11, 12, 11, 12, 11, 12, 10, 12, 11, 12, 11, 11, 10, 12, 10, 11, 10, 12, 10, 12, 10, 12, 10, 11, 11, 11, 10, 11, 11, 11, 10, 12, 11, 12, 10, 10, 11, 11, 9, 12, 11, 12, 10, 11, 10, 12, 10, 11, 10, 12, 10, 11, 10, 7, 5, 4, 6, 6, 7, 7, 7, 8, 8, 7, 7, 6, 8, 6, 7, 7, 9, 8, 9, 9, 10, 11, 11, 11, 12, 11, 10, 11, 12, 11, 12, 11, 12, 12, 12, 12, 11, 12, 12, 11, 12, 11, 12, 11, 13, 11, 12, 10, 13, 10, 14, 14, 13, 14, 15, 14, 16, 15, 15, 18, 18, 18, 9, 18, 8,
};

static const uint8_t SIT13_SET1_OFFSET[11] = {
    5, 6, 3, 3, 3, 3, 3, 3, 3, 4, 6
};

static const uint8_t SIT13_SET2_FIRST[321] = {
    4, 7, 7, 8, 7, 8, 8, 8, 8, 7, 8, 7, 8, 7, 9, 8, 8, 8, 9, 9, 9, 9, 10, 10, 9, 10, 10, 10, 10, 10, 9, 9, 5, 9, 8, 9, 9, 11, 10, 9, 8, 9, 9, 9, 8, 9, 7, 8, 8, 8, 9, 9, 9, 9, 9, 10, 9, 9, 9, 10, 9, 9, 10, 9, 8, 8, 7, 7, 7, 8, 8, 9, 8, 8, 9, 9, 8, 8, 7, 8, 7, 10, 8, 7, 7, 9, 9, 9, 9, 10, 10, 11, 11, 11, 10, 9, 8, 6, 8, 7, 7, 5, 7, 7, 7, 6, 9, 8, 6, 7, 6, 6, 7, 9, 6, 6, 6, 7, 8, 8, 8, 8, 9, 10, 9, 10, 9, 9, 8, 9, 10, 10, 9, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 11, 10, 10, 10, 10, 10, 10, 10, 11, 10, 11, 10, 10, 9, 11, 10, 10, 10, 10, 10, 10, 9, 9, 10, 11, 10, 11, 10, 11, 10, 12, 10, 11, 10, 12, 11, 12, 10, 12, 10, 11, 10, 11, 11, 11, 9, 10, 11, 11, 11, 12, 12, 10, 10, 10, 11, 11, 10, 11, 10, 10, 9, 11, 10, 11, 10, 11, 11, 11, 10, 11, 11, 12, 11, 11, 10, 10, 10, 11, 10, 10, 11, 11, 12, 10, 10, 11, 11, 12, 11, 11, 10, 11, 9, 12, 10, 11, 11, 11, 10, 11, 10, 11, 10, 11, 9, 10, 9, 7, 3, 5, 6, 6, 7, 7, 8, 8, 8, 9, 9, 9, 11, 10, 10, 10, 12, 13, 11, 12, 12, 11, 13, 12, 12, 11, 12, 12, 13, 12, 14, 13, 14, 13, 15, 13, 14, 15, 15, 14, 13, 15, 15, 14, 15, 14, 15, 15, 14, 15, 13, 13, 14, 15, 15, 14, 14, 16, 16, 15, 15, 15, 12, 15, 10,
};

static const uint8_t SIT13_SET2_SECOND[321] = {
    5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 8, 7, 8, 7, 7, 7, 8, 8, 8, 8, 9, 8, 9, 8, 9, 9, 9, 7, 9, 8, 8, 6, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 8, 8, 8, 8, 9, 8, 9, 8, 9, 9, 10, 8, 10, 8, 9, 9, 8, 8, 8, 7, 8, 8, 9, 8, 9, 7, 9, 8, 10, 8, 9, 8, 9, 8, 9, 8, 8, 8, 9, 9, 9, 9, 10, 9, 11, 9, 10, 9, 10, 8, 8, 8, 9, 8, 8, 8, 9, 9, 8, 9, 10, 8, 9, 8, 8, 8, 11, 8, 7, 8, 9, 9, 9, 9, 10, 9, 10, 9, 10, 9, 8, 8, 9, 9, 10, 9, 10, 9, 10, 8, 10, 9, 10, 9, 11, 10, 11, 9, 11, 10, 10, 10, 11, 9, 11, 9, 10, 9, 11, 9, 11, 10, 10, 9, 10, 9, 9, 8, 10, 9, 11, 9, 9, 9, 11, 10, 11, 9, 11, 9, 11, 9, 11, 10, 11, 10, 11, 10, 11, 9, 10, 10, 11, 10, 10, 8, 10, 9, 10, 10, 11, 9, 11, 9, 10, 10, 11, 9, 10, 10, 9, 9, 10, 9, 10, 9, 10, 9, 10, 9, 11, 9, 11, 10, 10, 9, 10, 9, 11, 9, 11, 9, 11, 9, 10, 9, 11, 9, 11, 9, 11, 9, 10, 8, 11, 9, 10, 9, 10, 9, 10, 8, 10, 8, 9, 8, 9, 8, 7, 4, 4, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 7, 8, 8, 9, 9, 10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 12, 11, 11, 12, 12, 11, 12, 12, 11, 12, 12, 12, 12, 12, 12, 11, 12, 11, 13, 12, 13, 12, 13, 14, 14, 14, 15, 13, 14, 13, 14, 18, 18, 17, 7, 16, 9,
};

static const uint8_t SIT13_SET2_OFFSET[13] = {
    5, 6, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 6
};

static const uint8_t SIT13_SET3_FIRST[321] = {
    6, 6, 6, 6, 6, 9, 8, 8, 4, 9, 8, 9, 8, 9, 9, 9, 8, 9, 9, 10, 8, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 9, 9, 9, 8, 10, 9, 10, 9, 10, 9, 10, 9, 10, 9, 9, 8, 9, 8, 9, 9, 9, 10, 10, 10, 10, 9, 9, 9, 10, 9, 10, 9, 9, 7, 8, 8, 9, 8, 9, 9, 9, 8, 9, 9, 10, 9, 9, 8, 9, 8, 9, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 9, 8, 8, 9, 8, 9, 7, 8, 8, 9, 8, 10, 10, 8, 9, 8, 8, 8, 10, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 10, 9, 7, 9, 9, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 9, 8, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 9, 9, 9, 10, 10, 10, 10, 10, 10, 9, 9, 10, 9, 9, 8, 9, 8, 9, 4, 6, 6, 6, 7, 8, 8, 9, 9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 7, 10, 10, 10, 7, 10, 10, 7, 7, 7, 7, 7, 6, 7, 10, 7, 7, 10, 7, 7, 7, 6, 7, 6, 6, 7, 7, 6, 6, 9, 6, 9, 10, 6, 10,
};

static const uint8_t SIT13_SET3_SECOND[321] = {
    5, 6, 6, 6, 6, 7, 7, 7, 6, 8, 7, 8, 7, 9, 8, 8, 7, 7, 8, 9, 9, 9, 9, 10, 8, 9, 9, 10, 8, 10, 9, 8, 6, 10, 8, 10, 8, 10, 9, 9, 9, 9, 9, 10, 9, 9, 8, 9, 8, 9, 8, 9, 9, 10, 9, 10, 9, 9, 8, 10, 9, 11, 10, 8, 8, 8, 8, 9, 7, 9, 9, 10, 8, 9, 8, 11, 9, 10, 9, 10, 8, 9, 9, 9, 9, 8, 9, 9, 10, 10, 10, 12, 10, 11, 10, 10, 8, 9, 9, 9, 8, 9, 8, 8, 10, 9, 10, 11, 8, 10, 9, 9, 8, 12, 8, 9, 9, 9, 9, 8, 9, 10, 9, 12, 10, 10, 10, 8, 7, 11, 10, 9, 10, 11, 9, 11, 7, 11, 10, 12, 10, 12, 10, 11, 9, 11, 9, 12, 10, 12, 10, 12, 10, 9, 11, 12, 10, 12, 10, 11, 9, 10, 9, 10, 9, 11, 11, 12, 9, 10, 8, 12, 11, 12, 9, 12, 10, 12, 10, 13, 10, 12, 10, 12, 10, 12, 10, 9, 10, 12, 10, 9, 8, 11, 10, 12, 10, 12, 10, 12, 10, 11, 10, 12, 8, 12, 10, 11, 10, 10, 10, 12, 9, 11, 10, 12, 10, 12, 11, 12, 10, 9, 10, 12, 9, 10, 10, 12, 10, 11, 10, 11, 10, 12, 8, 12, 9, 12, 8, 12, 8, 11, 10, 11, 10, 11, 9, 10, 8, 10, 9, 9, 8, 9, 8, 7, 4, 3, 5, 5, 6, 5, 6, 6, 7, 7, 8, 8, 8, 7, 7, 7, 9, 8, 9, 9, 11, 9, 11, 9, 8, 9, 9, 11, 12, 11, 12, 12, 13, 13, 12, 13, 14, 13, 14, 13, 14, 13, 13, 13, 12, 13, 13, 12, 13, 13, 14, 14, 13, 13, 14, 14, 14, 14, 15, 18, 17, 18, 8, 16, 10,
};

static const uint8_t SIT13_SET3_OFFSET[14] = {
    6, 7, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 5, 7
};

static const uint8_t SIT13_SET4_FIRST[321] = {
    2, 6, 6, 7, 7, 8, 7, 8, 7, 8, 8, 9, 8, 9, 9, 9, 8, 8, 9, 9, 9, 10, 10, 9, 8, 10, 9, 10, 9, 10, 9, 9, 6, 9, 8, 9, 9, 10, 9, 9, 9, 10, 9, 9, 9, 9, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 9, 7, 7, 8, 8, 8, 8, 9, 9, 7, 8, 9, 10, 8, 8, 7, 8, 8, 10, 8, 8, 8, 9, 8, 9, 9, 10, 9, 11, 10, 11, 9, 9, 8, 7, 9, 8, 8, 6, 8, 8, 8, 7, 10, 9, 7, 8, 7, 7, 8, 10, 7, 7, 7, 8, 9, 9, 9, 9, 10, 11, 9, 11, 10, 9, 7, 9, 10, 10, 10, 11, 11, 10, 10, 11, 10, 10, 10, 11, 11, 10, 9, 10, 10, 11, 10, 11, 10, 11, 10, 10, 10, 11, 10, 11, 10, 10, 9, 10, 10, 11, 10, 10, 10, 10, 9, 10, 10, 10, 10, 11, 10, 11, 10, 11, 10, 11, 11, 11, 10, 12, 10, 11, 10, 11, 10, 11, 11, 10, 8, 10, 10, 11, 10, 11, 11, 11, 10, 11, 10, 11, 10, 11, 11, 11, 9, 10, 11, 11, 10, 11, 11, 11, 10, 11, 11, 11, 10, 10, 10, 10, 10, 11, 10, 10, 11, 11, 10, 10, 9, 11, 10, 10, 11, 11, 10, 10, 10, 11, 10, 10, 10, 10, 10, 10, 9, 11, 10, 10, 8, 10, 8, 6, 5, 6, 6, 7, 7, 8, 8, 8, 9, 10, 11, 10, 10, 11, 11, 12, 12, 10, 11, 12, 12, 12, 12, 13, 13, 13, 13, 13, 12, 13, 13, 15, 14, 12, 14, 15, 16, 12, 12, 13, 15, 14, 16, 15, 17, 18, 15, 17, 16, 15, 15, 15, 15, 13, 13, 10, 14, 12, 13, 17, 17, 18, 10, 17, 4,
};

static const uint8_t SIT13_SET4_SECOND[321] = {
    4, 5, 6, 6, 6, 6, 7, 7, 6, 7, 7, 9, 6, 8, 8, 7, 7, 8, 8, 8, 6, 9, 8, 8, 7, 9, 8, 9, 8, 9, 8, 9, 6, 9, 8, 9, 8, 10, 9, 9, 8, 10, 8, 10, 8, 9, 8, 9, 8, 8, 7, 9, 9, 9, 9, 9, 8, 10, 9, 10, 9, 10, 9, 8, 7, 8, 9, 9, 8, 9, 9, 9, 7, 10, 9, 10, 9, 9, 8, 9, 8, 9, 8, 8, 8, 9, 9, 10, 9, 9, 8, 11, 9, 11, 10, 10, 8, 8, 10, 8, 8, 9, 9, 9, 10, 9, 10, 11, 9, 9, 9, 9, 8, 9, 8, 8, 8, 10, 10, 9, 9, 8, 10, 11, 10, 11, 11, 9, 8, 9, 10, 11, 9, 10, 11, 11, 9, 12, 10, 10, 10, 12, 11, 11, 9, 11, 11, 12, 9, 11, 9, 10, 10, 10, 10, 12, 9, 11, 10, 11, 9, 11, 11, 11, 10, 11, 11, 12, 9, 10, 10, 12, 11, 11, 10, 11, 9, 11, 10, 11, 10, 11, 9, 11, 11, 9, 8, 11, 10, 11, 11, 10, 7, 12, 11, 11, 11, 11, 11, 12, 10, 12, 11, 13, 11, 10, 12, 11, 10, 11, 10, 11, 10, 11, 11, 11, 10, 12, 11, 11, 10, 11, 10, 10, 10, 11, 10, 12, 11, 12, 10, 11, 9, 11, 10, 11, 10, 11, 10, 12, 9, 11, 11, 11, 9, 11, 10, 10, 9, 11, 10, 10, 9, 10, 9, 7, 4, 5, 5, 5, 6, 6, 7, 6, 8, 7, 8, 9, 9, 7, 8, 8, 10, 9, 10, 10, 12, 10, 11, 11, 11, 11, 10, 11, 12, 11, 11, 11, 11, 11, 13, 12, 11, 12, 13, 12, 12, 12, 13, 11, 9, 12, 13, 7, 13, 11, 13, 11, 10, 11, 13, 15, 15, 12, 14, 15, 15, 15, 6, 15, 5,
};

static const uint8_t SIT13_SET4_OFFSET[11] = {
    3, 6, 5, 4, 2, 3, 3, 3, 4, 4, 6
};

static const uint8_t SIT13_SET5_FIRST[321] = {
    7, 9, 9, 9, 9, 9, 9, 9, 9, 8, 9, 9, 9, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 9, 10, 9, 10, 9, 10, 9, 9, 5, 9, 7, 9, 9, 9, 9, 9, 7, 7, 7, 9, 7, 7, 8, 7, 8, 8, 7, 7, 9, 9, 9, 9, 7, 7, 7, 9, 9, 9, 9, 9, 9, 7, 9, 7, 7, 7, 7, 9, 9, 7, 9, 9, 7, 7, 7, 7, 7, 9, 7, 8, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 7, 8, 7, 7, 7, 8, 8, 6, 7, 9, 7, 7, 8, 7, 5, 6, 9, 5, 7, 5, 6, 7, 7, 9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 10, 9, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 10, 9, 9, 10, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 10, 9, 9, 9, 10, 10, 10, 10, 10, 9, 10, 9, 10, 10, 9, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 9, 10, 9, 10, 10, 9, 5, 6, 8, 8, 7, 7, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 5, 10, 8, 9, 8, 9,
};

static const uint8_t SIT13_SET5_SECOND[321] = {
    8, 10, 11, 11, 11, 12, 11, 11, 12, 6, 11, 12, 10, 5, 12, 12, 12, 12, 12, 12, 12, 13, 13, 14, 13, 13, 12, 13, 12, 13, 12, 15, 4, 10, 7, 9, 11, 11, 10, 9, 6, 7, 8, 9, 6, 7, 6, 7, 8, 7, 7, 8, 8, 8, 8, 8, 8, 9, 8, 7, 10, 9, 10, 10, 11, 7, 8, 6, 7, 8, 8, 9, 8, 7, 10, 10, 8, 7, 8, 8, 7, 10, 7, 6, 7, 9, 9, 8, 11, 11, 11, 10, 11, 11, 11, 8, 11, 6, 7, 6, 6, 6, 6, 8, 7, 6, 10, 9, 6, 7, 6, 6, 7, 10, 6, 5, 6, 7, 7, 7, 10, 8, 11, 9, 13, 7, 14, 16, 12, 14, 14, 15, 15, 16, 16, 14, 15, 15, 15, 15, 15, 15, 15, 15, 14, 15, 13, 14, 14, 16, 15, 17, 14, 17, 15, 17, 12, 14, 13, 16, 12, 17, 13, 17, 14, 13, 13, 14, 14, 12, 13, 15, 15, 14, 15, 17, 14, 17, 15, 14, 15, 16, 12, 16, 15, 14, 15, 16, 15, 16, 17, 17, 15, 15, 17, 17, 13, 14, 15, 15, 13, 12, 16, 16, 17, 14, 15, 16, 15, 15, 13, 13, 15, 13, 16, 17, 15, 17, 17, 17, 16, 17, 14, 17, 14, 16, 15, 17, 15, 15, 14, 17, 15, 17, 15, 16, 15, 15, 16, 16, 14, 17, 17, 15, 15, 16, 15, 17, 15, 14, 16, 16, 16, 16, 16, 12, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10, 11, 10, 11, 11, 11, 11, 11, 12, 12, 12, 13, 13, 12, 13, 12, 14, 14, 12, 13, 13, 13, 13, 14, 12, 13, 13, 14, 14, 14, 13, 14, 14, 15, 15, 13, 15, 13, 17, 17, 17, 9, 17, 7,
};

static const uint8_t SIT13_SET5_OFFSET[11] = {
    6, 7, 7, 6, 4, 3, 2, 2, 3, 3, 6
};

static const uint8_t *const SIT13_FIRST[5] = {
    SIT13_SET1_FIRST, SIT13_SET2_FIRST, SIT13_SET3_FIRST, SIT13_SET4_FIRST, SIT13_SET5_FIRST
};
static const uint8_t *const SIT13_SECOND[5] = {
    SIT13_SET1_SECOND, SIT13_SET2_SECOND, SIT13_SET3_SECOND, SIT13_SET4_SECOND, SIT13_SET5_SECOND
};
static const uint8_t *const SIT13_OFFSET[5] = {
    SIT13_SET1_OFFSET, SIT13_SET2_OFFSET, SIT13_SET3_OFFSET, SIT13_SET4_OFFSET, SIT13_SET5_OFFSET
};
static const uint8_t SIT13_OFFSET_SIZE[5] = {11, 13, 14, 11, 11};

typedef struct sit13_bitreader_s {
    const uint8_t *data;
    size_t size;
    size_t bit_pos;
} sit13_bitreader;

typedef struct sit13_huffman_s {
    uint32_t *links;
    uint32_t *leaf;
    size_t node_count;
    size_t node_capacity;
} sit13_huffman;

static int sit13_read_bit(sit13_bitreader *r, uint32_t *value) {
    size_t byte_index;
    if (!r || !value) return 1;
    byte_index = r->bit_pos >> 3;
    if (byte_index >= r->size) return 2;
    *value = (uint32_t)((r->data[byte_index] >> (r->bit_pos & 7U)) & 1U);
    r->bit_pos++;
    return 0;
}

static int sit13_read_bits(sit13_bitreader *r, uint32_t count,
                           uint32_t *value) {
    uint32_t index;
    uint32_t result = 0U;
    if (!r || !value || count > 32U) return 1;
    if (count > 0U && (r->bit_pos > r->size * 8U ||
                       (size_t)count > r->size * 8U - r->bit_pos))
        return 2;
    for (index = 0U; index < count; ++index) {
        uint32_t bit = 0U;
        int rc = sit13_read_bit(r, &bit);
        if (rc) return rc;
        result |= bit << index;
    }
    *value = result;
    return 0;
}

static void sit13_huffman_destroy(sit13_huffman *tree) {
    if (!tree) return;
    if (tree->links) xx_mem_free(tree->links);
    if (tree->leaf) xx_mem_free(tree->leaf);
    xx_mem_zero(tree, sizeof(*tree));
}

static int sit13_huffman_init(sit13_huffman *tree, size_t symbols) {
    size_t capacity;
    if (!tree || symbols == 0U || symbols > (SIZE_MAX - 1U) / 32U) return 1;
    xx_mem_zero(tree, sizeof(*tree));
    capacity = symbols * 32U + 1U;
    tree->links = (uint32_t *)xx_mem_alloc(capacity * 2U * sizeof(uint32_t));
    tree->leaf = (uint32_t *)xx_mem_alloc(capacity * sizeof(uint32_t));
    if (!tree->links || !tree->leaf) {
        sit13_huffman_destroy(tree);
        return 4;
    }
    tree->node_capacity = capacity;
    tree->node_count = 1U;
    tree->links[0] = tree->links[1] = SIT13_NONE;
    tree->leaf[0] = SIT13_NONE;
    return 0;
}

static int sit13_huffman_new_node(sit13_huffman *tree, uint32_t *index) {
    size_t node;
    if (!tree || !index || tree->node_count >= tree->node_capacity) return 3;
    node = tree->node_count++;
    tree->links[node * 2U] = tree->links[node * 2U + 1U] = SIT13_NONE;
    tree->leaf[node] = SIT13_NONE;
    *index = (uint32_t)node;
    return 0;
}

static int sit13_huffman_insert(sit13_huffman *tree, uint32_t code,
                                uint32_t length, uint32_t symbol) {
    uint32_t node = 0U;
    uint32_t index;
    if (!tree || length == 0U || length > SIT13_MAX_CODE_LENGTH) return 3;
    for (index = 0U; index < length; ++index) {
        uint32_t bit;
        size_t slot;
        if (node >= tree->node_count || tree->leaf[node] != SIT13_NONE)
            return 3;
        bit = (code >> index) & 1U;
        slot = (size_t)node * 2U + bit;
        if (tree->links[slot] == SIT13_NONE) {
            uint32_t created = 0U;
            int rc = sit13_huffman_new_node(tree, &created);
            if (rc) return rc;
            tree->links[slot] = created;
        }
        node = tree->links[slot];
    }
    if (node >= tree->node_count || tree->leaf[node] != SIT13_NONE ||
        tree->links[(size_t)node * 2U] != SIT13_NONE ||
        tree->links[(size_t)node * 2U + 1U] != SIT13_NONE)
        return 3;
    tree->leaf[node] = symbol;
    return 0;
}

static uint32_t sit13_reverse_bits(uint32_t value, uint32_t count) {
    uint32_t index;
    uint32_t result = 0U;
    for (index = 0U; index < count; ++index)
        result |= ((value >> index) & 1U) << (count - 1U - index);
    return result;
}

static int sit13_huffman_from_lengths(sit13_huffman *tree,
                                      const uint8_t *lengths,
                                      size_t symbol_count) {
    uint32_t counts[SIT13_MAX_CODE_LENGTH + 1U];
    uint32_t next_code[SIT13_MAX_CODE_LENGTH + 1U];
    uint32_t max_length = 0U;
    uint32_t code = 0U;
    uint64_t kraft = 0U;
    size_t index;
    int rc;
    if (!tree || !lengths || symbol_count == 0U) return 1;
    xx_mem_zero(counts, sizeof(counts));
    xx_mem_zero(next_code, sizeof(next_code));
    for (index = 0U; index < symbol_count; ++index) {
        uint32_t length = lengths[index];
        if (length > SIT13_MAX_CODE_LENGTH) return 3;
        if (length) {
            counts[length]++;
            if (length > max_length) max_length = length;
        }
    }
    if (max_length == 0U) return 3;
    for (index = 1U; index <= max_length; ++index)
        kraft += (uint64_t)counts[index] << (max_length - (uint32_t)index);
    /* A non-complete code would leave a decodable hole; refuse it. */
    if (kraft != ((uint64_t)1U << max_length)) return 3;
    rc = sit13_huffman_init(tree, symbol_count);
    if (rc) return rc;
    for (index = 1U; index <= max_length; ++index) {
        next_code[index] = code;
        code = (code + counts[index]) << 1U;
    }
    for (index = 0U; index < symbol_count; ++index) {
        uint32_t length = lengths[index];
        if (length) {
            uint32_t canonical = next_code[length]++;
            rc = sit13_huffman_insert(tree,
                                      sit13_reverse_bits(canonical, length),
                                      length, (uint32_t)index);
            if (rc) {
                sit13_huffman_destroy(tree);
                return rc;
            }
        }
    }
    return 0;
}

static int sit13_huffman_from_codes(sit13_huffman *tree,
                                    const uint32_t *codes,
                                    const uint8_t *lengths,
                                    size_t symbol_count) {
    size_t index;
    int rc = sit13_huffman_init(tree, symbol_count);
    if (rc) return rc;
    for (index = 0U; index < symbol_count; ++index) {
        if (lengths[index]) {
            rc = sit13_huffman_insert(tree, codes[index], lengths[index],
                                      (uint32_t)index);
            if (rc) {
                sit13_huffman_destroy(tree);
                return rc;
            }
        }
    }
    return 0;
}

static int sit13_huffman_decode(const sit13_huffman *tree,
                                sit13_bitreader *reader, uint32_t *symbol) {
    uint32_t node = 0U;
    if (!tree || !reader || !symbol) return 1;
    for (;;) {
        uint32_t bit = 0U;
        uint32_t next;
        int rc;
        if (node >= tree->node_count) return 3;
        if (tree->leaf[node] != SIT13_NONE) {
            *symbol = tree->leaf[node];
            return 0;
        }
        rc = sit13_read_bit(reader, &bit);
        if (rc) return rc;
        next = tree->links[(size_t)node * 2U + bit];
        if (next == SIT13_NONE) return 3;
        node = next;
    }
}

static int sit13_read_code_lengths(sit13_bitreader *reader,
                                   const sit13_huffman *meta,
                                   uint8_t *lengths, size_t count) {
    size_t used = 0U;
    int accumulator = 0;
    if (!reader || !meta || !lengths) return 1;
    while (used < count) {
        uint32_t value = 0U;
        size_t extra = 0U;
        size_t index;
        uint8_t length;
        int rc = sit13_huffman_decode(meta, reader, &value);
        if (rc) return rc;
        if (value <= 30U) {
            accumulator = (int)value + 1;
        } else if (value == 31U) {
            accumulator = -1;
        } else if (value == 32U) {
            if (accumulator == INT_MAX) return 3;
            accumulator++;
        } else if (value == 33U) {
            if (accumulator == INT_MIN) return 3;
            accumulator--;
        } else if (value == 34U) {
            uint32_t bit = 0U;
            rc = sit13_read_bit(reader, &bit);
            if (rc) return rc;
            if (bit) extra = 1U;
        } else if (value == 35U) {
            uint32_t bits = 0U;
            rc = sit13_read_bits(reader, 3U, &bits);
            if (rc) return rc;
            extra = (size_t)bits + 2U;
        } else if (value == 36U) {
            uint32_t bits = 0U;
            rc = sit13_read_bits(reader, 6U, &bits);
            if (rc) return rc;
            extra = (size_t)bits + 10U;
        } else {
            return 3;
        }
        if (accumulator > (int)SIT13_MAX_CODE_LENGTH) return 3;
        length = accumulator >= 1 ? (uint8_t)accumulator : 0U;
        for (index = 0U; index <= extra && used < count; ++index)
            lengths[used++] = length;
    }
    return 0;
}

static int sit13_prepare_dynamic_codes(sit13_bitreader *reader,
                                       uint8_t control, sit13_huffman *first,
                                       sit13_huffman *second,
                                       sit13_huffman *offset) {
    sit13_huffman meta;
    uint8_t first_lengths[SIT13_LITLEN_SYMBOLS];
    uint8_t second_lengths[SIT13_LITLEN_SYMBOLS];
    uint8_t offset_lengths[17];
    size_t offset_count = (size_t)(control & 7U) + 10U;
    int rc;
    xx_mem_zero(&meta, sizeof(meta));
    rc = sit13_huffman_from_codes(&meta, SIT13_META_CODE_VALUES,
                                  SIT13_META_CODE_LENGTHS, 37U);
    if (rc) return rc;
    rc = sit13_read_code_lengths(reader, &meta, first_lengths,
                                 SIT13_LITLEN_SYMBOLS);
    if (!rc)
        rc = sit13_huffman_from_lengths(first, first_lengths,
                                        SIT13_LITLEN_SYMBOLS);
    if (!rc) {
        if (control & 8U)
            xx_rt_memcpy(second_lengths, first_lengths, SIT13_LITLEN_SYMBOLS);
        else
            rc = sit13_read_code_lengths(reader, &meta, second_lengths,
                                         SIT13_LITLEN_SYMBOLS);
    }
    if (!rc)
        rc = sit13_huffman_from_lengths(second, second_lengths,
                                        SIT13_LITLEN_SYMBOLS);
    if (!rc)
        rc = sit13_read_code_lengths(reader, &meta, offset_lengths,
                                     offset_count);
    if (!rc)
        rc = sit13_huffman_from_lengths(offset, offset_lengths, offset_count);
    sit13_huffman_destroy(&meta);
    return rc;
}

static bool sit13_decode(const uint8_t *input, size_t input_size,
                         uint8_t *output, size_t output_size) {
    sit13_bitreader reader;
    sit13_huffman first;
    sit13_huffman second;
    sit13_huffman offset;
    uint32_t control_value = 0U;
    uint8_t control;
    uint8_t high;
    size_t output_pos = 0U;
    bool use_first = true;
    int rc = 0;
    xx_mem_zero(&first, sizeof(first));
    xx_mem_zero(&second, sizeof(second));
    xx_mem_zero(&offset, sizeof(offset));
    if (output_size == 0U) return true;
    if (!input || input_size == 0U || !output) return false;
    reader.data = input;
    reader.size = input_size;
    reader.bit_pos = 0U;
    rc = sit13_read_bits(&reader, 8U, &control_value);
    if (rc) goto done;
    control = (uint8_t)control_value;
    high = (uint8_t)(control >> 4U);
    if (high == 0U) {
        rc = sit13_prepare_dynamic_codes(&reader, control, &first, &second,
                                         &offset);
        if (rc) goto done;
    } else if (high <= 5U) {
        size_t set = (size_t)high - 1U;
        rc = sit13_huffman_from_lengths(&first, SIT13_FIRST[set],
                                        SIT13_LITLEN_SYMBOLS);
        if (!rc)
            rc = sit13_huffman_from_lengths(&second, SIT13_SECOND[set],
                                            SIT13_LITLEN_SYMBOLS);
        if (!rc)
            rc = sit13_huffman_from_lengths(&offset, SIT13_OFFSET[set],
                                            SIT13_OFFSET_SIZE[set]);
        if (rc) goto done;
    } else {
        rc = 3;
        goto done;
    }
    while (output_pos < output_size) {
        uint32_t symbol = 0U;
        const sit13_huffman *code = use_first ? &first : &second;
        rc = sit13_huffman_decode(code, &reader, &symbol);
        if (rc) goto done;
        if (symbol <= 0xffU) {
            output[output_pos++] = (uint8_t)symbol;
            use_first = true;
        } else if (symbol == SIT13_EOS) {
            rc = 3;
            goto done;
        } else {
            size_t length;
            size_t distance;
            uint32_t offset_bits = 0U;
            uint32_t extra = 0U;
            size_t index;
            if (symbol <= 0x13dU) {
                length = (size_t)(symbol - 0x100U) + 3U;
            } else if (symbol == 0x13eU) {
                rc = sit13_read_bits(&reader, 10U, &extra);
                if (rc) goto done;
                length = (size_t)extra + 65U;
            } else if (symbol == 0x13fU) {
                rc = sit13_read_bits(&reader, 15U, &extra);
                if (rc) goto done;
                length = (size_t)extra + 65U;
            } else {
                rc = 3;
                goto done;
            }
            rc = sit13_huffman_decode(&offset, &reader, &offset_bits);
            if (rc) goto done;
            if (offset_bits == 0U) {
                distance = 1U;
            } else if (offset_bits == 1U) {
                distance = 2U;
            } else {
                if (offset_bits > 17U) {
                    rc = 3;
                    goto done;
                }
                rc = sit13_read_bits(&reader, offset_bits - 1U, &extra);
                if (rc) goto done;
                distance = ((size_t)1U << (offset_bits - 1U)) +
                           (size_t)extra + 1U;
            }
            if (distance == 0U || distance > 0x10000U ||
                distance > output_pos || length > output_size - output_pos) {
                rc = 3;
                goto done;
            }
            for (index = 0U; index < length; ++index) {
                output[output_pos] = output[output_pos - distance];
                output_pos++;
            }
            use_first = false;
        }
    }
done:
    sit13_huffman_destroy(&first);
    sit13_huffman_destroy(&second);
    sit13_huffman_destroy(&offset);
    return rc == 0;
}

/* Method 1: RLE90.  0x90 introduces a repeat count for the byte just
 * emitted; a count of zero is a literal 0x90. */
static bool sit_rle90_decode(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size) {
    size_t in_pos = 0U;
    size_t out_pos = 0U;
    int previous = -1;
    while (in_pos < input_size && out_pos < output_size) {
        uint8_t byte = input[in_pos++];
        if (byte != 0x90U) {
            output[out_pos++] = byte;
            previous = (int)byte;
            continue;
        }
        if (in_pos >= input_size) return false;
        {
            uint8_t count = input[in_pos++];
            if (count == 0U) {
                output[out_pos++] = 0x90U;
                previous = 0x90;
            } else {
                size_t repeat = (size_t)count - 1U;
                if (previous < 0 || repeat > output_size - out_pos)
                    return false;
                while (repeat-- != 0U)
                    output[out_pos++] = (uint8_t)previous;
            }
        }
    }
    return out_pos == output_size;
}

/* ------------------------------------------------------------------ */
/* Container.                                                         */
/* ------------------------------------------------------------------ */

typedef struct sit_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t mac_type;
    uint32_t mac_creator;
    uint32_t modified;
    uint16_t finder_flags;
    uint16_t crc16;
    uint8_t method;
    bool encrypted;
    bool folder;
    bool resource;
} sit_member;

typedef struct sit_stream_s {
    sit_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t declared_members;
    uint8_t version;
} sit_stream;

static uint16_t sit_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t sit_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool sit_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Mac Roman names may legally contain bytes a file system would choke on, so
 * only the filesystem-facing representation is sanitized.  '/' is not a path
 * separator in a StuffIt name -- the tree comes from folder markers -- so it
 * is replaced rather than honoured. */
static char *sit_component(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input;
    size_t output = 0U;
    if (size > SIT_MAX_NAME) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c == 0x7fU)
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output != 0U &&
           (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static char *sit_join(const char *prefix, const char *component,
                      const char *suffix) {
    char *joined;
    char *result;
    if (!component) return NULL;
    joined = (prefix && prefix[0]) ? xx_str_concat3(prefix, "/", component)
                                   : xx_str_dup(component);
    if (!joined || !suffix) return joined;
    result = xx_str_concat(joined, suffix);
    xx_str_free(joined);
    return result;
}

static bool sit_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void sit_stream_free(void *opaque) {
    sit_stream *stream = (sit_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool sit_add_member(sit_stream *stream, const sit_member *member) {
    sit_member *grown;
    if (!stream || !member || stream->count >= SIT_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (sit_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) *
                                             sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool sit_parse(Abstractformat *format, sit_stream **result) {
    uint8_t header[SIT_MASTER_HEADER_SIZE];
    sit_stream *stream = NULL;
    char *path[SIT_MAX_DEPTH + 1U];
    size_t depth = 0U;
    int64_t total;
    int64_t size;
    int64_t archive_size;
    int64_t cursor;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < 0 || format->base_address > total) return false;
    size = total - format->base_address;
    if (size < SIT_MASTER_HEADER_SIZE + SIT_MEMBER_HEADER_SIZE ||
        !sit_read_at(format->device, format->base_address, header,
                     sizeof(header)) ||
        xx_rt_memcmp(header, "SIT!", 4U) != 0 ||
        xx_rt_memcmp(header + 10U, "rLau", 4U) != 0)
        return false;

    /* The declared archive size is the anchor: bound it against the real
     * file before anything is walked. */
    archive_size = (int64_t)sit_be32(header + 6U);
    if (archive_size < SIT_MASTER_HEADER_SIZE + SIT_MEMBER_HEADER_SIZE ||
        archive_size > size)
        return false;

    stream = (sit_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->declared_members = sit_be16(header + 4U);
    stream->version = header[14];
    path[0] = NULL;

    cursor = SIT_MASTER_HEADER_SIZE;
    while (cursor + SIT_MEMBER_HEADER_SIZE <= archive_size) {
        uint8_t entry[SIT_MEMBER_HEADER_SIZE];
        sit_member member;
        uint8_t rsrc_method;
        uint8_t data_method;
        uint8_t name_length;
        uint32_t rsrc_unpacked;
        uint32_t data_unpacked;
        uint32_t rsrc_packed;
        uint32_t data_packed;
        char *component;
        char *full;
        bool rsrc_encrypted;
        bool data_encrypted;

        if (!sit_read_at(format->device, format->base_address + cursor, entry,
                         sizeof(entry)))
            goto fail;
        if (xx_crc16_arc_calc(0U, entry, 110U) != sit_be16(entry + 110U))
            goto fail;

        rsrc_method = entry[0];
        data_method = entry[1];
        if (rsrc_method == SIT_MARK_FOLDER_END ||
            data_method == SIT_MARK_FOLDER_END) {
            if (depth == 0U) goto fail;
            xx_str_free(path[depth]);
            path[depth--] = NULL;
            cursor += SIT_MEMBER_HEADER_SIZE;
            continue;
        }
        if (rsrc_method > SIT_MARK_FOLDER || data_method > SIT_MARK_FOLDER)
            goto fail;

        name_length = entry[2];
        if (name_length > SIT_MAX_NAME) name_length = (uint8_t)SIT_MAX_NAME;
        component = sit_component(entry + 3U, name_length);
        if (!component) goto fail;
        full = sit_join(depth != 0U ? path[depth] : NULL, component, NULL);
        xx_str_free(component);
        if (!full) goto fail;

        if (rsrc_method == SIT_MARK_FOLDER ||
            data_method == SIT_MARK_FOLDER) {
            if (depth >= SIT_MAX_DEPTH) {
                xx_str_free(full);
                goto fail;
            }
            xx_mem_zero(&member, sizeof(member));
            member.name = xx_str_dup(full);
            member.header_offset = format->base_address + cursor;
            member.data_offset = format->base_address + cursor +
                                 SIT_MEMBER_HEADER_SIZE;
            member.folder = true;
            member.modified = sit_be32(entry + 80U);
            if (!member.name || !sit_add_member(stream, &member)) {
                if (member.name) xx_str_free(member.name);
                xx_str_free(full);
                goto fail;
            }
            path[++depth] = full;
            cursor += SIT_MEMBER_HEADER_SIZE;
            continue;
        }

        rsrc_encrypted = (rsrc_method & 16U) != 0U;
        data_encrypted = (data_method & 16U) != 0U;
        rsrc_method = (uint8_t)(rsrc_method & 15U);
        data_method = (uint8_t)(data_method & 15U);

        rsrc_unpacked = sit_be32(entry + 84U);
        data_unpacked = sit_be32(entry + 88U);
        rsrc_packed = sit_be32(entry + 92U);
        data_packed = sit_be32(entry + 96U);
        if (rsrc_unpacked > SIT_MAX_FORK || data_unpacked > SIT_MAX_FORK ||
            rsrc_packed > SIT_MAX_FORK || data_packed > SIT_MAX_FORK) {
            xx_str_free(full);
            goto fail;
        }
        /* Bound both packed extents against what is really there before they
         * are recorded or used. */
        cursor += SIT_MEMBER_HEADER_SIZE;
        if ((int64_t)rsrc_packed > archive_size - cursor ||
            (int64_t)data_packed > archive_size - cursor -
                                       (int64_t)rsrc_packed) {
            xx_str_free(full);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = format->base_address + cursor -
                               SIT_MEMBER_HEADER_SIZE;
        member.mac_type = sit_be32(entry + 66U);
        member.mac_creator = sit_be32(entry + 70U);
        member.finder_flags = sit_be16(entry + 74U);
        member.modified = sit_be32(entry + 80U);

        /* Data fork first, resource fork second, matching the MacBinary and
         * AppleSingle readers' ".rsrc" member naming. */
        member.name = xx_str_dup(full);
        member.data_offset = format->base_address + cursor +
                             (int64_t)rsrc_packed;
        member.packed_size = (int64_t)data_packed;
        member.unpacked_size = data_unpacked;
        member.crc16 = sit_be16(entry + 102U);
        member.method = data_method;
        member.encrypted = data_encrypted;
        member.resource = false;
        if (!member.name || !sit_add_member(stream, &member)) {
            if (member.name) xx_str_free(member.name);
            xx_str_free(full);
            goto fail;
        }

        if (rsrc_unpacked != 0U || rsrc_packed != 0U) {
            member.name = sit_join(NULL, full, ".rsrc");
            member.data_offset = format->base_address + cursor;
            member.packed_size = (int64_t)rsrc_packed;
            member.unpacked_size = rsrc_unpacked;
            member.crc16 = sit_be16(entry + 100U);
            member.method = rsrc_method;
            member.encrypted = rsrc_encrypted;
            member.resource = true;
            if (!member.name || !sit_add_member(stream, &member)) {
                if (member.name) xx_str_free(member.name);
                xx_str_free(full);
                goto fail;
            }
        }
        xx_str_free(full);
        cursor += (int64_t)rsrc_packed + (int64_t)data_packed;
    }

    while (depth != 0U) xx_str_free(path[depth--]);
    if (stream->count == 0U) goto fail;
    stream->archive_size = archive_size;
    *result = stream;
    return true;
fail:
    while (depth != 0U) xx_str_free(path[depth--]);
    sit_stream_free(stream);
    return false;
}

static bool sit_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *sit_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sit_set_record(xx_archive_record *record,
                           const sit_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = SIT_MEMBER_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (member->resource) record->header_offset = -1;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc16) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->modified) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->finder_flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->mac_type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

static bool sit_decode_member(Abstractformat *format, const sit_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->encrypted ||
        member->packed_size < 0 || member->unpacked_size > SIZE_MAX)
        return false;
    if (member->folder) {
        *plain = NULL;
        *plain_size = 0U;
        return true;
    }
    output_size = (size_t)member->unpacked_size;
    if (member->method == SIT_METHOD_NONE &&
        (uint64_t)member->packed_size != member->unpacked_size)
        return false;
    packed = (uint8_t *)xx_mem_alloc(
        member->packed_size != 0 ? (size_t)member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        (member->packed_size != 0 &&
         !sit_read_at(format->device, member->data_offset, packed,
                      (size_t)member->packed_size)))
        goto fail;
    if (member->method == SIT_METHOD_NONE) {
        if (output_size != 0U) xx_mem_copy(output, packed, output_size);
        decoded = true;
    } else if (member->method == SIT_METHOD_RLE) {
        decoded = sit_rle90_decode(packed, (size_t)member->packed_size, output,
                                   output_size);
    } else if (member->method == SIT_METHOD_LZHUFF) {
        decoded = sit13_decode(packed, (size_t)member->packed_size, output,
                               output_size);
    }
    /* The stored CRC-16 is the only thing that separates a correct decode
     * from a plausible one, so it is a hard gate, not a warning. */
    if (!decoded || xx_crc16_arc_calc(0U, output, output_size) != member->crc16)
        goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = output_size;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_stuffit_init(xx_stuffit *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_STUFFIT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stuffit");
    xx_format_set_extension(&archive->format, "sit");
    archive->format.check_is_valid = xx_stuffit_check_is_valid;
    archive->format.handle_base_info = xx_stuffit_handle_base_info;
    archive->format.get_format_size = xx_stuffit_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_stuffit_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_stuffit_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_stuffit_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_stuffit_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_stuffit_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_stuffit_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_stuffit *xx_stuffit_create(xx_io_device *device, int64_t base_address) {
    xx_stuffit *archive = (xx_stuffit *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_stuffit_init(archive, device, base_address);
    return archive;
}

void xx_stuffit_destroy(xx_stuffit *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_stuffit_free(xx_stuffit *archive) {
    if (!archive) return;
    xx_stuffit_destroy(archive);
    xx_mem_free(archive);
}

bool xx_stuffit_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    sit_stream *stream;
    (void)pd;
    if (!sit_parse(format, &stream)) return false;
    sit_stream_free(stream);
    return true;
}

bool xx_stuffit_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    sit_stream *stream;
    xx_stuffit *archive;
    (void)pd;
    if (!format || !sit_parse(format, &stream)) {
        if (format) format->is_valid = false;
        return false;
    }
    archive = (xx_stuffit *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->declared_members = stream->declared_members;
    archive->version = stream->version;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_STUFFIT_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    sit_stream_free(stream);
    return true;
}

int64_t xx_stuffit_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_stuffit_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_stuffit_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_stuffit_handle_base_info(format, pd))
               ? ((xx_stuffit *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_stuffit_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    sit_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!sit_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sit_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sit_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!sit_copy_options(&state->options, options) ||
        !sit_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_stuffit_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_stuffit_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    sit_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (sit_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = sit_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_stuffit_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    sit_stream *stream;
    const sit_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (sit_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!sit_safe_output_name(member->name) ||
        !sit_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = sit_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_stuffit_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
