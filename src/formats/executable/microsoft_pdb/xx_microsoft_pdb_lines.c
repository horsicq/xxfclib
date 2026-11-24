/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft PDB: C13 line tables, source file checksums, inlinee lines.
 */
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_debug.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "xxfclib/memory/xx_arena.h"
#include "xxfclib/memory/xx_memory.h"

#define LINES_MAX_MODULE_STREAM (512ULL * 1024ULL * 1024ULL)

typedef struct lines_vec {
    void *items;
    size_t count;
    size_t capacity;
    size_t elem_size;
} lines_vec;

static bool lines_vec_init(lines_vec *v, size_t elem_size, size_t initial_cap)
{
    v->count = 0;
    v->capacity = (initial_cap > 0) ? initial_cap : 8;
    v->elem_size = elem_size;
    v->items = xx_mem_alloc(v->capacity * elem_size);
    return v->items != NULL;
}

static void *lines_vec_add(lines_vec *v)
{
    if (v->count >= v->capacity) {
        size_t new_cap = v->capacity < 8 ? 8 : v->capacity * 2;
        void *p = xx_mem_realloc(v->items, new_cap * v->elem_size);
        if (!p) {
            return NULL;
        }
        v->items = p;
        v->capacity = new_cap;
    }
    void *item = (char *)v->items + (v->count * v->elem_size);
    v->count++;
    return item;
}

static void lines_vec_free(lines_vec *v)
{
    xx_mem_free(v->items);
    v->items = NULL;
    v->count = 0;
    v->capacity = 0;
}

static uint16_t lines_read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t lines_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t lines_find_file(const xx_ms_pdb_source_file *files, size_t count, uint32_t checksum_offset, bool is_sorted)
{
    if (count == 0 || !files) {
        return -1;
    }
    if (is_sorted) {
        int32_t low = 0;
        int32_t high = (int32_t)count - 1;
        while (low <= high) {
            int32_t mid = low + (high - low) / 2;
            if (files[mid].checksum_offset == checksum_offset) {
                return mid;
            }
            if (files[mid].checksum_offset < checksum_offset) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (files[i].checksum_offset == checksum_offset) {
            return (int32_t)i;
        }
    }
    return -1;
}

const char *xx_ms_pdb_checksum_kind_to_string(uint8_t kind)
{
    switch (kind) {
    case XX_MS_PDB_CHECKSUM_NONE: return "None";
    case XX_MS_PDB_CHECKSUM_MD5: return "MD5";
    case XX_MS_PDB_CHECKSUM_SHA1: return "SHA1";
    case XX_MS_PDB_CHECKSUM_SHA256: return "SHA256";
    default: return "Unknown";
    }
}

const char *xx_ms_pdb_debug_subsection_to_string(uint32_t kind)
{
    switch (kind & 0x7FFFFFFFu) {
    case 0xF1: return "DEBUG_S_SYMBOLS";
    case 0xF2: return "DEBUG_S_LINES";
    case 0xF3: return "DEBUG_S_STRINGTABLE";
    case 0xF4: return "DEBUG_S_FILECHKSMS";
    case 0xF5: return "DEBUG_S_FRAMEDATA";
    case 0xF6: return "DEBUG_S_INLINEELINES";
    case 0xF7: return "DEBUG_S_CROSSSCOPEIMPORTS";
    case 0xF8: return "DEBUG_S_CROSSSCOPEEXPORTS";
    case 0xF9: return "DEBUG_S_IL_LINES";
    case 0xFA: return "DEBUG_S_FUNC_MDTOKEN_MAP";
    case 0xFB: return "DEBUG_S_TYPE_MDTOKEN_MAP";
    case 0xFC: return "DEBUG_S_MERGED_ASSEMBLYINPUT";
    case 0xFD: return "DEBUG_S_COFF_SYMBOL_RVA";
    default: return "Unknown";
    }
}

void xx_ms_pdb_module_lines_free(xx_ms_pdb_module_lines *lines)
{
    if (!lines) {
        return;
    }
    if (lines->internal_) {
        xx_arena_t *arena = (xx_arena_t *)lines->internal_;
        xx_arena_free(arena);
        xx_mem_free(arena);
    }
    memset(lines, 0, sizeof(*lines));
}

bool xx_ms_pdb_get_module_lines(xx_ms_pdb *pdb, int32_t module_index,
                                const xx_ms_pdb_names *names,
                                const xx_ms_pdb_addr_map *map,
                                xx_ms_pdb_module_lines *out)
{
    if (!pdb || !out || module_index < 0) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->module_index = module_index;

    xx_arena_t *arena = (xx_arena_t *)xx_mem_calloc(1, sizeof(xx_arena_t));
    if (!arena) {
        return false;
    }
    xx_arena_init(arena);
    out->internal_ = arena;

    xx_ms_pdb_module_list mod_list = {0};
    if (!xx_ms_pdb_get_modules(pdb, &mod_list)) {
        return true;
    }
    if ((size_t)module_index >= mod_list.count) {
        xx_ms_pdb_module_list_free(&mod_list);
        xx_ms_pdb_module_lines_free(out);
        return false;
    }
    xx_ms_pdb_module mod = mod_list.items[module_index];
    xx_ms_pdb_module_list_free(&mod_list);

    if (mod.stream_index == 0xFFFF || mod.c13_byte_size == 0) {
        return true;
    }

    int64_t stream_size = xx_ms_pdb_get_stream_size(pdb, mod.stream_index);
    if (stream_size < 4) {
        return true;
    }

    uint32_t stream_sig = 0;
    if (xx_ms_pdb_read_stream(pdb, mod.stream_index, 0, &stream_sig, 4) != 4) {
        return true;
    }
    if (stream_sig != 4) {
        return true;
    }

    uint64_t c13_off = (uint64_t)mod.sym_byte_size + (uint64_t)mod.c11_byte_size;
    if (c13_off >= (uint64_t)stream_size) {
        return true;
    }
    uint64_t c13_size = (uint64_t)mod.c13_byte_size;
    if (c13_off + c13_size > (uint64_t)stream_size) {
        c13_size = (uint64_t)stream_size - c13_off;
    }
    if (c13_size > LINES_MAX_MODULE_STREAM) {
        c13_size = LINES_MAX_MODULE_STREAM;
    }
    if (c13_size == 0) {
        return true;
    }

    uint8_t *c13_buf = (uint8_t *)xx_mem_alloc((size_t)c13_size);
    if (!c13_buf) {
        xx_ms_pdb_module_lines_free(out);
        return false;
    }
    int64_t bytes_read = xx_ms_pdb_read_stream(pdb, mod.stream_index, (int64_t)c13_off, c13_buf, (int64_t)c13_size);
    if (bytes_read <= 0) {
        xx_mem_free(c13_buf);
        return true;
    }
    c13_size = (uint64_t)bytes_read;

    /* Pass 1: Collect files from DEBUG_S_FILECHKSMS (0xF4) */
    lines_vec files_vec;
    if (!lines_vec_init(&files_vec, sizeof(xx_ms_pdb_source_file), 16)) {
        xx_mem_free(c13_buf);
        xx_ms_pdb_module_lines_free(out);
        return false;
    }

    bool files_sorted = true;
    uint32_t last_chk_off = 0;

    size_t sub_off = 0;
    while (sub_off + 8 <= (size_t)c13_size) {
        uint32_t kind = lines_read_u32(c13_buf + sub_off);
        uint32_t length = lines_read_u32(c13_buf + sub_off + 4);
        size_t payload_off = sub_off + 8;
        if ((uint64_t)payload_off + (uint64_t)length > c13_size) {
            break;
        }
        const uint8_t *payload = c13_buf + payload_off;
        uint32_t kind_id = kind & 0x7FFFFFFFu;

        if (kind_id == 0xF4) {
            size_t entry_off = 0;
            while (entry_off + 6 <= (size_t)length) {
                uint32_t name_offset = lines_read_u32(payload + entry_off);
                uint8_t chk_size = payload[entry_off + 4];
                uint8_t chk_kind = payload[entry_off + 5];
                size_t entry_end = entry_off + 6 + (size_t)chk_size;
                if (entry_end > (size_t)length) {
                    break;
                }
                xx_ms_pdb_source_file *sf = (xx_ms_pdb_source_file *)lines_vec_add(&files_vec);
                if (!sf) {
                    break;
                }
                memset(sf, 0, sizeof(*sf));
                sf->checksum_offset = (uint32_t)entry_off;
                sf->name_offset = name_offset;
                sf->checksum_kind = chk_kind;
                sf->checksum_size = (chk_size > 64) ? 64 : chk_size;
                memcpy(sf->checksum, payload + entry_off + 6, sf->checksum_size);
                const char *str = names ? xx_ms_pdb_names_get(names, name_offset) : NULL;
                sf->name = xx_arena_strdup(arena, str ? str : "");

                if (files_vec.count > 1) {
                    if (sf->checksum_offset <= last_chk_off) {
                        files_sorted = false;
                    }
                }
                last_chk_off = sf->checksum_offset;

                size_t next_off = (entry_end + 3u) & ~3u;
                if (next_off <= entry_off) {
                    break;
                }
                entry_off = next_off;
            }
        }

        size_t padded = (length + 3u) & ~3u;
        if (payload_off + padded < payload_off) {
            break;
        }
        sub_off = payload_off + padded;
    }

    if (files_vec.count > 0) {
        out->files = (xx_ms_pdb_source_file *)xx_arena_alloc_array(arena, files_vec.count, sizeof(xx_ms_pdb_source_file));
        if (!out->files) {
            lines_vec_free(&files_vec);
            xx_mem_free(c13_buf);
            xx_ms_pdb_module_lines_free(out);
            return false;
        }
        memcpy(out->files, files_vec.items, files_vec.count * sizeof(xx_ms_pdb_source_file));
        out->file_count = files_vec.count;
    }
    lines_vec_free(&files_vec);

    /* Pass 2: Lines, Inlinees, Others */
    lines_vec lines_v, inlinees_v, others_v;
    if (!lines_vec_init(&lines_v, sizeof(xx_ms_pdb_line), 64) ||
        !lines_vec_init(&inlinees_v, sizeof(xx_ms_pdb_inlinee_line), 16) ||
        !lines_vec_init(&others_v, sizeof(xx_ms_pdb_subsection), 8)) {
        lines_vec_free(&lines_v);
        lines_vec_free(&inlinees_v);
        lines_vec_free(&others_v);
        xx_mem_free(c13_buf);
        xx_ms_pdb_module_lines_free(out);
        return false;
    }

    sub_off = 0;
    while (sub_off + 8 <= (size_t)c13_size) {
        uint32_t kind = lines_read_u32(c13_buf + sub_off);
        uint32_t length = lines_read_u32(c13_buf + sub_off + 4);
        size_t payload_off = sub_off + 8;
        if ((uint64_t)payload_off + (uint64_t)length > c13_size) {
            break;
        }
        const uint8_t *payload = c13_buf + payload_off;
        uint32_t kind_id = kind & 0x7FFFFFFFu;

        if (kind_id == 0xF2) {
            /* DEBUG_S_LINES */
            if (length >= 12) {
                uint32_t offCon = lines_read_u32(payload + 0);
                uint16_t segCon = lines_read_u16(payload + 4);
                uint16_t flags  = lines_read_u16(payload + 6);
                uint32_t cbCon  = lines_read_u32(payload + 8);

                size_t blk_off = 12;
                while (blk_off + 12 <= (size_t)length) {
                    uint32_t file_id = lines_read_u32(payload + blk_off + 0);
                    uint32_t nLines  = lines_read_u32(payload + blk_off + 4);
                    uint32_t cbBlock = lines_read_u32(payload + blk_off + 8);

                    if (nLines > (SIZE_MAX - 12u) / 12u) {
                        break;
                    }
                    size_t line_bytes = (size_t)nLines * 8u;
                    size_t col_bytes  = (flags & 1) ? ((size_t)nLines * 4u) : 0u;
                    size_t min_block_size = 12u + line_bytes + col_bytes;

                    if ((size_t)cbBlock < min_block_size || blk_off + (size_t)cbBlock > (size_t)length) {
                        break;
                    }

                    int32_t file_idx = lines_find_file(out->files, out->file_count, file_id, files_sorted);
                    const uint8_t *lines_data = payload + blk_off + 12;
                    const uint8_t *cols_data  = (flags & 1) ? (lines_data + line_bytes) : NULL;

                    for (uint32_t i = 0; i < nLines; ++i) {
                        uint32_t entry_off = lines_read_u32(lines_data + (size_t)i * 8u);
                        uint32_t bits      = lines_read_u32(lines_data + (size_t)i * 8u + 4);

                        xx_ms_pdb_line *ln = (xx_ms_pdb_line *)lines_vec_add(&lines_v);
                        if (!ln) {
                            break;
                        }
                        memset(ln, 0, sizeof(*ln));
                        ln->segment = segCon;
                        ln->offset  = offCon + entry_off;
                        ln->file    = file_idx;

                        if (map) {
                            ln->rva = xx_ms_pdb_addr_map_rva(map, ln->segment, ln->offset);
                        } else {
                            ln->rva = XX_MS_PDB_NO_RVA;
                        }

                        uint32_t linenumStart = bits & 0x00FFFFFFu;
                        uint32_t deltaLineEnd = (bits >> 24) & 0x7Fu;
                        ln->line = linenumStart;
                        ln->line_end = linenumStart + deltaLineEnd;
                        ln->is_statement = ((bits >> 31) & 1u) != 0;

                        if (flags & 1) {
                            ln->column     = lines_read_u16(cols_data + (size_t)i * 4u);
                            ln->column_end = lines_read_u16(cols_data + (size_t)i * 4u + 2);
                        } else {
                            ln->column     = 0;
                            ln->column_end = 0;
                        }

                        if (i + 1 < nLines) {
                            uint32_t next_entry_off = lines_read_u32(lines_data + (size_t)(i + 1) * 8u);
                            if (next_entry_off >= entry_off) {
                                ln->length = next_entry_off - entry_off;
                            } else {
                                ln->length = 0;
                            }
                        } else {
                            if (cbCon >= entry_off) {
                                ln->length = cbCon - entry_off;
                            } else {
                                ln->length = 0;
                            }
                        }
                    }

                    blk_off += (size_t)cbBlock;
                }
            }
        } else if (kind_id == 0xF6) {
            /* DEBUG_S_INLINEELINES */
            if (length >= 4) {
                uint32_t sig = lines_read_u32(payload);
                size_t in_off = 4;
                if (sig == 0) {
                    while (in_off + 12 <= (size_t)length) {
                        uint32_t inlinee_id = lines_read_u32(payload + in_off);
                        uint32_t file_id    = lines_read_u32(payload + in_off + 4);
                        uint32_t line_no    = lines_read_u32(payload + in_off + 8);
                        in_off += 12;

                        xx_ms_pdb_inlinee_line *inl = (xx_ms_pdb_inlinee_line *)lines_vec_add(&inlinees_v);
                        if (!inl) {
                            break;
                        }
                        memset(inl, 0, sizeof(*inl));
                        inl->inlinee = inlinee_id;
                        inl->file = lines_find_file(out->files, out->file_count, file_id, files_sorted);
                        inl->line = line_no;
                        inl->extra_file_count = 0;
                        inl->extra_files = NULL;
                    }
                } else if (sig == 1) {
                    while (in_off + 16 <= (size_t)length) {
                        uint32_t inlinee_id = lines_read_u32(payload + in_off);
                        uint32_t file_id    = lines_read_u32(payload + in_off + 4);
                        uint32_t line_no    = lines_read_u32(payload + in_off + 8);
                        uint32_t extra_cnt  = lines_read_u32(payload + in_off + 12);

                        if ((size_t)extra_cnt > ((size_t)length - (in_off + 16)) / 4u) {
                            break;
                        }

                        xx_ms_pdb_inlinee_line *inl = (xx_ms_pdb_inlinee_line *)lines_vec_add(&inlinees_v);
                        if (!inl) {
                            break;
                        }
                        memset(inl, 0, sizeof(*inl));
                        inl->inlinee = inlinee_id;
                        inl->file = lines_find_file(out->files, out->file_count, file_id, files_sorted);
                        inl->line = line_no;
                        inl->extra_file_count = extra_cnt;
                        if (extra_cnt > 0) {
                            inl->extra_files = (int32_t *)xx_arena_alloc_array(arena, extra_cnt, sizeof(int32_t));
                            if (inl->extra_files) {
                                for (uint32_t k = 0; k < extra_cnt; ++k) {
                                    uint32_t ef_id = lines_read_u32(payload + in_off + 16 + (size_t)k * 4u);
                                    inl->extra_files[k] = lines_find_file(out->files, out->file_count, ef_id, files_sorted);
                                }
                            }
                        }
                        in_off += 16 + (size_t)extra_cnt * 4u;
                    }
                }
            }
        } else if (kind_id != 0xF4) {
            xx_ms_pdb_subsection *other = (xx_ms_pdb_subsection *)lines_vec_add(&others_v);
            if (other) {
                other->kind = kind;
                other->offset = (uint32_t)(c13_off + sub_off + 8);
                other->size = length;
            }
        }

        size_t padded = (length + 3u) & ~3u;
        if (payload_off + padded < payload_off) {
            break;
        }
        sub_off = payload_off + padded;
    }

    if (lines_v.count > 0) {
        out->lines = (xx_ms_pdb_line *)xx_arena_alloc_array(arena, lines_v.count, sizeof(xx_ms_pdb_line));
        if (!out->lines) {
            lines_vec_free(&lines_v);
            lines_vec_free(&inlinees_v);
            lines_vec_free(&others_v);
            xx_mem_free(c13_buf);
            xx_ms_pdb_module_lines_free(out);
            return false;
        }
        memcpy(out->lines, lines_v.items, lines_v.count * sizeof(xx_ms_pdb_line));
        out->line_count = lines_v.count;
    }
    lines_vec_free(&lines_v);

    if (inlinees_v.count > 0) {
        out->inlinees = (xx_ms_pdb_inlinee_line *)xx_arena_alloc_array(arena, inlinees_v.count, sizeof(xx_ms_pdb_inlinee_line));
        if (!out->inlinees) {
            lines_vec_free(&inlinees_v);
            lines_vec_free(&others_v);
            xx_mem_free(c13_buf);
            xx_ms_pdb_module_lines_free(out);
            return false;
        }
        memcpy(out->inlinees, inlinees_v.items, inlinees_v.count * sizeof(xx_ms_pdb_inlinee_line));
        out->inlinee_count = inlinees_v.count;
    }
    lines_vec_free(&inlinees_v);

    if (others_v.count > 0) {
        out->others = (xx_ms_pdb_subsection *)xx_arena_alloc_array(arena, others_v.count, sizeof(xx_ms_pdb_subsection));
        if (!out->others) {
            lines_vec_free(&others_v);
            xx_mem_free(c13_buf);
            xx_ms_pdb_module_lines_free(out);
            return false;
        }
        memcpy(out->others, others_v.items, others_v.count * sizeof(xx_ms_pdb_subsection));
        out->other_count = others_v.count;
    }
    lines_vec_free(&others_v);

    xx_mem_free(c13_buf);
    return true;
}
