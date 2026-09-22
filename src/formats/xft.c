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

#include "../formats/xft.h"
#include "../formats/pyc/xpyc.h"



/* Display names, matching XBinary::fileTypeIdToString so that the printed
 * format line and the --showdatabase listing look exactly like diec.      */
static const char *g_pFileTypeNames[XFT_COUNT] = {
    "Unknown",  "Binary",  "COM",      "MSDOS",     "NE",         "LE",        "LX",         "PE",
    "PE32",     "PE64",    "ELF",      "ELF32",     "ELF64",      "Mach-O",    "Mach-O32",   "Mach-O64",
    "ZIP",      "JAR",     "APK",      "IPA",       "DEX",        "NPM",       "Mach-O FAT", "Archive",
    "PDF",      "CFBF",    "Image",    "JPEG",      "PNG",        "RAR",       "ISO 9660",   "Amiga Hunk",
    "Atari ST", "Java Class", "Python Bytecode",    "DOS/16M",    "DOS/4G",     ".NET"};

const char *xft_to_string(XFileType type)
{
    /* One unsigned comparison covers both ends: XFileType has no negative
     * enumerator, so a compiler that gives it an unsigned representation makes
     * "type >= 0" vacuous, and a negative value on a compiler that keeps it
     * signed wraps to a value above XFT_COUNT here.                          */
    if ((cd_u32)type < (cd_u32)XFT_COUNT) {
        return g_pFileTypeNames[type];
    }

    return "Unknown";
}

/* One unsigned comparison covers both ends of the bTypes[] index: XFileType has
 * no negative enumerator, so a compiler that gives it an unsigned
 * representation makes "type >= 0" vacuous, and a negative value on a compiler
 * that keeps it signed wraps to a value above XFT_COUNT here.               */
static void add(XFTSet *pSet, XFileType type)
{
    if ((cd_u32)type < (cd_u32)XFT_COUNT) {
        pSet->bTypes[type] = 1;
    }
}

int xft_contains(XFTSet *pSet, XFileType type)
{
    if ((cd_u32)type < (cd_u32)XFT_COUNT) {
        return pSet->bTypes[type];
    }

    return 0;
}

static int match(DieFile *pFile, cd_i64 nOffset, const char *pBytes, size_t nSize)
{
    if (nOffset + (cd_i64)nSize > pFile->nSize) {
        return 0;
    }

    return (x_memcmp(pFile->pData + nOffset, pBytes, nSize) == 0) ? 1 : 0;
}

/* XBinary::getFileTypes' fat validation: a CAFEBABE header only counts as a
 * Mach-O fat binary when the whole fat_header/fat_arch table describes slices
 * that actually fit in the file. Only the 32-bit big-endian layout can reach
 * here, which is the one that collides with the Java class magic. */
static int is_macho_fat(DieFile *pFile)
{
    cd_u32 nFatRecords = 0;
    cd_u64 nFatTableEnd = 0;
    cd_u64 nFileSize = 0;
    cd_u32 i = 0;

    if (pFile->nSize < 8) {
        return 0;
    }

    nFileSize = (cd_u64)pFile->nSize;
    nFatRecords = xx_io_get_u32(pFile->pDevice, 4, true);

    if ((nFatRecords == 0) || (nFatRecords > 1000000)) {
        return 0;
    }

    if ((cd_u64)nFatRecords > (cd_u64)((pFile->nSize - 8) / 20)) {
        return 0;
    }

    nFatTableEnd = 8 + (cd_u64)nFatRecords * 20;

    for (i = 0; i < nFatRecords; i++) {
        cd_i64 nRecord = 8 + (cd_i64)i * 20;
        cd_u32 nCpuType = xx_io_get_u32(pFile->pDevice, nRecord, true);
        cd_u64 nArchOffset = xx_io_get_u32(pFile->pDevice, nRecord + 8, true);
        cd_u64 nArchSize = xx_io_get_u32(pFile->pDevice, nRecord + 12, true);
        cd_u32 nAlign = xx_io_get_u32(pFile->pDevice, nRecord + 16, true);
        cd_u64 nAlignMask = 0;

        if (nAlign > 63) {
            return 0;
        }

        if (nAlign != 0) {
            nAlignMask = ((cd_u64)1 << nAlign) - 1;
        }

        if ((nCpuType == 0) || (nArchSize == 0) || (nArchOffset < nFatTableEnd) || ((nArchOffset & nAlignMask) != 0) || (nArchOffset > nFileSize) ||
            (nArchSize > (nFileSize - nArchOffset))) {
            return 0;
        }
    }

    return 1;
}

static void detect_zip_family(DieFile *pFile, XFTSet *pSet)
{
    add(pSet, XFT_ZIP);
    add(pSet, XFT_ARCHIVE);

    /* Classify by looking for the marker entries. The names may live in the
     * central directory at the end of the archive, so the whole file is
     * searched rather than only its head.                                  */
    if (die_find_ansi_string(pFile, 0, -1, "AndroidManifest.xml") != -1) {
        add(pSet, XFT_APK);
        add(pSet, XFT_JAR);
    } else if (die_find_ansi_string(pFile, 0, -1, "META-INF/MANIFEST.MF") != -1) {
        add(pSet, XFT_JAR);
    } else if (die_find_ansi_string(pFile, 0, -1, "Payload/") != -1) {
        add(pSet, XFT_IPA);
    } else if (die_find_ansi_string(pFile, 0, -1, "package/package.json") != -1) {
        add(pSet, XFT_NPM);
    }
}

void xft_detect(DieFile *pFile, XFTSet *pSet)
{
    x_memset(pSet, 0, sizeof(*pSet));

    add(pSet, XFT_BINARY);

    if (pFile->nSize < 4) {
        return;
    }

    /* MZ family */
    if (match(pFile, 0, "MZ", 2) || match(pFile, 0, "ZM", 2)) {
        cd_i64 nLfanew = (cd_i64)xx_io_get_u32(pFile->pDevice, 0x3C, false);

        add(pSet, XFT_MSDOS);

        if ((nLfanew > 0) && (nLfanew + 4 <= pFile->nSize)) {
            if (xx_io_get_u32(pFile->pDevice, nLfanew, false) == 0x00004550) { /* PE\0\0 */
                cd_u16 nMagic = xx_io_get_u16(pFile->pDevice, nLfanew + 24, false);

                add(pSet, XFT_PE);

                if (nMagic == 0x20B) {
                    add(pSet, XFT_PE64);
                } else {
                    add(pSet, XFT_PE32);
                }

                /* .NET / CLI assembly: a non-empty COM(CLR) descriptor data
                 * directory (entry 14). Mirrors XBinary::getFileTypes, which
                 * inserts FT_CLI_ASSEMBLY alongside FT_PE32/64.               */
                {
                    cd_i64 nDataDir = -1;
                    cd_u32 nNumberOfRvaAndSizes = 0;

                    if (nMagic == 0x10B) { /* PE32 */
                        nNumberOfRvaAndSizes = xx_io_get_u32(pFile->pDevice, nLfanew + 24 + 92, false);
                        nDataDir = nLfanew + 24 + 96;
                    } else if (nMagic == 0x20B) { /* PE32+ */
                        nNumberOfRvaAndSizes = xx_io_get_u32(pFile->pDevice, nLfanew + 24 + 108, false);
                        nDataDir = nLfanew + 24 + 112;
                    }

                    if ((nDataDir != -1) && (nNumberOfRvaAndSizes > 14)) {
                        cd_u32 nCliRva = xx_io_get_u32(pFile->pDevice, nDataDir + 14 * 8, false);
                        cd_u32 nCliSize = xx_io_get_u32(pFile->pDevice, nDataDir + 14 * 8 + 4, false);

                        if (nCliRva && nCliSize) {
                            add(pSet, XFT_CLI_ASSEMBLY);
                        }
                    }
                }
            } else if (match(pFile, nLfanew, "NE", 2)) {
                add(pSet, XFT_NE);
            } else if (match(pFile, nLfanew, "LE", 2)) {
                add(pSet, XFT_LE);
            } else if (match(pFile, nLfanew, "LX", 2)) {
                add(pSet, XFT_LX);
            }
        }

        return;
    }

    if (match(pFile, 0, "\x7f" "ELF", 4)) {
        add(pSet, XFT_ELF);

        if (xx_io_get_u8(pFile->pDevice, 4) == 2) {
            add(pSet, XFT_ELF64);
        } else {
            add(pSet, XFT_ELF32);
        }

        return;
    }

    if (match(pFile, 0, "\xCA\xFE\xBA\xBE", 4)) {
        /* Java class files and Mach-O fat binaries share this magic. The
         * reference validates the fat table first and only then reads a word at
         * offset 4 above 10 (the class file's minor/major version) as a Java
         * class; anything else stays plain Binary. */
        if (is_macho_fat(pFile)) {
            add(pSet, XFT_MACHOFAT);

            return;
        }

        if ((pFile->nSize >= 24) && (xx_io_get_u32(pFile->pDevice, 4, true) > 10)) {
            add(pSet, XFT_JAVACLASS);

            return;
        }
    }

    if (match(pFile, 0, "\xFE\xED\xFA\xCE", 4) || match(pFile, 0, "\xCE\xFA\xED\xFE", 4)) {
        add(pSet, XFT_MACHO);
        add(pSet, XFT_MACHO32);

        return;
    }

    if (match(pFile, 0, "\xFE\xED\xFA\xCF", 4) || match(pFile, 0, "\xCF\xFA\xED\xFE", 4)) {
        add(pSet, XFT_MACHO);
        add(pSet, XFT_MACHO64);

        return;
    }

    if (match(pFile, 0, "PK\x03\x04", 4) || match(pFile, 0, "PK\x05\x06", 4)) {
        detect_zip_family(pFile, pSet);

        return;
    }

    if (match(pFile, 0, "dex\n", 4)) {
        add(pSet, XFT_DEX);

        return;
    }

    if (match(pFile, 0, "%PDF", 4)) {
        add(pSet, XFT_PDF);

        return;
    }

    if (match(pFile, 0, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8)) {
        add(pSet, XFT_CFBF);

        return;
    }

    if (match(pFile, 0, "\x89PNG\r\n\x1a\n", 8)) {
        add(pSet, XFT_PNG);
        add(pSet, XFT_IMAGE);

        return;
    }

    if (match(pFile, 0, "\xFF\xD8\xFF", 3)) {
        add(pSet, XFT_JPEG);
        add(pSet, XFT_IMAGE);

        return;
    }

    if (match(pFile, 0, "Rar!\x1a\x07", 6)) {
        add(pSet, XFT_RAR);
        add(pSet, XFT_ARCHIVE);

        return;
    }

    if (match(pFile, 0, "\x00\x00\x03\xF3", 4)) {
        add(pSet, XFT_AMIGAHUNK);

        return;
    }

    if ((pFile->nSize > 0x8006) && match(pFile, 0x8001, "CD001", 5)) {
        add(pSet, XFT_ISO9660);

        return;
    }

    /* Python bytecode: 2 byte magic + "\r\n" marker at offset 2, and the magic
     * must be a known interpreter release. Mirrors XBinary::getFileTypes, whose
     * FT_PYC branch validates the magic through XPYC (getFileTypes' own range
     * check is a superset that the shipped diec narrows to a known magic).    */
    if ((pFile->nSize >= 12) && (xx_io_get_u16(pFile->pDevice, 2, false) == 0x0A0D) && xpyc_is_known_magic(xx_io_get_u16(pFile->pDevice, 0, false))) {
        add(pSet, XFT_PYC);
    }

    /* Anything else stays plain Binary; small files may also be COM. */
    if ((pFile->nSize > 0) && (pFile->nSize <= 0x10000)) {
        add(pSet, XFT_COM);
    }
}

int xft_check(XFileType databaseType, XFileType fileType)
{
    if (databaseType == fileType) {
        return 1;
    }

    if ((databaseType == XFT_PE) && ((fileType == XFT_PE32) || (fileType == XFT_PE64))) {
        return 1;
    }

    if ((databaseType == XFT_ELF) && ((fileType == XFT_ELF32) || (fileType == XFT_ELF64))) {
        return 1;
    }

    if ((databaseType == XFT_MACHO) && ((fileType == XFT_MACHO32) || (fileType == XFT_MACHO64))) {
        return 1;
    }

    return 0;
}
