/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for Aladdin StuffIt's original "SIT!" container, as written
 * by StuffIt 1.x through 4.x, and the same layout under the tags later
 * StuffIt versions and StuffIt InstallerMaker used ("ST46", "ST50", "ST60",
 * "ST65", "STin", "STi2".."STi4").  StuffIt 5's "StuffIt (c)1997-" container
 * is a different format and is deliberately not claimed here.
 *
 * Layout, taken from XArchive's StuffIt module
 * (Algos/xdearkmodule_stuffit_p.cpp) and confirmed against the corpus:
 *
 *   master header, 22 bytes
 *     0   "SIT!" (or one of the tags above)
 *     4   uint16be  number of top-level members
 *     6   uint32be  total archive size -- exact, and used as the anchor
 *     10  "rLau"
 *     14  uint8     version
 *     15  7 reserved bytes
 *
 *   member header, 112 bytes, then the resource fork's packed bytes and then
 *   the data fork's packed bytes
 *     0   uint8     resource fork method: low nibble = algorithm, 32 =
 *                   folder, 33 = end of folder, 0x10 = encrypted (1.x),
 *                   0x80 = password protected (4.5 and later)
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
 * Decoding covers method 0 (stored), 1 (RLE90), 2 (LZW, the compress 4.0
 * transport), 3 (Huffman with a stored tree), 5 (LZAH, i.e. LHArc -lh1-,
 * through the library's lzh codec), 6 (fixed Huffman + PackBits, ported from
 * Deark, MIT) and 13 (LZ+Huffman, the method StuffIt 3 and later use for
 * almost everything); the method-13 decoder is a port of XArchive's
 * xdearkstuffit13_p.cpp, which is itself a C port of the MIT-licensed compcol
 * clean-room implementation (Copyright (c) 2026 Karpeles Lab Inc.).  Methods
 * 8 (MW), 14 (installer) and 15 (Arsenic), plus every encrypted fork, are
 * listed but fail closed.  Every decode is verified against the fork's
 * stored CRC-16/ARC before it is accepted.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stuffit/xx_stuffit.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
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
/* A fork is decoded in memory; refuse to allocate more than this for one. */
#define SIT_MAX_DECODE UINT64_C(0x10000000)

#define SIT_METHOD_NONE 0U
#define SIT_METHOD_RLE 1U
#define SIT_METHOD_LZW 2U
#define SIT_METHOD_HUFFMAN 3U
#define SIT_METHOD_LZAH 5U
#define SIT_METHOD_FIXEDHUFF 6U
#define SIT_METHOD_LZHUFF 13U
#define SIT_MARK_FOLDER 32U
#define SIT_MARK_FOLDER_END 33U
/* Method byte flags: 0x10 is StuffIt 1.x encryption, 0x80 the password
 * protection of StuffIt 4.5 and later.  On a folder marker 0x10 means the
 * folder holds encrypted items. */
#define SIT_FLAG_ENCRYPTED_OLD 0x10U
#define SIT_FLAG_ENCRYPTED 0x80U
#define SIT_FLAG_ENCRYPTED_ANY (SIT_FLAG_ENCRYPTED_OLD | SIT_FLAG_ENCRYPTED)

/* The folder-marker value of a method byte, flags stripped. */
static uint8_t sit_marker(uint8_t method) {
    return (uint8_t)(method & (uint8_t)~SIT_FLAG_ENCRYPTED_ANY);
}

/* "SIT!" is StuffIt 1.x-4.x; later StuffIt and its installer maker kept the
 * same classic layout under other tags, always with "rLau" at offset 10. */
static bool sit_signature(const uint8_t *header) {
    static const char tags[9][5] = {"SIT!", "ST46", "ST50", "ST60", "ST65",
                                    "STin", "STi2", "STi3", "STi4"};
    size_t index;
    if (xx_rt_memcmp(header + 10U, "rLau", 4U) != 0) return false;
    for (index = 0U; index < 9U; ++index)
        if (xx_rt_memcmp(header, tags[index], 4U) == 0) return true;
    return false;
}

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

static uint16_t sit_be16(const uint8_t *bytes);
static uint32_t sit_be32(const uint8_t *bytes);

/* ------------------------------------------------------------------ */
/* Method 2: LZW.                                                     */
/* ------------------------------------------------------------------ */

/* The Unix compress 4.0 transport without its 3-byte header: LSB-first
 * codes from 9 to 14 bits, code 256 clears the table (block mode), the
 * first free code is 257, and codes travel in groups of eight of one width,
 * so both a clear and a width change skip the rest of the current group. */
#define SIT_LZW_MIN_BITS 9U
#define SIT_LZW_MAX_BITS 14U
#define SIT_LZW_TABLE (1U << SIT_LZW_MAX_BITS)
#define SIT_LZW_CLEAR 256U
#define SIT_LZW_FIRST 257U
#define SIT_LZW_NONE UINT32_MAX

typedef struct sit_lsb_reader_s {
    const uint8_t *data;
    size_t size;
    size_t byte_pos;
    uint32_t bits;
    unsigned count;
    uint64_t consumed;
} sit_lsb_reader;

static bool sit_lsb_read(sit_lsb_reader *r, unsigned width, uint32_t *value) {
    while (r->count < width) {
        if (r->byte_pos >= r->size) return false;
        r->bits |= (uint32_t)r->data[r->byte_pos++] << r->count;
        r->count += 8U;
    }
    *value = r->bits & ((UINT32_C(1) << width) - 1U);
    r->bits >>= width;
    r->count -= width;
    r->consumed += width;
    return true;
}

/* Skip to the end of the current group of eight codes; running out of input
 * here is not an error by itself -- the next code read reports it. */
static void sit_lzw_end_group(sit_lsb_reader *r, unsigned width,
                              uint64_t *group_start) {
    uint64_t group_bits = (uint64_t)width * 8U;
    uint64_t used = r->consumed - *group_start;
    uint64_t skip = (group_bits - used % group_bits) % group_bits;
    while (skip != 0U) {
        uint32_t ignored;
        unsigned take = skip > 16U ? 16U : (unsigned)skip;
        if (!sit_lsb_read(r, take, &ignored)) break;
        skip -= take;
    }
    *group_start = r->consumed;
}

static bool sit_lzw_decode(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size) {
    sit_lsb_reader reader;
    uint16_t *prefix = NULL;
    uint8_t *suffix = NULL;
    uint8_t *stack = NULL;
    uint64_t group_start = 0U;
    uint32_t next_code = SIT_LZW_FIRST;
    uint32_t old_code = SIT_LZW_NONE;
    uint8_t first_byte = 0U;
    unsigned width = SIT_LZW_MIN_BITS;
    size_t out_pos = 0U;
    bool ok = false;

    if (output_size == 0U) return true;
    if (!input || !output) return false;
    xx_mem_zero(&reader, sizeof(reader));
    reader.data = input;
    reader.size = input_size;
    prefix = (uint16_t *)xx_mem_alloc(SIT_LZW_TABLE * sizeof(*prefix));
    suffix = (uint8_t *)xx_mem_alloc(SIT_LZW_TABLE);
    stack = (uint8_t *)xx_mem_alloc(SIT_LZW_TABLE);
    if (!prefix || !suffix || !stack) goto done;

    while (out_pos < output_size) {
        uint32_t code;
        uint32_t in_code;
        size_t depth = 0U;
        if (!sit_lsb_read(&reader, width, &code)) goto done;
        if (code == SIT_LZW_CLEAR) {
            sit_lzw_end_group(&reader, width, &group_start);
            width = SIT_LZW_MIN_BITS;
            next_code = SIT_LZW_FIRST;
            old_code = SIT_LZW_NONE;
            continue;
        }
        if (old_code == SIT_LZW_NONE) {
            if (code > 0xffU) goto done;
            output[out_pos++] = (uint8_t)code;
            old_code = code;
            first_byte = (uint8_t)code;
            continue;
        }
        in_code = code;
        if (code >= next_code) {
            /* Only the code about to be defined may be used early. */
            if (code != next_code || next_code >= SIT_LZW_TABLE) goto done;
            stack[depth++] = first_byte;
            code = old_code;
        }
        while (code > 0xffU) {
            if (code >= next_code || depth >= SIT_LZW_TABLE) goto done;
            stack[depth++] = suffix[code];
            code = prefix[code];
        }
        if (depth >= SIT_LZW_TABLE) goto done;
        first_byte = (uint8_t)code;
        stack[depth++] = first_byte;
        while (depth != 0U && out_pos < output_size)
            output[out_pos++] = stack[--depth];
        if (next_code < SIT_LZW_TABLE) {
            prefix[next_code] = (uint16_t)old_code;
            suffix[next_code] = first_byte;
            ++next_code;
            if (next_code >= (UINT32_C(1) << width) &&
                width < SIT_LZW_MAX_BITS) {
                sit_lzw_end_group(&reader, width, &group_start);
                ++width;
            }
        }
        old_code = in_code;
    }
    ok = true;
done:
    if (prefix) xx_mem_free(prefix);
    if (suffix) xx_mem_free(suffix);
    if (stack) xx_mem_free(stack);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Methods 3 and 6: Huffman.                                          */
/* ------------------------------------------------------------------ */

/* A binary decode tree shared by both Huffman methods.  Node 0 is the root;
 * a node is either a leaf (symbol) or has two children.  Codes are read
 * MSB-first. */
#define SIT_HUFF_MAX_NODES 1024U
#define SIT_HUFF_EMPTY 0xffffU

typedef struct sit_huff_tree_s {
    uint16_t child[SIT_HUFF_MAX_NODES][2];
    uint16_t symbol[SIT_HUFF_MAX_NODES];
    size_t count;
} sit_huff_tree;

typedef struct sit_msb_reader_s {
    const uint8_t *data;
    size_t size;
    size_t bit_pos;
} sit_msb_reader;

static bool sit_msb_read(sit_msb_reader *r, unsigned count, uint32_t *value) {
    uint32_t result = 0U;
    unsigned index;
    if (count > 32U || r->bit_pos > r->size * 8U ||
        (size_t)count > r->size * 8U - r->bit_pos)
        return false;
    for (index = 0U; index < count; ++index) {
        result = (result << 1U) |
                 ((uint32_t)(r->data[r->bit_pos >> 3U] >>
                             (7U - (r->bit_pos & 7U))) & 1U);
        r->bit_pos++;
    }
    *value = result;
    return true;
}

static bool sit_huff_new_node(sit_huff_tree *tree, uint16_t *index) {
    if (tree->count >= SIT_HUFF_MAX_NODES) return false;
    tree->child[tree->count][0] = SIT_HUFF_EMPTY;
    tree->child[tree->count][1] = SIT_HUFF_EMPTY;
    tree->symbol[tree->count] = SIT_HUFF_EMPTY;
    *index = (uint16_t)tree->count++;
    return true;
}

/* Method 3 stores its tree in preorder: a 1 bit is a leaf followed by the
 * 8-bit symbol, a 0 bit is an inner node followed by its 0 and 1 subtrees.
 * A tree with n leaves has 2n - 1 nodes, so 511 bounds any tree over byte
 * symbols (and 255 its depth); the walk is iterative with an explicit list
 * of open slots. */
static bool sit_huff_read_tree(sit_huff_tree *tree, sit_msb_reader *reader) {
    uint16_t pending[512];
    size_t open = 0U;
    uint16_t root;
    uint32_t bit;
    tree->count = 0U;
    if (!sit_huff_new_node(tree, &root)) return false;
    if (!sit_msb_read(reader, 1U, &bit)) return false;
    if (bit) return false; /* a lone leaf carries no code bits */
    pending[open++] = (uint16_t)(root * 2U + 1U);
    pending[open++] = (uint16_t)(root * 2U);
    while (open != 0U) {
        uint16_t slot = pending[--open];
        uint16_t node;
        if (!sit_huff_new_node(tree, &node) || tree->count > 511U) return false;
        tree->child[slot >> 1U][slot & 1U] = node;
        if (!sit_msb_read(reader, 1U, &bit)) return false;
        if (bit) {
            uint32_t value;
            if (!sit_msb_read(reader, 8U, &value)) return false;
            tree->symbol[node] = (uint16_t)value;
        } else {
            if (open + 2U > sizeof(pending) / sizeof(pending[0])) return false;
            pending[open++] = (uint16_t)(node * 2U + 1U);
            pending[open++] = (uint16_t)(node * 2U);
        }
    }
    return true;
}

static bool sit_huff_decode_symbol(const sit_huff_tree *tree,
                                   sit_msb_reader *reader, uint32_t *symbol) {
    uint16_t node = 0U;
    unsigned depth = 0U;
    while (tree->symbol[node] == SIT_HUFF_EMPTY) {
        uint32_t bit;
        uint16_t next;
        if (++depth >= SIT_HUFF_MAX_NODES || !sit_msb_read(reader, 1U, &bit))
            return false;
        next = tree->child[node][bit];
        if (next == SIT_HUFF_EMPTY || next >= tree->count) return false;
        node = next;
    }
    *symbol = tree->symbol[node];
    return true;
}

static bool sit_huffman_decode(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size) {
    sit_huff_tree *tree;
    sit_msb_reader reader;
    size_t out_pos = 0U;
    bool ok = false;
    if (output_size == 0U) return true;
    if (!input || !output) return false;
    tree = (sit_huff_tree *)xx_mem_alloc(sizeof(*tree));
    if (!tree) return false;
    reader.data = input;
    reader.size = input_size;
    reader.bit_pos = 0U;
    if (!sit_huff_read_tree(tree, &reader)) goto done;
    while (out_pos < output_size) {
        uint32_t symbol;
        if (!sit_huff_decode_symbol(tree, &reader, &symbol)) goto done;
        output[out_pos++] = (uint8_t)symbol;
    }
    ok = true;
done:
    xx_mem_free(tree);
    return ok;
}

/* Method 6, "fixed Huffman": ported from Deark's modules/stuffit.c
 * (do_decompr_fixedhuff and sit_fixedhuff_init_tree; Copyright (C) 2018
 * Jason Summers, MIT license).  A fixed set of 257 codes whose lengths are
 * given run-length encoded below; the codes are assigned in symbol order
 * (not canonically).  The data is a sequence of blocks, each introduced by a
 * signed 32-bit size: a non-negative size is a Huffman block (intermediate
 * length, a translation table, then codes until the length, the stop code
 * 256 or the block end), a negative size is a raw PackBits block.  Either
 * kind is PackBits-expanded into the output. */
#define SIT_FH_CODES 257U

static bool sit_fixedhuff_build(sit_huff_tree *tree) {
    static const uint8_t run_counts[13] = {1, 1, 4, 12, 32, 16, 49,
                                           2, 2, 40, 95, 2, 1};
    static const uint8_t run_lengths[13] = {3, 4, 5, 6, 7, 8, 9,
                                            10, 9, 10, 11, 13, 12};
    uint32_t previous_code = 0U;
    unsigned previous_length = 0U;
    unsigned symbol = 0U;
    size_t run;
    uint16_t root;
    tree->count = 0U;
    if (!sit_huff_new_node(tree, &root)) return false;
    for (run = 0U; run < 13U; ++run) {
        unsigned repeat;
        for (repeat = 0U; repeat < run_counts[run]; ++repeat) {
            unsigned length = run_lengths[run];
            uint32_t code;
            uint16_t node = root;
            unsigned bit_index;
            if (symbol >= SIT_FH_CODES) return false;
            if (previous_length == 0U)
                code = 0U;
            else if (length < previous_length)
                code = (previous_code >> (previous_length - length)) + 1U;
            else
                code = (previous_code + 1U) << (length - previous_length);
            previous_code = code;
            previous_length = length;
            for (bit_index = length; bit_index != 0U; --bit_index) {
                unsigned bit = (unsigned)(code >> (bit_index - 1U)) & 1U;
                if (tree->symbol[node] != SIT_HUFF_EMPTY) return false;
                if (tree->child[node][bit] == SIT_HUFF_EMPTY) {
                    uint16_t created;
                    if (!sit_huff_new_node(tree, &created)) return false;
                    tree->child[node][bit] = created;
                }
                node = tree->child[node][bit];
            }
            if (tree->symbol[node] != SIT_HUFF_EMPTY ||
                tree->child[node][0] != SIT_HUFF_EMPTY ||
                tree->child[node][1] != SIT_HUFF_EMPTY)
                return false;
            tree->symbol[node] = (uint16_t)symbol++;
        }
    }
    return symbol == SIT_FH_CODES;
}

typedef struct sit_packbits_s {
    uint8_t *output;
    size_t size;
    size_t pos;
    uint32_t literal;
    uint32_t repeat;
} sit_packbits;

static void sit_packbits_put(sit_packbits *pb, uint8_t byte) {
    if (pb->literal != 0U) {
        if (pb->pos < pb->size) pb->output[pb->pos++] = byte;
        pb->literal--;
    } else if (pb->repeat != 0U) {
        uint32_t count = pb->repeat;
        while (count-- != 0U && pb->pos < pb->size)
            pb->output[pb->pos++] = byte;
        pb->repeat = 0U;
    } else if (byte > 128U) {
        pb->repeat = 257U - (uint32_t)byte;
    } else if (byte < 128U) {
        pb->literal = (uint32_t)byte + 1U;
    }
}

static int32_t sit_be32s(const uint8_t *bytes) {
    uint32_t value = ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
                     ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
    return value <= INT32_MAX ? (int32_t)value
                              : -(int32_t)(UINT32_MAX - value) - 1;
}

static bool sit_fixedhuff_decode(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_size) {
    sit_huff_tree *tree;
    uint8_t translation[256];
    sit_packbits pb;
    size_t pos = 0U;
    bool ok = false;
    if (output_size == 0U) return true;
    if (!input || !output) return false;
    tree = (sit_huff_tree *)xx_mem_alloc(sizeof(*tree));
    if (!tree) return false;
    if (!sit_fixedhuff_build(tree)) goto done;
    xx_mem_zero(translation, sizeof(translation));
    xx_mem_zero(&pb, sizeof(pb));
    pb.output = output;
    pb.size = output_size;
    /* Every block consumes at least four bytes, so this loop is bounded. */
    while (pb.pos < output_size && input_size - pos >= 4U) {
        int32_t raw = sit_be32s(input + pos);
        size_t block_end;
        pb.literal = pb.repeat = 0U;
        if (raw >= 0) {
            uint32_t intermediate;
            uint32_t produced = 0U;
            uint32_t definitions;
            uint32_t index;
            sit_msb_reader reader;
            if (raw < 10) break;
            if ((uint32_t)raw > input_size - pos) goto done;
            block_end = pos + (size_t)raw;
            intermediate = sit_be32(input + pos + 4U);
            definitions = sit_be16(input + pos + 8U);
            if (definitions > 256U || definitions > block_end - pos - 10U)
                goto done;
            for (index = 0U; index < definitions; ++index)
                translation[index] = input[pos + 10U + index];
            reader.data = input + pos + 10U + definitions;
            reader.size = block_end - (pos + 10U + definitions);
            reader.bit_pos = 0U;
            while (produced < intermediate && pb.pos < output_size) {
                uint32_t symbol;
                if (reader.bit_pos >= reader.size * 8U) break;
                if (!sit_huff_decode_symbol(tree, &reader, &symbol)) {
                    /* Running off the block end is how a block stops;
                     * anything else is a code outside the fixed set. */
                    if (reader.bit_pos >= reader.size * 8U) break;
                    goto done;
                }
                if (symbol > 0xffU) break; /* stop code */
                sit_packbits_put(&pb, translation[symbol]);
                produced++;
            }
        } else {
            uint32_t length = raw == INT32_MIN ? UINT32_C(0x80000000)
                                               : (uint32_t)(-raw);
            size_t index;
            if (length < 4U) break;
            if (length > input_size - pos) goto done;
            block_end = pos + (size_t)length;
            for (index = pos + 4U; index < block_end && pb.pos < output_size;
                 ++index)
                sit_packbits_put(&pb, input[index]);
        }
        pos = block_end;
    }
    ok = pb.pos == output_size;
done:
    xx_mem_free(tree);
    return ok;
}

/* Method 5, LZAH: the LHArc -lh1- coder (4 KiB window preset to spaces,
 * adaptive Huffman), which the library already implements. */
static bool sit_lzah_decode(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size) {
    size_t written = 0U;
    if (output_size == 0U) return true;
    if (!input || !output) return false;
    return xx_lzh1_decode_memory(input, input_size, output, output_size,
                                 &written) &&
           written == output_size;
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

/* Mac OS Roman 0x80..0xFF as Unicode (Apple's ROMAN.TXT, euro at 0xDB), the
 * same table the MacBinary reader uses. */
static const uint16_t SIT_MAC_ROMAN_HIGH[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

static char sit_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Windows opens a device instead of a file for CON, PRN, AUX, NUL, COM0-9,
 * LPT0-9, CONIN$, CONOUT$ and CLOCK$, with or without an extension, in any
 * case and with trailing spaces before the extension. */
static bool sit_is_device_name(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U;
    size_t index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        const char *word = devices[index];
        size_t at = 0U;
        while (at < stem && word[at] && sit_upper(name[at]) == word[at]) ++at;
        if (at == stem && word[at] == 0) return true;
    }
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((sit_upper(name[0]) == 'C' && sit_upper(name[1]) == 'O' &&
             sit_upper(name[2]) == 'M') ||
            (sit_upper(name[0]) == 'L' && sit_upper(name[1]) == 'P' &&
             sit_upper(name[2]) == 'T'));
}

/* Mac Roman names may legally contain bytes a file system would choke on, so
 * only the filesystem-facing representation is sanitized: the name becomes
 * UTF-8, separators, wildcards and control codes become '_', trailing dots
 * and spaces go (so "." and ".." cannot survive), an empty result becomes
 * "_" and a Windows device name gets a '_' prefix.  '/' is not a path
 * separator in a StuffIt name -- the tree comes from folder markers -- so it
 * is replaced rather than honoured. */
static char *sit_component(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input;
    size_t output = 0U;
    if (size > SIT_MAX_NAME) return NULL;
    name = (char *)xx_mem_alloc(1U + size * 3U + 2U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c == 0x7fU) {
            name[output++] = '_';
        } else if (c < 0x80U) {
            name[output++] = (char)c;
        } else {
            uint32_t point = SIT_MAC_ROMAN_HIGH[c - 0x80U];
            if (point < 0x800U) {
                name[output++] = (char)(0xC0U | (point >> 6U));
                name[output++] = (char)(0x80U | (point & 0x3FU));
            } else {
                name[output++] = (char)(0xE0U | (point >> 12U));
                name[output++] = (char)(0x80U | ((point >> 6U) & 0x3FU));
                name[output++] = (char)(0x80U | (point & 0x3FU));
            }
        }
    }
    while (output != 0U &&
           (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    if (sit_is_device_name(name, output)) {
        xx_rt_memmove(name + 1U, name, output);
        name[0] = '_';
        ++output;
    }
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
                (length == 2U && segment[0] == '.' && segment[1] == '.') ||
                sit_is_device_name(segment, length))
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

typedef struct sit_name_key_s {
    const char *name;
    size_t item;
} sit_name_key;

/* Next code point of a name this reader produced (ASCII plus the two- and
 * three-byte UTF-8 sequences of the Mac Roman table); anything else is taken
 * one byte at a time.  A NUL is never a continuation byte, so this cannot
 * step past the terminator. */
static uint32_t sit_next_code_point(const char **text) {
    const unsigned char *at = (const unsigned char *)*text;
    if (at[0] >= 0xC0U && at[0] < 0xE0U && (at[1] & 0xC0U) == 0x80U) {
        *text += 2;
        return ((uint32_t)(at[0] & 0x1FU) << 6U) | (uint32_t)(at[1] & 0x3FU);
    }
    if (at[0] >= 0xE0U && at[0] < 0xF0U && (at[1] & 0xC0U) == 0x80U &&
        (at[2] & 0xC0U) == 0x80U) {
        *text += 3;
        return ((uint32_t)(at[0] & 0x0FU) << 12U) |
               ((uint32_t)(at[1] & 0x3FU) << 6U) | (uint32_t)(at[2] & 0x3FU);
    }
    *text += 1;
    return at[0];
}

/* Case folding as a case-insensitive file system applies it to the
 * characters a Mac Roman name can produce: ASCII, Latin-1 letters, y with
 * diaeresis, the oe ligature, dotless i, pi and micro.  Folding too much only
 * costs an extra rename; folding too little would let one member overwrite
 * another. */
static uint32_t sit_fold_code_point(uint32_t point) {
    if (point >= 'a' && point <= 'z') return point - 0x20U;
    if (point >= 0xE0U && point <= 0xFEU && point != 0xF7U)
        return point - 0x20U;
    switch (point) {
    case 0xFFU: return 0x178U;
    case 0x153U: return 0x152U;
    case 0x131U: return 'I';
    case 0x3C0U: return 0x3A0U;
    case 0xB5U: return 0x39CU;
    default: return point;
    }
}

static int sit_name_order(const char *left, const char *right) {
    for (;;) {
        uint32_t a;
        uint32_t b;
        if (!*left || !*right) return (*left ? 1 : 0) - (*right ? 1 : 0);
        a = sit_fold_code_point(sit_next_code_point(&left));
        b = sit_fold_code_point(sit_next_code_point(&right));
        if (a != b) return a < b ? -1 : 1;
    }
}

static int sit_compare_names(const void *left, const void *right) {
    const sit_name_key *a = (const sit_name_key *)left;
    const sit_name_key *b = (const sit_name_key *)right;
    int order = sit_name_order(a->name, b->name);
    if (order != 0) return order;
    return a->item < b->item ? -1 : (a->item > b->item ? 1 : 0);
}

/* Two members may end up with the same output path, ignoring case: the
 * same name twice in a folder, names that only differ by the characters the
 * sanitizer replaces, a data fork called "x.rsrc" next to the resource fork
 * of "x", or a file named like a folder.  Folders may share a path (they
 * just merge); every clashing file except the first -- and every file that
 * clashes with a folder -- gets "_<record index>" appended, repeated until
 * nothing clashes or the round limit refuses the archive. */
static bool sit_make_names_unique(sit_stream *stream) {
    sit_name_key *keys;
    size_t round;
    if (stream->count < 2U) return true;
    if (stream->count > SIZE_MAX / sizeof(*keys)) return false;
    keys = (sit_name_key *)xx_mem_alloc(stream->count * sizeof(*keys));
    if (!keys) return false;
    for (round = 0U; round < 4U; ++round) {
        size_t index;
        size_t start;
        bool changed = false;
        for (index = 0U; index < stream->count; ++index) {
            keys[index].name = stream->items[index].name;
            keys[index].item = index;
        }
        xx_rt_qsort(keys, stream->count, sizeof(*keys), sit_compare_names);
        for (start = 0U; start < stream->count;) {
            size_t end = start + 1U;
            bool has_folder = stream->items[keys[start].item].folder;
            while (end < stream->count &&
                   sit_name_order(keys[end].name, keys[start].name) == 0) {
                if (stream->items[keys[end].item].folder) has_folder = true;
                ++end;
            }
            for (index = start; index < end && end - start > 1U; ++index) {
                sit_member *member = &stream->items[keys[index].item];
                char suffix[48];
                char *renamed;
                if (member->folder || (!has_folder && index == start))
                    continue;
                if (round == 0U)
                    (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u",
                                         (unsigned)keys[index].item);
                else
                    (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u_%u",
                                         (unsigned)keys[index].item,
                                         (unsigned)round);
                renamed = xx_str_concat(member->name, suffix);
                if (!renamed) {
                    xx_mem_free(keys);
                    return false;
                }
                xx_str_free(member->name);
                member->name = renamed;
                changed = true;
            }
            start = end;
        }
        if (!changed) {
            xx_mem_free(keys);
            return true;
        }
    }
    xx_mem_free(keys);
    return false;
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
        !sit_signature(header))
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
        if (sit_marker(rsrc_method) == SIT_MARK_FOLDER_END ||
            sit_marker(data_method) == SIT_MARK_FOLDER_END) {
            if (depth == 0U) goto fail;
            xx_str_free(path[depth]);
            path[depth--] = NULL;
            cursor += SIT_MEMBER_HEADER_SIZE;
            continue;
        }
        /* 0x20 and 0x40 only ever appear in the folder markers. */
        if ((sit_marker(rsrc_method) != SIT_MARK_FOLDER &&
             (rsrc_method & 0x60U) != 0U) ||
            (sit_marker(data_method) != SIT_MARK_FOLDER &&
             (data_method & 0x60U) != 0U))
            goto fail;

        name_length = entry[2];
        if (name_length > SIT_MAX_NAME) name_length = (uint8_t)SIT_MAX_NAME;
        component = sit_component(entry + 3U, name_length);
        if (!component) goto fail;
        full = sit_join(depth != 0U ? path[depth] : NULL, component, NULL);
        xx_str_free(component);
        if (!full) goto fail;

        if (sit_marker(rsrc_method) == SIT_MARK_FOLDER ||
            sit_marker(data_method) == SIT_MARK_FOLDER) {
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

        rsrc_encrypted = (rsrc_method & SIT_FLAG_ENCRYPTED_ANY) != 0U;
        data_encrypted = (data_method & SIT_FLAG_ENCRYPTED_ANY) != 0U;
        rsrc_method = (uint8_t)(rsrc_method & 15U);
        data_method = (uint8_t)(data_method & 15U);

        rsrc_unpacked = sit_be32(entry + 84U);
        data_unpacked = sit_be32(entry + 88U);
        rsrc_packed = sit_be32(entry + 92U);
        data_packed = sit_be32(entry + 96U);
        /* Bound both packed extents against what is really there before they
         * are recorded or used.  Unpacked sizes are only claims; the decoder
         * caps what it will allocate for them. */
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
    if (stream->count == 0U || !sit_make_names_unique(stream)) goto fail;
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

/* The most output one packed byte can yield, rounded up generously: RLE90
 * gives at most 254 bytes per two-byte pair, method 3 at least one bit per
 * byte, LZAH at most 60 bytes per match of ten bits or more, fixed Huffman
 * at most 128 bytes per PackBits pair of two three-bit codes, and LZW and
 * method 13 at most one 16 KiB string per 9 bits and 32832 bytes per match
 * of 17 bits or more.  An unpacked size beyond this is a lie that would
 * only make the decoder allocate for nothing. */
static uint64_t sit_max_expansion(uint8_t method) {
    switch (method) {
    case SIT_METHOD_NONE: return 1U;
    case SIT_METHOD_RLE: return 128U;
    case SIT_METHOD_HUFFMAN: return 16U;
    case SIT_METHOD_LZAH:
    case SIT_METHOD_FIXEDHUFF: return 256U;
    default: return 16384U;
    }
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
    if (member->unpacked_size > SIT_MAX_DECODE ||
        (uint64_t)member->packed_size > SIT_MAX_DECODE)
        return false;
    output_size = (size_t)member->unpacked_size;
    if (member->method == SIT_METHOD_NONE &&
        (uint64_t)member->packed_size != member->unpacked_size)
        return false;
    /* Methods 8 (MW), 14 (installer) and 15 (Arsenic) are not decoded; fail
     * before reading anything. */
    if (member->method != SIT_METHOD_NONE && member->method != SIT_METHOD_RLE &&
        member->method != SIT_METHOD_LZW &&
        member->method != SIT_METHOD_HUFFMAN &&
        member->method != SIT_METHOD_LZAH &&
        member->method != SIT_METHOD_FIXEDHUFF &&
        member->method != SIT_METHOD_LZHUFF)
        return false;
    if (member->unpacked_size >
        (uint64_t)member->packed_size * sit_max_expansion(member->method) +
            4096U)
        return false;
    packed =(uint8_t *)xx_mem_alloc(
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
    } else if (member->method == SIT_METHOD_LZW) {
        decoded = sit_lzw_decode(packed, (size_t)member->packed_size, output,
                                 output_size);
    } else if (member->method == SIT_METHOD_HUFFMAN) {
        decoded = sit_huffman_decode(packed, (size_t)member->packed_size,
                                     output, output_size);
    } else if (member->method == SIT_METHOD_LZAH) {
        decoded = sit_lzah_decode(packed, (size_t)member->packed_size, output,
                                  output_size);
    } else if (member->method == SIT_METHOD_FIXEDHUFF) {
        decoded = sit_fixedhuff_decode(packed, (size_t)member->packed_size,
                                       output, output_size);
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
    bool created = false;

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
        created = destination != NULL;
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
    if (!result && path && !member->folder && created) xx_rt_remove(path);
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
