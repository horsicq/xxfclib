/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft PDB debug information: embedded sources, srcsrv, SourceLink,
 * and FPO / frame data.
 */
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_debug.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xxfclib/memory/xx_arena.h"
#include "xxfclib/memory/xx_memory.h"

#define SRCS_MAX_STREAM_SIZE (512LL * 1024LL * 1024LL)
#define SRCS_HEADER_VERSION 19980827u

static inline uint16_t srcs_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t srcs_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t srcs_u64(const uint8_t *p)
{
    return (uint64_t)srcs_u32(p) | ((uint64_t)srcs_u32(p + 4) << 32);
}

static int srcs_stricmp(const char *s1, const char *s2)
{
    if (!s1) {
        s1 = "";
    }
    if (!s2) {
        s2 = "";
    }
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

static int srcs_strnicmp(const char *s1, const char *s2, size_t n)
{
    if (!s1) {
        s1 = "";
    }
    if (!s2) {
        s2 = "";
    }
    while (n && *s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

/* ======================================================================
 * Named stream reader
 * ====================================================================== */

bool xx_ms_pdb_read_named_stream(xx_ms_pdb *pdb, const char *name, uint8_t **data, size_t *size)
{
    int32_t stream_idx = -1;
    int64_t stream_size = 0;
    uint8_t *buf = NULL;

    if (!pdb || !name || !data || !size) {
        return false;
    }
    *data = NULL;
    *size = 0;

    stream_idx = xx_ms_pdb_get_named_stream_index(pdb, name);
    if (stream_idx < 0) {
        return false;
    }

    stream_size = xx_ms_pdb_get_stream_size(pdb, stream_idx);
    if (stream_size < 0 || stream_size > SRCS_MAX_STREAM_SIZE) {
        return false;
    }

    buf = (uint8_t *)xx_mem_alloc((size_t)stream_size + 1);
    if (!buf) {
        return false;
    }

    if (stream_size > 0) {
        int64_t nread = xx_ms_pdb_read_stream(pdb, stream_idx, 0, buf, stream_size);
        if (nread != stream_size) {
            xx_mem_free(buf);
            return false;
        }
    }
    buf[stream_size] = 0;
    *data = buf;
    *size = (size_t)stream_size;
    return true;
}

/* ======================================================================
 * Embedded sources (/src/headerblock & /src/files/...)
 * ====================================================================== */

bool xx_ms_pdb_get_embedded_sources(xx_ms_pdb *pdb, const xx_ms_pdb_names *names, xx_ms_pdb_embedded_source_list *out)
{
    xx_arena_t *arena = NULL;
    uint8_t *hb_data = NULL;
    size_t hb_size = 0;
    xx_ms_pdb_embedded_source *tmp_items = NULL;
    size_t tmp_count = 0;
    size_t tmp_cap = 0;
    xx_ms_pdb_stream_list named_list = {0};
    size_t i = 0;

    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!pdb) {
        return false;
    }

    arena = (xx_arena_t *)xx_mem_calloc(1, sizeof(xx_arena_t));
    if (!arena) {
        return false;
    }
    xx_arena_init(arena);
    out->internal_ = arena;

    if (xx_ms_pdb_read_named_stream(pdb, "/src/headerblock", &hb_data, &hb_size)) {
        if (hb_size >= 64) {
            uint32_t ver = srcs_u32(hb_data);
            out->has_header_block = true;
            out->header_version = ver;
            out->header_filetime = srcs_u64(hb_data + 8);
            out->header_age = srcs_u32(hb_data + 16);

            if (hb_size >= 84) {
                uint32_t num_entries = srcs_u32(hb_data + 64);
                uint32_t present_words = srcs_u32(hb_data + 72);
                size_t off = 76 + (size_t)present_words * 4;

                if (off + 4 <= hb_size) {
                    uint32_t deleted_words = srcs_u32(hb_data + off);
                    off += 4 + (size_t)deleted_words * 4;

                    for (i = 0; i < (size_t)num_entries; i++) {
                        uint32_t key = 0;
                        uint32_t entry_size = 0;
                        const uint8_t *entry = NULL;
                        uint32_t crc = 0;
                        uint32_t file_size = 0;
                        uint32_t file_ni = 0;
                        uint32_t obj_ni = 0;
                        uint32_t vfile_ni = 0;
                        uint8_t compression = 0;
                        uint8_t is_virtual = 0;
                        const char *file_name = NULL;
                        const char *object_name = NULL;
                        const char *virtual_name = NULL;
                        char stream_name_buf[1024];
                        char *p = NULL;
                        int32_t s_idx = -1;
                        int64_t s_size = 0;

                        if (off + 44 > hb_size) {
                            break;
                        }
                        key = srcs_u32(hb_data + off);
                        entry_size = srcs_u32(hb_data + off + 4);
                        if (entry_size < 40) {
                            entry_size = 40;
                        }
                        if (off + 4 + (size_t)entry_size > hb_size) {
                            break;
                        }
                        entry = hb_data + off + 4;
                        crc = srcs_u32(entry + 8);
                        file_size = srcs_u32(entry + 12);
                        file_ni = srcs_u32(entry + 16);
                        obj_ni = srcs_u32(entry + 20);
                        vfile_ni = srcs_u32(entry + 24);
                        compression = entry[28];
                        is_virtual = entry[29];

                        if (names) {
                            file_name = xx_ms_pdb_names_get(names, file_ni);
                            object_name = xx_ms_pdb_names_get(names, obj_ni);
                            virtual_name = xx_ms_pdb_names_get(names, vfile_ni);
                            if (!virtual_name) {
                                virtual_name = xx_ms_pdb_names_get(names, key);
                            }
                        }

                        snprintf(stream_name_buf, sizeof(stream_name_buf), "/src/files/%s", virtual_name ? virtual_name : "");
                        for (p = stream_name_buf + 11; *p; p++) {
                            *p = (char)tolower((unsigned char)*p);
                        }

                        s_idx = xx_ms_pdb_get_named_stream_index(pdb, stream_name_buf);
                        if (s_idx >= 0) {
                            s_size = xx_ms_pdb_get_stream_size(pdb, s_idx);
                            if (s_size < 0) {
                                s_size = 0;
                            }
                        }

                        if (tmp_count >= tmp_cap) {
                            size_t new_cap = (tmp_cap == 0) ? 16 : tmp_cap * 2;
                            xx_ms_pdb_embedded_source *n_items = (xx_ms_pdb_embedded_source *)xx_mem_realloc(tmp_items, new_cap * sizeof(xx_ms_pdb_embedded_source));
                            if (!n_items) {
                                break;
                            }
                            tmp_items = n_items;
                            tmp_cap = new_cap;
                        }

                        tmp_items[tmp_count].file_name = xx_arena_strdup(arena, file_name ? file_name : "");
                        tmp_items[tmp_count].object_name = xx_arena_strdup(arena, object_name ? object_name : "");
                        tmp_items[tmp_count].virtual_name = xx_arena_strdup(arena, virtual_name ? virtual_name : "");
                        tmp_items[tmp_count].stream_name = xx_arena_strdup(arena, stream_name_buf);
                        tmp_items[tmp_count].stream_index = s_idx;
                        tmp_items[tmp_count].stream_size = s_size;
                        tmp_items[tmp_count].file_size = file_size;
                        tmp_items[tmp_count].crc = crc;
                        tmp_items[tmp_count].compression = compression;
                        tmp_items[tmp_count].is_virtual = (is_virtual != 0);
                        tmp_count++;

                        off += 4 + (size_t)entry_size;
                    }
                }
            }
        }
        xx_mem_free(hb_data);
    }

    /* Also list natvis and other "/src/files/" or "/natvis/" named streams */
    if (xx_ms_pdb_get_named_streams(pdb, &named_list)) {
        for (i = 0; i < named_list.count; i++) {
            const char *sname = named_list.items[i].name;
            bool is_match = false;
            size_t k = 0;
            bool already_have = false;

            if (!sname) {
                continue;
            }
            if (srcs_strnicmp(sname, "/src/files/", 11) == 0 || srcs_strnicmp(sname, "/natvis/", 8) == 0 || srcs_stricmp(sname, "natvis") == 0 ||
                srcs_stricmp(sname, "/natvis") == 0) {
                is_match = true;
            }

            if (!is_match) {
                continue;
            }

            for (k = 0; k < tmp_count; k++) {
                if (srcs_stricmp(tmp_items[k].stream_name, sname) == 0) {
                    already_have = true;
                    break;
                }
            }

            if (!already_have) {
                if (tmp_count >= tmp_cap) {
                    size_t new_cap = (tmp_cap == 0) ? 16 : tmp_cap * 2;
                    xx_ms_pdb_embedded_source *n_items = (xx_ms_pdb_embedded_source *)xx_mem_realloc(tmp_items, new_cap * sizeof(xx_ms_pdb_embedded_source));
                    if (!n_items) {
                        break;
                    }
                    tmp_items = n_items;
                    tmp_cap = new_cap;
                }

                tmp_items[tmp_count].file_name = xx_arena_strdup(arena, "");
                tmp_items[tmp_count].object_name = xx_arena_strdup(arena, "");
                tmp_items[tmp_count].virtual_name = xx_arena_strdup(arena, "");
                tmp_items[tmp_count].stream_name = xx_arena_strdup(arena, sname);
                tmp_items[tmp_count].stream_index = named_list.items[i].index;
                tmp_items[tmp_count].stream_size = (named_list.items[i].size >= 0) ? named_list.items[i].size : 0;
                tmp_items[tmp_count].file_size = (named_list.items[i].size > 0) ? (uint32_t)named_list.items[i].size : 0;
                tmp_items[tmp_count].crc = 0;
                tmp_items[tmp_count].compression = 0;
                tmp_items[tmp_count].is_virtual = false;
                tmp_count++;
            }
        }
        xx_ms_pdb_stream_list_free(&named_list);
    }

    if (tmp_count > 0) {
        out->items = (xx_ms_pdb_embedded_source *)xx_arena_alloc_array(arena, tmp_count, sizeof(xx_ms_pdb_embedded_source));
        if (out->items) {
            memcpy(out->items, tmp_items, tmp_count * sizeof(xx_ms_pdb_embedded_source));
            out->count = tmp_count;
        }
    }
    if (tmp_items) {
        xx_mem_free(tmp_items);
    }

    return true;
}

void xx_ms_pdb_embedded_source_list_free(xx_ms_pdb_embedded_source_list *list)
{
    if (!list) {
        return;
    }
    if (list->internal_) {
        xx_arena_free((xx_arena_t *)list->internal_);
        xx_mem_free(list->internal_);
    }
    memset(list, 0, sizeof(*list));
}

/* ======================================================================
 * Source Server (srcsrv)
 * ====================================================================== */

static const char *srcs_lookup_var(const char *name, const xx_ms_pdb_env_pair *variables, size_t var_count, const char **file_vars, size_t file_var_count,
                                   const char *targ)
{
    size_t i = 0;

    if (!name || !name[0]) {
        return NULL;
    }

    if (srcs_stricmp(name, "targ") == 0) {
        return targ ? targ : "";
    }

    if (srcs_strnicmp(name, "var", 3) == 0 && isdigit((unsigned char)name[3])) {
        int idx = atoi(name + 3);
        if (idx >= 1 && (size_t)idx <= file_var_count) {
            return file_vars[idx - 1] ? file_vars[idx - 1] : "";
        }
    }

    for (i = 0; i < var_count; i++) {
        if (variables[i].key && srcs_stricmp(variables[i].key, name) == 0) {
            return variables[i].value ? variables[i].value : "";
        }
    }

    return NULL;
}

static char *srcs_expand_srcsrv(xx_arena_t *arena, const char *pattern, const xx_ms_pdb_env_pair *variables, size_t var_count, const char **file_vars,
                                size_t file_var_count, const char *targ, int depth)
{
    char buf[4096];
    size_t len = 0;
    const char *p = pattern;

    if (depth >= 10 || !pattern || !pattern[0]) {
        return xx_arena_strdup(arena, pattern ? pattern : "");
    }

    while (*p && len + 2 < sizeof(buf)) {
        if (*p != '%') {
            buf[len++] = *p++;
            continue;
        }

        /* Check for functions: %fnbksl%(, %fnbksl(, %fnfile%(, %fnfile(, %fnvar%(, %fnvar( */
        {
            int fn_type = 0; /* 1: fnbksl, 2: fnfile, 3: fnvar */
            size_t fn_prefix_len = 0;

            if (srcs_strnicmp(p, "%fnbksl%(", 9) == 0) {
                fn_type = 1;
                fn_prefix_len = 9;
            } else if (srcs_strnicmp(p, "%fnbksl(", 8) == 0) {
                fn_type = 1;
                fn_prefix_len = 8;
            } else if (srcs_strnicmp(p, "%fnfile%(", 9) == 0) {
                fn_type = 2;
                fn_prefix_len = 9;
            } else if (srcs_strnicmp(p, "%fnfile(", 8) == 0) {
                fn_type = 2;
                fn_prefix_len = 8;
            } else if (srcs_strnicmp(p, "%fnvar%(", 8) == 0) {
                fn_type = 3;
                fn_prefix_len = 8;
            } else if (srcs_strnicmp(p, "%fnvar(", 7) == 0) {
                fn_type = 3;
                fn_prefix_len = 7;
            }

            if (fn_type != 0) {
                const char *arg_start = p + fn_prefix_len;
                const char *paren = strchr(arg_start, ')');
                if (paren) {
                    size_t arg_len = (size_t)(paren - arg_start);
                    char *arg_copy = (char *)xx_mem_alloc(arg_len + 1);
                    if (arg_copy) {
                        char *exp_arg = NULL;
                        memcpy(arg_copy, arg_start, arg_len);
                        arg_copy[arg_len] = '\0';
                        exp_arg = srcs_expand_srcsrv(arena, arg_copy, variables, var_count, file_vars, file_var_count, targ, depth + 1);
                        xx_mem_free(arg_copy);

                        if (fn_type == 1) { /* fnbksl: '/' -> '\\' */
                            for (char *c = exp_arg; *c && len + 1 < sizeof(buf); c++) {
                                buf[len++] = (*c == '/') ? '\\' : *c;
                            }
                        } else if (fn_type == 2) { /* fnfile: filename part */
                            const char *slash = strrchr(exp_arg, '/');
                            const char *bslash = strrchr(exp_arg, '\\');
                            const char *fname = exp_arg;
                            if (slash && slash >= fname) {
                                fname = slash + 1;
                            }
                            if (bslash && bslash >= fname) {
                                fname = bslash + 1;
                            }
                            while (*fname && len + 1 < sizeof(buf)) {
                                buf[len++] = *fname++;
                            }
                        } else if (fn_type == 3) { /* fnvar: value of variable named by exp_arg */
                            const char *val = srcs_lookup_var(exp_arg, variables, var_count, file_vars, file_var_count, targ);
                            if (val) {
                                char *exp_val = srcs_expand_srcsrv(arena, val, variables, var_count, file_vars, file_var_count, targ, depth + 1);
                                while (*exp_val && len + 1 < sizeof(buf)) {
                                    buf[len++] = *exp_val++;
                                }
                            }
                        }
                    }
                    p = paren + 1;
                    if (*p == '%') {
                        p++;
                    }
                    continue;
                }
            }
        }

        /* Normal variable: look for next '%' */
        {
            const char *close_pct = strchr(p + 1, '%');
            if (!close_pct) {
                buf[len++] = *p++;
                continue;
            }

            size_t var_name_len = (size_t)(close_pct - (p + 1));
            char var_name[256];
            if (var_name_len < sizeof(var_name)) {
                const char *val = NULL;
                memcpy(var_name, p + 1, var_name_len);
                var_name[var_name_len] = '\0';
                val = srcs_lookup_var(var_name, variables, var_count, file_vars, file_var_count, targ);
                if (val) {
                    char *exp_val = srcs_expand_srcsrv(arena, val, variables, var_count, file_vars, file_var_count, targ, depth + 1);
                    while (*exp_val && len + 1 < sizeof(buf)) {
                        buf[len++] = *exp_val++;
                    }
                } else {
                    /* Unknown variable: keep verbatim */
                    for (const char *k = p; k <= close_pct && len + 1 < sizeof(buf); k++) {
                        buf[len++] = *k;
                    }
                }
            } else {
                buf[len++] = *p;
            }
            p = close_pct + 1;
        }
    }
    buf[len] = '\0';
    return xx_arena_strdup(arena, buf);
}

bool xx_ms_pdb_parse_srcsrv(const char *text, size_t size, xx_ms_pdb_srcsrv *out)
{
    xx_arena_t *arena = NULL;
    enum {
        SEC_NONE,
        SEC_INI,
        SEC_VARS,
        SEC_FILES,
        SEC_END
    } sec = SEC_NONE;
    const char *p = text;
    const char *end = text ? text + size : NULL;
    xx_ms_pdb_env_pair *tmp_ini = NULL;
    size_t ini_count = 0, ini_cap = 0;
    xx_ms_pdb_env_pair *tmp_vars = NULL;
    size_t var_count = 0, var_cap = 0;
    xx_ms_pdb_srcsrv_file *tmp_files = NULL;
    size_t file_count = 0, file_cap = 0;
    const char *srcsrvtrg = NULL;
    const char *srcsrvcmd = NULL;
    size_t i = 0;

    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!text || size == 0) {
        return false;
    }

    arena = (xx_arena_t *)xx_mem_calloc(1, sizeof(xx_arena_t));
    if (!arena) {
        return false;
    }
    xx_arena_init(arena);
    out->internal_ = arena;

    while (p < end) {
        const char *line_start = p;
        const char *line_end = p;
        size_t line_len = 0;
        char line_buf[2048];

        while (line_end < end && *line_end != '\r' && *line_end != '\n') {
            line_end++;
        }
        line_len = (size_t)(line_end - line_start);

        p = line_end;
        if (p < end && *p == '\r') {
            p++;
        }
        if (p < end && *p == '\n') {
            p++;
        }

        if (line_len >= sizeof(line_buf)) {
            line_len = sizeof(line_buf) - 1;
        }
        memcpy(line_buf, line_start, line_len);
        line_buf[line_len] = '\0';

        if (srcs_strnicmp(line_buf, "SRCSRV: ini", 11) == 0) {
            sec = SEC_INI;
            out->is_valid = true;
            continue;
        }
        if (srcs_strnicmp(line_buf, "SRCSRV: variables", 17) == 0) {
            sec = SEC_VARS;
            out->is_valid = true;
            continue;
        }
        if (srcs_strnicmp(line_buf, "SRCSRV: source files", 20) == 0) {
            sec = SEC_FILES;
            out->is_valid = true;
            continue;
        }
        if (srcs_strnicmp(line_buf, "SRCSRV: end", 11) == 0) {
            sec = SEC_END;
            continue;
        }

        if (line_len == 0 || sec == SEC_NONE || sec == SEC_END) {
            continue;
        }

        if (sec == SEC_INI || sec == SEC_VARS) {
            char *eq = strchr(line_buf, '=');
            if (eq) {
                char *k = line_buf;
                char *v = eq + 1;
                *eq = '\0';

                if (sec == SEC_INI) {
                    if (ini_count >= ini_cap) {
                        size_t ncap = (ini_cap == 0) ? 16 : ini_cap * 2;
                        xx_ms_pdb_env_pair *np = (xx_ms_pdb_env_pair *)xx_mem_realloc(tmp_ini, ncap * sizeof(xx_ms_pdb_env_pair));
                        if (!np) break;
                        tmp_ini = np;
                        ini_cap = ncap;
                    }
                    tmp_ini[ini_count].key = xx_arena_strdup(arena, k);
                    tmp_ini[ini_count].value = xx_arena_strdup(arena, v);
                    ini_count++;
                } else {
                    if (var_count >= var_cap) {
                        size_t ncap = (var_cap == 0) ? 16 : var_cap * 2;
                        xx_ms_pdb_env_pair *np = (xx_ms_pdb_env_pair *)xx_mem_realloc(tmp_vars, ncap * sizeof(xx_ms_pdb_env_pair));
                        if (!np) break;
                        tmp_vars = np;
                        var_cap = ncap;
                    }
                    tmp_vars[var_count].key = xx_arena_strdup(arena, k);
                    tmp_vars[var_count].value = xx_arena_strdup(arena, v);
                    var_count++;
                }
            }
        } else if (sec == SEC_FILES) {
            size_t asterisks = 0;
            const char *c = line_buf;
            size_t vcount = 0;
            const char **vars = NULL;
            char *token = NULL;
            char *ctx = NULL;

            while (*c) {
                if (*c == '*') asterisks++;
                c++;
            }
            vcount = asterisks + 1;
            vars = (const char **)xx_arena_alloc_array(arena, vcount, sizeof(const char *));
            if (!vars) {
                continue;
            }

            token = strtok_s(line_buf, "*", &ctx);
            size_t vi = 0;
            while (token && vi < vcount) {
                vars[vi++] = xx_arena_strdup(arena, token);
                token = strtok_s(NULL, "*", &ctx);
            }
            while (vi < vcount) {
                vars[vi++] = xx_arena_strdup(arena, "");
            }

            if (file_count >= file_cap) {
                size_t ncap = (file_cap == 0) ? 16 : file_cap * 2;
                xx_ms_pdb_srcsrv_file *np = (xx_ms_pdb_srcsrv_file *)xx_mem_realloc(tmp_files, ncap * sizeof(xx_ms_pdb_srcsrv_file));
                if (!np) break;
                tmp_files = np;
                file_cap = ncap;
            }

            tmp_files[file_count].line = xx_arena_strndup(arena, line_start, line_len);
            tmp_files[file_count].path = (vcount > 0 && vars[0]) ? vars[0] : "";
            tmp_files[file_count].var_count = vcount;
            tmp_files[file_count].vars = vars;
            tmp_files[file_count].target = "";
            tmp_files[file_count].command = "";
            file_count++;
        }
    }

    /* Find SRCSRVTRG and SRCSRVCMD */
    for (i = 0; i < var_count; i++) {
        if (tmp_vars[i].key && srcs_stricmp(tmp_vars[i].key, "SRCSRVTRG") == 0) {
            srcsrvtrg = tmp_vars[i].value;
        } else if (tmp_vars[i].key && srcs_stricmp(tmp_vars[i].key, "SRCSRVCMD") == 0) {
            srcsrvcmd = tmp_vars[i].value;
        }
    }

    /* Expand target and command for each file */
    for (i = 0; i < file_count; i++) {
        if (srcsrvtrg && srcsrvtrg[0]) {
            tmp_files[i].target = srcs_expand_srcsrv(arena, srcsrvtrg, tmp_vars, var_count, tmp_files[i].vars, tmp_files[i].var_count, NULL, 0);
        }
        if (srcsrvcmd && srcsrvcmd[0]) {
            tmp_files[i].command = srcs_expand_srcsrv(arena, srcsrvcmd, tmp_vars, var_count, tmp_files[i].vars, tmp_files[i].var_count, tmp_files[i].target, 0);
        }
    }

    if (ini_count > 0) {
        out->ini = (xx_ms_pdb_env_pair *)xx_arena_alloc_array(arena, ini_count, sizeof(xx_ms_pdb_env_pair));
        if (out->ini) {
            memcpy(out->ini, tmp_ini, ini_count * sizeof(xx_ms_pdb_env_pair));
            out->ini_count = ini_count;
        }
    }
    if (tmp_ini) {
        xx_mem_free(tmp_ini);
    }

    if (var_count > 0) {
        out->variables = (xx_ms_pdb_env_pair *)xx_arena_alloc_array(arena, var_count, sizeof(xx_ms_pdb_env_pair));
        if (out->variables) {
            memcpy(out->variables, tmp_vars, var_count * sizeof(xx_ms_pdb_env_pair));
            out->variable_count = var_count;
        }
    }
    if (tmp_vars) {
        xx_mem_free(tmp_vars);
    }

    if (file_count > 0) {
        out->files = (xx_ms_pdb_srcsrv_file *)xx_arena_alloc_array(arena, file_count, sizeof(xx_ms_pdb_srcsrv_file));
        if (out->files) {
            memcpy(out->files, tmp_files, file_count * sizeof(xx_ms_pdb_srcsrv_file));
            out->file_count = file_count;
        }
    }
    if (tmp_files) {
        xx_mem_free(tmp_files);
    }

    return true;
}

void xx_ms_pdb_srcsrv_free(xx_ms_pdb_srcsrv *srcsrv)
{
    if (!srcsrv) {
        return;
    }
    if (srcsrv->internal_) {
        xx_arena_free((xx_arena_t *)srcsrv->internal_);
        xx_mem_free(srcsrv->internal_);
    }
    memset(srcsrv, 0, sizeof(*srcsrv));
}

/* ======================================================================
 * SourceLink
 * ====================================================================== */

static const char *srcs_json_skip_ws(const char *p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

static char *srcs_parse_json_string(xx_arena_t *arena, const char **pp)
{
    const char *p = *pp;
    char buf[4096];
    size_t len = 0;

    p = srcs_json_skip_ws(p);
    if (*p != '"') {
        return NULL;
    }
    p++;

    while (*p && *p != '"' && len + 5 < sizeof(buf)) {
        if (*p == '\\') {
            p++;
            if (*p == '"') {
                buf[len++] = '"';
                p++;
            } else if (*p == '\\') {
                buf[len++] = '\\';
                p++;
            } else if (*p == '/') {
                buf[len++] = '/';
                p++;
            } else if (*p == 'b') {
                buf[len++] = '\b';
                p++;
            } else if (*p == 'f') {
                buf[len++] = '\f';
                p++;
            } else if (*p == 'n') {
                buf[len++] = '\n';
                p++;
            } else if (*p == 'r') {
                buf[len++] = '\r';
                p++;
            } else if (*p == 't') {
                buf[len++] = '\t';
                p++;
            } else if (*p == 'u') {
                uint32_t cp = 0;
                char hex[5] = {0};
                p++;
                for (int h = 0; h < 4 && *p; h++) {
                    hex[h] = *p++;
                }
                cp = (uint32_t)strtoul(hex, NULL, 16);

                if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                    p += 2;
                    for (int h = 0; h < 4 && *p; h++) {
                        hex[h] = *p++;
                    }
                    uint32_t low_cp = (uint32_t)strtoul(hex, NULL, 16);
                    if (low_cp >= 0xDC00 && low_cp <= 0xDFFF) {
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (low_cp - 0xDC00));
                    }
                }

                if (cp < 0x80) {
                    buf[len++] = (char)cp;
                } else if (cp < 0x800) {
                    buf[len++] = (char)(0xC0 | (cp >> 6));
                    buf[len++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    buf[len++] = (char)(0xE0 | (cp >> 12));
                    buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    buf[len++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp <= 0x10FFFF) {
                    buf[len++] = (char)(0xF0 | (cp >> 18));
                    buf[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    buf[len++] = (char)(0x80 | (cp & 0x3F));
                }
            } else {
                buf[len++] = *p++;
            }
        } else {
            buf[len++] = *p++;
        }
    }
    if (*p == '"') {
        p++;
    }
    buf[len] = '\0';
    *pp = p;
    return xx_arena_strdup(arena, buf);
}

static void srcs_parse_sourcelink_json(xx_arena_t *arena, const char *json, xx_ms_pdb_env_pair **docs, size_t *doc_cnt, size_t *doc_cap)
{
    const char *p = strstr(json, "\"documents\"");
    if (!p) {
        return;
    }
    p += 11;
    p = srcs_json_skip_ws(p);
    if (*p != ':') {
        return;
    }
    p++;
    p = srcs_json_skip_ws(p);
    if (*p != '{') {
        return;
    }
    p++;

    for (;;) {
        p = srcs_json_skip_ws(p);
        if (*p == '}' || *p == '\0') {
            break;
        }

        char *key = srcs_parse_json_string(arena, &p);
        if (!key) {
            break;
        }

        p = srcs_json_skip_ws(p);
        if (*p != ':') {
            break;
        }
        p++;

        char *val = srcs_parse_json_string(arena, &p);
        if (!val) {
            break;
        }

        if (*doc_cnt >= *doc_cap) {
            size_t ncap = (*doc_cap == 0) ? 16 : *doc_cap * 2;
            xx_ms_pdb_env_pair *np = (xx_ms_pdb_env_pair *)xx_mem_realloc(*docs, ncap * sizeof(xx_ms_pdb_env_pair));
            if (!np) {
                break;
            }
            *docs = np;
            *doc_cap = ncap;
        }
        (*docs)[*doc_cnt].key = key;
        (*docs)[*doc_cnt].value = val;
        (*doc_cnt)++;

        p = srcs_json_skip_ws(p);
        if (*p == ',') {
            p++;
        }
    }
}

bool xx_ms_pdb_get_sourcelink(xx_ms_pdb *pdb, xx_ms_pdb_sourcelink *out)
{
    xx_arena_t *arena = NULL;
    xx_ms_pdb_stream_list named_list = {0};
    xx_ms_pdb_env_pair *tmp_docs = NULL;
    size_t doc_count = 0, doc_cap = 0;
    size_t stream_count = 0;
    size_t i = 0;

    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!pdb) {
        return false;
    }

    arena = (xx_arena_t *)xx_mem_calloc(1, sizeof(xx_arena_t));
    if (!arena) {
        return false;
    }
    xx_arena_init(arena);
    out->internal_ = arena;

    if (xx_ms_pdb_get_named_streams(pdb, &named_list)) {
        for (i = 0; i < named_list.count; i++) {
            const char *name = named_list.items[i].name;
            bool is_sl = false;

            if (!name) {
                continue;
            }
            if (srcs_stricmp(name, "sourcelink") == 0) {
                is_sl = true;
            } else if (srcs_strnicmp(name, "sourcelink$", 11) == 0) {
                const char *d = name + 11;
                while (*d && isdigit((unsigned char)*d)) {
                    d++;
                }
                if (*d == '\0') {
                    is_sl = true;
                }
            }

            if (is_sl && named_list.items[i].size > 0) {
                int32_t s_idx = named_list.items[i].index;
                int64_t s_sz = named_list.items[i].size;
                if (s_idx >= 0 && s_sz > 0 && s_sz < SRCS_MAX_STREAM_SIZE) {
                    char *buf = (char *)xx_mem_alloc((size_t)s_sz + 1);
                    if (buf) {
                        if (xx_ms_pdb_read_stream(pdb, s_idx, 0, buf, s_sz) == s_sz) {
                            buf[s_sz] = '\0';
                            stream_count++;
                            srcs_parse_sourcelink_json(arena, buf, &tmp_docs, &doc_count, &doc_cap);
                        }
                        xx_mem_free(buf);
                    }
                }
            }
        }
        xx_ms_pdb_stream_list_free(&named_list);
    }

    out->stream_count = stream_count;
    if (doc_count > 0) {
        out->documents = (xx_ms_pdb_env_pair *)xx_arena_alloc_array(arena, doc_count, sizeof(xx_ms_pdb_env_pair));
        if (out->documents) {
            memcpy(out->documents, tmp_docs, doc_count * sizeof(xx_ms_pdb_env_pair));
            out->document_count = doc_count;
        }
    }
    if (tmp_docs) {
        xx_mem_free(tmp_docs);
    }

    return true;
}

void xx_ms_pdb_sourcelink_free(xx_ms_pdb_sourcelink *sl)
{
    if (!sl) {
        return;
    }
    if (sl->internal_) {
        xx_arena_free((xx_arena_t *)sl->internal_);
        xx_mem_free(sl->internal_);
    }
    memset(sl, 0, sizeof(*sl));
}

char *xx_ms_pdb_sourcelink_map(const xx_ms_pdb_sourcelink *sl, const char *path)
{
    size_t i = 0;

    if (!sl || !path) {
        return NULL;
    }

    for (i = 0; i < sl->document_count; i++) {
        const char *pattern = sl->documents[i].key;
        const char *url = sl->documents[i].value;
        const char *star = NULL;

        if (!pattern || !url) {
            continue;
        }

        star = strchr(pattern, '*');
        if (!star) {
            const char *p = pattern;
            const char *t = path;
            bool match = true;

            while (*p && *t) {
                int cp = (*p == '\\') ? '/' : *p;
                int ct = (*t == '\\') ? '/' : *t;
                if (tolower((unsigned char)cp) != tolower((unsigned char)ct)) {
                    match = false;
                    break;
                }
                p++;
                t++;
            }
            if (match && *p == '\0' && *t == '\0') {
                const char *ustar = strchr(url, '*');
                if (ustar) {
                    size_t u1 = (size_t)(ustar - url);
                    size_t u2 = strlen(ustar + 1);
                    char *res = (char *)xx_mem_alloc(u1 + u2 + 1);
                    if (res) {
                        memcpy(res, url, u1);
                        memcpy(res + u1, ustar + 1, u2 + 1);
                    }
                    return res;
                } else {
                    size_t ulen = strlen(url);
                    char *res = (char *)xx_mem_alloc(ulen + 1);
                    if (res) {
                        memcpy(res, url, ulen + 1);
                    }
                    return res;
                }
            }
        } else {
            size_t prefix_len = (size_t)(star - pattern);
            bool prefix_match = true;
            size_t k = 0;

            for (k = 0; k < prefix_len; k++) {
                if (!path[k]) {
                    prefix_match = false;
                    break;
                }
                int cp = (pattern[k] == '\\') ? '/' : pattern[k];
                int ct = (path[k] == '\\') ? '/' : path[k];
                if (tolower((unsigned char)cp) != tolower((unsigned char)ct)) {
                    prefix_match = false;
                    break;
                }
            }
            if (!prefix_match) {
                continue;
            }

            const char *rest = path + prefix_len;
            const char *pat_suffix = star + 1;
            size_t suff_len = strlen(pat_suffix);
            size_t rest_len = strlen(rest);

            if (rest_len < suff_len) {
                continue;
            }

            if (suff_len > 0) {
                bool suff_match = true;
                const char *rest_suff = rest + (rest_len - suff_len);
                for (k = 0; k < suff_len; k++) {
                    int cp = (pat_suffix[k] == '\\') ? '/' : pat_suffix[k];
                    int ct = (rest_suff[k] == '\\') ? '/' : rest_suff[k];
                    if (tolower((unsigned char)cp) != tolower((unsigned char)ct)) {
                        suff_match = false;
                        break;
                    }
                }
                if (!suff_match) {
                    continue;
                }
            }

            size_t subst_len = rest_len - suff_len;
            const char *ustar = strchr(url, '*');
            if (ustar) {
                size_t u1 = (size_t)(ustar - url);
                size_t u2 = strlen(ustar + 1);
                size_t total = u1 + subst_len + u2;
                char *res = (char *)xx_mem_alloc(total + 1);
                if (!res) {
                    return NULL;
                }
                memcpy(res, url, u1);
                for (size_t s = 0; s < subst_len; s++) {
                    res[u1 + s] = (rest[s] == '\\') ? '/' : rest[s];
                }
                memcpy(res + u1 + subst_len, ustar + 1, u2 + 1);
                return res;
            } else {
                size_t ulen = strlen(url);
                char *res = (char *)xx_mem_alloc(ulen + 1);
                if (res) {
                    memcpy(res, url, ulen + 1);
                }
                return res;
            }
        }
    }

    return NULL;
}

/* ======================================================================
 * FPO and Frame Data
 * ====================================================================== */

bool xx_ms_pdb_get_frame_info(xx_ms_pdb *pdb, const xx_ms_pdb_names *names, xx_ms_pdb_frame_info *out)
{
    xx_arena_t *arena = NULL;
    xx_ms_pdb_dbi_info dbi = {0};
    size_t i = 0;

    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!pdb) {
        return false;
    }

    arena = (xx_arena_t *)xx_mem_calloc(1, sizeof(xx_arena_t));
    if (!arena) {
        return false;
    }
    xx_arena_init(arena);
    out->internal_ = arena;

    dbi = xx_ms_pdb_get_dbi_info(pdb);

    /* FPO (DBI debug stream 0) */
    if (dbi.dbg_stream_count > 0 && dbi.dbg_streams[0] != 0xFFFF) {
        int32_t s_idx = (int32_t)dbi.dbg_streams[0];
        int64_t s_size = xx_ms_pdb_get_stream_size(pdb, s_idx);

        if (s_size >= 16 && s_size < SRCS_MAX_STREAM_SIZE) {
            size_t fpo_count = (size_t)(s_size / 16);
            uint8_t *fpo_data = (uint8_t *)xx_mem_alloc((size_t)s_size);

            if (fpo_data) {
                if (xx_ms_pdb_read_stream(pdb, s_idx, 0, fpo_data, s_size) == s_size) {
                    out->fpo = (xx_ms_pdb_fpo *)xx_arena_alloc_array(arena, fpo_count, sizeof(xx_ms_pdb_fpo));
                    if (out->fpo) {
                        for (i = 0; i < fpo_count; i++) {
                            const uint8_t *p = fpo_data + i * 16;
                            uint16_t bits = srcs_u16(p + 14);

                            out->fpo[i].rva_start = srcs_u32(p);
                            out->fpo[i].proc_size = srcs_u32(p + 4);
                            out->fpo[i].locals_dwords = srcs_u32(p + 8);
                            out->fpo[i].params_dwords = srcs_u16(p + 12);
                            out->fpo[i].prolog_size = (uint8_t)(bits & 0xFF);
                            out->fpo[i].saved_regs = (uint8_t)((bits >> 8) & 0x07);
                            out->fpo[i].has_seh = (uint8_t)((bits >> 11) & 0x01);
                            out->fpo[i].use_bp = (uint8_t)((bits >> 12) & 0x01);
                            out->fpo[i].frame_type = (uint8_t)((bits >> 14) & 0x03);
                        }
                        out->fpo_count = fpo_count;
                    }
                }
                xx_mem_free(fpo_data);
            }
        }
    }

    /* Frame Data (DBI debug stream 9) */
    if (dbi.dbg_stream_count > 9 && dbi.dbg_streams[9] != 0xFFFF) {
        int32_t s_idx = (int32_t)dbi.dbg_streams[9];
        int64_t s_size = xx_ms_pdb_get_stream_size(pdb, s_idx);

        if (s_size >= 32 && s_size < SRCS_MAX_STREAM_SIZE) {
            size_t offset = (s_size % 32 != 0) ? (size_t)(s_size % 32) : 0;
            size_t frame_count = (size_t)((s_size - offset) / 32);
            uint8_t *frame_data = (uint8_t *)xx_mem_alloc((size_t)s_size);

            if (frame_data) {
                if (xx_ms_pdb_read_stream(pdb, s_idx, 0, frame_data, s_size) == s_size) {
                    out->frames = (xx_ms_pdb_frame_data *)xx_arena_alloc_array(arena, frame_count, sizeof(xx_ms_pdb_frame_data));
                    if (out->frames) {
                        for (i = 0; i < frame_count; i++) {
                            const uint8_t *p = frame_data + offset + i * 32;
                            uint32_t flags = srcs_u32(p + 28);
                            const char *prog = NULL;

                            out->frames[i].rva_start = srcs_u32(p);
                            out->frames[i].code_size = srcs_u32(p + 4);
                            out->frames[i].locals_size = srcs_u32(p + 8);
                            out->frames[i].params_size = srcs_u32(p + 12);
                            out->frames[i].max_stack_size = srcs_u32(p + 16);
                            out->frames[i].frame_func = srcs_u32(p + 20);
                            out->frames[i].prolog_size = srcs_u16(p + 24);
                            out->frames[i].saved_regs_size = srcs_u16(p + 26);
                            out->frames[i].has_seh = ((flags & 0x01) != 0);
                            out->frames[i].has_eh = ((flags & 0x02) != 0);
                            out->frames[i].is_function_start = ((flags & 0x04) != 0);

                            if (names) {
                                prog = xx_ms_pdb_names_get(names, out->frames[i].frame_func);
                            }
                            out->frames[i].program = xx_arena_strdup(arena, prog ? prog : "");
                        }
                        out->frame_count = frame_count;
                    }
                }
                xx_mem_free(frame_data);
            }
        }
    }

    return true;
}

void xx_ms_pdb_frame_info_free(xx_ms_pdb_frame_info *info)
{
    if (!info) {
        return;
    }
    if (info->internal_) {
        xx_arena_free((xx_arena_t *)info->internal_);
        xx_mem_free(info->internal_);
    }
    memset(info, 0, sizeof(*info));
}
