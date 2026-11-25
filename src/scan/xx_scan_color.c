/* Copyright (c) 2024-2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/* Ported from XScanEngine/xscanengine.cpp typeToColorRecord/isProtection and
 * XScanEngine/xscanengineoptionswidget.cpp setDefaultValues (local Qt reference).
 * Keep category aliases, precedence and both palettes identical to upstream. */
#include "xxfclib/scan/xx_scan_color.h"
#include <stddef.h>

typedef struct color_entry {
    const char *type;
    xx_scan_color_category category;
    xx_scan_color_record light, dark;
} color_entry;
static const color_entry entries[] = {
    {"installer", XX_SCAN_COLOR_INSTALLER, {0x008000, UINT32_MAX}, {0x00ffff, UINT32_MAX}},
    {"sfx", XX_SCAN_COLOR_SFX, {0x008000, UINT32_MAX}, {0x00ffff, UINT32_MAX}},
    {"archive", XX_SCAN_COLOR_ARCHIVE, {0x008000, UINT32_MAX}, {0x00ffff, UINT32_MAX}},
    {"protection", XX_SCAN_COLOR_PROTECTION, {0x800000, UINT32_MAX}, {0xff0000, UINT32_MAX}},
    {"pe tool", XX_SCAN_COLOR_PETOOL, {0x008000, UINT32_MAX}, {0x00ff00, UINT32_MAX}},
    {"apk tool", XX_SCAN_COLOR_APKTOOL, {0x008000, UINT32_MAX}, {0x00ff00, UINT32_MAX}},
    {"operation system", XX_SCAN_COLOR_OS, {0x808000, UINT32_MAX}, {0xffff00, UINT32_MAX}},
    {"virtual machine", XX_SCAN_COLOR_VM, {0x808000, UINT32_MAX}, {0xffff00, UINT32_MAX}},
    {"platform", XX_SCAN_COLOR_PLATFORM, {0x808000, UINT32_MAX}, {0xffff00, UINT32_MAX}},
    {"dos extender", XX_SCAN_COLOR_DOSEXTENDER, {0x808000, UINT32_MAX}, {0xffff00, UINT32_MAX}},
    {"format", XX_SCAN_COLOR_FORMAT, {0x008000, UINT32_MAX}, {0x00ff00, UINT32_MAX}},
    {"sign tool", XX_SCAN_COLOR_SIGNTOOL, {0x808080, UINT32_MAX}, {0xc0c0c0, UINT32_MAX}},
    {"certificate", XX_SCAN_COLOR_CERTIFICATE, {0x808080, UINT32_MAX}, {0xc0c0c0, UINT32_MAX}},
    {"licensing", XX_SCAN_COLOR_LICENSING, {0x808080, UINT32_MAX}, {0xc0c0c0, UINT32_MAX}},
    {"language", XX_SCAN_COLOR_LANGUAGE, {0x008080, UINT32_MAX}, {0x00ffff, UINT32_MAX}},
    {"corrupted data", XX_SCAN_COLOR_CORRUPTEDDATA, {0x800000, UINT32_MAX}, {0xff0000, UINT32_MAX}},
    {"personal data", XX_SCAN_COLOR_PERSONALDATA, {0x800000, UINT32_MAX}, {0xff0000, UINT32_MAX}},
    {"author", XX_SCAN_COLOR_AUTHOR, {0x800000, UINT32_MAX}, {0xff0000, UINT32_MAX}},
    {"virus", XX_SCAN_COLOR_VIRUS, {0xffffff, 0x800000}, {0xffffff, 0xff0000}},
    {"trojan", XX_SCAN_COLOR_TROJAN, {0xffffff, 0x800000}, {0xffffff, 0xff0000}},
    {"malware", XX_SCAN_COLOR_MALWARE, {0xffffff, 0x800000}, {0xffffff, 0xff0000}},
    {"debug", XX_SCAN_COLOR_DEBUG, {0x000080, UINT32_MAX}, {0xffff00, UINT32_MAX}},
    {"debug data", XX_SCAN_COLOR_DEBUGDATA, {0x000080, UINT32_MAX}, {0xffff00, UINT32_MAX}},
    {"game engine", XX_SCAN_COLOR_GAMEENGINE, {0x008000, UINT32_MAX}, {0x00ff00, UINT32_MAX}},
    {"compiler", XX_SCAN_COLOR_COMPILER, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"compressor", XX_SCAN_COLOR_COMPRESSOR, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"converter", XX_SCAN_COLOR_CONVERTER, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"creator", XX_SCAN_COLOR_CREATOR, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"data", XX_SCAN_COLOR_DATA, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"database", XX_SCAN_COLOR_DATABASE, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"document", XX_SCAN_COLOR_DOCUMENT, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"generic", XX_SCAN_COLOR_GENERIC, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"image", XX_SCAN_COLOR_IMAGE, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"installer data", XX_SCAN_COLOR_INSTALLERDATA, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"library", XX_SCAN_COLOR_LIBRARY, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"linker", XX_SCAN_COLOR_LINKER, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"loader", XX_SCAN_COLOR_LOADER, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"obfuscator", XX_SCAN_COLOR_OBFUSCATOR, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"overlay", XX_SCAN_COLOR_OVERLAY, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"package", XX_SCAN_COLOR_PACKAGE, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"player", XX_SCAN_COLOR_PLAYER, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"producer", XX_SCAN_COLOR_PRODUCER, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"protector data", XX_SCAN_COLOR_PROTECTORDATA, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"rom", XX_SCAN_COLOR_ROM, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"sfx data", XX_SCAN_COLOR_SFXDATA, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"source code", XX_SCAN_COLOR_SOURCECODE, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"stub", XX_SCAN_COLOR_STUB, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
    {"tool", XX_SCAN_COLOR_TOOL, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
};
static const char *const protection_aliases[] = {"protector", "apk obfuscator", "jar obfuscator", ".net obfuscator", ".net compressor", "dongle protection",
                                                 "joiner",    "packer",         "protection",     "crypter",         "cryptor"};

static int category_equal(const char *type, const char *category)
{
    if (!type) return 0;
    for (;;) {
        unsigned char c;
        while (*type == '~' || *type == '!') ++type;
        c = (unsigned char)*type++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)*category++) return 0;
        if (!c) return 1;
    }
}

xx_scan_color_category xx_scan_type_to_color_category(const char *type)
{
    size_t i;
    for (i = 0; i < sizeof(protection_aliases) / sizeof(protection_aliases[0]); ++i)
        if (category_equal(type, protection_aliases[i])) return XX_SCAN_COLOR_PROTECTION;
    for (i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
        if (category_equal(type, entries[i].type)) return entries[i].category;
    return XX_SCAN_COLOR_UNKNOWN;
}

void xx_scan_color_options_init(xx_scan_color_options *options, int dark_mode)
{
    size_t i;
    if (!options) return;
    for (i = 0; i < XX_SCAN_COLOR_COUNT; ++i) options->records[i] = (xx_scan_color_record){UINT32_MAX, UINT32_MAX};
    for (i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) options->records[entries[i].category] = dark_mode ? entries[i].dark : entries[i].light;
}

xx_scan_color_record xx_scan_type_to_color_record(const char *type, const xx_scan_color_options *options)
{
    xx_scan_color_category category = xx_scan_type_to_color_category(type);
    if (options && category != XX_SCAN_COLOR_UNKNOWN) return options->records[category];
    return (xx_scan_color_record){UINT32_MAX, UINT32_MAX};
}

xx_scan_color_record xx_scan_default_color(const char *type, int dark_mode)
{
    xx_scan_color_options options;
    xx_scan_color_options_init(&options, dark_mode);
    return xx_scan_type_to_color_record(type, &options);
}
