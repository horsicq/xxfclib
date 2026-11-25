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
/* C port of XScanEngine::typeToColorRecord/isProtection and the defaults in
 * XScanEngineOptionsWidget::setDefaultValues. No Qt or runtime dependency. */
#ifndef XX_SCAN_COLOR_H
#define XX_SCAN_COLOR_H
#include <stdint.h>
#include "xxfclib/xxfc_defs.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_scan_color_category {
    XX_SCAN_COLOR_UNKNOWN = 0,
    XX_SCAN_COLOR_INSTALLER,
    XX_SCAN_COLOR_SFX,
    XX_SCAN_COLOR_ARCHIVE,
    XX_SCAN_COLOR_PROTECTION,
    XX_SCAN_COLOR_PETOOL,
    XX_SCAN_COLOR_APKTOOL,
    XX_SCAN_COLOR_OS,
    XX_SCAN_COLOR_VM,
    XX_SCAN_COLOR_PLATFORM,
    XX_SCAN_COLOR_DOSEXTENDER,
    XX_SCAN_COLOR_FORMAT,
    XX_SCAN_COLOR_SIGNTOOL,
    XX_SCAN_COLOR_CERTIFICATE,
    XX_SCAN_COLOR_LICENSING,
    XX_SCAN_COLOR_LANGUAGE,
    XX_SCAN_COLOR_CORRUPTEDDATA,
    XX_SCAN_COLOR_PERSONALDATA,
    XX_SCAN_COLOR_AUTHOR,
    XX_SCAN_COLOR_VIRUS,
    XX_SCAN_COLOR_TROJAN,
    XX_SCAN_COLOR_MALWARE,
    XX_SCAN_COLOR_DEBUG,
    XX_SCAN_COLOR_DEBUGDATA,
    XX_SCAN_COLOR_GAMEENGINE,
    XX_SCAN_COLOR_COMPILER,
    XX_SCAN_COLOR_COMPRESSOR,
    XX_SCAN_COLOR_CONVERTER,
    XX_SCAN_COLOR_CREATOR,
    XX_SCAN_COLOR_DATA,
    XX_SCAN_COLOR_DATABASE,
    XX_SCAN_COLOR_DOCUMENT,
    XX_SCAN_COLOR_GENERIC,
    XX_SCAN_COLOR_IMAGE,
    XX_SCAN_COLOR_INSTALLERDATA,
    XX_SCAN_COLOR_LIBRARY,
    XX_SCAN_COLOR_LINKER,
    XX_SCAN_COLOR_LOADER,
    XX_SCAN_COLOR_OBFUSCATOR,
    XX_SCAN_COLOR_OVERLAY,
    XX_SCAN_COLOR_PACKAGE,
    XX_SCAN_COLOR_PLAYER,
    XX_SCAN_COLOR_PRODUCER,
    XX_SCAN_COLOR_PROTECTORDATA,
    XX_SCAN_COLOR_ROM,
    XX_SCAN_COLOR_SFXDATA,
    XX_SCAN_COLOR_SOURCECODE,
    XX_SCAN_COLOR_STUB,
    XX_SCAN_COLOR_TOOL,
    XX_SCAN_COLOR_COUNT
} xx_scan_color_category;

/* 0xRRGGBB, or UINT32_MAX to retain the UI's default color. */
typedef struct xx_scan_color_record {
    uint32_t foreground, background;
} xx_scan_color_record;
typedef struct xx_scan_color_options {
    xx_scan_color_record records[XX_SCAN_COLOR_COUNT];
} xx_scan_color_options;

/* Initialize the upstream light/dark defaults, then override any category. */
XXFC_API void xx_scan_color_options_init(xx_scan_color_options *options, int dark_mode);
/* Case-insensitive ASCII category matching, ignoring ~ and !, as upstream. */
XXFC_API xx_scan_color_category xx_scan_type_to_color_category(const char *type);
/* NULL options or an unknown category returns the UI's default colors. */
XXFC_API xx_scan_color_record xx_scan_type_to_color_record(const char *type, const xx_scan_color_options *options);
XXFC_API xx_scan_color_record xx_scan_default_color(const char *type, int dark_mode);
#ifdef __cplusplus
}
#endif
#endif
