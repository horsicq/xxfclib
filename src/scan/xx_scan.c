/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/formats/xx_format.h"

const char *xx_scan_file_type_name(xx_file_type_t type)
{
    switch (type) {
        case XX_FILE_TYPE_UNKNOWN: return "Unknown";
        case XX_FILE_TYPE_BINARY: return "Binary";
        case XX_FILE_TYPE_COM: return "COM";
        case XX_FILE_TYPE_MSDOS: return "MSDOS";
        case XX_FILE_TYPE_NE: return "NE";
        case XX_FILE_TYPE_LE: return "LE";
        case XX_FILE_TYPE_LX: return "LX";
        case XX_FILE_TYPE_PE: return "PE";
        case XX_FILE_TYPE_PE32: return "PE32";
        case XX_FILE_TYPE_PE64: return "PE64";
        case XX_FILE_TYPE_ELF: return "ELF";
        case XX_FILE_TYPE_ELF32: return "ELF32";
        case XX_FILE_TYPE_ELF64: return "ELF64";
        case XX_FILE_TYPE_MACHO: return "Mach-O";
        case XX_FILE_TYPE_MACHO32: return "Mach-O32";
        case XX_FILE_TYPE_MACHO64: return "Mach-O64";
        case XX_FILE_TYPE_ZIP:
        case XX_FILE_TYPE_ZIP64: return "ZIP";
        case XX_FILE_TYPE_JAR: return "JAR";
        case XX_FILE_TYPE_APK: return "APK";
        case XX_FILE_TYPE_IPA: return "IPA";
        case XX_FILE_TYPE_DEX: return "DEX";
        case XX_FILE_TYPE_NPM: return "NPM";
        case XX_FILE_TYPE_MACHOFAT: return "Mach-O FAT";
        case XX_FILE_TYPE_ARCHIVE: return "Archive";
        case XX_FILE_TYPE_PDF: return "PDF";
        case XX_FILE_TYPE_CFBF: return "CFBF";
        case XX_FILE_TYPE_IMAGE: return "Image";
        case XX_FILE_TYPE_JPEG: return "JPEG";
        case XX_FILE_TYPE_PNG: return "PNG";
        case XX_FILE_TYPE_RAR: return "RAR";
        case XX_FILE_TYPE_ISO9660: return "ISO 9660";
        case XX_FILE_TYPE_AMIGAHUNK: return "Amiga Hunk";
        case XX_FILE_TYPE_ATARIST: return "Atari ST";
        case XX_FILE_TYPE_JAVA_CLASS: return "Java Class";
        case XX_FILE_TYPE_PYC: return "Python Bytecode";
        case XX_FILE_TYPE_DOS16M: return "DOS/16M";
        case XX_FILE_TYPE_DOS4G: return "DOS/4G";
        case XX_FILE_TYPE_DOTNET: return ".NET";
        default: return xx_format_file_type_to_string(type);
    }
}
