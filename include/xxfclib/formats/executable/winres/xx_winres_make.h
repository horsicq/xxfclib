/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://learn.microsoft.com/en-us/windows/win32/menurc/resource-file-formats
 * Builders for binary Win32 resource payloads (the data of one resource as
 * stored in a PE resource directory or a .res entry). Strings are UTF-8 and
 * are stored as UTF-16LE.
 */
#ifndef XX_WINRES_MAKE_H
#define XX_WINRES_MAKE_H

#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One StringFileInfo value. */
typedef struct xx_winres_version_string {
    const char *key;
    const char *value;
} xx_winres_version_string;

typedef struct xx_winres_version_info {
    uint16_t file_version[4];    /**< FILEVERSION a,b,c,d */
    uint16_t product_version[4]; /**< PRODUCTVERSION a,b,c,d */
    uint32_t file_flags_mask;
    uint32_t file_flags;
    uint32_t file_os;   /**< e.g. 0x40004 (VOS_NT_WINDOWS32) */
    uint32_t file_type; /**< e.g. 1 (VFT_APP) */
    uint32_t file_subtype;
    uint16_t language;  /**< StringFileInfo / Translation language */
    uint16_t code_page; /**< e.g. 0x04B0 (Unicode) */
    const xx_winres_version_string *strings;
    size_t string_count;
} xx_winres_version_info;

/** RT_VERSION: VS_VERSIONINFO with VS_FIXEDFILEINFO, one StringFileInfo table
 * and a VarFileInfo Translation entry. */
XXFC_API bool xx_winres_make_version(xx_buf_t *out, const xx_winres_version_info *info);

/** RT_STRING block: 16 counted strings; NULL or "" entries are empty. The
 * block named N holds string ids (N - 1) * 16 .. (N - 1) * 16 + 15. */
XXFC_API bool xx_winres_make_string_table(xx_buf_t *out, const char *const strings[16]);

#define XX_WINRES_MF_GRAYED 0x0001U
#define XX_WINRES_MF_CHECKED 0x0008U
#define XX_WINRES_MF_POPUP 0x0010U
#define XX_WINRES_MF_END 0x0080U

/** One item of a standard menu template, in template order. Popups (flag
 * MF_POPUP) are followed by their children; MF_END marks the last item of
 * each level. A separator is flags 0, id 0 and an empty text. */
typedef struct xx_winres_menu_item {
    uint16_t flags;
    uint16_t id; /**< ignored for popups */
    const char *text;
} xx_winres_menu_item;

/** RT_MENU: MENUHEADER (version 0) followed by the items. */
XXFC_API bool xx_winres_make_menu(xx_buf_t *out, const xx_winres_menu_item *items, size_t count);

/** Predefined control classes (sz_Or_Ord atoms). */
#define XX_WINRES_CLASS_BUTTON 0x0080U
#define XX_WINRES_CLASS_EDIT 0x0081U
#define XX_WINRES_CLASS_STATIC 0x0082U
#define XX_WINRES_CLASS_LISTBOX 0x0083U
#define XX_WINRES_CLASS_SCROLLBAR 0x0084U
#define XX_WINRES_CLASS_COMBOBOX 0x0085U

typedef struct xx_winres_dialog_control {
    uint32_t style;
    uint32_t ex_style;
    int16_t x, y, cx, cy;
    uint16_t id;
    uint16_t class_atom; /**< XX_WINRES_CLASS_* */
    const char *text;
} xx_winres_dialog_control;

typedef struct xx_winres_dialog_template {
    uint32_t style; /**< DS_SETFONT (0x40) adds point_size and face */
    uint32_t ex_style;
    int16_t x, y, cx, cy;
    const char *caption;
    uint16_t point_size;
    const char *face;
    const xx_winres_dialog_control *controls;
    size_t control_count;
} xx_winres_dialog_template;

/** RT_DIALOG: a classic DLGTEMPLATE (no menu or class) and its items. */
XXFC_API bool xx_winres_make_dialog(xx_buf_t *out, const xx_winres_dialog_template *dialog);

#define XX_WINRES_FVIRTKEY 0x01U
#define XX_WINRES_FNOINVERT 0x02U
#define XX_WINRES_FSHIFT 0x04U
#define XX_WINRES_FCONTROL 0x08U
#define XX_WINRES_FALT 0x10U

typedef struct xx_winres_accelerator {
    uint16_t flags; /**< XX_WINRES_F*; the end marker is added automatically */
    uint16_t key;
    uint16_t id;
} xx_winres_accelerator;

/** RT_ACCELERATOR: ACCELTABLEENTRY records, the last one flagged 0x80. */
XXFC_API bool xx_winres_make_accelerators(xx_buf_t *out, const xx_winres_accelerator *entries, size_t count);

#ifdef __cplusplus
}
#endif

#endif
