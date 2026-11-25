/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/winres/xx_winres_make.h"

#include <string.h>

static bool winres_u16(xx_buf_t *out, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return xx_buf_append(out, bytes, sizeof(bytes));
}

static bool winres_u32(xx_buf_t *out, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    return xx_buf_append(out, bytes, sizeof(bytes));
}

static void winres_patch_u16(xx_buf_t *out, size_t offset, uint16_t value)
{
    out->data[offset] = (char)(uint8_t)value;
    out->data[offset + 1U] = (char)(uint8_t)(value >> 8);
}

static bool winres_align4(xx_buf_t *out)
{
    while (out->size & 3U)
        if (!xx_buf_append(out, "", 1U)) return false;
    return true;
}

/* Decodes one UTF-8 sequence; malformed bytes become U+FFFD. */
static uint32_t winres_utf8_next(const unsigned char **cursor)
{
    const unsigned char *p = *cursor;
    uint32_t code;
    int extra;
    int index;
    if (p[0] < 0x80U) {
        *cursor = p + 1;
        return p[0];
    }
    if ((p[0] & 0xE0U) == 0xC0U) {
        code = p[0] & 0x1FU;
        extra = 1;
    } else if ((p[0] & 0xF0U) == 0xE0U) {
        code = p[0] & 0x0FU;
        extra = 2;
    } else if ((p[0] & 0xF8U) == 0xF0U) {
        code = p[0] & 0x07U;
        extra = 3;
    } else {
        *cursor = p + 1;
        return 0xFFFDU;
    }
    for (index = 1; index <= extra; ++index) {
        if ((p[index] & 0xC0U) != 0x80U) {
            *cursor = p + 1;
            return 0xFFFDU;
        }
        code = (code << 6) | (p[index] & 0x3FU);
    }
    *cursor = p + extra + 1;
    return (code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU)) ? 0xFFFDU : code;
}

/* Appends UTF-16LE text; counts the code units written into *units. */
static bool winres_utf16(xx_buf_t *out, const char *text, size_t *units)
{
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    size_t written = 0U;
    while (*cursor) {
        uint32_t code = winres_utf8_next(&cursor);
        if (code >= 0x10000U) {
            code -= 0x10000U;
            if (!winres_u16(out, (uint16_t)(0xD800U | (code >> 10))) || !winres_u16(out, (uint16_t)(0xDC00U | (code & 0x3FFU)))) return false;
            written += 2U;
        } else {
            if (!winres_u16(out, (uint16_t)code)) return false;
            ++written;
        }
    }
    if (units) *units = written;
    return true;
}

static bool winres_utf16z(xx_buf_t *out, const char *text)
{
    return winres_utf16(out, text, NULL) && winres_u16(out, 0U);
}

/* ---- VERSIONINFO ---------------------------------------------------------- */

/* Starts a version node; returns its offset for winres_version_end. */
static bool winres_version_begin(xx_buf_t *out, const char *key, uint16_t type, uint16_t value_length, size_t *start)
{
    if (!winres_align4(out)) return false;
    *start = out->size;
    return winres_u16(out, 0U) && winres_u16(out, value_length) && winres_u16(out, type) && winres_utf16z(out, key) && winres_align4(out);
}

static bool winres_version_end(xx_buf_t *out, size_t start)
{
    if (!xx_buf_ok(out) || out->size - start > 0xFFFFU) return false;
    winres_patch_u16(out, start, (uint16_t)(out->size - start));
    return true;
}

static bool winres_version_string(xx_buf_t *out, const char *key, const char *value)
{
    xx_buf_t text;
    size_t units = 0U;
    size_t start;
    bool ok;
    xx_buf_init(&text);
    ok = winres_utf16(&text, value, &units) && winres_u16(&text, 0U) && units < 0xFFFFU && winres_version_begin(out, key, 1U, (uint16_t)(units + 1U), &start) &&
         xx_buf_append(out, text.data, text.size) && winres_version_end(out, start);
    xx_buf_free(&text);
    return ok;
}

bool xx_winres_make_version(xx_buf_t *out, const xx_winres_version_info *info)
{
    const uint16_t *fv;
    const uint16_t *pv;
    char table[9];
    static const char hex[] = "0123456789ABCDEF";
    uint32_t pair;
    size_t root, string_info, string_table, var_info, translation;
    size_t index;
    if (!out || !info || (info->string_count && !info->strings)) return false;
    fv = info->file_version;
    pv = info->product_version;
    if (!winres_version_begin(out, "VS_VERSION_INFO", 0U, 52U, &root)) return false;
    /* VS_FIXEDFILEINFO */
    if (!winres_u32(out, 0xFEEF04BDU) || !winres_u32(out, 0x00010000U) || !winres_u32(out, ((uint32_t)fv[0] << 16) | fv[1]) ||
        !winres_u32(out, ((uint32_t)fv[2] << 16) | fv[3]) || !winres_u32(out, ((uint32_t)pv[0] << 16) | pv[1]) || !winres_u32(out, ((uint32_t)pv[2] << 16) | pv[3]) ||
        !winres_u32(out, info->file_flags_mask) || !winres_u32(out, info->file_flags) || !winres_u32(out, info->file_os) || !winres_u32(out, info->file_type) ||
        !winres_u32(out, info->file_subtype) || !winres_u32(out, 0U) || !winres_u32(out, 0U))
        return false;

    pair = ((uint32_t)info->language << 16) | info->code_page;
    for (index = 0U; index < 8U; ++index) table[index] = hex[(pair >> (28U - 4U * index)) & 0xFU];
    table[8] = '\0';
    if (!winres_version_begin(out, "StringFileInfo", 1U, 0U, &string_info) || !winres_version_begin(out, table, 1U, 0U, &string_table)) return false;
    for (index = 0U; index < info->string_count; ++index)
        if (!winres_version_string(out, info->strings[index].key, info->strings[index].value)) return false;
    if (!winres_version_end(out, string_table) || !winres_version_end(out, string_info)) return false;

    if (!winres_version_begin(out, "VarFileInfo", 1U, 0U, &var_info) || !winres_version_begin(out, "Translation", 0U, 4U, &translation) ||
        !winres_u16(out, info->language) || !winres_u16(out, info->code_page) || !winres_version_end(out, translation) || !winres_version_end(out, var_info))
        return false;
    return winres_version_end(out, root);
}

/* ---- STRINGTABLE / MENU / DIALOG / ACCELERATORS --------------------------- */

bool xx_winres_make_string_table(xx_buf_t *out, const char *const strings[16])
{
    unsigned int slot;
    if (!out || !strings) return false;
    for (slot = 0U; slot < 16U; ++slot) {
        size_t length_at = out->size;
        size_t units = 0U;
        if (!winres_u16(out, 0U) || !winres_utf16(out, strings[slot], &units) || units > 0xFFFFU) return false;
        winres_patch_u16(out, length_at, (uint16_t)units);
    }
    return xx_buf_ok(out);
}

bool xx_winres_make_menu(xx_buf_t *out, const xx_winres_menu_item *items, size_t count)
{
    size_t index;
    if (!out || (count && !items)) return false;
    if (!winres_u16(out, 0U) || !winres_u16(out, 0U)) return false; /* MENUHEADER */
    for (index = 0U; index < count; ++index) {
        if (!winres_u16(out, items[index].flags)) return false;
        if (!(items[index].flags & XX_WINRES_MF_POPUP) && !winres_u16(out, items[index].id)) return false;
        if (!winres_utf16z(out, items[index].text)) return false;
    }
    return xx_buf_ok(out);
}

bool xx_winres_make_dialog(xx_buf_t *out, const xx_winres_dialog_template *dialog)
{
    size_t index;
    if (!out || !dialog || dialog->control_count > 0xFFFFU || (dialog->control_count && !dialog->controls)) return false;
    if (!winres_u32(out, dialog->style) || !winres_u32(out, dialog->ex_style) || !winres_u16(out, (uint16_t)dialog->control_count) ||
        !winres_u16(out, (uint16_t)dialog->x) || !winres_u16(out, (uint16_t)dialog->y) || !winres_u16(out, (uint16_t)dialog->cx) ||
        !winres_u16(out, (uint16_t)dialog->cy) || !winres_u16(out, 0U) /* menu */ || !winres_u16(out, 0U) /* class */ || !winres_utf16z(out, dialog->caption))
        return false;
    if ((dialog->style & 0x40U) && (!winres_u16(out, dialog->point_size) || !winres_utf16z(out, dialog->face))) return false;
    for (index = 0U; index < dialog->control_count; ++index) {
        const xx_winres_dialog_control *control = dialog->controls + index;
        if (!winres_align4(out) || !winres_u32(out, control->style) || !winres_u32(out, control->ex_style) || !winres_u16(out, (uint16_t)control->x) ||
            !winres_u16(out, (uint16_t)control->y) || !winres_u16(out, (uint16_t)control->cx) || !winres_u16(out, (uint16_t)control->cy) ||
            !winres_u16(out, control->id) || !winres_u16(out, 0xFFFFU) || !winres_u16(out, control->class_atom) || !winres_utf16z(out, control->text) ||
            !winres_u16(out, 0U) /* no creation data */)
            return false;
    }
    return xx_buf_ok(out);
}

bool xx_winres_make_accelerators(xx_buf_t *out, const xx_winres_accelerator *entries, size_t count)
{
    size_t index;
    if (!out || !entries || !count) return false;
    for (index = 0U; index < count; ++index) {
        uint16_t flags = (uint16_t)(entries[index].flags & 0x7FU);
        if (index + 1U == count) flags |= 0x80U;
        if (!winres_u16(out, flags) || !winres_u16(out, entries[index].key) || !winres_u16(out, entries[index].id) || !winres_u16(out, 0U)) return false;
    }
    return xx_buf_ok(out);
}
