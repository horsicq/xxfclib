/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xx_die_engine_api_internal.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/entropy/xx_entropy.h"
#include "xxfclib/algo/md5/xx_md5.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/kpa/xx_kpa.h"
#include "xxfclib/fs/xx_fs.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/formats/other/javascript/xx_javascript.h"
#include "xxfclib/formats/filesystem/iso9660/xx_iso9660.h"
#include "xx_die_engine_xdisasm.h"
#include "../js/xx_js_internal.h"
#include "../regex/xx_regex_die.h"
#include "xxfclib/formats/executable/pe/xx_pe_unwind_validate.h"
#include "xxfclib/formats/executable/amigahunk/xx_amigahunk.h"
#include "xxfclib/data/xx_data.h"

#include <limits.h>

static const char *machofat_cpu_to_arch(uint32_t cpu)
{
    switch (cpu) {
        case 1: return "VAX";
        case 2: return "ROMP";
        case 4: return "NS32032";
        case 5: return "NS32332";
        case 6: return "MC680x0";
        case 7: return "I386";
        case 0x1000007: return "X86_64";
        case 8: return "MIPS";
        case 9: return "NS32532";
        case 0xB: return "HPPA";
        case 0xC: return "ARM";
        case 0x100000C: return "ARM64";
        case 0x200000C: return "ARM64_32";
        case 0xD: return "MC88000";
        case 0xE: return "SPARC";
        case 0xF: return "I860";
        case 0x10: return "I860_LITTLE";
        case 0x11: return "RS6000";
        case 0x12: return "POWERPC";
        case 0x1000012: return "POWERPC64";
        case 255: return "VEO";
        default: return "Unknown";
    }
}

static bool arch_in_list(const char *list, const char *arch)
{
    size_t arch_len;
    if (!list || !arch) return false;
    arch_len = xx_str_len(arch);
    while (*list) {
        if (!memcmp(list, arch, arch_len) && (list[arch_len] == ',' || list[arch_len] == '\0')) {
            return true;
        }
        while (*list && *list != ',') list++;
        if (*list == ',') {
            list++;
            while (*list == ' ') list++;
        }
    }
    return false;
}

static char *native_iso_identifier(xx_io_device *file, int64_t offset, size_t size)
{
    xx_iso9660 iso;
    char *text;
    xx_iso9660_init(&iso, file, 0);
    text = xx_iso9660_get_identifier(&iso, offset, size, NULL);
    xx_iso9660_destroy(&iso);
    return text;
}

/* The NE target-OS byte (IMAGE_OS2_HEADER::ne_exetyp at NE-header offset 0x36),
 * read straight from the file as XNE does. */
static cd_u8 ne_target_os(xx_io_device *pFile)
{
    cd_i64 nLfanew = (cd_i64)xx_io_get_u32(pFile, 0x3C, false);

    return xx_io_get_u8(pFile, nLfanew + 0x36);
}

/* XNE::getOsName. */
static const char *ne_os_name(cd_u8 nOS)
{
    if ((nOS == 1) || (nOS == 0x81)) return "OS/2";
    if ((nOS == 2) || (nOS == 4) || (nOS == 0x82)) return "Windows";
    if (nOS == 3) return "MS-DOS";
    if (nOS == 5) return "Borland OS Services";

    return "Unknown";
}

/* XNE::getArch. */
static const char *ne_arch_name(cd_u8 nOS)
{
    return ((nOS == 4) || (nOS == 5)) ? "386" : "286";
}

/* ------------------------------------------------------- plain text  -----
 *
 * XBinary::isPlainTextType, byte for byte. It looks at the first 32 KB and
 * classifies every byte, then applies three ratio tests. Any NUL disqualifies
 * the file outright, as does a UTF-8 or UTF-16 byte order mark - those are
 * reported by isUTF8Text and isUnicodeText instead.
 */
/* The classification sample sizes below are semantic limits. Device bytes
 * are borrowed from globally sized file windows; no separate prefix buffer
 * is needed. Small fixed counters and decoder lookahead are parser state. */
typedef struct {
    xx_io_device *file;
    const unsigned char *bytes, *view;
    cd_i64 size, base, verified;
    size_t capacity, valid;
    int failed;
} PrefixInput;

static int prefix_window(PrefixInput *input, cd_i64 offset)
{
    size_t count = (cd_u64)(input->size - offset) < input->capacity ? (size_t)(input->size - offset) : input->capacity;
    input->view = xx_io_buffer_view(input->file, offset, &count);
    if (!input->view || !count) {
        input->failed = 1;
        return 0;
    }
    input->base = offset;
    input->valid = count;
    if (offset <= input->verified && offset + (cd_i64)count > input->verified) input->verified = offset + (cd_i64)count;
    return 1;
}

static unsigned char prefix_byte(PrefixInput *input, cd_i64 offset)
{
    if (input->bytes) return input->bytes[(size_t)offset];
    if (input->failed) return 0;
    if (!input->view || offset < input->base || (cd_u64)(offset - input->base) >= input->valid) {
        if (!prefix_window(input, offset)) return 0;
    }
    return input->view[(size_t)(offset - input->base)];
}

static int is_plain_text_data(PrefixInput *pInput, cd_i64 nSize)
{
    cd_i64 i = 0;
    cd_i64 nControl = 0;
    cd_i64 nPrintable = 0;
    cd_i64 nExtended = 0;
    if ((nSize >= 3) && (prefix_byte(pInput, 0) == 0xEF) && (prefix_byte(pInput, 1) == 0xBB) && (prefix_byte(pInput, 2) == 0xBF)) {
        return 0;
    }

    if (nSize >= 2) {
        if (((prefix_byte(pInput, 0) == 0xFF) && (prefix_byte(pInput, 1) == 0xFE)) || ((prefix_byte(pInput, 0) == 0xFE) && (prefix_byte(pInput, 1) == 0xFF))) {
            return 0;
        }
    }

    for (i = 0; i < nSize; i++) {
        unsigned char nByte = prefix_byte(pInput, i);

        if (nByte == 0) {
            return 0;
        } else if (nByte < 0x09) {
            nControl++;
        } else if ((nByte == 0x09) || (nByte == 0x0A) || (nByte == 0x0D)) {
            nPrintable++;
        } else if ((nByte >= 0x20) && (nByte <= 0x7E)) {
            nPrintable++;
        } else if (nByte >= 0x80) {
            nExtended++;
        } else {
            /* 0x0B, 0x0C and 0x0E..0x1F. */
            nControl++;
        }
    }

    {
        double nTotal = (double)nSize;
        double nPrintableRatio = (double)(nPrintable + nExtended) / nTotal;
        double nControlRatio = (double)nControl / nTotal;
        double nExtendedRatio = (double)nExtended / nTotal;

        return ((nPrintableRatio >= 0.85) && (nControlRatio <= 0.05) && (nExtendedRatio <= 0.50)) ? 1 : 0;
    }
}

/* XBinary::isUTF8TextType over the first 0x2000 bytes: the whole sample has to
 * decode as UTF-8, and without a byte order mark it also needs a few
 * multi-byte sequences and a mostly printable body.                        */
static int is_utf8_text_data(PrefixInput *pInput, cd_i64 nSize)
{
    cd_i64 i = 0;
    cd_i64 nStart = 0;
    cd_i64 nValid = 0;
    cd_i64 nMultiByte = 0;
    cd_i64 nPrintable = 0;
    int bHasBOM = 0;
    if ((nSize >= 3) && (prefix_byte(pInput, 0) == 0xEF) && (prefix_byte(pInput, 1) == 0xBB) && (prefix_byte(pInput, 2) == 0xBF)) {
        bHasBOM = 1;
        nStart = 3;
    }

    for (i = nStart; i < nSize;) {
        unsigned char nByte = prefix_byte(pInput, i);

        if (nByte == 0) {
            return 0;
        } else if (nByte < 0x80) {
            if ((nByte >= 0x20) || (nByte == 0x09) || (nByte == 0x0A) || (nByte == 0x0D)) {
                nPrintable++;
            }

            nValid++;
            i++;
        } else if ((nByte & 0xE0) == 0xC0) {
            if (((i + 1) >= nSize) || ((prefix_byte(pInput, i + 1) & 0xC0) != 0x80) || (nByte < 0xC2)) {
                return 0;
            }

            nMultiByte++;
            nValid++;
            i += 2;
        } else if ((nByte & 0xF0) == 0xE0) {
            if (((i + 2) >= nSize) || ((prefix_byte(pInput, i + 1) & 0xC0) != 0x80) || ((prefix_byte(pInput, i + 2) & 0xC0) != 0x80)) {
                return 0;
            }

            if ((nByte == 0xE0) && (prefix_byte(pInput, i + 1) < 0xA0)) {
                return 0;
            }

            nMultiByte++;
            nValid++;
            i += 3;
        } else if ((nByte & 0xF8) == 0xF0) {
            if (((i + 3) >= nSize) || ((prefix_byte(pInput, i + 1) & 0xC0) != 0x80) || ((prefix_byte(pInput, i + 2) & 0xC0) != 0x80) ||
                ((prefix_byte(pInput, i + 3) & 0xC0) != 0x80)) {
                return 0;
            }

            if ((nByte == 0xF0) && (prefix_byte(pInput, i + 1) < 0x90)) {
                return 0;
            }

            if ((nByte > 0xF4) || ((nByte == 0xF4) && (prefix_byte(pInput, i + 1) > 0x8F))) {
                return 0;
            }

            nMultiByte++;
            nValid++;
            i += 4;
        } else {
            return 0;
        }
    }

    if (bHasBOM) {
        return (nValid > 0) ? 1 : 0;
    }

    if (nValid > 0) {
        double nPrintableRatio = (double)nPrintable / (double)nValid;
        double nMultiByteRatio = (double)nMultiByte / (double)nValid;

        return ((nMultiByteRatio > 0.05) && (nPrintableRatio >= 0.70)) ? 1 : 0;
    }

    return 0;
}

/* XBinary::getUnicodeType over the first 0x1000 bytes: the byte order mark
 * first, then the alternating-NUL heuristic over a 512 byte sample.
 * 0 is UNICODE_TYPE_NONE, 1 is _LE and 2 is _BE.                           */
static int unicode_type_data(PrefixInput *pInput, cd_i64 nSize)
{
    cd_i64 nSample = 0;
    cd_i64 i = 0;
    cd_i64 nNull = 0;
    cd_i64 nEvenNulls = 0;
    cd_i64 nOddNulls = 0;
    cd_i64 nPrintable = 0;
    if (nSize >= 2) {
        cd_u16 nSymbol = (cd_u16)(prefix_byte(pInput, 0) | ((cd_u16)prefix_byte(pInput, 1) << 8));

        if (nSymbol == 0xFFFE) {
            return 2;
        } else if (nSymbol == 0xFEFF) {
            return 1;
        }
    }

    if (nSize < 4) {
        return 0;
    }

    nSample = (nSize < 512) ? nSize : 512;

    for (i = 0; i < nSample; i++) {
        unsigned char nByte = prefix_byte(pInput, i);

        if (nByte == 0) {
            nNull++;

            if ((i % 2) == 0) {
                nEvenNulls++;
            } else {
                nOddNulls++;
            }
        } else if ((nByte >= 0x20) && (nByte <= 0x7E)) {
            nPrintable++;
        }
    }

    if ((nNull > 0) && (nSample > 4)) {
        double nNullRatio = (double)nNull / (double)nSample;
        double nPrintableRatio = (double)nPrintable / (double)nSample;

        if ((nNullRatio >= 0.30) && (nPrintableRatio >= 0.30)) {
            cd_i64 nLEPairs = 0;
            cd_i64 nBEPairs = 0;

            if (nEvenNulls > (nOddNulls * 2)) {
                return 1;
            } else if (nOddNulls > (nEvenNulls * 2)) {
                return 2;
            }

            for (i = 0; i < (nSample - 1); i += 2) {
                if ((prefix_byte(pInput, i + 1) == 0) && (prefix_byte(pInput, i) >= 0x20) && (prefix_byte(pInput, i) <= 0x7E)) {
                    nLEPairs++;
                }

                if ((prefix_byte(pInput, i) == 0) && (prefix_byte(pInput, i + 1) >= 0x20) && (prefix_byte(pInput, i + 1) <= 0x7E)) {
                    nBEPairs++;
                }
            }

            if (nLEPairs > nBEPairs) {
                return 1;
            } else if (nBEPairs > nLEPairs) {
                return 2;
            }
        }
    }

    return 0;
}

/* Preserve both the classifier's original sample and its exact-read error
 * behaviour. Early BOM/rejection decisions still validate the unvisited
 * sample suffix, without allocating a whole-prefix copy. */
static int classify_file_prefix(xx_io_device *pFile, cd_i64 nMaximum, int (*classify)(PrefixInput *, cd_i64))
{
    PrefixInput input;
    int result;

    if (!pFile || xx_io_total_size(pFile) <= 0) return 0;
    x_memset(&input, 0, sizeof(input));
    input.file = pFile;
    input.bytes = xx_io_buffer_whole(pFile, 0);
    input.size = xx_io_total_size(pFile) < nMaximum ? xx_io_total_size(pFile) : nMaximum;
    input.capacity = xx_io_buffer_capacity(pFile);
    if (input.bytes) input.verified = input.size;
    result = classify(&input, input.size);
    while (!input.failed && input.verified < input.size) {
        if (!prefix_window(&input, input.verified)) break;
    }
    return input.failed ? 0 : result;
}

static int is_plain_text(xx_io_device *pFile)
{
    return classify_file_prefix(pFile, 0x8000, is_plain_text_data);
}

static int is_utf8_text(xx_io_device *pFile)
{
    return classify_file_prefix(pFile, 0x2000, is_utf8_text_data);
}

static int unicode_type(xx_io_device *pFile)
{
    return classify_file_prefix(pFile, 0x1000, unicode_type_data);
}

/* --------------------------------------------------- operation system  ---
 *
 * Reproduces xx_pe_inspection::getOperatingSystemVersionsS and the IMAGE_FILE_HEADER
 * machine table, which together make up the "Operation system" line that
 * db/PE/_PE.0.sg prints under --verbose.
 */

typedef struct {
    unsigned int nId;
    const char *pName;
} XIdName;

static const XIdName g_windowsVersions[] = {
    {0x0003000A, "NT 3.1"}, {0x00030032, "NT 3.5"},      {0x00030033, "NT 3.51"}, {0x00040000, "95"}, {0x00040001, "98"}, {0x00040009, "Millenium"}, {0x00050000, "2000"},
    {0x00050001, "XP"},     {0x00050002, "Server 2003"}, {0x00060000, "Vista"},   {0x00060001, "7"},  {0x00060002, "8"},  {0x00060003, "8.1"},       {0x000A0000, "10"}};

static const XIdName g_peMachines[] = {
    {0x0000, "UNKNOWN"},   {0x014c, "I386"},      {0x014d, "I486"},    {0x014e, "PENTIUM"}, {0x0160, "R3000_BE"},      {0x0162, "R3000"},       {0x0166, "R4000"},
    {0x0168, "R10000"},    {0x0169, "WCEMIPSV2"}, {0x0184, "ALPHA"},   {0x01F0, "POWERPC"}, {0x01a2, "SH3"},           {0x01a3, "SH3DSP"},      {0x01a4, "SH3E"},
    {0x01a6, "SH4"},       {0x01a8, "SH5"},       {0x01c0, "ARM"},     {0x01c2, "THUMB"},   {0x01c4, "ARMNT"},         {0x01d3, "AM33"},        {0x01f1, "POWERPCFP"},
    {0x01f2, "POWERPCBE"}, {0x0200, "IA64"},      {0x0266, "MIPS16"},  {0x0284, "ALPHA64"}, {0x0366, "MIPSFPU"},       {0x0466, "MIPSFPU16"},   {0x0520, "TRICORE"},
    {0x0CEF, "CEF"},       {0x0EBC, "EBC"},       {0x5032, "RISCV32"}, {0x5064, "RISCV64"}, {0x5128, "RISCV128"},      {0x6232, "LOONGARCH32"}, {0x6264, "LOONGARCH64"},
    {0x8664, "AMD64"},     {0x9041, "M32R"},      {0xAA64, "ARM64"},   {0xC0EE, "CEE"},     {0xfd1d, "AMD64_LINUX_NI"}};

static const char *windows_version_name(unsigned int nVersion)
{
    size_t i = 0;

    if (nVersion == 0) {
        return NULL;
    }

    for (i = 0; i < sizeof(g_windowsVersions) / sizeof(g_windowsVersions[0]); i++) {
        if (g_windowsVersions[i].nId == nVersion) {
            return g_windowsVersions[i].pName;
        }
    }

    return NULL;
}

static const char *pe_machine_name(unsigned int nMachine)
{
    size_t i = 0;

    /* xx_pe_inspection::_getMachine folds the Linux native-OS override onto AMD64. */
    if (nMachine == (0xFD1D ^ 0x8664)) {
        nMachine = 0x8664;
    }

    for (i = 0; i < sizeof(g_peMachines) / sizeof(g_peMachines[0]); i++) {
        if (g_peMachines[i].nId == nMachine) {
            return g_peMachines[i].pName;
        }
    }

    return "Unknown";
}

static void javaclass_get_version(xx_io_device *pFile, char *sBuf, size_t nBufSize)
{
    cd_u16 nMinor, nMajor;
    sBuf[0] = '\0';
    if (!pFile) return;
    nMinor = xx_io_get_u16(pFile, 4, true);
    nMajor = xx_io_get_u16(pFile, 6, true);
    if (nMajor == 45) {
        x_snprintf(sBuf, nBufSize, "JDK 1.1");
    } else if (nMajor == 46) {
        x_snprintf(sBuf, nBufSize, "JDK 1.2");
    } else if (nMajor == 47) {
        x_snprintf(sBuf, nBufSize, "JDK 1.3");
    } else if (nMajor == 48) {
        x_snprintf(sBuf, nBufSize, "JDK 1.4");
    } else if (nMajor == 49) {
        x_snprintf(sBuf, nBufSize, "Java SE 5.0");
    } else if (nMajor >= 50 && nMajor <= 0x4A) {
        x_snprintf(sBuf, nBufSize, "Java SE %u", (unsigned int)(nMajor - 44));
    }
    if (sBuf[0] != '\0' && nMinor != 0) {
        size_t len = x_strlen(sBuf);
        x_snprintf(sBuf + len, nBufSize - len, ".%u", (unsigned int)nMinor);
    }
}

enum {
    DIE_HM_STORE = 1,
    DIE_HM_DEFLATE = 3,
    DIE_HM_DEFLATE64 = 4,
    DIE_HM_BZIP2 = 5,
    DIE_HM_LZMA = 6,
    DIE_HM_XZ = 8,
    DIE_HM_PPMD8 = 12,
    DIE_HM_IMPLODED_4K_2 = 20,
    DIE_HM_IMPLODED_4K_3 = 21,
    DIE_HM_IMPLODED_8K_2 = 22,
    DIE_HM_IMPLODED_8K_3 = 23,
    DIE_HM_SHRINK = 24,
    DIE_HM_REDUCE_1 = 25,
    DIE_HM_REDUCE_2 = 26,
    DIE_HM_REDUCE_3 = 27,
    DIE_HM_REDUCE_4 = 28,
    DIE_HM_ZIP_AES = 30,
    DIE_HM_ZSTD = 47,
    DIE_HM_WINZIP_JPEG = 85,
    DIE_HM_WAVPACK = 86
};

static int zip_to_handle_method(cd_u16 method, cd_u16 flags)
{
    switch (method) {
        case 0: return DIE_HM_STORE;
        case 1: return DIE_HM_SHRINK;
        case 2: return DIE_HM_REDUCE_1;
        case 3: return DIE_HM_REDUCE_2;
        case 4: return DIE_HM_REDUCE_3;
        case 5: return DIE_HM_REDUCE_4;
        case 6: {
            int b8k = (flags & 0x02) ? 1 : 0;
            int b3tree = (flags & 0x04) ? 1 : 0;
            if (!b8k && !b3tree) return DIE_HM_IMPLODED_4K_2;
            if (!b8k && b3tree) return DIE_HM_IMPLODED_4K_3;
            if (b8k && !b3tree) return DIE_HM_IMPLODED_8K_2;
            return DIE_HM_IMPLODED_8K_3;
        }
        case 8: return DIE_HM_DEFLATE;
        case 9: return DIE_HM_DEFLATE64;
        case 12: return DIE_HM_BZIP2;
        case 14: return DIE_HM_LZMA;
        case 93: return DIE_HM_ZSTD;
        case 95: return DIE_HM_XZ;
        case 96: return DIE_HM_WINZIP_JPEG;
        case 97: return DIE_HM_WAVPACK;
        case 98: return DIE_HM_PPMD8;
        case 99: return DIE_HM_ZIP_AES;
        default: return 0;
    }
}

static const char *handle_method_to_string(int hm)
{
    switch (hm) {
        case DIE_HM_STORE: return "Store";
        case DIE_HM_DEFLATE: return "Deflate";
        case DIE_HM_DEFLATE64: return "Deflate64";
        case DIE_HM_BZIP2: return "Bzip2";
        case DIE_HM_LZMA: return "LZMA";
        case DIE_HM_XZ: return "XZ";
        case DIE_HM_PPMD8: return "PPMD8";
        case DIE_HM_IMPLODED_4K_2: return "Imploded 4K/2";
        case DIE_HM_IMPLODED_4K_3: return "Imploded 4K/3";
        case DIE_HM_IMPLODED_8K_2: return "Imploded 8K/2";
        case DIE_HM_IMPLODED_8K_3: return "Imploded 8K/3";
        case DIE_HM_SHRINK: return "Shrink";
        case DIE_HM_REDUCE_1: return "Reduce 1";
        case DIE_HM_REDUCE_2: return "Reduce 2";
        case DIE_HM_REDUCE_3: return "Reduce 3";
        case DIE_HM_REDUCE_4: return "Reduce 4";
        case DIE_HM_ZIP_AES: return "ZIP AES";
        case DIE_HM_ZSTD: return "Zstandard";
        case DIE_HM_WINZIP_JPEG: return "WinZip JPEG";
        case DIE_HM_WAVPACK: return "WavPack";
        default: return NULL;
    }
}

static bool zip_get_format_version(DieEngine *pEngine, char *sBuf, size_t nBufSize)
{
    cd_i64 cd_offset = -1;
    cd_u16 nVersion = 0;

    if (!pEngine || !pEngine->device || !sBuf || nBufSize < 16) return false;
    sBuf[0] = '\0';

    if (pEngine->bHasZip && pEngine->zip.cd_offset >= 0) {
        cd_offset = pEngine->zip.cd_offset;
    } else if (pEngine->bHasZip && pEngine->zip.eocd_offset >= 0) {
        cd_offset = (cd_i64)xx_io_get_u32(pEngine->device, pEngine->zip.eocd_offset + 16, false);
    }

    if (cd_offset >= 0) {
        cd_u32 nSignature = xx_io_get_u32(pEngine->device, cd_offset, false);
        if (nSignature == 0x02014b50) {
            nVersion = xx_io_get_u8(pEngine->device, cd_offset + 4);
            if (nVersion == 0) {
                nVersion = xx_io_get_u8(pEngine->device, cd_offset + 6);
            }
        }
    }

    if (nVersion == 0) {
        cd_u32 nSignature = xx_io_get_u32(pEngine->device, 0, false);
        if (nSignature == 0x04034b50) {
            nVersion = xx_io_get_u8(pEngine->device, 4);
        }
    }

    if (nVersion != 0) {
        x_snprintf(sBuf, nBufSize, "%u.%u", (unsigned int)(nVersion / 10), (unsigned int)(nVersion % 10));
        return true;
    }

    return false;
}

static bool zip_get_format_options(DieEngine *pEngine, char *sBuf, size_t nBufSize)
{
    cd_i64 cd_offset = -1;
    int methods[20];
    int method_count = 0;

    if (!pEngine || !pEngine->device || !sBuf || nBufSize < 16) return false;
    sBuf[0] = '\0';

    if (pEngine->bHasZip && pEngine->zip.cd_offset >= 0) {
        cd_offset = pEngine->zip.cd_offset;
    } else if (pEngine->bHasZip && pEngine->zip.eocd_offset >= 0) {
        cd_offset = (cd_i64)xx_io_get_u32(pEngine->device, pEngine->zip.eocd_offset + 16, false);
    }

    if (cd_offset >= 0) {
        cd_i64 offset = cd_offset;
        for (int i = 0; i < 20; i++) {
            cd_u32 nSignature = xx_io_get_u32(pEngine->device, offset, false);
            if (nSignature != 0x02014b50) break;

            cd_u16 nFlags = xx_io_get_u16(pEngine->device, offset + 8, false);
            cd_u16 nMethod = xx_io_get_u16(pEngine->device, offset + 10, false);
            cd_u32 nUncompressedSize = xx_io_get_u32(pEngine->device, offset + 24, false);

            if (nUncompressedSize > 0) {
                int hm = zip_to_handle_method(nMethod, nFlags);
                if (hm > 0) {
                    int found = 0;
                    for (int j = 0; j < method_count; j++) {
                        if (methods[j] == hm) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found && method_count < 20) {
                        methods[method_count++] = hm;
                    }
                }
            }

            cd_u16 nFileNameLength = xx_io_get_u16(pEngine->device, offset + 28, false);
            cd_u16 nExtraFieldLength = xx_io_get_u16(pEngine->device, offset + 30, false);
            cd_u16 nFileCommentLength = xx_io_get_u16(pEngine->device, offset + 32, false);

            offset += 46 + (cd_i64)nFileNameLength + (cd_i64)nExtraFieldLength + (cd_i64)nFileCommentLength;
        }
    }

    if (method_count == 0) return true;

    for (int i = 0; i < method_count - 1; i++) {
        for (int j = i + 1; j < method_count; j++) {
            if (methods[i] > methods[j]) {
                int tmp = methods[i];
                methods[i] = methods[j];
                methods[j] = tmp;
            }
        }
    }

    size_t pos = 0;
    for (int i = 0; i < method_count; i++) {
        const char *name = handle_method_to_string(methods[i]);
        if (!name) continue;
        if (pos > 0) {
            if (pos + 2 < nBufSize) {
                sBuf[pos++] = ',';
                sBuf[pos++] = ' ';
            }
        }
        size_t nlen = x_strlen(name);
        if (pos + nlen < nBufSize) {
            x_memcpy(sBuf + pos, name, nlen);
            pos += nlen;
        }
    }
    sBuf[pos] = '\0';
    return true;
}


static bool check_string_number(const char *str, unsigned int min_val, unsigned int max_val)
{
    unsigned long long val;
    const char *endptr;
    if (!str || !*str) return false;
    val = xx_rt_strtoull(str, &endptr, 10);
    if (endptr == str || *endptr != '\0') return false;
    return (val >= min_val) && (val <= max_val);
}

static bool check_string_number_section0(const char *str, unsigned int min_val, unsigned int max_val)
{
    char part[32];
    size_t i = 0;
    if (!str || !*str) return false;
    while (str[i] && str[i] != '.' && i + 1 < sizeof(part)) {
        part[i] = str[i];
        i++;
    }
    part[i] = '\0';
    return check_string_number(part, min_val, max_val);
}

static const char *apk_android_version_from_api(unsigned int api)
{
    switch (api) {
        case 1: return "1.0";
        case 2: return "1.1";
        case 3: return "1.5";
        case 4: return "1.6";
        case 5: return "2.0";
        case 6: return "2.0.1";
        case 7: return "2.1";
        case 8: return "2.2.X";
        case 9: return "2.3-2.3.2";
        case 10: return "2.3.3-2.3.7";
        case 11: return "3.0";
        case 12: return "3.1";
        case 13: return "3.2.X";
        case 14: return "4.0.1-4.0.2";
        case 15: return "4.0.3-4.0.4";
        case 16: return "4.1.X";
        case 17: return "4.2.X";
        case 18: return "4.3.X";
        case 19: return "4.4-4.4.4";
        case 20: return "4.4W";
        case 21: return "5.0";
        case 22: return "5.1";
        case 23: return "6.0";
        case 24: return "7.0";
        case 25: return "7.1";
        case 26: return "8.0";
        case 27: return "8.1";
        case 28: return "9.0";
        case 29: return "10.0";
        case 30: return "11.0";
        case 31: return "12.0";
        case 32: return "12.1";
        case 33: return "13.0";
        case 34: return "14.0";
        case 35: return "15.0";
        case 36: return "16.0";
        default: return "Unknown";
    }
}

static const char *apk_get_os_version(xx_apk *apk, char *buf, size_t buf_size)
{
    char *sCompileSdkVersion = NULL;
    char *sCompileSdkVersionCodename = NULL;
    char *sPlatformBuildVersionCode = NULL;
    char *sPlatformBuildVersionName = NULL;
    char *sTargetSdkVersion = NULL;
    char *sMinSdkVersion = NULL;
    const char *sVersion = "";
    const char *sAndroidVersion = "";

    buf[0] = '\0';
    if (!apk) return "";
    if (!apk->manifest_text) {
        xx_apk_analyze(apk, NULL);
    }
    if (!apk->manifest_text) return "";

    sCompileSdkVersion = xx_apk_manifest_record(apk, "android:compileSdkVersion");
    sCompileSdkVersionCodename = xx_apk_manifest_record(apk, "android:compileSdkVersionCodename");
    sPlatformBuildVersionCode = xx_apk_manifest_record(apk, "platformBuildVersionCode");
    sPlatformBuildVersionName = xx_apk_manifest_record(apk, "platformBuildVersionName");
    sTargetSdkVersion = xx_apk_manifest_record(apk, "android:targetSdkVersion");
    sMinSdkVersion = xx_apk_manifest_record(apk, "android:minSdkVersion");

    if (!check_string_number(sCompileSdkVersion, 1, 40)) {
        if (sCompileSdkVersion) sCompileSdkVersion[0] = '\0';
    }
    if (!check_string_number(sPlatformBuildVersionCode, 1, 40)) {
        if (sPlatformBuildVersionCode) sPlatformBuildVersionCode[0] = '\0';
    }
    if (!check_string_number(sTargetSdkVersion, 1, 40)) {
        if (sTargetSdkVersion) sTargetSdkVersion[0] = '\0';
    }
    if (!check_string_number(sMinSdkVersion, 1, 40)) {
        if (sMinSdkVersion) sMinSdkVersion[0] = '\0';
    }
    if (!check_string_number_section0(sCompileSdkVersionCodename, 1, 15)) {
        if (sCompileSdkVersionCodename) sCompileSdkVersionCodename[0] = '\0';
    }
    if (!check_string_number_section0(sPlatformBuildVersionName, 1, 15)) {
        if (sPlatformBuildVersionName) sPlatformBuildVersionName[0] = '\0';
    }

    if ((sCompileSdkVersion && sCompileSdkVersion[0]) ||
        (sCompileSdkVersionCodename && sCompileSdkVersionCodename[0]) ||
        (sPlatformBuildVersionCode && sPlatformBuildVersionCode[0]) ||
        (sPlatformBuildVersionName && sPlatformBuildVersionName[0]) ||
        (sTargetSdkVersion && sTargetSdkVersion[0]) ||
        (sMinSdkVersion && sMinSdkVersion[0])) {

        if (sTargetSdkVersion && sTargetSdkVersion[0]) sVersion = sTargetSdkVersion;
        else if (sMinSdkVersion && sMinSdkVersion[0]) sVersion = sMinSdkVersion;
        else if (sCompileSdkVersion && sCompileSdkVersion[0]) sVersion = sCompileSdkVersion;
        else if (sPlatformBuildVersionCode && sPlatformBuildVersionCode[0]) sVersion = sPlatformBuildVersionCode;

        if (sCompileSdkVersionCodename && sCompileSdkVersionCodename[0]) sAndroidVersion = sCompileSdkVersionCodename;
        else if (sPlatformBuildVersionName && sPlatformBuildVersionName[0]) sAndroidVersion = sPlatformBuildVersionName;

        if ((!sAndroidVersion || !sAndroidVersion[0]) && sVersion && sVersion[0]) {
            unsigned int api = (unsigned int)xx_rt_strtoull(sVersion, NULL, 10);
            sAndroidVersion = apk_android_version_from_api(api);
        }

        if (sAndroidVersion && sAndroidVersion[0]) {
            xx_rt_strncpy(buf, sAndroidVersion, buf_size - 1);
            buf[buf_size - 1] = '\0';
        }
    }

    xx_mem_free(sCompileSdkVersion);
    xx_mem_free(sCompileSdkVersionCodename);
    xx_mem_free(sPlatformBuildVersionCode);
    xx_mem_free(sPlatformBuildVersionName);
    xx_mem_free(sTargetSdkVersion);
    xx_mem_free(sMinSdkVersion);

    return buf;
}

bool die_engine_api_dispatch_binary(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    xx_io_device *pFile = pEngine->device;
    xx_pe_inspection *pPE = &pEngine->pe;
    xx_memory_map *pMap = pEngine->pMap;
    xx_memory_map_lookup_t addressLookup = (pEngine->bHasPE || pMap->file_type == XX_FILE_TYPE_NE) ? XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL
                                                                                         : XX_MEMORY_MAP_LOOKUP_FIRST_MATCH;
    /* NE's script base is zero even though its segmented map starts at
     * 0x10000. XNE's module address and Binary_Script's RVA base differ.
     * The same holds for the DOS script map: XMSDOS's module address is
     * 0x10000000, Binary_Script's RVA base (XBinary::getBaseAddress) is 0. */
    cd_u64 nRvaBase = (pMap->file_type == XX_FILE_TYPE_NE || has_legacy_script_map(pMap)) ? 0 : pMap->module_address;

    switch (id) {
        case A_getSize:
            *pResult = js_num((double)xx_io_total_size(pFile));
            return true;

        case A_compare: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int bResult = die_engine_signature_compare(pEngine, nOffset, pSignature, 1, 0);

            cd_free(pSignature);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_compareEP: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_i64 nEntryPoint = script_entry_point_offset(pEngine);
            int bResult = 0;

            if (nEntryPoint != -1) {
                bResult = die_engine_signature_compare(pEngine, nEntryPoint + nOffset, pSignature, 0, nEntryPoint);
            }

            cd_free(pSignature);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_compareOverlay: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int bResult = 0;

            if (pEngine->bHasPE && (pPE->nOverlaySize > 0)) {
                bResult = die_engine_signature_compare(pEngine, pPE->nOverlayOffset + nOffset, pSignature, 2, pPE->nOverlayOffset);
            } else if (has_legacy_script_map(pMap)) {
                cd_i64 nOverlay = legacy_overlay_offset(pMap);
                if (nOverlay >= 0 && nOffset <= INT64_MAX - nOverlay) {
                    bResult = die_engine_signature_compare(pEngine, nOverlay + nOffset, pSignature, 2, nOverlay);
                }
            }

            cd_free(pSignature);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_readByte:
        case A_read_uint8:
            *pResult = js_num((double)xx_io_get_u8(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0)));
            return true;

        case A_readSByte:
        case A_read_int8:
            *pResult = js_num((double)xx_io_get_i8(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0)));
            return true;

        case A_readWord:
            *pResult = js_num((double)xx_io_get_u16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), false));
            return true;
        case A_readSWord:
            *pResult = js_num((double)xx_io_get_i16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), false));
            return true;
        case A_readDword:
            *pResult = js_num((double)xx_io_get_u32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), false));
            return true;
        case A_readSDword:
            *pResult = js_num((double)xx_io_get_i32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), false));
            return true;
        case A_readQword:
            *pResult = js_num((double)xx_io_get_u64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), false));
            return true;
        case A_readSQword:
            *pResult = js_num((double)xx_io_get_i64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), false));
            return true;

        case A_read_uint16:
            *pResult = js_num((double)xx_io_get_u16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_int16:
            *pResult = js_num((double)xx_io_get_i16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_uint24:
            *pResult = js_num((double)xx_io_get_u24(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_int24: {
            cd_u32 nValue = xx_io_get_u24(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false);

            if (nValue & 0x800000u) {
                *pResult = js_num((double)(cd_i32)(nValue | 0xFF000000u));
            } else {
                *pResult = js_num((double)nValue);
            }
            return true;
        }
        case A_read_uint32:
            *pResult = js_num((double)xx_io_get_u32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_int32:
            *pResult = js_num((double)xx_io_get_i32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_uint64:
            *pResult = js_num((double)xx_io_get_u64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_int64:
            *pResult = js_num((double)xx_io_get_i64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_float:
            *pResult = js_num((double)xx_io_get_f32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_double:
            *pResult = js_num(xx_io_get_f64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_float16:
            *pResult = js_num((double)xx_io_get_f16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0) ? true : false));
            return true;
        case A_read_UUID:
            *pResult = str_take_native(pCtx, xx_io_get_uuid(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0)));
            return true;
        case A_read_ucsdString:
            *pResult = str_take_native(pCtx, xx_str_read_ucsd(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0)));
            return true;

        case A_isNE:
            *pResult = js_bool(xx_io_get_u16(pFile, (cd_i64)xx_io_get_u32(pFile, 0x3C, false), false) == 0x454E);
            return true;
        case A_isLE:
            *pResult = js_bool(xx_io_get_u16(pFile, (cd_i64)xx_io_get_u32(pFile, 0x3C, false), false) == 0x454C);
            return true;
        case A_isLX:
            *pResult = js_bool(xx_io_get_u16(pFile, (cd_i64)xx_io_get_u32(pFile, 0x3C, false), false) == 0x584C);
            return true;

        case A_getApplicationIdentifier:
            *pResult = native_str_take(pCtx, native_iso_identifier(pFile, 574, 128));
            return true;
        case A_getDataPreparerIdentifier:
            *pResult = native_str_take(pCtx, native_iso_identifier(pFile, 446, 128));
            return true;

        case A_isArchiveRecordPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasZip ? xx_zip_record_present(&pEngine->zip, pName) : 0;

            cd_free(pName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isArchiveRecordPresentExp: {
            char *pPattern = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = xx_regex_die_archive_present(pEngine, pPattern);

            cd_free(pPattern);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getManifestRecord: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = native_str_take(pCtx, pEngine->bHasZip ? xx_zip_manifest_record(&pEngine->zip, pKey) : xx_str_create(""));

            cd_free(pKey);

            *pResult = result;
            return true;
        }

        case A_getPackageJsonRecord: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = native_str_take(pCtx, pEngine->bHasZip ? xx_zip_packagejson_record(&pEngine->zip, pKey) : xx_str_create(""));

            cd_free(pKey);

            *pResult = result;
            return true;
        }

        case A_isConstPresent: {
            char *pValue = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasPyc ? xx_pyc_const_present(&pEngine->pyc, pValue) : 0;

            xx_str_free(pValue);

            *pResult = js_bool(bResult);
            return true;
        }

        /* Util: exact 64-bit arithmetic (JS numbers are doubles). Mirrors
         * Util_script; a zero divisor yields -1 as in the reference, and the
         * shifts take the low 6 bits of the count as x86 does. */
        case A_div64: {
            cd_i64 nA = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nB = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_i64 nResult = (cd_i64)-1;

            if (nB != 0) {
                /* Avoid the INT64_MIN / -1 overflow (a hardware trap on x86);
                 * the database never divides those, so this only guards a
                 * crafted call. */
                if ((nA == (-0x7FFFFFFFFFFFFFFFll - 1)) && (nB == (cd_i64)-1)) {
                    nResult = nA;
                } else {
                    nResult = nA / nB;
                }
            }

            *pResult = js_num((double)nResult);
            return true;
        }
        case A_divu64: {
            cd_u64 nA = arg_u64(pCtx, nArgc, pArgv, 0);
            cd_u64 nB = arg_u64(pCtx, nArgc, pArgv, 1);

            *pResult = js_num((double)(nB ? (nA / nB) : (cd_u64)0xFFFFFFFFFFFFFFFFULL));
            return true;
        }
        case A_shlu64: {
            cd_u64 nValue = arg_u64(pCtx, nArgc, pArgv, 0);
            cd_u64 nShift = arg_u64(pCtx, nArgc, pArgv, 1);

            *pResult = js_num((double)(nValue << (nShift & 63)));
            return true;
        }
        case A_shru64: {
            cd_u64 nValue = arg_u64(pCtx, nArgc, pArgv, 0);
            cd_u64 nShift = arg_u64(pCtx, nArgc, pArgv, 1);

            *pResult = js_num((double)(nValue >> (nShift & 63)));
            return true;
        }
        case A_shl64: {
            cd_i64 nValue = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nShift = arg_i64(pCtx, nArgc, pArgv, 1, 0);

            /* Shifting into the sign bit is undefined for a signed left
             * operand; the unsigned shift has the two's-complement result
             * the reference gets out of qint64 << qint64.                 */
            *pResult = js_num((double)(cd_i64)((cd_u64)nValue << (nShift & 63)));
            return true;
        }
        case A_shr64: {
            cd_i64 nValue = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nShift = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int nCount = (int)(nShift & 63);

            /* qint64 >> n is an arithmetic shift; spelled out through the
             * complement so it does not rest on implementation-defined
             * behaviour for a negative left operand.                      */
            if (nValue < 0) {
                *pResult = js_num((double)(cd_i64) ~((cd_u64)(~nValue) >> nCount));
            } else {
                *pResult = js_num((double)(nValue >> nCount));
            }
            return true;
        }
        case A_secondsToTimeStr: {
            /* QTime(0, 0).addSecs(n).toString(): addSecs reduces the count
             * modulo a day, addMSecs folds a negative result back into the
             * day, and the default format is HH:mm:ss.                    */
            cd_i32 nValue = (cd_i32)((nArgc > 0) ? js_to_int32(pCtx, pArgv[0]) : 0);
            cd_i32 nSeconds = nValue % 86400;
            char sBuf[32];

            if (nSeconds < 0) {
                nSeconds += 86400;
            }

            x_snprintf(sBuf, sizeof(sBuf), "%02d:%02d:%02d", (int)(nSeconds / 3600), (int)((nSeconds / 60) % 60), (int)(nSeconds % 60));

            *pResult = js_str(pCtx, sBuf);
            return true;
        }

        case A_getString:
        case A_read_ansiString:
            *pResult = str_take_native(pCtx, xx_str_read_ansi(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 50)));
            return true;

        case A_isJavaScriptMinified: {
            cd_i64 nStart = die_engine_profile_start(pEngine);
            bool bResult = xx_javascript_is_minified(pFile, 0, xx_io_total_size(pFile), pEngine->pd);
            die_engine_profile_end(pEngine, nStart, "isJavaScriptMinified");
            *pResult = js_bool(bResult);
            return true;
        }

        case A_read_unicodeString:
            *pResult = str_take_native(pCtx, xx_str_read_unicode(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 50), 0));
            return true;

        case A_read_utf8String:
            *pResult = str_take_native(pCtx, xx_str_read_utf8(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 50)));
            return true;

        case A_read_codePageString:
            *pResult = str_take_native(pCtx, xx_str_read_ansi(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 256)));
            return true;

        case A_findSignature: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 2);
            cd_i64 nResult = 0;
            cd_i64 nStart = die_engine_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = die_engine_signature_find(pEngine, nOffset, nSize, pSignature);

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                /* The reference spells this one with an underscore. */
                die_engine_profile_end(pEngine, nStart, "find_signature[%s]: %s %s", pSignature, sOffset, sSize);
            }

            cd_free(pSignature);

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_findString:
        case A_find_ansiString: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pString = arg_string(pCtx, nArgc, pArgv, 2);
            xx_data_sig_record record = {0};
            xx_data_signature signature = {&record, 1};
            cd_i64 nResult = -1;
            /* Only findString is measured; find_ansiString is the same search
             * under a second name and the reference does not profile it. */
            cd_i64 nStart = (id == A_findString) ? die_engine_profile_start(pEngine) : -1;

            fix_offset_size(pEngine, &nOffset, &nSize);
            record.kind = XX_DATA_SIG_BYTES;
            record.data = (const unsigned char *)pString;
            record.data_size = (cd_i64)x_strlen(pString);
            if (!pEngine->bStop) {
                nResult = xx_io_signature_find(pFile, nOffset, nSize, &signature, &pEngine->sigContext, pEngine->pd);
            }

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                die_engine_profile_end(pEngine, nStart, "findString[%s]: %s %s", pString, sOffset, sSize);
            }

            cd_free(pString);

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_find_unicodeString: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pString = arg_string(pCtx, nArgc, pArgv, 2);
            cd_i64 nResult = 0;

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xx_io_find_unicode_bytes_in_range(pFile, nOffset, nSize, pString, false, NULL);
            cd_free(pString);

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_find_utf8String: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pString = arg_string(pCtx, nArgc, pArgv, 2);
            cd_i64 nResult = 0;

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xx_io_find_ansi_string_in_range(pFile, nOffset, nSize, pString, NULL);
            cd_free(pString);

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_findByte: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_u8 nValue = (cd_u8)arg_i64(pCtx, nArgc, pArgv, 2, 0);
            cd_i64 nResult = 0;
            cd_i64 nStart = die_engine_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xx_io_find_u8_in_range(pFile, nOffset, nSize, nValue, NULL);

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                /* XBinary::valueToHex of a fixed-width integer: always the
                 * full field, unlike valueToHexEx above. */
                die_engine_profile_end(pEngine, nStart, "findByte[%0*llx]: %s %s", 2, (unsigned long long)nValue, sOffset, sSize);
            }

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_findWord: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_u16 nValue = (cd_u16)arg_i64(pCtx, nArgc, pArgv, 2, 0);
            cd_i64 nResult = 0;
            cd_i64 nStart = die_engine_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            /* Binary_Script passes its non-null PDSTRUCT as find_uint16's
             * endian argument, so the reference slot searches big endian. */
            nResult = xx_io_find_u16_in_range(pFile, nOffset, nSize, nValue, true, NULL);

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                /* XBinary::valueToHex of a fixed-width integer: always the
                 * full field, unlike valueToHexEx above. */
                die_engine_profile_end(pEngine, nStart, "findWord[%0*llx]: %s %s", 4, (unsigned long long)nValue, sOffset, sSize);
            }

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_findDword: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_u32 nValue = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 2, 0);
            cd_i64 nResult = 0;
            cd_i64 nStart = die_engine_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            /* Same reference slot behavior as findWord; scalar reads remain
             * little endian, and the general search helper is unchanged. */
            nResult = xx_io_find_u32_in_range(pFile, nOffset, nSize, nValue, true, NULL);

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                /* XBinary::valueToHex of a fixed-width integer: always the
                 * full field, unlike valueToHexEx above. */
                die_engine_profile_end(pEngine, nStart, "findDword[%0*llx]: %s %s", 8, (unsigned long long)nValue, sOffset, sSize);
            }

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_getEntryPointOffset:
            *pResult = js_num((double)script_entry_point_offset(pEngine));
            return true;

        case A_getOverlayOffset:
            if (has_legacy_script_map(pMap)) {
                *pResult = js_num((double)legacy_overlay_offset(pMap));
                return true;
            }
            if (pEngine->bHasElf) {
                *pResult = js_num((double)pEngine->elf.nOverlayOffset);
                return true;
            }

            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nOverlayOffset : -1));
            return true;

        case A_getOverlaySize:
            if (has_legacy_script_map(pMap)) {
                cd_i64 nOverlay = legacy_overlay_offset(pMap);
                *pResult = js_num((double)(nOverlay >= 0 ? pMap->binary_size - nOverlay : 0));
                return true;
            }
            if (pEngine->bHasElf) {
                *pResult = js_num((double)pEngine->elf.nOverlaySize);
                return true;
            }

            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nOverlaySize : 0));
            return true;

        case A_isOverlayPresent:
            if (has_legacy_script_map(pMap)) {
                cd_i64 nOverlay = legacy_overlay_offset(pMap);
                *pResult = js_bool(nOverlay >= 0 && nOverlay < pMap->binary_size);
                return true;
            }
            if (pEngine->bHasElf) {
                *pResult = js_bool(pEngine->elf.nOverlaySize != 0);
                return true;
            }

            *pResult = js_bool(pEngine->bHasPE && (pPE->nOverlaySize != 0));
            return true;

        case A_getAddressOfEntryPoint:
            if (has_legacy_script_map(pMap)) {
                *pResult = js_num((double)pMap->entry_point_address);
                return true;
            }
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nEntryPointAddress : 0));
            return true;

        case A_isSignaturePresent: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 2);
            int bResult = 0;
            cd_i64 nStart = die_engine_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            bResult = (die_engine_signature_find(pEngine, nOffset, nSize, pSignature) != -1) ? 1 : 0;

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                die_engine_profile_end(pEngine, nStart, "isSignaturePresent[%s]: %s %s", pSignature, sOffset, sSize);
            }

            cd_free(pSignature);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_swapBytes: {
            cd_u32 nValue = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            *pResult = js_num((double)(cd_u32)(((nValue & 0xFF) << 24) | ((nValue & 0xFF00) << 8) | ((nValue >> 8) & 0xFF00) | ((nValue >> 24) & 0xFF)));
            return true;
        }

        case A_RVAToOffset:
            *pResult = js_num((double)xx_memory_map_address_to_offset_ex(pMap, nRvaBase + (cd_u64)arg_i64(pCtx, nArgc, pArgv, 0, 0), addressLookup));
            return true;
        case A_VAToOffset:
            *pResult = js_num((double)xx_memory_map_address_to_offset_ex(pMap, (cd_u64)arg_i64(pCtx, nArgc, pArgv, 0, 0), addressLookup));
            return true;
        case A_OffsetToVA:
            *pResult = js_num((double)(cd_i64)xx_memory_map_offset_to_address_ex(pMap, arg_i64(pCtx, nArgc, pArgv, 0, 0), addressLookup));
            return true;
        case A_OffsetToRVA: {
            cd_i64 nAddress = xx_memory_map_offset_to_address_ex(pMap, arg_i64(pCtx, nArgc, pArgv, 0, 0), addressLookup);

            if (nAddress == -1) {
                *pResult = js_num(-1);
            } else {
                *pResult = js_num((double)(nAddress - (cd_i64)nRvaBase));
            }
            return true;
        }

        case A_getFileDirectory:
            *pResult = str_take_native(pCtx, xx_fs_path_dir(pEngine->file_name));
            return true;
        case A_getFileBaseName:
            *pResult = str_take_native(pCtx, xx_fs_path_base_name(pEngine->file_name));
            return true;
        /* false: cdie counts a leading dot, so ".hidden.txt" has no complete
         * suffix here. The signature databases were written against that. */
        case A_getFileCompleteSuffix:
            *pResult = str_take_native(pCtx, xx_fs_path_complete_suffix_ex(pEngine->file_name, false));
            return true;
        case A_getFileSuffix:
            *pResult = str_take_native(pCtx, xx_fs_path_suffix(pEngine->file_name));
            return true;

        case A_getSignature:
            *pResult = str_take_native(pCtx, xx_io_get_signature_hex(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0)));
            return true;

        case A_calculateEntropy: {
            cd_i64 nStart = die_engine_profile_start(pEngine);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            double dEntropy = xx_entropy_calculate_device(pFile, nOffset, nSize);
            die_engine_profile_end(pEngine, nStart, "calculateEntropy: %lld %lld", (long long)nOffset, (long long)nSize);
            *pResult = js_num(dEntropy);
            return true;
        }
        case A_isZeroFilled:
            *pResult = js_bool(xx_io_is_zero_filled(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0)));
            return true;
        case A_scanBufferForEncryptedPe: {
            cd_i64 nStart = die_engine_profile_start(pEngine);
            const char *pszAlgo = "";
            if (nArgc > 0 && pArgv[0].tag == JT_OBJ) {
                int64_t nLen = js_array_length(pCtx, pArgv[0]);
                int64_t nMax = arg_i64(pCtx, nArgc, pArgv, 1, nLen);
                if (nMax < 0 || nMax > nLen) nMax = nLen;
                if (nMax >= 0x100) {
                    uint8_t *pBytes = (uint8_t *)cd_malloc((size_t)nMax);
                    if (pBytes) {
                        for (int64_t i = 0; i < nMax; i++) {
                            JSVal elem = js_get_index(pCtx, pArgv[0], i);
                            pBytes[i] = (uint8_t)(js_to_int32(pCtx, elem) & 0xFF);
                        }
                        pszAlgo = xx_kpa_scan_buffer_encrypted_pe(pBytes, (size_t)nMax);
                        cd_free(pBytes);
                    }
                }
            } else if (nArgc >= 2) {
                cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
                cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
                if (nSize >= 0x100 && pFile) {
                    cd_i64 nFileSize = xx_io_total_size(pFile);
                    if (nOffset >= 0 && nOffset < nFileSize) {
                        const unsigned char *pWhole;
                        /* nOffset is inside the file, so this cannot overflow
                         * the way nOffset + nSize can for a huge nSize. */
                        if (nSize > nFileSize - nOffset) nSize = nFileSize - nOffset;
                        pWhole = xx_io_buffer_whole(pFile, nSize);
                        if (pWhole) {
                            pszAlgo = xx_kpa_scan_buffer_encrypted_pe(pWhole + nOffset, (size_t)nSize);
                        } else {
                            uint8_t *pBytes = (uint8_t *)cd_malloc((size_t)nSize);
                            if (pBytes) {
                                if (xx_io_read_at(pFile, nOffset, pBytes, (size_t)nSize)) {
                                    pszAlgo = xx_kpa_scan_buffer_encrypted_pe(pBytes, (size_t)nSize);
                                }
                                cd_free(pBytes);
                            }
                        }
                    }
                }
            }
            die_engine_profile_end(pEngine, nStart, "scanBufferForEncryptedPe: [%s]", pszAlgo ? pszAlgo : "");
            *pResult = js_str(pCtx, pszAlgo ? pszAlgo : "");
            return true;
        }
        case A_validateAmd64Unwind: {
            cd_i64 nRVA = arg_i64(pCtx, nArgc, pArgv, 0, -1);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, -1);
            cd_i64 nStart;
            xx_pe_amd64_unwind_result validation;
            JSVal result;
            int available;

            if (!pEngine->bHasPE || nArgc < 2 || nRVA < 0 || nRVA > UINT32_MAX ||
                nSize < 0 || nSize > UINT32_MAX) {
                *pResult = js_null();
                return true;
            }
            nStart = die_engine_profile_start(pEngine);
            available = xx_pe_inspect_validate_amd64_unwind(pPE, pFile, (uint32_t)nRVA,
                                                           (uint32_t)nSize, &validation);
            die_engine_profile_end(pEngine, nStart, "validateAmd64Unwind: %lld %lld",
                                   (long long)nRVA, (long long)nSize);
            if (!available) {
                *pResult = js_null();
                return true;
            }
            result = js_new_object(pCtx);
            js_set(pCtx, result, "status", js_num((double)validation.status));
            js_set(pCtx, result, "entryIndex", js_num((double)validation.entry_index));
            js_set(pCtx, result, "beginRva", js_num((double)validation.begin_rva));
            js_set(pCtx, result, "endRva", js_num((double)validation.end_rva));
            js_set(pCtx, result, "unwindRva", js_num((double)validation.unwind_rva));
            *pResult = result;
            return true;
        }
        case A_validateGuardCfFunctionTable: {
            cd_u32 nTableRVA;
            cd_u32 nEntryCount;
            cd_u32 nEntrySize;
            cd_i64 nStart;
            xx_pe_guard_cf_result validation;
            JSVal result;
            int available;

            if (!pEngine->bHasPE || !arg_exact_u32(nArgc, pArgv, 0, &nTableRVA) ||
                !arg_exact_u32(nArgc, pArgv, 1, &nEntryCount) || !nEntryCount ||
                !arg_exact_u32(nArgc, pArgv, 2, &nEntrySize) || nEntrySize < 4 || nEntrySize > 19) {
                *pResult = js_null();
                return true;
            }
            nStart = die_engine_profile_start(pEngine);
            available = xx_pe_inspect_validate_guard_cf_function_table(
                pPE, pFile, nTableRVA, nEntryCount, nEntrySize, &validation);
            die_engine_profile_end(pEngine, nStart,
                                   "validateGuardCfFunctionTable: %lld %lld %lld",
                                   (long long)nTableRVA, (long long)nEntryCount,
                                   (long long)nEntrySize);
            if (!available) {
                *pResult = js_null();
                return true;
            }
            result = js_new_object(pCtx);
            js_set(pCtx, result, "status", js_num((double)validation.status));
            js_set(pCtx, result, "entryIndex", js_num((double)validation.entry_index));
            js_set(pCtx, result, "functionRva", js_num((double)validation.function_rva));
            *pResult = result;
            return true;
        }
        case A_calculateMD5: {
            static const char digits[] = "0123456789abcdef";
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_i64 nPosition;
            unsigned char digest[XX_MD5_DIGEST_SIZE];
            char hex[XX_MD5_DIGEST_SIZE * 2 + 1];
            bool bHasDigest;
            size_t i;

            /* Binary_Script::calculateMD5 uses XBinary::getHash: strict
             * ranges, lowercase hex, and an empty string on failure.
             * The native xx_md5 helper has a separate uppercase/clamping
             * contract, so adapt the script API without changing it. */
            if (nSize < -1 || xx_io_buffer_failed(pFile) || xx_pd_is_stopped(pEngine->pd)) {
                *pResult = js_str(pCtx, "");
                return true;
            }
            nPosition = xx_io_tell(pFile);
            if (nPosition < 0) {
                *pResult = js_str(pCtx, "");
                return true;
            }
            bHasDigest = xx_md5_device(pFile, nOffset, nSize, digest, sizeof(digest), pEngine->pd);
            if (xx_io_seek64(pFile, nPosition, SEEK_SET) != 0 || !bHasDigest ||
                xx_io_buffer_failed(pFile) || xx_pd_is_stopped(pEngine->pd)) {
                *pResult = js_str(pCtx, "");
                return true;
            }
            for (i = 0; i < sizeof(digest); i++) {
                hex[i * 2] = digits[digest[i] >> 4];
                hex[i * 2 + 1] = digits[digest[i] & 0x0f];
            }
            hex[sizeof(hex) - 1] = '\0';
            *pResult = js_str(pCtx, hex);
            return true;
        }
        case A_calculateCRC32:
            *pResult = js_num((double)xx_crc32_device(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0), 0));
            return true;
        case A_crc32:
            *pResult = js_num((double)xx_crc32_device(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0), (cd_u32)arg_i64(pCtx, nArgc, pArgv, 2, 0)));
            return true;
        case A_crc16: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_u16 nInit = (cd_u16)arg_i64(pCtx, nArgc, pArgv, 2, 0);
            cd_u16 nResult;

            /* Binary_Script::crc16 uses XBinary's range API: valid, nonempty
             * reads receive a final XOR; rejected or failed reads return 0.
             * Keep the native CRC-16/ARC accumulator contract unchanged. */
            if (nSize < -1 || !xx_io_range_clamp(pFile, nOffset, &nSize) || !nSize ||
                xx_io_buffer_failed(pFile) || xx_pd_is_stopped(pEngine->pd)) {
                *pResult = js_num(0);
                return true;
            }
            nResult = xx_crc16_device(pFile, nOffset, nSize, nInit);
            if (xx_io_buffer_failed(pFile) || xx_pd_is_stopped(pEngine->pd)) {
                *pResult = js_num(0);
                return true;
            }
            *pResult = js_num((double)(nResult ^ 0xffffU));
            return true;
        }
        case A_adler32:
            *pResult = js_num((double)xx_adler32_device(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0)));
            return true;

        case A_isSignatureInSectionPresent: {
            cd_i64 nNumber = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 1);
            int bResult = 0;
            cd_i64 nStart = die_engine_profile_start(pEngine);

            /* The reference numbered the map's records and skipped the PE
             * header at index 0. Sections are asked for by their own number
             * here instead, because the map now splits a section whose
             * virtual size exceeds its raw size into two records and a raw
             * index would no longer land on the section the script meant. */
            if (pEngine->bHasPE) {
                if ((nNumber >= 0) && (nNumber < pPE->nSectionCount)) {
                    bResult = (die_engine_signature_find(pEngine, pPE->pSections[nNumber].nMappedOffset, pPE->pSections[nNumber].nMappedSize, pSignature) != -1) ? 1 : 0;
                } else if ((nNumber == pPE->nSectionCount) && (pPE->nOverlaySize > 0)) {
                    /* Index one past the last section reached the overlay. */
                    bResult = (die_engine_signature_find(pEngine, pPE->nOverlayOffset, pPE->nOverlaySize, pSignature) != -1) ? 1 : 0;
                }
            } else if (nNumber == 0) {
                bResult = (die_engine_signature_find(pEngine, 0, xx_io_total_size(pFile), pSignature) != -1) ? 1 : 0;
            }

            /* The reference passes the section number as a plain decimal and
             * leaves the trailing space of its two-argument format. */
            die_engine_profile_end(pEngine, nStart, "isSignatureInSectionPresent[%s]: %lld ", pSignature, (long long)nNumber);
            cd_free(pSignature);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isAnySignatureInSectionPresent: {
            cd_i64 nNumber = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nOffset = -1;
            cd_i64 nSize = 0;
            cd_i64 nStart = die_engine_profile_start(pEngine);
            int64_t nLength;
            size_t i;
            char **ppSignatures;
            int bResult = 0;

            if (nArgc < 2 || pArgv[1].tag != JT_OBJ || !pArgv[1].u.o || pArgv[1].u.o->cls != JCLASS_ARRAY) {
                *pResult = js_bool(0);
                return true;
            }
            nLength = js_array_length(pCtx, pArgv[1]);
            if (nLength <= 0 || (cd_u64)nLength > (size_t)-1 / sizeof(*ppSignatures)) {
                *pResult = js_bool(0);
                return true;
            }
            ppSignatures = (char **)xx_mem_calloc((size_t)nLength, sizeof(*ppSignatures));
            if (!ppSignatures) {
                *pResult = js_bool(0);
                return true;
            }
            for (i = 0; i < (size_t)nLength; i++) {
                JSVal value = js_get_index(pCtx, pArgv[1], (int64_t)i);
                /* No coercion of numbers, holes, or null into signatures. */
                if (value.tag == JT_STR && x_strlen(js_str_data(value)) == js_str_len(value)) ppSignatures[i] = js_to_cstr(pCtx, value);
                js_release(pCtx, value);
                if (!ppSignatures[i] || js_has_exception(pCtx)) goto any_signature_cleanup;
            }

            if (pEngine->bHasPE) {
                if (nNumber >= 0 && nNumber < pPE->nSectionCount) {
                    nOffset = pPE->pSections[nNumber].nMappedOffset;
                    nSize = pPE->pSections[nNumber].nMappedSize;
                } else if (nNumber == pPE->nSectionCount && pPE->nOverlaySize > 0) {
                    nOffset = pPE->nOverlayOffset;
                    nSize = pPE->nOverlaySize;
                }
            } else if (nNumber == 0) {
                nOffset = 0;
                nSize = xx_io_total_size(pFile);
            }
            bResult = die_engine_signature_find_any(pEngine, nOffset, nSize, (const char *const *)ppSignatures, (size_t)nLength);

any_signature_cleanup:
            for (i = 0; i < (size_t)nLength; i++) cd_free(ppSignatures[i]);
            xx_mem_free(ppSignatures);
            die_engine_profile_end(pEngine, nStart, "isAnySignatureInSectionPresent: %lld (%lld patterns) ", (long long)nNumber, (long long)nLength);
            *pResult = js_bool(bResult);
            return true;
        }

        case A_getImageBase:
            *pResult = js_num((double)pMap->module_address);
            return true;

        case A_upperCase:
        case A_lowerCase: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            size_t i = 0;

            for (i = 0; pString[i]; i++) {
                if (id == A_upperCase) {
                    if ((pString[i] >= 'a') && (pString[i] <= 'z')) {
                        pString[i] = (char)(pString[i] - 'a' + 'A');
                    }
                } else {
                    if ((pString[i] >= 'A') && (pString[i] <= 'Z')) {
                        pString[i] = (char)(pString[i] - 'A' + 'a');
                    }
                }
            }

            *pResult = str_take(pCtx, pString);
            return true;
        }

        case A_isPlainText:
            *pResult = js_bool(is_plain_text(pFile));
            return true;

        case A_isUTF8Text:
            *pResult = js_bool(is_utf8_text(pFile));
            return true;
        /* m_bIsUnicodeText is set only on the getUnicodeType branch of the
         * constructor, so it means "not UNICODE_TYPE_NONE".                */
        case A_isUnicodeText:
            *pResult = js_bool(unicode_type(pFile) != 0);
            return true;
        /* isText ORs the three flags together, as the reference does. */
        case A_isText:
            *pResult = js_bool(is_plain_text(pFile) || is_utf8_text(pFile) || (unicode_type(pFile) != 0));
            return true;

        /* Binary_Script's constructor fills m_sHeaderString with the head of
         * the file in the first encoding that answers: read_unicodeString
         * past the byte order mark, then read_utf8String past a UTF-8 mark,
         * then read_ansiString from the start. Anything that is not text at
         * all keeps the empty string the member was constructed with. The
         * length is always qMin(size, 0x1000).                             */
        case A_getHeaderString: {
            cd_i64 nMaxSize = pFile ? xx_io_total_size(pFile) : 0;
            int nUnicode = unicode_type(pFile);

            if (nMaxSize > 0x1000) {
                nMaxSize = 0x1000;
            }

            if (nUnicode != 0) {
                *pResult = str_take_native(pCtx, xx_str_read_unicode(pFile, 2, nMaxSize, (nUnicode == 2) ? 1 : 0));
                return true;
            }

            if (is_utf8_text(pFile)) {
                *pResult = str_take_native(pCtx, xx_str_read_utf8(pFile, 3, nMaxSize));
                return true;
            }

            if (is_plain_text(pFile)) {
                *pResult = str_take_native(pCtx, xx_str_read_ansi(pFile, 0, nMaxSize));
                return true;
            }

            *pResult = js_str(pCtx, "");
            return true;
        }

        case A_getDisasmLength: {
            cd_i64 nAddress = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nOffset = xx_memory_map_address_to_offset_ex(pMap, (cd_u64)nAddress, XX_MEMORY_MAP_LOOKUP_FIRST_MATCH);
            XDisasmResult disasm;

            if (nOffset == -1) {
                *pResult = js_num(0);
                return true;
            }

            disasm = xdisasm_at(pFile, nOffset, pEngine->nBits, (cd_u64)nAddress);

            *pResult = js_num((double)disasm.nSize);
            return true;
        }

        case A_getDisasmString: {
            cd_i64 nAddress = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nOffset = xx_memory_map_address_to_offset_ex(pMap, (cd_u64)nAddress, XX_MEMORY_MAP_LOOKUP_FIRST_MATCH);
            XDisasmResult disasm;

            if (nOffset == -1) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            disasm = xdisasm_at(pFile, nOffset, pEngine->nBits, (cd_u64)nAddress);

            /* The decoder supplies complete uppercase Intel text, including
             * prefixes and the absolute target of a relative transfer. */
            *pResult = js_str(pCtx, disasm.sInstruction);
            return true;
        }

        case A_getDisasmNextAddress: {
            cd_i64 nAddress = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nOffset = xx_memory_map_address_to_offset_ex(pMap, (cd_u64)nAddress, XX_MEMORY_MAP_LOOKUP_FIRST_MATCH);
            XDisasmResult disasm;

            if (nOffset == -1) {
                *pResult = js_num(0);
                return true;
            }

            disasm = xdisasm_at(pFile, nOffset, pEngine->nBits, (cd_u64)nAddress);

            /* The script API walks encoded instructions sequentially; branch
             * targets are already included in the formatted instruction. */
            *pResult = js_num(disasm.nSize > 0 ? (double)xdisasm_next_address(&disasm, (cd_u64)nAddress) : 0);
            return true;
        }

        case A_is16:
            *pResult = js_bool(pEngine->nBits == 16);
            return true;
        case A_is32:
            *pResult = js_bool(pEngine->nBits == 32);
            return true;
        case A_is64:
            *pResult = js_bool(pEngine->nBits == 64);
            return true;

        case A_isDeepScan:
            *pResult = js_bool(pEngine->pOptions->deep_scan);
            return true;
        case A_isHeuristicScan:
            *pResult = js_bool(pEngine->pOptions->heuristic_scan);
            return true;
        case A_isFirstWrapperScan:
            *pResult = js_bool(0);
            return true;
        case A_isAggressiveScan:
            *pResult = js_bool(pEngine->pOptions->aggressive_scan);
            return true;
        case A_isRecursiveScan:
            *pResult = js_bool(pEngine->pOptions->recursive_scan);
            return true;
        case A_isOverlayScan:
            *pResult = js_bool(0);
            return true;
        case A_isVerbose:
            *pResult = js_bool(pEngine->pOptions->verbose);
            return true;
        case A_isProfiling:
            *pResult = js_bool(pEngine->pOptions->profiling);
            return true;
        case A_getScanID:
            *pResult = js_str(pCtx, pEngine->pOptions->resource_type ? pEngine->pOptions->resource_type :
                                   pEngine->pOptions->archive_member ? pEngine->pOptions->archive_member : "");
            return true;
        case A_getStartOffset:
            *pResult = js_num(0);
            return true;

        /* Binary_Script::bytesCountToString uses XBinary's default base. */
        case A_bytesCountToString: {
            cd_u64 nValue = arg_u64(pCtx, nArgc, pArgv, 0);
            char sBuf[96];

            if (!xx_str_bytes_count_to_string(nValue, 1024U, sBuf, sizeof(sBuf))) sBuf[0] = '\0';
            *pResult = js_str(pCtx, sBuf);
            return true;
        }

        case A_getOperationSystemName:
            if (pEngine->bHasDex || pEngine->bHasApk || pEngine->fileType == XFT_APK) {
                *pResult = js_str(pCtx, "Android");
                return true;
            }

            if (pEngine->bHasMach) {
                *pResult = js_str(pCtx, pEngine->mach.sOsName);
                return true;
            }

            if (pEngine->bHasElf) {
                *pResult = js_str(pCtx, pEngine->elf.sOsName);
                return true;
            }

            if (pEngine->fileType == XFT_NE) {
                *pResult = js_str(pCtx, ne_os_name(ne_target_os(pFile)));
                return true;
            }

            if (pEngine->fileType == XFT_JAR) {
                /* XJAR::getFileFormatInfo: osName = OSNAME_JVM. */
                *pResult = js_str(pCtx, "JVM");
                return true;
            }

            if (pEngine->fileType == XFT_COM || pEngine->fileType == XFT_MSDOS) {
                /* XCOM and XMSDOS: OSNAME_MSDOS. */
                *pResult = js_str(pCtx, "MS-DOS");
                return true;
            }

            if (pEngine->fileType == XFT_AMIGAHUNK) {
                *pResult = js_str(pCtx, "Amiga");
                return true;
            }

            if (pEngine->fileType == XFT_ATARIST) {
                *pResult = js_str(pCtx, "Atari ST");
                return true;
            }

            if (pEngine->fileType == XFT_LX) {
                *pResult = js_str(pCtx, "OS/2");
                return true;
            }

            if (pEngine->fileType == XFT_MACHOFAT) {
                *pResult = js_str(pCtx, "macOS");
                return true;
            }

            if (pEngine->bHasPE) {
                if (pPE->nMachine == (0xFD1D ^ 0x8664)) {
                    *pResult = js_str(pCtx, "Linux");
                    return true;
                }
                if (pPE->nSubsystem == 9) {
                    *pResult = js_str(pCtx, "Windows CE");
                    return true;
                }
                if (pPE->nSubsystem >= 10 && pPE->nSubsystem <= 13) {
                    *pResult = js_str(pCtx, "UEFI");
                    return true;
                }
                if (pPE->nSubsystem == 14 || pPE->nSubsystem == 17) {
                    *pResult = js_str(pCtx, "XBOX");
                    return true;
                }
                if (pPE->nSubsystem == 5) {
                    *pResult = js_str(pCtx, "OS/2");
                    return true;
                }
                if (pPE->nSubsystem == 7) {
                    *pResult = js_str(pCtx, "Posix");
                    return true;
                }
                *pResult = js_str(pCtx, "Windows");
                return true;
            }

            *pResult = js_str(pCtx, "Windows");
            return true;

        /* xx_pe_inspection::getFileFormatInfo: the raw version number is mapped to a
         * marketing name, and a 64-bit image is floored at Server 2003
         * because no earlier Windows had a 64-bit edition. Anything the
         * table does not know falls back to Server 2003 or XP by bitness,
         * so this never reports a bare number.                            */
        case A_getOperationSystemVersion: {
            unsigned int nVersion = 0;
            int bIs64 = 0;
            const char *pName = NULL;

            if (pEngine->bHasDex) {
                *pResult = js_str(pCtx, xx_dex_android_version(&pEngine->dex));
                return true;
            }

            if (pEngine->bHasApk || pEngine->fileType == XFT_APK) {
                char sVerBuf[64];
                *pResult = js_str(pCtx, apk_get_os_version(&pEngine->apk, sVerBuf, sizeof(sVerBuf)));
                return true;
            }

            if (pEngine->bHasMach) {
                *pResult = js_str(pCtx, pEngine->mach.sOsVersion);
                return true;
            }

            if (pEngine->bHasElf) {
                *pResult = js_str(pCtx, pEngine->elf.sOsVersion);
                return true;
            }

            if (pEngine->fileType == XFT_JAR) {
                /* XJAR: Java release read from the first *.class member. */
                *pResult = js_str(pCtx, xx_zip_get_jvm_version(&pEngine->zip));
                return true;
            }

            if (!pEngine->bHasPE) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            if (pPE->nMachine == (0xFD1D ^ 0x8664) ||
                pPE->nSubsystem == 9 ||
                (pPE->nSubsystem >= 10 && pPE->nSubsystem <= 13) ||
                pPE->nSubsystem == 14 || pPE->nSubsystem == 17 ||
                pPE->nSubsystem == 5 || pPE->nSubsystem == 7) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            bIs64 = (pEngine->nBits == 64) ? 1 : 0;
            nVersion = ((unsigned int)pPE->nMajorOperatingSystemVersion << 16) | (unsigned int)pPE->nMinorOperatingSystemVersion;

            if (bIs64 && (nVersion < 0x00050002)) {
                nVersion = 0x00050002;
            }

            pName = windows_version_name(nVersion);

            if (pName == NULL) {
                pName = bIs64 ? "Server 2003" : "XP";
            }

            *pResult = js_str(pCtx, pName);
            return true;
        }

        /* XBinary::getOperationSystemInfoString: "<arch>, <mode>". */
        case A_getOperationSystemOptions: {
            char sBuf[64];

            if (pEngine->bHasApk || pEngine->fileType == XFT_APK) {
                *pResult = js_str(pCtx, "Universal, Data");
                return true;
            }

            if (pEngine->bHasDex) {
                *pResult = js_str(pCtx, "Dalvik, 32-bit");
                return true;
            }

            if (pEngine->bHasMach) {
                x_snprintf(sBuf, sizeof(sBuf), "%s, %d-bit", pEngine->mach.sArch, pEngine->mach.bIs64 ? 64 : 32);

                *pResult = js_str(pCtx, sBuf);
                return true;
            }

            if (pEngine->bHasElf) {
                x_snprintf(sBuf, sizeof(sBuf), "%s, %d-bit", pEngine->elf.sArch, pEngine->elf.bIs64 ? 64 : 32);

                *pResult = js_str(pCtx, sBuf);
                return true;
            }

            if (pEngine->fileType == XFT_NE) {
                /* XNE: mode is always MODE_16SEG. */
                x_snprintf(sBuf, sizeof(sBuf), "%s, 16SEG", ne_arch_name(ne_target_os(pFile)));

                *pResult = js_str(pCtx, sBuf);
                return true;
            }

            if (pEngine->fileType == XFT_JAR) {
                /* XJAR: getArch() = "Universal", getMode() = MODE_DATA. */
                *pResult = js_str(pCtx, "Universal, Data");
                return true;
            }

            if (pEngine->fileType == XFT_COM || pEngine->fileType == XFT_MSDOS) {
                /* XCOM and XMSDOS: getArch() = "8086", getMode() = MODE_16. */
                *pResult = js_str(pCtx, "8086, 16-bit");
                return true;
            }

            if (pEngine->fileType == XFT_AMIGAHUNK) {
                xx_amigahunk amigahunk;
                const char *sArch = "68K";
                const char *sMode = "16-bit";
                xx_amigahunk_init(&amigahunk, pEngine->device, 0);
                if (xx_amigahunk_handle_base_info(&amigahunk.format, NULL)) {
                    if (amigahunk.has_ppc_code) sArch = "PPC";
                    if (amigahunk.has_reloc32) sMode = "32-bit";
                }
                xx_amigahunk_destroy(&amigahunk);
                x_snprintf(sBuf, sizeof(sBuf), "%s, %s", sArch, sMode);
                *pResult = js_str(pCtx, sBuf);
                return true;
            }

            if (pEngine->fileType == XFT_ATARIST) {
                *pResult = js_str(pCtx, "68K, 32-bit");
                return true;
            }

            if (pEngine->fileType == XFT_MACHOFAT) {
                uint8_t h[8];
                char sArchs[256];
                int archs_len = 0;
                sArchs[0] = '\0';
                if (pEngine->device && xx_io_read_at(pEngine->device, 0, h, 8)) {
                    uint32_t magic = xx_data_get_u32(h, 4, 0, true);
                    if (magic == 0xcafebabe || magic == 0xcafebabf || magic == 0xbebafeca || magic == 0xbfbafeca) {
                        bool big = (magic == 0xcafebabe || magic == 0xcafebabf);
                        bool wide = (magic == 0xcafebabf || magic == 0xbfbafeca);
                        uint32_t count = xx_data_get_u32(h + 4, 4, 0, big);
                        uint32_t stride = wide ? 32 : 20;
                        if (count > 64) count = 64;
                        for (uint32_t i = 0; i < count; i++) {
                            uint8_t entry[4];
                            if (xx_io_read_at(pEngine->device, 8 + (int64_t)i * stride, entry, 4)) {
                                uint32_t cpu = xx_data_get_u32(entry, 4, 0, big);
                                const char *arch = machofat_cpu_to_arch(cpu);
                                if (!arch_in_list(sArchs, arch)) {
                                    if (archs_len > 0 && archs_len < (int)sizeof(sArchs) - 3) {
                                        archs_len += x_snprintf(sArchs + archs_len, sizeof(sArchs) - archs_len, ", ");
                                    }
                                    if (archs_len < (int)sizeof(sArchs) - 1) {
                                        archs_len += x_snprintf(sArchs + archs_len, sizeof(sArchs) - archs_len, "%s", arch);
                                    }
                                }
                            }
                        }
                    }
                }
                if (!sArchs[0]) x_snprintf(sArchs, sizeof(sArchs), "Universal");
                x_snprintf(sBuf, sizeof(sBuf), "%s, Data", sArchs);
                *pResult = js_str(pCtx, sBuf);
                return true;
            }

            if (!pEngine->bHasPE) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            x_snprintf(sBuf, sizeof(sBuf), "%s, %d-bit", pe_machine_name(pPE->nMachine), (int)pEngine->nBits);

            *pResult = js_str(pCtx, sBuf);
            return true;
        }
        case A_getFileFormatName:
            *pResult = js_str(pCtx, xft_to_string(pEngine->fileType));
            return true;

        case A_getFileFormatVersion:
            if (pEngine->bHasJpeg && xx_jpeg_get_version(&pEngine->jpeg)) {
                *pResult = js_str(pCtx, xx_jpeg_get_version(&pEngine->jpeg));
                return true;
            }

            if (pEngine->bHasPdf) {
                *pResult = js_str(pCtx, xx_pdf_get_version(&pEngine->pdf));
                return true;
            }

            if (pEngine->bHasDex) {
                *pResult = js_str(pCtx, xx_dex_get_version(&pEngine->dex));
                return true;
            }

            if (pEngine->bHasPyc) {
                *pResult = js_str(pCtx, pEngine->pyc.version);
                return true;
            }

            if (pEngine->fileType == XFT_JAVACLASS) {
                char sVer[64];
                javaclass_get_version(pFile, sVer, sizeof(sVer));
                *pResult = js_str(pCtx, sVer);
                return true;
            }

            if (pEngine->fileType == XFT_ZIP || pEngine->bHasZip) {
                char sVer[64];
                if (zip_get_format_version(pEngine, sVer, sizeof(sVer)) && sVer[0]) {
                    *pResult = js_str(pCtx, sVer);
                    return true;
                }
            }

            *pResult = js_str(pCtx, "");
            return true;

        case A_getFileFormatOptions:
            if (pEngine->bHasPng) {
                char *pInfo = xx_png_get_info(&pEngine->png, NULL);
                JSVal result = js_str(pCtx, pInfo);

                xx_str_free(pInfo);

                *pResult = result;
                return true;
            }

            if (pEngine->bHasPdf) {
                char *pInfo = xx_pdf_get_info(&pEngine->pdf, NULL);
                JSVal result;
                if (pEngine->pdf.encrypted && pInfo && *pInfo) {
                    char *pPrefixed = xx_str_concat("Encrypted, ", pInfo);
                    result = js_str(pCtx, pPrefixed ? pPrefixed : pInfo);
                    xx_str_free(pPrefixed);
                } else if (pEngine->pdf.encrypted) {
                    result = js_str(pCtx, "Encrypted");
                } else {
                    result = js_str(pCtx, pInfo ? pInfo : "");
                }
                xx_str_free(pInfo);

                *pResult = result;
                return true;
            }

            if (pEngine->bHasDex) {
                char *pHex = xx_dex_map_hash_hex(&pEngine->dex);
                JSVal result = js_str(pCtx, pHex);

                xx_str_free(pHex);

                *pResult = result;
                return true;
            }

            if (pEngine->fileType == XFT_ZIP || pEngine->bHasZip) {
                char sOpt[256];
                if (zip_get_format_options(pEngine, sOpt, sizeof(sOpt)) && sOpt[0]) {
                    *pResult = js_str(pCtx, sOpt);
                    return true;
                }
            }

            *pResult = js_str(pCtx, "");
            return true;

        case A_isSigned:
        case A_isSignedFile: {
            /* The security directory stores a file offset, not an RVA, and
             * xx_pe_inspection::isSigned() validates that the range is inside the file.  */
            cd_i64 nOffset = 0;
            cd_i64 nSize = 0;

            if (!pEngine->bHasPE) {
                *pResult = js_bool(0);
                return true;
            }

            nOffset = (cd_i64)pPE->pDirRVA[XX_PE_INSPECT_DIR_SECURITY];
            nSize = (cd_i64)pPE->pDirSize[XX_PE_INSPECT_DIR_SECURITY];

            *pResult = js_bool((nSize > 0) && (nOffset > 0) && (nOffset < xx_io_total_size(pFile)) && (nSize <= xx_io_total_size(pFile) - nOffset));
            return true;
        }

        case A_cleanString: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            char *pClean = clean_string(pString);

            cd_free(pString);

            *pResult = str_take(pCtx, pClean);
            return true;
        }

        case A_readBytes: {
            cd_i64 nStart = die_engine_profile_start(pEngine);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int bReplaceZero = arg_bool(pCtx, nArgc, pArgv, 2, 0);
            JSVal result = js_new_array(pCtx);
            cd_i64 i = 0;
            size_t nCapacity = xx_io_buffer_capacity(pFile);

            fix_offset_size(pEngine, &nOffset, &nSize);

            if ((nOffset < 0) || (nOffset >= xx_io_total_size(pFile)) || (nSize < 0)) {
                *pResult = result;
                return true;
            }

            while (i < nSize) {
                size_t nCount = (cd_u64)(nSize - i) < nCapacity ? (size_t)(nSize - i) : nCapacity;
                const unsigned char *bytes = xx_io_buffer_view(pFile, nOffset + i, &nCount);
                size_t j;

                if (!bytes || !nCount) break;
                for (j = 0; j < nCount; ++j) {
                    cd_u8 nByte = bytes[j];
                    js_set_index(pCtx, result, i + (cd_i64)j, js_num((bReplaceZero && (nByte == 0)) ? 32 : (double)nByte));
                }
                i += (cd_i64)nCount;
            }

            die_engine_profile_end(pEngine, nStart, "readBytes: %lld %lld", (long long)nOffset, (long long)nSize);

            *pResult = result;
            return true;
        }

        case A_isOverlay:
            *pResult = js_bool(pEngine->pOptions->scan_part == XX_SCAN_PART_OVERLAY);
            return true;
        case A_isResource:
            *pResult = js_bool(pEngine->pOptions->scan_part == XX_SCAN_PART_RESOURCE);
            return true;
        case A_isDebugData:
            *pResult = js_bool(0);
            return true;
        case A_isFilePart:
            *pResult = js_bool(pEngine->pOptions->scan_part != XX_SCAN_PART_INPUT);
            return true;

        case A_isReleaseBuild:
            *pResult = js_bool(1);
            return true;
        case A_isDebugBuild:
            *pResult = js_bool(0);
            return true;

        case A_isChecksumCorrect:
        case A_isEntryPointCorrect:
        case A_isSectionAlignmentCorrect:
        case A_isFileAlignmentCorrect:
        case A_isHeaderCorrect:
        case A_isRelocsTableCorrect:
        case A_isImportTableCorrect:
        case A_isExportTableCorrect:
        case A_isResourcesTableCorrect:
        case A_isSectionsTableCorrect:
            *pResult = js_bool(1);
            return true;

        case A_getFormatMessages:
            *pResult = js_new_array(pCtx);
            return true;
        case A_getListOfCompressionMethods:
            *pResult = js_new_array(pCtx);
            return true;

        case A_startTiming: {
            /* The reference hands out a random 32-bit handle and keeps the
             * running timer in a map; a counter is the same contract without
             * the collisions. The handle is issued whether or not profiling
             * is on, so a script's endTiming() always finds it. */
            cd_i64 nHandle = ++pEngine->nNextTimingHandle;

            if (pEngine->nTimingCount >= pEngine->nTimingCapacity) {
                pEngine->nTimingCapacity = pEngine->nTimingCapacity ? (pEngine->nTimingCapacity * 2) : 8;
                pEngine->pTimings = (TimingRecord *)cd_realloc(pEngine->pTimings, (size_t)pEngine->nTimingCapacity * sizeof(TimingRecord));
            }

            pEngine->pTimings[pEngine->nTimingCount].nHandle = nHandle;
            pEngine->pTimings[pEngine->nTimingCount].nStart = die_engine_profile_start(pEngine);
            pEngine->nTimingCount++;

            *pResult = js_num((double)nHandle);
            return true;
        }

        case A_endTiming: {
            cd_i64 nHandle = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            char *pInfo = arg_string(pCtx, nArgc, pArgv, 1);
            int i = 0;

            for (i = 0; i < pEngine->nTimingCount; i++) {
                if (pEngine->pTimings[i].nHandle != nHandle) {
                    continue;
                }

                die_engine_profile_end(pEngine, pEngine->pTimings[i].nStart, "%s", pInfo);
                x_memmove(&pEngine->pTimings[i], &pEngine->pTimings[i + 1], (size_t)(pEngine->nTimingCount - i - 1) * sizeof(TimingRecord));
                pEngine->nTimingCount--;

                break;
            }

            cd_free(pInfo);

            *pResult = js_num(0);
            return true;
        }

        case A_detectZLIB:
        case A_detectGZIP:
        case A_detectZIP:
            *pResult = js_num(-1);
            return true;

        default:
            return false;
    }
}
