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

#include "xxfclib/die_engine/die_engine.h"
#include "../scan/xx_scan_types_internal.h"
#include "xxfclib/fs/xx_fs.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/rt/xx_rt.h"

static const char *die_file_type_names[XFT_COUNT] = {
    "Unknown",    "Binary",          "COM",     "MSDOS",    "NE",       "LE",   "LX",  "PE",  "PE32",     "PE64",       "ELF",
    "ELF32",      "ELF64",           "Mach-O",  "Mach-O32", "Mach-O64", "ZIP",  "JAR", "APK", "IPA",      "DEX",        "NPM",
    "Mach-O FAT", "Archive",         "PDF",     "CFBF",     "Image",    "JPEG", "PNG", "RAR", "ISO 9660", "Amiga Hunk", "Atari ST",
    "Java Class", "Python Bytecode", "DOS/16M", "DOS/4G",   ".NET"};

const char *xft_to_string(XFileType type)
{
    if ((uint32_t)type < (uint32_t)XFT_COUNT) return die_file_type_names[type];
    return "Unknown";
}

int xft_check(XFileType databaseType, XFileType fileType)
{
    if (databaseType == fileType) return 1;
    if (databaseType == XFT_PE && (fileType == XFT_PE32 || fileType == XFT_PE64)) return 1;
    if (databaseType == XFT_ELF && (fileType == XFT_ELF32 || fileType == XFT_ELF64)) return 1;
    if (databaseType == XFT_MACHO && (fileType == XFT_MACHO32 || fileType == XFT_MACHO64)) return 1;
    return 0;
}

static void xx_scan_file_type_set_add(xx_scan_file_type_set *set, XFileType type)
{
    if ((uint32_t)type < (uint32_t)XFT_COUNT) set->types[type] = true;
}

bool xx_scan_file_type_set_contains(const xx_scan_file_type_set *set, XFileType type)
{
    return set && (uint32_t)type < (uint32_t)XFT_COUNT && set->types[type];
}

bool xx_scan_file_type_set_detect(xx_io_device *device, const char *file_name, xx_scan_file_type_set *set)
{
    xx_scan_options options;
    xx_list_t *types;
    size_t index;
    if (!device || !set) return false;
    xx_mem_zero(set, sizeof(*set));
    xx_scan_file_type_set_add(set, XFT_BINARY);
    xx_scan_options_init(&options);
    options.file_name = file_name;
    types = xx_scan_detect_type(device, &options, NULL);
    if (!types) return false;
    for (index = 0; index < xx_list_count(types); ++index) {
        xx_file_type_t type = *(xx_file_type_t *)xx_list_at(types, index);
        switch (type) {
            case XX_FILE_TYPE_MSDOS: xx_scan_file_type_set_add(set, XFT_MSDOS); break;
            case XX_FILE_TYPE_PE: xx_scan_file_type_set_add(set, XFT_PE); break;
            case XX_FILE_TYPE_ELF: xx_scan_file_type_set_add(set, XFT_ELF); break;
            case XX_FILE_TYPE_MACHO: xx_scan_file_type_set_add(set, XFT_MACHO); break;
            case XX_FILE_TYPE_ARCHIVE: xx_scan_file_type_set_add(set, XFT_ARCHIVE); break;
            case XX_FILE_TYPE_IMAGE: xx_scan_file_type_set_add(set, XFT_IMAGE); break;
            case XX_FILE_TYPE_DOTNET: {
                /* The detected list already includes the PE32/PE64 carrier. */
                xx_scan_file_type_set_add(set, XFT_MSDOS);
                xx_scan_file_type_set_add(set, XFT_PE);
                xx_scan_file_type_set_add(set, XFT_CLI_ASSEMBLY);
                break;
            }
            case XX_FILE_TYPE_PE32:
                xx_scan_file_type_set_add(set, XFT_MSDOS);
                xx_scan_file_type_set_add(set, XFT_PE);
                xx_scan_file_type_set_add(set, XFT_PE32);
                break;
            case XX_FILE_TYPE_PE64:
                xx_scan_file_type_set_add(set, XFT_MSDOS);
                xx_scan_file_type_set_add(set, XFT_PE);
                xx_scan_file_type_set_add(set, XFT_PE64);
                break;
            case XX_FILE_TYPE_ELF32:
                xx_scan_file_type_set_add(set, XFT_ELF);
                xx_scan_file_type_set_add(set, XFT_ELF32);
                break;
            case XX_FILE_TYPE_ELF64:
                xx_scan_file_type_set_add(set, XFT_ELF);
                xx_scan_file_type_set_add(set, XFT_ELF64);
                break;
            case XX_FILE_TYPE_MACHO32:
                xx_scan_file_type_set_add(set, XFT_MACHO);
                xx_scan_file_type_set_add(set, XFT_MACHO32);
                break;
            case XX_FILE_TYPE_MACHO64:
                xx_scan_file_type_set_add(set, XFT_MACHO);
                xx_scan_file_type_set_add(set, XFT_MACHO64);
                break;
            case XX_FILE_TYPE_ZIP:
                xx_scan_file_type_set_add(set, XFT_ZIP);
                xx_scan_file_type_set_add(set, XFT_ARCHIVE);
                break;
            case XX_FILE_TYPE_JAR:
                xx_scan_file_type_set_add(set, XFT_ZIP);
                xx_scan_file_type_set_add(set, XFT_ARCHIVE);
                xx_scan_file_type_set_add(set, XFT_JAR);
                break;
            case XX_FILE_TYPE_APK:
                xx_scan_file_type_set_add(set, XFT_ZIP);
                xx_scan_file_type_set_add(set, XFT_ARCHIVE);
                xx_scan_file_type_set_add(set, XFT_APK);
                xx_scan_file_type_set_add(set, XFT_JAR);
                break;
            case XX_FILE_TYPE_IPA:
                xx_scan_file_type_set_add(set, XFT_ZIP);
                xx_scan_file_type_set_add(set, XFT_ARCHIVE);
                xx_scan_file_type_set_add(set, XFT_IPA);
                break;
            case XX_FILE_TYPE_NPM:
                xx_scan_file_type_set_add(set, XFT_NPM);
                xx_scan_file_type_set_add(set, XFT_ARCHIVE);
                break;
            case XX_FILE_TYPE_DOS16M: xx_scan_file_type_set_add(set, XFT_DOS16M); break;
            case XX_FILE_TYPE_DOS4G: xx_scan_file_type_set_add(set, XFT_DOS4G); break;
            case XX_FILE_TYPE_NE: xx_scan_file_type_set_add(set, XFT_NE); break;
            case XX_FILE_TYPE_LE: xx_scan_file_type_set_add(set, XFT_LE); break;
            case XX_FILE_TYPE_LX: xx_scan_file_type_set_add(set, XFT_LX); break;
            case XX_FILE_TYPE_MACHOFAT: xx_scan_file_type_set_add(set, XFT_MACHOFAT); break;
            case XX_FILE_TYPE_JAVA_CLASS: xx_scan_file_type_set_add(set, XFT_JAVACLASS); break;
            case XX_FILE_TYPE_DEX: xx_scan_file_type_set_add(set, XFT_DEX); break;
            case XX_FILE_TYPE_PYC: xx_scan_file_type_set_add(set, XFT_PYC); break;
            case XX_FILE_TYPE_PDF: xx_scan_file_type_set_add(set, XFT_PDF); break;
            case XX_FILE_TYPE_CFBF: xx_scan_file_type_set_add(set, XFT_CFBF); break;
            case XX_FILE_TYPE_JPEG:
                xx_scan_file_type_set_add(set, XFT_JPEG);
                xx_scan_file_type_set_add(set, XFT_IMAGE);
                break;
            case XX_FILE_TYPE_PNG:
                xx_scan_file_type_set_add(set, XFT_PNG);
                xx_scan_file_type_set_add(set, XFT_IMAGE);
                break;
            case XX_FILE_TYPE_RAR:
                xx_scan_file_type_set_add(set, XFT_RAR);
                xx_scan_file_type_set_add(set, XFT_ARCHIVE);
                break;
            case XX_FILE_TYPE_ISO9660: xx_scan_file_type_set_add(set, XFT_ISO9660); break;
            case XX_FILE_TYPE_AMIGAHUNK: xx_scan_file_type_set_add(set, XFT_AMIGAHUNK); break;
            case XX_FILE_TYPE_ATARIST: xx_scan_file_type_set_add(set, XFT_ATARIST); break;
            case XX_FILE_TYPE_COM: xx_scan_file_type_set_add(set, XFT_COM); break;
            default: break;
        }
    }
    xx_list_destroy(types);
    return true;
}

XFileType xx_scan_pick_file_type(xx_io_device *device, const char *file_name, const xx_scan_file_type_set *pSet)
{
    int64_t size;
    if (!device || !pSet) return XFT_BINARY;
    if (xx_scan_file_type_set_contains(pSet, XFT_PE32)) return XFT_PE32;
    if (xx_scan_file_type_set_contains(pSet, XFT_PE64)) return XFT_PE64;
    if (xx_scan_file_type_set_contains(pSet, XFT_ELF32)) return XFT_ELF32;
    if (xx_scan_file_type_set_contains(pSet, XFT_ELF64)) return XFT_ELF64;
    if (xx_scan_file_type_set_contains(pSet, XFT_MACHO32)) return XFT_MACHO32;
    if (xx_scan_file_type_set_contains(pSet, XFT_MACHO64)) return XFT_MACHO64;
    if (xx_scan_file_type_set_contains(pSet, XFT_LX)) return XFT_LX;
    if (xx_scan_file_type_set_contains(pSet, XFT_LE)) return XFT_LE;
    if (xx_scan_file_type_set_contains(pSet, XFT_NE)) return XFT_NE;
    if (xx_scan_file_type_set_contains(pSet, XFT_DOS16M)) return XFT_DOS16M;
    if (xx_scan_file_type_set_contains(pSet, XFT_DOS4G)) return XFT_DOS4G;
    if (xx_scan_file_type_set_contains(pSet, XFT_MSDOS)) return XFT_MSDOS;
    if (xx_scan_file_type_set_contains(pSet, XFT_APK)) return XFT_APK;
    if (xx_scan_file_type_set_contains(pSet, XFT_IPA)) return XFT_IPA;
    if (xx_scan_file_type_set_contains(pSet, XFT_JAR)) return XFT_JAR;
    if (xx_scan_file_type_set_contains(pSet, XFT_ZIP)) return XFT_ZIP;
    if (xx_scan_file_type_set_contains(pSet, XFT_DEX)) return XFT_DEX;
    if (xx_scan_file_type_set_contains(pSet, XFT_NPM)) return XFT_NPM;
    if (xx_scan_file_type_set_contains(pSet, XFT_MACHOFAT)) return XFT_MACHOFAT;
    if (xx_scan_file_type_set_contains(pSet, XFT_AMIGAHUNK)) return XFT_AMIGAHUNK;
    if (xx_scan_file_type_set_contains(pSet, XFT_PDF)) return XFT_PDF;
    if (xx_scan_file_type_set_contains(pSet, XFT_CFBF)) return XFT_CFBF;
    if (xx_scan_file_type_set_contains(pSet, XFT_RAR)) return XFT_RAR;
    if (xx_scan_file_type_set_contains(pSet, XFT_ISO9660)) return XFT_ISO9660;
    if (xx_scan_file_type_set_contains(pSet, XFT_JPEG)) return XFT_JPEG;
    if (xx_scan_file_type_set_contains(pSet, XFT_PNG)) return XFT_PNG;
    if (xx_scan_file_type_set_contains(pSet, XFT_JAVACLASS)) return XFT_JAVACLASS;
    if (xx_scan_file_type_set_contains(pSet, XFT_PYC)) return XFT_PYC;

    /* g_arrPrefFileTypeOrder puts FT_COM ahead of FT_TEXT/FT_DATA/FT_BINARY,
     * and XBinary::getFileTypes inserts FT_COM only when nothing else was
     * recognised (the set still holds just FT_BINARY) or the file is plain
     * text, the size leaves room for the 0x100-byte PSP, and the file suffix
     * uppercases to "COM". Reaching the fallback below is this port's form of
     * that first condition. A file without the suffix still meets the COM
     * scripts: scan_engine_run's binary arm runs them the way the reference's
     * last scanProcess branch does.                                        */
    size = xx_io_total_size(device);
    if (size >= 0 && size <= XX_SCAN_COM_MAX_SIZE && xx_scan_has_com_suffix(file_name, false)) {
        return XFT_COM;
    }

    return XFT_BINARY;
}
