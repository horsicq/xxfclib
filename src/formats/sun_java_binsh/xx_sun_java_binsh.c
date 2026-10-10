/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/installers/xbinshsfx.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/sun_java_binsh/xx_sun_java_binsh.h"
#include "../common/xx_carrier_helpers.h"

/* The generic tail-carve face shares framing while the Sun face retains its
 * product banner identity. No shell text or payload is executed. */
static bool binsh_word(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool binsh_blank(char c)
{
    return c == ' ' || c == '\t';
}
static bool binsh_tail(const char *text, size_t len, size_t at, uint64_t *line, size_t *end)
{
    size_t p = at + 4, start, digits;
    if ((at && binsh_word(text[at - 1])) || p >= len || !binsh_blank(text[p])) return false;
    while (p < len && binsh_blank(text[p])) ++p;
    if (p + 1 < len && text[p] == '-' && text[p + 1] == 'n') {
        p += 2;
        while (p < len && binsh_blank(text[p])) ++p;
    }
    if (p >= len || text[p++] != '+') return false;
    start = p;
    while (p < len && text[p] >= '0' && text[p] <= '9') ++p;
    digits = p - start;
    if (!digits || digits > 6 || !carrier_decimal(text + start, digits, line) || *line < 2 || *line > 100000 || p >= len || !binsh_blank(text[p])) return false;
    while (p < len && binsh_blank(text[p])) ++p;
    if (p + 4 <= len && (!xx_rt_memcmp(text + p, "\"$0\"", 4) || !xx_rt_memcmp(text + p, "${0}", 4))) p += 4;
    else if (p + 2 <= len && !xx_rt_memcmp(text + p, "$0", 2)) p += 2;
    else return false;
    if (p < len && text[p] && !binsh_blank(text[p]) && text[p] != '\r' && text[p] != '\n' && text[p] != ';' && text[p] != '|' && text[p] != '&' && text[p] != ')')
        return false;
    *end = p;
    return true;
}
static char *binsh_generic_text(Abstractformat *f, size_t *len)
{
    int64_t total = pm_available(f);
    size_t n;
    char *text;
    bool bang = false;
    if (total < 4096) return NULL;
    n = total > 65536 ? 65536U : (size_t)total;
    text = (char *)xx_mem_alloc(n + 1);
    if (!text || !pm_read(f, 0, text, n)) {
        xx_mem_free(text);
        return NULL;
    }
    if (n >= 9 && !xx_rt_memcmp(text, "#!/bin/sh", 9)) bang = true;
    if (n >= 10 && (!xx_rt_memcmp(text, "#! /bin/sh", 10) || !xx_rt_memcmp(text, "#!/sbin/sh", 10))) bang = true;
    if (!bang) {
        xx_mem_free(text);
        return NULL;
    }
    text[n] = 0;
    *len = n;
    return text;
}
static bool carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    bool generic = f->file_type == XX_FILE_TYPE_BINSH_SFX, ok = false;
    size_t len = 0, at, candidates = 0;
    char *text;
    int64_t limit = pm_available(f);
    text = generic ? binsh_generic_text(f, &len) : carrier_shell(f, &len);
    if (!text) return false;
    if (!generic && (!xx_rt_strstr(text, "Java") || (!xx_rt_strstr(text, "SUN MICROSYSTEMS") && !xx_rt_strstr(text, "Sun Microsystems")) ||
                     xx_rt_strstr(text, "InstallAnywhere") || xx_rt_strstr(text, "Makeself")))
        goto done;
    for (at = 0; at + 4 <= len && text[at]; ++at) {
        uint64_t line;
        size_t match_end;
        int64_t offset;
        uint8_t h[3];
        if (carrier_stop(pd)) goto done;
        if (xx_rt_memcmp(text + at, "tail", 4)) continue;
        if (++candidates > 256) goto done;
        if (!binsh_tail(text, len, at, &line, &match_end) || !carrier_lines(f, line - 1, &offset, pd) || offset <= (int64_t)match_end || limit - offset < 512 ||
            !pm_read(f, offset, h, 3))
            continue;
        if (h[0] == 31 && h[1] == 157 && !(h[2] & 96) && (h[2] & 31) >= 9 && (h[2] & 31) <= 16) ok = pm_add(f, s, "archive.tar.Z", offset, limit - offset);
        else if (carrier_tar(f, offset, limit, pd)) ok = pm_add(f, s, "archive.tar", offset, limit - offset);
        if (ok) {
            s->size = limit;
            break;
        }
    }
done:
    xx_mem_free(text);
    return ok;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_sun_java_binsh_init(xx_sun_java_binsh *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SUN_JAVA_BINSH, "sh");
    }
}
xx_sun_java_binsh *xx_sun_java_binsh_create(xx_io_device *d, int64_t b)
{
    xx_sun_java_binsh *r = (xx_sun_java_binsh *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sun_java_binsh_init(r, d, b);
    return r;
}
void xx_sun_java_binsh_destroy(xx_sun_java_binsh *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sun_java_binsh_free(xx_sun_java_binsh *r)
{
    if (r) {
        xx_sun_java_binsh_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sun_java_binsh_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sun_java_binsh_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
