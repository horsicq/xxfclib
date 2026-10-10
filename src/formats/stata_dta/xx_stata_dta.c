/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/WizardMac/ReadStat/master/src/stata/readstat_dta_read.c */
#include "xxfclib/formats/stata_dta/xx_stata_dta.h"
#include "../common/xx_binary_cursor.h"

static bool dt_tag(binary_cursor *c, const char *s)
{
    uint8_t h[40];
    size_t n = xx_rt_strlen(s);
    return n <= sizeof(h) && binary_get(c, h, n) && !xx_rt_memcmp(h, s, n);
}
static bool dt_number(binary_cursor *c, bool be, unsigned n, uint64_t *v)
{
    uint8_t b[8];
    if (!binary_get(c, b, n)) return false;
    *v = n == 1   ? b[0]
         : n == 2 ? xx_data_get_u16(b, 2, 0, be)
         : n == 4 ? xx_data_get_u32(b, 4, 0, be)
         : be     ? xx_data_get_u64(b, 8, 0, true)
                  : xx_data_get_u64(b, 8, 0, false);
    return true;
}
static bool dt_section(Abstractformat *f, uint64_t start, uint64_t end, const char *open, const char *close, uint64_t expected, uint64_t *at, uint64_t *size)
{
    uint64_t a = xx_rt_strlen(open), b = xx_rt_strlen(close);
    if (start > end || a + b > end - start || !binary_equal(f, (int64_t)start, open, (size_t)a) || !binary_equal(f, (int64_t)(end - b), close, (size_t)b)) return false;
    *at = start + a;
    *size = end - start - a - b;
    return expected == UINT64_MAX || *size == expected;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    binary_cursor c = {f, 0, (uint64_t)pm_available(f), pd, 0};
    uint8_t text[3], order[3];
    uint64_t vars, rows, n, map[14], mapstart, width = 0, data_at, data_size;
    unsigned version, i;
    bool be;
    const char *opens[] = {"<variable_types>",  "<varnames>",        "<sortlist>", "<formats>", "<value_label_names>",
                           "<variable_labels>", "<characteristics>", "<data>",     "<strls>",   "<value_labels>"};
    const char *closes[] = {"</variable_types>",  "</varnames>",        "</sortlist>", "</formats>", "</value_label_names>",
                            "</variable_labels>", "</characteristics>", "</data>",     "</strls>",   "</value_labels>"};
    if (!dt_tag(&c, "<stata_dta><header><release>") || !binary_get(&c, text, 3) || text[0] != '1' || text[1] != '1' || (text[2] != '7' && text[2] != '8') ||
        !dt_tag(&c, "</release><byteorder>") || !binary_get(&c, order, 3))
        return false;
    version = text[2] - '0' + 110;
    be = !xx_rt_memcmp(order, "MSF", 3);
    if (!be && xx_rt_memcmp(order, "LSF", 3)) return false;
    if (!dt_tag(&c, "</byteorder><K>") || !dt_number(&c, be, 2, &vars) || !vars || vars > 4096 || !dt_tag(&c, "</K><N>") ||
        !dt_number(&c, be, version == 118 ? 8 : 4, &rows) || rows > INT64_MAX || !dt_tag(&c, "</N><label>") || !dt_number(&c, be, version == 118 ? 2 : 1, &n) ||
        n > 65535 || !binary_skip(&c, n) || !dt_tag(&c, "</label><timestamp>") || !dt_number(&c, be, 1, &n) || n > 17 || !binary_skip(&c, n) ||
        !dt_tag(&c, "</timestamp></header>"))
        return false;
    mapstart = c.at;
    if (!dt_tag(&c, "<map>")) return false;
    for (i = 0; i < 14; ++i)
        if (!dt_number(&c, be, 8, &map[i]) || map[i] > c.end || (i && map[i] < map[i - 1])) return false;
    if (!dt_tag(&c, "</map>") || map[0] != 0 || map[1] != mapstart || map[2] != c.at || map[13] != map[12] + 12 || !binary_equal(f, (int64_t)map[12], "</stata_dta>", 12))
        return false;
    for (i = 0; i < 10; ++i) {
        uint64_t at, size, expected = UINT64_MAX;
        char name[64];
        if (binary_stop(pd)) return false;
        if (i == 0) expected = vars * 2;
        else if (i == 1 || i == 4) expected = vars * (version == 118 ? 129 : 33);
        else if (i == 2) expected = (vars + 1) * 2;
        else if (i == 3) expected = vars * (version == 118 ? 57 : 49);
        else if (i == 5) expected = vars * (version == 118 ? 321 : 81);
        else if (i == 6 || i == 8 || i == 9) expected = 0;
        if (!dt_section(f, map[i + 2], map[i + 3], opens[i], closes[i], expected, &at, &size)) return false;
        if (i == 0) {
            unsigned j;
            for (j = 0; j < vars; ++j) {
                uint8_t b[2];
                uint16_t t;
                unsigned w;
                if (!pm_read(f, (int64_t)at + j * 2, b, 2)) {
                    return false;
                }
                t = xx_data_get_u16(b, 2, 0, be);
                if (t >= 1 && t <= 2045) w = t;
                else w = t == 65526 ? 8 : t == 65527 || t == 65528 ? 4 : t == 65529 ? 2 : t == 65530 ? 1 : 0;
                if (!w) {
                    return false;
                }
                width += w;
            }
        }
        if (i == 2) {
            unsigned j;
            for (j = 0; j <= vars; ++j) {
                uint8_t b[2];
                if (!pm_read(f, (int64_t)at + j * 2, b, 2) || xx_data_get_u16(b, 2, 0, be) > vars) return false;
            }
        }
        if (i == 7) {
            uint64_t wanted;
            if (!binary_mul(width, rows, &wanted) || wanted != size) return false;
            data_at = at;
            data_size = size;
        }
        xx_rt_snprintf(name, sizeof(name), "section-%u.bin", i);
        if (!pm_add(f, s, name, (int64_t)at, (int64_t)size)) return false;
    }
    (void)data_at;
    (void)data_size;
    s->size = (int64_t)map[13];
    return true;
}

void xx_stata_dta_init(xx_stata_dta *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_STATA_DTA, "stata_dta");
    }
}
xx_stata_dta *xx_stata_dta_create(xx_io_device *d, int64_t b)
{
    xx_stata_dta *r = (xx_stata_dta *)xx_mem_alloc(sizeof(*r));
    if (r) xx_stata_dta_init(r, d, b);
    return r;
}
void xx_stata_dta_destroy(xx_stata_dta *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_stata_dta_free(xx_stata_dta *r)
{
    if (r) {
        xx_stata_dta_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_stata_dta_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_stata_dta_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
