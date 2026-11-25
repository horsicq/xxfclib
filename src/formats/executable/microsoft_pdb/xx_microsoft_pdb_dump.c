/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft PDB generic stream walker: walks every stream of a PDB and emits
 * all of its data as a hierarchy of events (BEGIN, FIELD, END).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_debug.h"
#include "xxfclib/memory/xx_memory.h"

#define DUMP_MAX_STREAM_SIZE (512u * 1024u * 1024u)
#define DUMP_BLOB_CAP 4096u

/* Matching internal layout of xx_ms_pdb up to stream directory */
typedef struct pdb_chunk pdb_chunk;
typedef struct pdb_istr pdb_istr;
typedef struct pdb_stream_rec {
    uint32_t size;
    uint32_t block_count;
    uint32_t dir_offset;
    bool is_nil;
} pdb_stream_rec;

typedef struct xx_ms_pdb_internal {
    xx_io_device *device;
    xx_pd_struct *pd;
    int64_t file_size;
    xx_ms_pdb_type type;
    bool is_legacy;
    bool strict;
    xx_ms_pdb_msf_header msf;

    /* string arena + intern table */
    pdb_chunk *chunks;
    pdb_istr *intern;
    size_t intern_capacity;
    size_t intern_count;
    uint8_t *scratch;
    size_t scratch_size;

    /* stream directory */
    bool streams_loaded;
    uint8_t *directory;
    int32_t directory_size;
    int32_t block_width; /* bytes per block number: 4 (DS) or 2 (JG) */
    pdb_stream_rec *streams;
    int32_t stream_count;
} xx_ms_pdb_internal;

typedef struct dump_ctx {
    xx_ms_pdb *pdb;
    const xx_ms_pdb_names *names;
    xx_ms_pdb_dump_fn fn;
    void *user;
    int32_t depth;
    bool stopped;

    uint8_t *buf;
    size_t buf_cap;

    xx_ms_pdb_msf_header msf;
    int32_t stream_count;
    xx_ms_pdb_info pdb_info;
    xx_ms_pdb_tpi_info tpi_info;
    xx_ms_pdb_tpi_info ipi_info;
    xx_ms_pdb_dbi_info dbi_info;

    char **stream_roles;
    int32_t roles_count;
} dump_ctx;

/* ---- Little-endian helpers ------------------------------------------------ */

static uint8_t dump_u8(const uint8_t *data, size_t size, size_t offset)
{
    if (offset < size) {
        return data[offset];
    }
    return 0;
}

static uint16_t dump_u16(const uint8_t *data, size_t size, size_t offset)
{
    if (offset + 1 < size) {
        return (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8));
    }
    return 0;
}

static uint32_t dump_u32(const uint8_t *data, size_t size, size_t offset)
{
    if (offset + 3 < size) {
        return (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
    }
    return 0;
}

static uint64_t dump_u64(const uint8_t *data, size_t size, size_t offset)
{
    if (offset + 7 < size) {
        uint64_t lo = dump_u32(data, size, offset);
        uint64_t hi = dump_u32(data, size, offset + 4);
        return lo | (hi << 32);
    }
    return 0;
}

static int16_t dump_i16(const uint8_t *data, size_t size, size_t offset)
{
    return (int16_t)dump_u16(data, size, offset);
}

static int32_t dump_i32(const uint8_t *data, size_t size, size_t offset)
{
    return (int32_t)dump_u32(data, size, offset);
}

static void dump_guid_str(const uint8_t *g, char *out, size_t out_sz)
{
    uint32_t d1 = (uint32_t)g[0] | ((uint32_t)g[1] << 8) | ((uint32_t)g[2] << 16) | ((uint32_t)g[3] << 24);
    uint16_t d2 = (uint16_t)((uint16_t)g[4] | ((uint16_t)g[5] << 8));
    uint16_t d3 = (uint16_t)((uint16_t)g[6] | ((uint16_t)g[7] << 8));
    snprintf(out, out_sz, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", d1, d2, d3, g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

/* ---- Event emission helpers ------------------------------------------------ */

static void dump_begin(dump_ctx *ctx, const char *name, int32_t stream, int64_t offset)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_BEGIN;
    ev.value_kind = XX_MS_PDB_FV_NONE;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.s = "";
    ev.stream = stream;
    ev.offset = offset;
    if (!ctx->fn(ctx->user, &ev)) {
        ctx->stopped = true;
    }
    ctx->depth++;
}

static void dump_end(dump_ctx *ctx, const char *name, int32_t stream, int64_t offset)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    if (ctx->depth > 0) ctx->depth--;
    ev.event = XX_MS_PDB_EV_END;
    ev.value_kind = XX_MS_PDB_FV_NONE;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.s = "";
    ev.stream = stream;
    ev.offset = offset;
    if (!ctx->fn(ctx->user, &ev)) {
        ctx->stopped = true;
    }
}

static void dump_field_uint(dump_ctx *ctx, const char *name, uint64_t val)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_UINT;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.u = val;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void dump_field_int(dump_ctx *ctx, const char *name, int64_t val)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_INT;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.i = val;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void dump_field_hex(dump_ctx *ctx, const char *name, uint64_t val)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_HEX;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.u = val;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void dump_field_string(dump_ctx *ctx, const char *name, const char *str)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_STRING;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.s = str ? str : "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void dump_field_bool(dump_ctx *ctx, const char *name, bool val)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_BOOL;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.u = val ? 1 : 0;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void dump_field_enum(dump_ctx *ctx, const char *name, uint64_t val, const char *enum_str)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_ENUM;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.u = val;
    ev.s = enum_str ? enum_str : "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void dump_field_flags(dump_ctx *ctx, const char *name, uint64_t val, const char *flags_str)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_FLAGS;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.u = val;
    ev.s = flags_str ? flags_str : "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void dump_field_guid(dump_ctx *ctx, const char *name, const char *guid_str)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_GUID;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.s = guid_str ? guid_str : "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void dump_field_bytes(dump_ctx *ctx, const char *name, const uint8_t *bytes, size_t size)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_BYTES;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.bytes = bytes;
    ev.size = size;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void dump_field_error(dump_ctx *ctx, const char *err)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_STRING;
    ev.depth = ctx->depth;
    ev.name = "error";
    ev.s = err ? err : "error";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

/* ---- Reusable stream reading buffer --------------------------------------- */

static uint8_t *dump_read_stream(dump_ctx *ctx, int32_t stream_index, size_t *out_size)
{
    int64_t size = 0;
    *out_size = 0;
    if (stream_index < 0 || stream_index >= ctx->stream_count) {
        return NULL;
    }
    size = xx_ms_pdb_get_stream_size(ctx->pdb, stream_index);
    if (size <= 0 || size > (int64_t)DUMP_MAX_STREAM_SIZE) {
        return NULL;
    }
    if (ctx->buf_cap < (size_t)size + 8) {
        size_t new_cap = (size_t)size + 16384;
        uint8_t *new_buf = (uint8_t *)xx_mem_realloc(ctx->buf, new_cap);
        if (!new_buf) {
            return NULL;
        }
        ctx->buf = new_buf;
        ctx->buf_cap = new_cap;
    }
    if (xx_ms_pdb_read_stream(ctx->pdb, stream_index, 0, ctx->buf, size) != size) {
        return NULL;
    }
    memset(ctx->buf + size, 0, 8);
    *out_size = (size_t)size;
    return ctx->buf;
}

/* ---- Roles mapping -------------------------------------------------------- */

static void dump_set_role(dump_ctx *ctx, int32_t stream, const char *role)
{
    if (stream >= 0 && stream < ctx->roles_count && role && !ctx->stream_roles[stream]) {
        size_t len = strlen(role);
        char *copy = (char *)xx_mem_alloc(len + 1);
        if (copy) {
            memcpy(copy, role, len + 1);
            ctx->stream_roles[stream] = copy;
        }
    }
}

static void dump_init_roles(dump_ctx *ctx)
{
    int32_t i = 0;
    xx_ms_pdb_module_list mods = {0};
    uint8_t *info_data = NULL;
    size_t info_size = 0;

    if (ctx->stream_count <= 0) return;
    ctx->roles_count = ctx->stream_count;
    ctx->stream_roles = (char **)xx_mem_calloc((size_t)ctx->roles_count, sizeof(char *));
    if (!ctx->stream_roles) return;

    dump_set_role(ctx, 0, "Old MSF Directory");
    dump_set_role(ctx, 1, "PDB Stream");
    dump_set_role(ctx, 2, "TPI Stream");
    dump_set_role(ctx, 3, "DBI Stream");
    dump_set_role(ctx, 4, "IPI Stream");

    if (ctx->dbi_info.is_valid) {
        if (ctx->dbi_info.sym_record_stream != 0xFFFF) dump_set_role(ctx, ctx->dbi_info.sym_record_stream, "Symbol Records");
        if (ctx->dbi_info.global_stream_index != 0xFFFF) dump_set_role(ctx, ctx->dbi_info.global_stream_index, "Global Symbol Hash");
        if (ctx->dbi_info.public_stream_index != 0xFFFF) dump_set_role(ctx, ctx->dbi_info.public_stream_index, "Public Symbol Hash");

        if (ctx->dbi_info.dbg_streams) {
            static const char *const dbg_names[] = {
                "FPO Data",      "Exception Data", "Fixup Data", "OMAP To Source Data", "OMAP From Source Data",       "Section Header Data",
                "Token RID Map", "Xdata",          "Pdata",      "New FPO Data",        "Original Section Header Data"};
            for (i = 0; i < (int32_t)ctx->dbi_info.dbg_stream_count && i < 11; i++) {
                if (ctx->dbi_info.dbg_streams[i] != 0xFFFF) {
                    dump_set_role(ctx, ctx->dbi_info.dbg_streams[i], dbg_names[i]);
                }
            }
        }
    }

    if (ctx->tpi_info.is_valid && ctx->tpi_info.hash_stream_index != 0xFFFF) {
        dump_set_role(ctx, ctx->tpi_info.hash_stream_index, "TPI Hash");
    }
    if (ctx->ipi_info.is_valid && ctx->ipi_info.hash_stream_index != 0xFFFF) {
        dump_set_role(ctx, ctx->ipi_info.hash_stream_index, "IPI Hash");
    }

    if (xx_ms_pdb_get_modules(ctx->pdb, &mods)) {
        for (i = 0; i < (int32_t)mods.count; i++) {
            if (mods.items[i].stream_index != 0xFFFF) {
                char mod_role[512];
                snprintf(mod_role, sizeof(mod_role), "Module \"%s\"", mods.items[i].module_name ? mods.items[i].module_name : "");
                dump_set_role(ctx, mods.items[i].stream_index, mod_role);
            }
        }
        xx_ms_pdb_module_list_free(&mods);
    }

    /* Read Stream 1 named stream map to tag named streams */
    info_data = dump_read_stream(ctx, XX_MS_PDB_STREAM_PDB, &info_size);
    if (info_data && info_size >= 28) {
        uint32_t version = dump_u32(info_data, info_size, 0);
        size_t off = (ctx->msf.is_valid && version >= XX_MS_PDB_PDB_IMPL_VC70) ? 28 : 12;
        if (off + 4 <= info_size) {
            uint32_t str_buf_sz = dump_u32(info_data, info_size, off);
            size_t str_buf_off = off + 4;
            size_t map_off = str_buf_off + str_buf_sz;
            if (map_off + 12 <= info_size) {
                uint32_t num_entries = dump_u32(info_data, info_size, map_off);
                uint32_t pres_words = dump_u32(info_data, info_size, map_off + 8);
                size_t del_off = map_off + 12 + (size_t)pres_words * 4;
                if (del_off + 4 <= info_size) {
                    uint32_t del_words = dump_u32(info_data, info_size, del_off);
                    size_t buckets_off = del_off + 4 + (size_t)del_words * 4;
                    for (i = 0; i < (int32_t)num_entries && buckets_off + 8 <= info_size; i++) {
                        uint32_t name_off = dump_u32(info_data, info_size, buckets_off);
                        uint32_t strm_idx = dump_u32(info_data, info_size, buckets_off + 4);
                        buckets_off += 8;
                        if (name_off < str_buf_sz && str_buf_off + name_off < info_size) {
                            const char *strm_name = (const char *)info_data + str_buf_off + name_off;
                            char named_role[512];
                            snprintf(named_role, sizeof(named_role), "Named Stream \"%s\"", strm_name);
                            dump_set_role(ctx, (int32_t)strm_idx, named_role);
                        }
                    }
                }
            }
        }
    }
}

static void dump_cleanup_roles(dump_ctx *ctx)
{
    if (ctx->stream_roles) {
        int32_t i = 0;
        for (i = 0; i < ctx->roles_count; i++) {
            if (ctx->stream_roles[i]) {
                xx_mem_free(ctx->stream_roles[i]);
            }
        }
        xx_mem_free(ctx->stream_roles);
        ctx->stream_roles = NULL;
    }
}

/* ---- Subsection and Flag String Converters -------------------------------- */

static const char *dump_subsection_kind_str(uint32_t kind)
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

static const char *dump_checksum_kind_str(uint8_t kind)
{
    switch (kind) {
        case 0: return "None";
        case 1: return "MD5";
        case 2: return "SHA1";
        case 3: return "SHA256";
        default: return "Unknown";
    }
}

static const char *dump_secmap_flags_str(uint16_t flags, char *out, size_t out_sz)
{
    out[0] = 0;
    if (flags & 0x0001) strcat_s(out, out_sz, "read | ");
    if (flags & 0x0002) strcat_s(out, out_sz, "write | ");
    if (flags & 0x0004) strcat_s(out, out_sz, "execute | ");
    if (flags & 0x0008) strcat_s(out, out_sz, "32 bit addr | ");
    if (flags & 0x0100) strcat_s(out, out_sz, "selector | ");
    if (flags & 0x0200) strcat_s(out, out_sz, "absolute addr | ");
    if (flags & 0x0400) strcat_s(out, out_sz, "group | ");
    size_t len = strlen(out);
    if (len >= 3) {
        out[len - 3] = 0;
    }
    return out;
}

static const char *dump_section_chars_str(uint32_t chars, char *out, size_t out_sz)
{
    out[0] = 0;
    if (chars & 0x00000020u) strcat_s(out, out_sz, "CODE | ");
    if (chars & 0x00000040u) strcat_s(out, out_sz, "INITIALIZED_DATA | ");
    if (chars & 0x00000080u) strcat_s(out, out_sz, "UNINITIALIZED_DATA | ");
    if (chars & 0x02000000u) strcat_s(out, out_sz, "DISCARDABLE | ");
    if (chars & 0x10000000u) strcat_s(out, out_sz, "SHARED | ");
    if (chars & 0x20000000u) strcat_s(out, out_sz, "EXECUTE | ");
    if (chars & 0x40000000u) strcat_s(out, out_sz, "READ | ");
    if (chars & 0x80000000u) strcat_s(out, out_sz, "WRITE | ");
    size_t len = strlen(out);
    if (len >= 3) {
        out[len - 3] = 0;
    }
    return out;
}

static const char *dump_dbi_flags_str(uint16_t flags, char *out, size_t out_sz)
{
    out[0] = 0;
    if (flags & XX_MS_PDB_DBI_FLAG_INCREMENTAL) strcat_s(out, out_sz, "INCREMENTAL | ");
    if (flags & XX_MS_PDB_DBI_FLAG_STRIPPED) strcat_s(out, out_sz, "STRIPPED | ");
    if (flags & XX_MS_PDB_DBI_FLAG_CTYPES) strcat_s(out, out_sz, "CTYPES | ");
    size_t len = strlen(out);
    if (len >= 3) {
        out[len - 3] = 0;
    }
    return out;
}

/* ===========================================================================
 * Section Walkers
 * =========================================================================== */

/* ---- 1. MSF --------------------------------------------------------------- */
static void dump_walk_msf(dump_ctx *ctx)
{
    int32_t i = 0;
    xx_ms_pdb_internal *p_int = (xx_ms_pdb_internal *)ctx->pdb;

    dump_begin(ctx, "msf", -1, -1);

    dump_begin(ctx, "superblock", -1, 0);
    dump_field_string(ctx, "magic",
                      "Microsoft C/C++ MSF 7.00\r\n\x1a"
                      "DS\0\0\0");
    dump_field_uint(ctx, "block_size", ctx->msf.block_size);
    dump_field_uint(ctx, "free_block_map_block", ctx->msf.free_block_map_block);
    dump_field_uint(ctx, "block_count", ctx->msf.num_blocks);
    dump_field_uint(ctx, "directory_bytes", ctx->msf.num_directory_bytes);
    dump_field_uint(ctx, "block_map_address", ctx->msf.block_map_addr);
    dump_end(ctx, "superblock", -1, 0);

    for (i = 0; i < ctx->stream_count && !ctx->stopped; i++) {
        int64_t size = xx_ms_pdb_get_stream_size(ctx->pdb, i);
        uint32_t block_count = 0;
        uint32_t dir_off = 0;
        bool has_blocks = false;

        if (p_int && p_int->streams && i < p_int->stream_count) {
            block_count = p_int->streams[i].block_count;
            dir_off = p_int->streams[i].dir_offset;
            has_blocks = (p_int->directory != NULL);
        } else if (size > 0 && ctx->msf.block_size > 0) {
            block_count = (uint32_t)(((uint64_t)size + ctx->msf.block_size - 1) / ctx->msf.block_size);
        }

        dump_begin(ctx, "stream", i, -1);
        dump_field_uint(ctx, "index", (uint64_t)i);
        if (size < 0) {
            dump_field_hex(ctx, "size", 0xFFFFFFFFu);
        } else {
            dump_field_uint(ctx, "size", (uint64_t)size);
        }
        dump_field_uint(ctx, "block_count", block_count);

        if (i < ctx->roles_count && ctx->stream_roles[i]) {
            dump_field_string(ctx, "role", ctx->stream_roles[i]);
        }

        if (has_blocks && block_count > 0) {
            uint32_t emit_count = (block_count > 64) ? 64 : block_count;
            uint32_t b = 0;
            dump_begin(ctx, "blocks", i, -1);
            for (b = 0; b < emit_count && !ctx->stopped; b++) {
                uint32_t blk = 0;
                if (p_int->block_width == 4) {
                    blk = dump_u32(p_int->directory, (size_t)p_int->directory_size, dir_off + (size_t)b * 4);
                } else {
                    blk = dump_u16(p_int->directory, (size_t)p_int->directory_size, dir_off + (size_t)b * 2);
                }
                dump_field_uint(ctx, "block", blk);
            }
            dump_end(ctx, "blocks", i, -1);
        }

        dump_end(ctx, "stream", i, -1);
    }

    dump_end(ctx, "msf", -1, -1);
}

/* ---- 2. INFO (Stream 1) --------------------------------------------------- */
static void dump_walk_info(dump_ctx *ctx)
{
    size_t size = 0;
    uint8_t *data = dump_read_stream(ctx, XX_MS_PDB_STREAM_PDB, &size);
    size_t off = 0;
    char guid[48] = {0};

    if (!data || size < 12) {
        dump_begin(ctx, "info", XX_MS_PDB_STREAM_PDB, 0);
        dump_field_error(ctx, "Stream 1 missing or truncated");
        dump_end(ctx, "info", XX_MS_PDB_STREAM_PDB, 0);
        return;
    }

    dump_begin(ctx, "info", XX_MS_PDB_STREAM_PDB, 0);
    uint32_t version = dump_u32(data, size, 0);
    uint32_t signature = dump_u32(data, size, 4);
    uint32_t age = dump_u32(data, size, 8);

    const char *ver_str = "Unknown";
    if (version == XX_MS_PDB_PDB_IMPL_VC70) ver_str = "VC70";
    else if (version == XX_MS_PDB_PDB_IMPL_VC80) ver_str = "VC80";
    else if (version == XX_MS_PDB_PDB_IMPL_VC110) ver_str = "VC110";
    else if (version == XX_MS_PDB_PDB_IMPL_VC140) ver_str = "VC140";
    dump_field_enum(ctx, "version", version, ver_str);
    dump_field_hex(ctx, "signature", signature);
    dump_field_uint(ctx, "age", age);

    off = 12;
    if (version >= XX_MS_PDB_PDB_IMPL_VC70 && size >= 28) {
        dump_guid_str(data + 12, guid, sizeof(guid));
        dump_field_guid(ctx, "guid", guid);
        off = 28;
    }

    /* Named stream hash map */
    if (off + 4 <= size) {
        uint32_t str_buf_sz = dump_u32(data, size, off);
        size_t str_buf_off = off + 4;
        size_t map_off = str_buf_off + str_buf_sz;

        if (map_off + 12 <= size) {
            uint32_t num_entries = dump_u32(data, size, map_off);
            uint32_t capacity = dump_u32(data, size, map_off + 4);
            uint32_t pres_words = dump_u32(data, size, map_off + 8);
            size_t pres_off = map_off + 12;
            size_t del_off = pres_off + (size_t)pres_words * 4;

            dump_begin(ctx, "named_streams", XX_MS_PDB_STREAM_PDB, (int64_t)map_off);
            dump_field_uint(ctx, "size", num_entries);
            dump_field_uint(ctx, "capacity", capacity);

            if (del_off + 4 <= size) {
                uint32_t del_words = dump_u32(data, size, del_off);
                size_t buckets_off = del_off + 4 + (size_t)del_words * 4;
                uint32_t w = 0;

                dump_begin(ctx, "present_bitmap", XX_MS_PDB_STREAM_PDB, (int64_t)pres_off);
                for (w = 0; w < pres_words && pres_off + (size_t)w * 4 + 4 <= size && !ctx->stopped; w++) {
                    dump_field_hex(ctx, "word", dump_u32(data, size, pres_off + (size_t)w * 4));
                }
                dump_end(ctx, "present_bitmap", XX_MS_PDB_STREAM_PDB, (int64_t)pres_off);

                dump_begin(ctx, "deleted_bitmap", XX_MS_PDB_STREAM_PDB, (int64_t)(del_off + 4));
                for (w = 0; w < del_words && del_off + 4 + (size_t)w * 4 + 4 <= size && !ctx->stopped; w++) {
                    dump_field_hex(ctx, "word", dump_u32(data, size, del_off + 4 + (size_t)w * 4));
                }
                dump_end(ctx, "deleted_bitmap", XX_MS_PDB_STREAM_PDB, (int64_t)(del_off + 4));

                dump_begin(ctx, "buckets", XX_MS_PDB_STREAM_PDB, (int64_t)buckets_off);
                for (w = 0; w < num_entries && buckets_off + 8 <= size; w++) {
                    uint32_t name_off = dump_u32(data, size, buckets_off);
                    uint32_t strm_idx = dump_u32(data, size, buckets_off + 4);
                    buckets_off += 8;

                    dump_begin(ctx, "bucket", XX_MS_PDB_STREAM_PDB, (int64_t)(buckets_off - 8));
                    dump_field_uint(ctx, "name_offset", name_off);
                    dump_field_uint(ctx, "stream_index", strm_idx);
                    if (name_off < str_buf_sz && str_buf_off + name_off < size) {
                        dump_field_string(ctx, "name", (const char *)data + str_buf_off + name_off);
                    }
                    dump_end(ctx, "bucket", XX_MS_PDB_STREAM_PDB, (int64_t)(buckets_off - 8));
                }
                dump_end(ctx, "buckets", XX_MS_PDB_STREAM_PDB, (int64_t)buckets_off);

                /* Feature codes */
                size_t feat_off = buckets_off;
                if (feat_off + 4 <= size) {
                    feat_off += 4; /* skip niMac */
                }
                if (feat_off < size) {
                    dump_begin(ctx, "features", XX_MS_PDB_STREAM_PDB, (int64_t)feat_off);
                    while (feat_off + 4 <= size && !ctx->stopped) {
                        uint32_t feat = dump_u32(data, size, feat_off);
                        const char *fstr = "Unknown";
                        if (feat == XX_MS_PDB_PDB_FEATURE_VC110) fstr = "VC110";
                        else if (feat == XX_MS_PDB_PDB_FEATURE_VC140) fstr = "VC140";
                        else if (feat == XX_MS_PDB_PDB_FEATURE_NOTYPEMERGE) fstr = "NoTypeMerge";
                        else if (feat == XX_MS_PDB_PDB_FEATURE_MINIMALDEBUGINFO) fstr = "MinimalDebugInfo";
                        dump_field_enum(ctx, "feature", feat, fstr);
                        feat_off += 4;
                    }
                    dump_end(ctx, "features", XX_MS_PDB_STREAM_PDB, (int64_t)feat_off);
                }
            }
            dump_end(ctx, "named_streams", XX_MS_PDB_STREAM_PDB, (int64_t)map_off);
        }
    }

    dump_end(ctx, "info", XX_MS_PDB_STREAM_PDB, 0);
}

/* ---- 3. NAMES (Stream "/names") ------------------------------------------- */
static void dump_walk_names(dump_ctx *ctx)
{
    int32_t strm_idx = xx_ms_pdb_get_named_stream_index(ctx->pdb, "/names");
    size_t size = 0;
    uint8_t *data = NULL;
    size_t off = 0;

    if (strm_idx < 0) return;
    data = dump_read_stream(ctx, strm_idx, &size);
    if (!data || size < 12) {
        dump_begin(ctx, "names", strm_idx, 0);
        dump_field_error(ctx, "/names stream missing or invalid");
        dump_end(ctx, "names", strm_idx, 0);
        return;
    }

    uint32_t magic = dump_u32(data, size, 0);
    uint32_t hash_ver = dump_u32(data, size, 4);
    uint32_t buf_sz = dump_u32(data, size, 8);

    dump_begin(ctx, "names", strm_idx, 0);
    dump_field_hex(ctx, "magic", magic);
    dump_field_uint(ctx, "hash_version", hash_ver);
    dump_field_uint(ctx, "buffer_size", buf_sz);

    if (buf_sz > size - 12) buf_sz = (uint32_t)(size - 12);

    /* Strings */
    dump_begin(ctx, "strings", strm_idx, 12);
    off = 0;
    while (off < buf_sz && !ctx->stopped) {
        const char *s = (const char *)data + 12 + off;
        size_t slen = strlen(s);
        if (slen > 0) {
            dump_begin(ctx, "string", strm_idx, (int64_t)(12 + off));
            dump_field_uint(ctx, "offset", (uint64_t)off);
            dump_field_string(ctx, "text", s);
            dump_end(ctx, "string", strm_idx, (int64_t)(12 + off));
        }
        off += slen + 1;
    }
    dump_end(ctx, "strings", strm_idx, 12);

    /* Hash table after strings */
    size_t hash_off = 12 + buf_sz;
    if (hash_off + 4 <= size) {
        uint32_t bucket_cnt = dump_u32(data, size, hash_off);
        dump_field_uint(ctx, "bucket_count", bucket_cnt);
        hash_off += 4;

        dump_begin(ctx, "buckets", strm_idx, (int64_t)hash_off);
        for (uint32_t b = 0; b < bucket_cnt && hash_off + 4 <= size && !ctx->stopped; b++) {
            uint32_t b_off = dump_u32(data, size, hash_off);
            dump_field_uint(ctx, "offset", b_off);
            hash_off += 4;
        }
        dump_end(ctx, "buckets", strm_idx, (int64_t)hash_off);

        if (hash_off + 4 <= size) {
            uint32_t name_cnt = dump_u32(data, size, hash_off);
            dump_field_uint(ctx, "name_count", name_cnt);
        }
    }

    dump_end(ctx, "names", strm_idx, 0);
}

/* ---- 4. TPI / IPI --------------------------------------------------------- */
static void dump_walk_types_stream(dump_ctx *ctx, int32_t strm_idx, const char *sec_name, const xx_ms_pdb_tpi_info *info)
{
    size_t size = 0;
    uint8_t *data = dump_read_stream(ctx, strm_idx, &size);
    size_t off = 0;
    uint32_t curr_ti = 0;
    size_t rec_limit = 0;

    if (!data || size < 56) {
        dump_begin(ctx, sec_name, strm_idx, 0);
        dump_field_error(ctx, "Stream missing or truncated");
        dump_end(ctx, sec_name, strm_idx, 0);
        return;
    }

    dump_begin(ctx, sec_name, strm_idx, 0);

    /* Header */
    dump_begin(ctx, "header", strm_idx, 0);
    const char *vstr = "Unknown";
    if (info->version == XX_MS_PDB_TPI_V40) vstr = "V40";
    else if (info->version == XX_MS_PDB_TPI_V41) vstr = "V41";
    else if (info->version == XX_MS_PDB_TPI_V50) vstr = "V50";
    else if (info->version == XX_MS_PDB_TPI_V70) vstr = "V70";
    else if (info->version == XX_MS_PDB_TPI_V80) vstr = "V80";
    dump_field_enum(ctx, "version", info->version, vstr);
    dump_field_uint(ctx, "header_size", info->header_size);
    dump_field_uint(ctx, "type_index_begin", info->type_index_begin);
    dump_field_uint(ctx, "type_index_end", info->type_index_end);
    dump_field_uint(ctx, "type_record_bytes", info->type_record_bytes);
    dump_field_int(ctx, "hash_stream_index", (int64_t)(int16_t)info->hash_stream_index);
    dump_field_int(ctx, "hash_aux_stream_index", (int64_t)(int16_t)info->hash_aux_stream_index);
    dump_field_uint(ctx, "hash_key_size", info->hash_key_size);
    dump_field_uint(ctx, "num_hash_buckets", info->num_hash_buckets);
    dump_field_int(ctx, "hash_value_buffer_offset", info->hash_value_buffer_offset);
    dump_field_uint(ctx, "hash_value_buffer_length", info->hash_value_buffer_length);
    dump_field_int(ctx, "index_offset_buffer_offset", info->index_offset_buffer_offset);
    dump_field_uint(ctx, "index_offset_buffer_length", info->index_offset_buffer_length);
    dump_field_int(ctx, "hash_adj_buffer_offset", info->hash_adj_buffer_offset);
    dump_field_uint(ctx, "hash_adj_buffer_length", info->hash_adj_buffer_length);
    dump_end(ctx, "header", strm_idx, 0);

    /* Records */
    off = info->header_size;
    rec_limit = (size_t)info->header_size + info->type_record_bytes;
    if (rec_limit > size) rec_limit = size;

    dump_begin(ctx, "records", strm_idx, (int64_t)off);
    curr_ti = info->type_index_begin;

    while (off + 4 <= rec_limit && curr_ti < info->type_index_end && !ctx->stopped) {
        uint16_t len = dump_u16(data, size, off);
        uint16_t kind = dump_u16(data, size, off + 2);
        size_t rec_sz = (size_t)len + 2;
        if (off + rec_sz > rec_limit) {
            dump_field_error(ctx, "Record truncated");
            break;
        }

        dump_begin(ctx, "record", strm_idx, (int64_t)off);
        dump_field_hex(ctx, "type_index", curr_ti);
        dump_field_enum(ctx, "kind", kind, xx_ms_pdb_leaf_kind_to_string(kind));
        dump_field_uint(ctx, "length", len);

        if (len >= 2) {
            xx_ms_pdb_decode_leaf(kind, data + off + 4, (size_t)len - 2, ctx->depth, ctx->fn, ctx->user);
        }

        dump_end(ctx, "record", strm_idx, (int64_t)off);

        off += rec_sz;
        curr_ti++;
    }

    dump_end(ctx, "records", strm_idx, (int64_t)info->header_size);
    dump_end(ctx, sec_name, strm_idx, 0);
}

/* ---- 5. TPI_HASH ---------------------------------------------------------- */
static void dump_walk_tpi_hash(dump_ctx *ctx)
{
    int32_t strm_idx = (int32_t)ctx->tpi_info.hash_stream_index;
    size_t size = 0;
    uint8_t *data = NULL;

    if (strm_idx < 0 || strm_idx == 0xFFFF) return;
    data = dump_read_stream(ctx, strm_idx, &size);
    if (!data) return;

    dump_begin(ctx, "tpi_hash", strm_idx, 0);

    /* 1. Hash values */
    if (ctx->tpi_info.hash_value_buffer_offset >= 0 && ctx->tpi_info.hash_value_buffer_length > 0) {
        size_t h_off = (size_t)ctx->tpi_info.hash_value_buffer_offset;
        size_t h_len = ctx->tpi_info.hash_value_buffer_length;
        if (h_off + h_len <= size) {
            size_t count = h_len / 4;
            dump_begin(ctx, "hash_values", strm_idx, (int64_t)h_off);
            for (size_t i = 0; i < count && !ctx->stopped; i++) {
                dump_field_hex(ctx, "hash", dump_u32(data, size, h_off + i * 4));
            }
            dump_end(ctx, "hash_values", strm_idx, (int64_t)h_off);
        }
    }

    /* 2. Index offsets */
    if (ctx->tpi_info.index_offset_buffer_offset >= 0 && ctx->tpi_info.index_offset_buffer_length > 0) {
        size_t io_off = (size_t)ctx->tpi_info.index_offset_buffer_offset;
        size_t io_len = ctx->tpi_info.index_offset_buffer_length;
        if (io_off + io_len <= size) {
            size_t count = io_len / 8;
            dump_begin(ctx, "index_offsets", strm_idx, (int64_t)io_off);
            for (size_t i = 0; i < count && !ctx->stopped; i++) {
                uint32_t ti = dump_u32(data, size, io_off + i * 8);
                uint32_t off = dump_u32(data, size, io_off + i * 8 + 4);
                dump_begin(ctx, "entry", strm_idx, (int64_t)(io_off + i * 8));
                dump_field_hex(ctx, "type_index", ti);
                dump_field_hex(ctx, "offset", off);
                dump_end(ctx, "entry", strm_idx, (int64_t)(io_off + i * 8));
            }
            dump_end(ctx, "index_offsets", strm_idx, (int64_t)io_off);
        }
    }

    /* 3. Hash adjusters */
    if (ctx->tpi_info.hash_adj_buffer_offset >= 0 && ctx->tpi_info.hash_adj_buffer_length >= 8) {
        size_t adj_off = (size_t)ctx->tpi_info.hash_adj_buffer_offset;
        size_t adj_len = ctx->tpi_info.hash_adj_buffer_length;
        if (adj_off + adj_len <= size) {
            uint32_t num_entries = dump_u32(data, size, adj_off);
            uint32_t pres_words = dump_u32(data, size, adj_off + 8);
            size_t del_off = adj_off + 12 + (size_t)pres_words * 4;
            if (del_off + 4 <= size) {
                uint32_t del_words = dump_u32(data, size, del_off);
                size_t buckets_off = del_off + 4 + (size_t)del_words * 4;
                dump_begin(ctx, "hash_adjusters", strm_idx, (int64_t)adj_off);
                for (uint32_t i = 0; i < num_entries && buckets_off + 8 <= size && !ctx->stopped; i++) {
                    uint32_t name_off = dump_u32(data, size, buckets_off);
                    uint32_t ti = dump_u32(data, size, buckets_off + 4);
                    buckets_off += 8;
                    dump_begin(ctx, "entry", strm_idx, (int64_t)(buckets_off - 8));
                    dump_field_uint(ctx, "name_offset", name_off);
                    dump_field_hex(ctx, "type_index", ti);
                    if (ctx->names) {
                        const char *s = xx_ms_pdb_names_get(ctx->names, name_off);
                        if (s) dump_field_string(ctx, "name", s);
                    }
                    dump_end(ctx, "entry", strm_idx, (int64_t)(buckets_off - 8));
                }
                dump_end(ctx, "hash_adjusters", strm_idx, (int64_t)adj_off);
            }
        }
    }

    dump_end(ctx, "tpi_hash", strm_idx, 0);
}

/* ---- 6. DBI --------------------------------------------------------------- */
static void dump_walk_dbi(dump_ctx *ctx)
{
    size_t size = 0;
    uint8_t *data = dump_read_stream(ctx, XX_MS_PDB_STREAM_DBI, &size);
    char flags_buf[256];

    if (!data || size < 64) {
        dump_begin(ctx, "dbi", XX_MS_PDB_STREAM_DBI, 0);
        dump_field_error(ctx, "DBI stream missing or truncated");
        dump_end(ctx, "dbi", XX_MS_PDB_STREAM_DBI, 0);
        return;
    }

    dump_begin(ctx, "dbi", XX_MS_PDB_STREAM_DBI, 0);

    /* 1. Header */
    dump_begin(ctx, "header", XX_MS_PDB_STREAM_DBI, 0);
    dump_field_int(ctx, "version_signature", ctx->dbi_info.version_signature);
    dump_field_enum(ctx, "version_header", ctx->dbi_info.version_header, ctx->dbi_info.version_string);
    dump_field_uint(ctx, "age", ctx->dbi_info.age);
    dump_field_uint(ctx, "global_stream_index", ctx->dbi_info.global_stream_index);
    dump_field_uint(ctx, "build_number", ctx->dbi_info.build_number);
    dump_field_uint(ctx, "public_stream_index", ctx->dbi_info.public_stream_index);
    dump_field_uint(ctx, "pdb_dll_version", ctx->dbi_info.pdb_dll_version);
    dump_field_uint(ctx, "sym_record_stream", ctx->dbi_info.sym_record_stream);
    dump_field_uint(ctx, "pdb_dll_rbld", ctx->dbi_info.pdb_dll_rbld);
    dump_field_uint(ctx, "mod_info_size", (uint64_t)ctx->dbi_info.mod_info_size);
    dump_field_uint(ctx, "section_contribution_size", (uint64_t)ctx->dbi_info.section_contribution_size);
    dump_field_uint(ctx, "section_map_size", (uint64_t)ctx->dbi_info.section_map_size);
    dump_field_uint(ctx, "source_info_size", (uint64_t)ctx->dbi_info.source_info_size);
    dump_field_uint(ctx, "type_server_map_size", (uint64_t)ctx->dbi_info.type_server_map_size);
    dump_field_uint(ctx, "mfc_type_server_index", ctx->dbi_info.mfc_type_server_index);
    dump_field_uint(ctx, "optional_dbg_header_size", (uint64_t)ctx->dbi_info.optional_dbg_header_size);
    dump_field_uint(ctx, "ec_substream_size", (uint64_t)ctx->dbi_info.ec_substream_size);
    dump_field_flags(ctx, "flags", ctx->dbi_info.flags, dump_dbi_flags_str(ctx->dbi_info.flags, flags_buf, sizeof(flags_buf)));
    dump_field_enum(ctx, "machine", ctx->dbi_info.machine, ctx->dbi_info.machine_string);
    dump_end(ctx, "header", XX_MS_PDB_STREAM_DBI, 0);

    size_t sub_off = 64;

    /* 2. ModInfo substream */
    if (ctx->dbi_info.mod_info_size > 0 && sub_off + (size_t)ctx->dbi_info.mod_info_size <= size) {
        size_t mod_end = sub_off + (size_t)ctx->dbi_info.mod_info_size;
        size_t cur = sub_off;
        dump_begin(ctx, "modules", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        while (cur + 64 <= mod_end && !ctx->stopped) {
            uint16_t sec = dump_u16(data, size, cur + 4);
            int32_t off = dump_i32(data, size, cur + 8);
            int32_t sz = dump_i32(data, size, cur + 12);
            uint32_t chars = dump_u32(data, size, cur + 16);
            uint16_t mod_idx = dump_u16(data, size, cur + 20);
            uint32_t data_crc = dump_u32(data, size, cur + 24);
            uint32_t reloc_crc = dump_u32(data, size, cur + 28);
            uint16_t flg = dump_u16(data, size, cur + 32);
            uint16_t strm = dump_u16(data, size, cur + 34);
            uint32_t sym_sz = dump_u32(data, size, cur + 36);
            uint32_t c11_sz = dump_u32(data, size, cur + 40);
            uint32_t c13_sz = dump_u32(data, size, cur + 44);
            uint16_t src_files = dump_u16(data, size, cur + 48);
            uint32_t src_off = dump_u32(data, size, cur + 52);
            uint32_t pdb_off = dump_u32(data, size, cur + 56);

            const char *mod_name = (const char *)data + cur + 64;
            size_t mlen = strlen(mod_name);
            const char *obj_name = (const char *)data + cur + 64 + mlen + 1;
            size_t olen = strlen(obj_name);

            dump_begin(ctx, "module_info", XX_MS_PDB_STREAM_DBI, (int64_t)cur);
            dump_field_hex(ctx, "flags", flg);
            dump_field_uint(ctx, "stream", strm);
            dump_field_uint(ctx, "sym_byte_size", sym_sz);
            dump_field_uint(ctx, "c11_byte_size", c11_sz);
            dump_field_uint(ctx, "c13_byte_size", c13_sz);
            dump_field_uint(ctx, "source_file_count", src_files);
            dump_field_uint(ctx, "src_file_name_offset", src_off);
            dump_field_uint(ctx, "pdb_file_path_name_offset", pdb_off);
            dump_field_string(ctx, "module_name", mod_name);
            dump_field_string(ctx, "object_name", obj_name);

            dump_begin(ctx, "section_contrib", XX_MS_PDB_STREAM_DBI, (int64_t)(cur + 4));
            dump_field_uint(ctx, "section", sec);
            dump_field_int(ctx, "offset", off);
            dump_field_int(ctx, "size", sz);
            dump_field_hex(ctx, "characteristics", chars);
            dump_field_uint(ctx, "module_index", mod_idx);
            dump_field_hex(ctx, "data_crc", data_crc);
            dump_field_hex(ctx, "reloc_crc", reloc_crc);
            dump_end(ctx, "section_contrib", XX_MS_PDB_STREAM_DBI, (int64_t)(cur + 4));

            dump_end(ctx, "module_info", XX_MS_PDB_STREAM_DBI, (int64_t)cur);

            size_t total_mod_sz = 64 + mlen + 1 + olen + 1;
            total_mod_sz = (total_mod_sz + 3) & ~3;
            cur += total_mod_sz;
        }
        dump_end(ctx, "modules", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        sub_off = mod_end;
    }

    /* 3. Section contribution substream */
    if (ctx->dbi_info.section_contribution_size > 4 && sub_off + (size_t)ctx->dbi_info.section_contribution_size <= size) {
        size_t sc_end = sub_off + (size_t)ctx->dbi_info.section_contribution_size;
        uint32_t sc_ver = dump_u32(data, size, sub_off);
        size_t entry_sz = (sc_ver == (uint32_t)XX_MS_PDB_DBI_SECCONTRIB_V2) ? 32 : 28;
        const char *vname = (sc_ver == (uint32_t)XX_MS_PDB_DBI_SECCONTRIB_V2) ? "V2" : (sc_ver == (uint32_t)XX_MS_PDB_DBI_SECCONTRIB_VER60) ? "V60" : "V1";

        dump_begin(ctx, "section_contribs", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        dump_field_enum(ctx, "version", sc_ver, vname);

        size_t cur = sub_off + 4;
        while (cur + entry_sz <= sc_end && !ctx->stopped) {
            uint16_t sec = dump_u16(data, size, cur + 0);
            int32_t off = dump_i32(data, size, cur + 4);
            int32_t sz = dump_i32(data, size, cur + 8);
            uint32_t chars = dump_u32(data, size, cur + 12);
            uint16_t mod_idx = dump_u16(data, size, cur + 16);
            uint32_t data_crc = dump_u32(data, size, cur + 20);
            uint32_t reloc_crc = dump_u32(data, size, cur + 24);

            dump_begin(ctx, "entry", XX_MS_PDB_STREAM_DBI, (int64_t)cur);
            dump_field_uint(ctx, "section", sec);
            dump_field_int(ctx, "offset", off);
            dump_field_int(ctx, "size", sz);
            dump_field_hex(ctx, "characteristics", chars);
            dump_field_uint(ctx, "module_index", mod_idx);
            dump_field_hex(ctx, "data_crc", data_crc);
            dump_field_hex(ctx, "reloc_crc", reloc_crc);
            if (entry_sz == 32) {
                dump_field_uint(ctx, "isect_coff", dump_u32(data, size, cur + 28));
            }
            dump_end(ctx, "entry", XX_MS_PDB_STREAM_DBI, (int64_t)cur);
            cur += entry_sz;
        }
        dump_end(ctx, "section_contribs", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        sub_off = sc_end;
    }

    /* 4. Section map substream */
    if (ctx->dbi_info.section_map_size >= 4 && sub_off + (size_t)ctx->dbi_info.section_map_size <= size) {
        size_t sm_end = sub_off + (size_t)ctx->dbi_info.section_map_size;
        uint16_t sec_cnt = dump_u16(data, size, sub_off);
        uint16_t log_cnt = dump_u16(data, size, sub_off + 2);

        dump_begin(ctx, "section_map", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        dump_field_uint(ctx, "count", sec_cnt);
        dump_field_uint(ctx, "log_count", log_cnt);

        size_t cur = sub_off + 4;
        for (uint16_t s = 0; s < sec_cnt && cur + 20 <= sm_end && !ctx->stopped; s++) {
            uint16_t flg = dump_u16(data, size, cur + 0);
            uint16_t ovl = dump_u16(data, size, cur + 2);
            uint16_t grp = dump_u16(data, size, cur + 4);
            uint16_t frm = dump_u16(data, size, cur + 6);
            uint16_t sname = dump_u16(data, size, cur + 8);
            uint16_t cname = dump_u16(data, size, cur + 10);
            uint32_t off = dump_u32(data, size, cur + 12);
            uint32_t sz = dump_u32(data, size, cur + 16);

            dump_begin(ctx, "entry", XX_MS_PDB_STREAM_DBI, (int64_t)cur);
            dump_field_flags(ctx, "flags", flg, dump_secmap_flags_str(flg, flags_buf, sizeof(flags_buf)));
            dump_field_uint(ctx, "ovl", ovl);
            dump_field_uint(ctx, "group", grp);
            dump_field_uint(ctx, "frame", frm);
            dump_field_uint(ctx, "section_name", sname);
            dump_field_uint(ctx, "class_name", cname);
            dump_field_uint(ctx, "offset", off);
            dump_field_uint(ctx, "size", sz);
            dump_end(ctx, "entry", XX_MS_PDB_STREAM_DBI, (int64_t)cur);
            cur += 20;
        }
        dump_end(ctx, "section_map", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        sub_off = sm_end;
    }

    /* 5. File info substream */
    if (ctx->dbi_info.source_info_size >= 4 && sub_off + (size_t)ctx->dbi_info.source_info_size <= size) {
        size_t fi_end = sub_off + (size_t)ctx->dbi_info.source_info_size;
        uint16_t num_mods = dump_u16(data, size, sub_off);
        uint16_t num_src_files = dump_u16(data, size, sub_off + 2);

        dump_begin(ctx, "file_info", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        dump_field_uint(ctx, "module_count", num_mods);
        dump_field_uint(ctx, "source_file_count", num_src_files);

        size_t mod_idx_off = sub_off + 4;
        size_t mod_cnt_off = mod_idx_off + (size_t)num_mods * 2;
        size_t fn_off = mod_cnt_off + (size_t)num_mods * 2;
        size_t names_buf_off = fn_off + (size_t)num_src_files * 4;

        if (names_buf_off <= fi_end) {
            uint32_t running_file_index = 0;
            for (uint16_t m = 0; m < num_mods && !ctx->stopped; m++) {
                uint16_t fi_count = dump_u16(data, size, mod_cnt_off + (size_t)m * 2);
                uint32_t fi_start = running_file_index;
                running_file_index += fi_count;

                dump_begin(ctx, "module_files", XX_MS_PDB_STREAM_DBI, (int64_t)(mod_idx_off + (size_t)m * 2));
                dump_field_uint(ctx, "module_index", m);
                dump_field_uint(ctx, "file_index_start", fi_start);
                dump_field_uint(ctx, "file_count", fi_count);

                dump_begin(ctx, "files", XX_MS_PDB_STREAM_DBI, -1);
                for (uint16_t f = 0; f < fi_count && (size_t)fi_start + f < num_src_files && !ctx->stopped; f++) {
                    uint32_t name_offset = dump_u32(data, size, fn_off + ((size_t)fi_start + f) * 4);
                    if (names_buf_off + name_offset < fi_end) {
                        const char *fname = (const char *)data + names_buf_off + name_offset;
                        dump_field_string(ctx, "name", fname);
                    }
                }
                dump_end(ctx, "files", XX_MS_PDB_STREAM_DBI, -1);

                dump_end(ctx, "module_files", XX_MS_PDB_STREAM_DBI, (int64_t)(mod_idx_off + (size_t)m * 2));
            }
        }
        dump_end(ctx, "file_info", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        sub_off = fi_end;
    }

    /* 6. Type server map */
    if (ctx->dbi_info.type_server_map_size > 0 && sub_off + (size_t)ctx->dbi_info.type_server_map_size <= size) {
        dump_begin(ctx, "type_server_map", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        dump_field_bytes(ctx, "data", data + sub_off, (size_t)ctx->dbi_info.type_server_map_size);
        dump_end(ctx, "type_server_map", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        sub_off += (size_t)ctx->dbi_info.type_server_map_size;
    }

    /* 7. EC substream */
    if (ctx->dbi_info.ec_substream_size > 0 && sub_off + (size_t)ctx->dbi_info.ec_substream_size <= size) {
        dump_begin(ctx, "ec", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        dump_field_bytes(ctx, "data", data + sub_off, (size_t)ctx->dbi_info.ec_substream_size);
        dump_end(ctx, "ec", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        sub_off += (size_t)ctx->dbi_info.ec_substream_size;
    }

    /* 8. Optional debug header */
    if (ctx->dbi_info.optional_dbg_header_size >= 2 && sub_off + (size_t)ctx->dbi_info.optional_dbg_header_size <= size) {
        static const char *const dbg_role_names[] = {"fpo",           "exception", "fixup", "omap_to_src", "omap_from_src",      "section_header",
                                                     "token_rid_map", "xdata",     "pdata", "new_fpo",     "section_header_orig"};
        size_t count = (size_t)ctx->dbi_info.optional_dbg_header_size / 2;
        dump_begin(ctx, "optional_debug_header", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
        for (size_t i = 0; i < count && i < 11 && !ctx->stopped; i++) {
            uint16_t sidx = dump_u16(data, size, sub_off + i * 2);
            dump_field_int(ctx, dbg_role_names[i], (sidx == 0xFFFF) ? -1 : (int64_t)sidx);
        }
        dump_end(ctx, "optional_debug_header", XX_MS_PDB_STREAM_DBI, (int64_t)sub_off);
    }

    dump_end(ctx, "dbi", XX_MS_PDB_STREAM_DBI, 0);
}

/* ---- 7. DBG_STREAMS ------------------------------------------------------- */
static void dump_walk_dbg_section_headers(dump_ctx *ctx, int32_t strm_idx, const char *name)
{
    size_t size = 0;
    uint8_t *data = NULL;
    char chars_buf[256];

    if (strm_idx < 0 || strm_idx == 0xFFFF) return;
    data = dump_read_stream(ctx, strm_idx, &size);
    if (!data) return;

    dump_begin(ctx, name, strm_idx, 0);
    size_t count = size / 40;
    for (size_t i = 0; i < count && !ctx->stopped; i++) {
        size_t off = i * 40;
        char sname[9] = {0};
        memcpy(sname, data + off, 8);
        uint32_t vsz = dump_u32(data, size, off + 8);
        uint32_t va = dump_u32(data, size, off + 12);
        uint32_t rsz = dump_u32(data, size, off + 16);
        uint32_t rptr = dump_u32(data, size, off + 20);
        uint32_t reloc_ptr = dump_u32(data, size, off + 24);
        uint32_t line_ptr = dump_u32(data, size, off + 28);
        uint16_t num_reloc = dump_u16(data, size, off + 32);
        uint16_t num_lines = dump_u16(data, size, off + 34);
        uint32_t chars = dump_u32(data, size, off + 36);

        dump_begin(ctx, "section_header", strm_idx, (int64_t)off);
        dump_field_string(ctx, "name", sname);
        dump_field_uint(ctx, "virtual_size", vsz);
        dump_field_hex(ctx, "virtual_address", va);
        dump_field_uint(ctx, "size_of_raw_data", rsz);
        dump_field_hex(ctx, "pointer_to_raw_data", rptr);
        dump_field_hex(ctx, "pointer_to_relocations", reloc_ptr);
        dump_field_hex(ctx, "pointer_to_linenumbers", line_ptr);
        dump_field_uint(ctx, "number_of_relocations", num_reloc);
        dump_field_uint(ctx, "number_of_linenumbers", num_lines);
        dump_field_flags(ctx, "characteristics", chars, dump_section_chars_str(chars, chars_buf, sizeof(chars_buf)));
        dump_end(ctx, "section_header", strm_idx, (int64_t)off);
    }
    dump_end(ctx, name, strm_idx, 0);
}

static void dump_walk_dbg_streams(dump_ctx *ctx)
{
    if (!ctx->dbi_info.is_valid || !ctx->dbi_info.dbg_streams) return;

    dump_begin(ctx, "dbg_streams", -1, -1);

    /* 0: FPO */
    if (ctx->dbi_info.dbg_stream_count > 0 && ctx->dbi_info.dbg_streams[0] != 0xFFFF) {
        int32_t strm = ctx->dbi_info.dbg_streams[0];
        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, strm, &size);
        if (data && size >= 16) {
            size_t count = size / 16;
            dump_begin(ctx, "fpo", strm, 0);
            for (size_t i = 0; i < count && !ctx->stopped; i++) {
                size_t off = i * 16;
                uint32_t rva = dump_u32(data, size, off);
                uint32_t proc_sz = dump_u32(data, size, off + 4);
                uint32_t locals = dump_u32(data, size, off + 8);
                uint16_t params = dump_u16(data, size, off + 12);
                uint16_t flg = dump_u16(data, size, off + 14);

                dump_begin(ctx, "entry", strm, (int64_t)off);
                dump_field_hex(ctx, "rva_start", rva);
                dump_field_uint(ctx, "proc_size", proc_sz);
                dump_field_uint(ctx, "locals_dwords", locals);
                dump_field_uint(ctx, "params_dwords", params);
                dump_field_hex(ctx, "flags", flg);
                dump_end(ctx, "entry", strm, (int64_t)off);
            }
            dump_end(ctx, "fpo", strm, 0);
        }
    }

    /* 1: Exception */
    if (ctx->dbi_info.dbg_stream_count > 1 && ctx->dbi_info.dbg_streams[1] != 0xFFFF) {
        int32_t strm = ctx->dbi_info.dbg_streams[1];
        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, strm, &size);
        if (data) {
            dump_begin(ctx, "exception", strm, 0);
            dump_field_bytes(ctx, "data", data, size);
            dump_end(ctx, "exception", strm, 0);
        }
    }

    /* 2: Fixup */
    if (ctx->dbi_info.dbg_stream_count > 2 && ctx->dbi_info.dbg_streams[2] != 0xFFFF) {
        int32_t strm = ctx->dbi_info.dbg_streams[2];
        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, strm, &size);
        if (data && size >= 12) {
            size_t count = size / 12;
            dump_begin(ctx, "fixups", strm, 0);
            for (size_t i = 0; i < count && !ctx->stopped; i++) {
                size_t off = i * 12;
                uint16_t type = dump_u16(data, size, off);
                uint16_t extra = dump_u16(data, size, off + 2);
                uint32_t rva = dump_u32(data, size, off + 4);
                uint32_t rva_target = dump_u32(data, size, off + 8);

                dump_begin(ctx, "entry", strm, (int64_t)off);
                dump_field_uint(ctx, "type", type);
                dump_field_uint(ctx, "extra", extra);
                dump_field_hex(ctx, "rva", rva);
                dump_field_hex(ctx, "rva_target", rva_target);
                dump_end(ctx, "entry", strm, (int64_t)off);
            }
            dump_end(ctx, "fixups", strm, 0);
        }
    }

    /* 3: OMAP to src */
    if (ctx->dbi_info.dbg_stream_count > 3 && ctx->dbi_info.dbg_streams[3] != 0xFFFF) {
        int32_t strm = ctx->dbi_info.dbg_streams[3];
        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, strm, &size);
        if (data && size >= 8) {
            size_t count = size / 8;
            dump_begin(ctx, "omap_to_src", strm, 0);
            for (size_t i = 0; i < count && !ctx->stopped; i++) {
                dump_begin(ctx, "entry", strm, (int64_t)(i * 8));
                dump_field_hex(ctx, "rva", dump_u32(data, size, i * 8));
                dump_field_hex(ctx, "rva_to", dump_u32(data, size, i * 8 + 4));
                dump_end(ctx, "entry", strm, (int64_t)(i * 8));
            }
            dump_end(ctx, "omap_to_src", strm, 0);
        }
    }

    /* 4: OMAP from src */
    if (ctx->dbi_info.dbg_stream_count > 4 && ctx->dbi_info.dbg_streams[4] != 0xFFFF) {
        int32_t strm = ctx->dbi_info.dbg_streams[4];
        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, strm, &size);
        if (data && size >= 8) {
            size_t count = size / 8;
            dump_begin(ctx, "omap_from_src", strm, 0);
            for (size_t i = 0; i < count && !ctx->stopped; i++) {
                dump_begin(ctx, "entry", strm, (int64_t)(i * 8));
                dump_field_hex(ctx, "rva", dump_u32(data, size, i * 8));
                dump_field_hex(ctx, "rva_to", dump_u32(data, size, i * 8 + 4));
                dump_end(ctx, "entry", strm, (int64_t)(i * 8));
            }
            dump_end(ctx, "omap_from_src", strm, 0);
        }
    }

    /* 5: Section headers */
    if (ctx->dbi_info.dbg_stream_count > 5) {
        dump_walk_dbg_section_headers(ctx, ctx->dbi_info.dbg_streams[5], "section_headers");
    }

    /* 6: Token RID map */
    if (ctx->dbi_info.dbg_stream_count > 6 && ctx->dbi_info.dbg_streams[6] != 0xFFFF) {
        int32_t strm = ctx->dbi_info.dbg_streams[6];
        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, strm, &size);
        if (data && size >= 4) {
            size_t count = size / 4;
            dump_begin(ctx, "token_rid_map", strm, 0);
            for (size_t i = 0; i < count && !ctx->stopped; i++) {
                dump_field_hex(ctx, "token", dump_u32(data, size, i * 4));
            }
            dump_end(ctx, "token_rid_map", strm, 0);
        }
    }

    /* 7: Xdata */
    if (ctx->dbi_info.dbg_stream_count > 7 && ctx->dbi_info.dbg_streams[7] != 0xFFFF) {
        int32_t strm = ctx->dbi_info.dbg_streams[7];
        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, strm, &size);
        if (data) {
            dump_begin(ctx, "xdata", strm, 0);
            dump_field_bytes(ctx, "data", data, size);
            dump_end(ctx, "xdata", strm, 0);
        }
    }

    /* 8: Pdata */
    if (ctx->dbi_info.dbg_stream_count > 8 && ctx->dbi_info.dbg_streams[8] != 0xFFFF) {
        int32_t strm = ctx->dbi_info.dbg_streams[8];
        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, strm, &size);
        if (data) {
            dump_begin(ctx, "pdata", strm, 0);
            if (ctx->dbi_info.machine == 0x8664) { /* x64 */
                size_t count = size / 12;
                for (size_t i = 0; i < count && !ctx->stopped; i++) {
                    size_t off = i * 12;
                    dump_begin(ctx, "runtime_function", strm, (int64_t)off);
                    dump_field_hex(ctx, "begin_address", dump_u32(data, size, off));
                    dump_field_hex(ctx, "end_address", dump_u32(data, size, off + 4));
                    dump_field_hex(ctx, "unwind_info_address", dump_u32(data, size, off + 8));
                    dump_end(ctx, "runtime_function", strm, (int64_t)off);
                }
            } else if (ctx->dbi_info.machine == 0xAA64) { /* ARM64 */
                size_t count = size / 8;
                for (size_t i = 0; i < count && !ctx->stopped; i++) {
                    size_t off = i * 8;
                    dump_begin(ctx, "runtime_function", strm, (int64_t)off);
                    dump_field_hex(ctx, "begin_address", dump_u32(data, size, off));
                    dump_field_hex(ctx, "unwind_data", dump_u32(data, size, off + 4));
                    dump_end(ctx, "runtime_function", strm, (int64_t)off);
                }
            }
            dump_end(ctx, "pdata", strm, 0);
        }
    }

    /* 9: Frame data */
    if (ctx->dbi_info.dbg_stream_count > 9 && ctx->dbi_info.dbg_streams[9] != 0xFFFF) {
        int32_t strm = ctx->dbi_info.dbg_streams[9];
        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, strm, &size);
        if (data && size >= 32) {
            size_t off = 0;
            dump_begin(ctx, "frame_data", strm, 0);
            if ((size % 32) != 0 && size >= 4) {
                dump_field_hex(ctx, "reloc_ptr", dump_u32(data, size, 0));
                off = 4;
            }
            while (off + 32 <= size && !ctx->stopped) {
                uint32_t rva = dump_u32(data, size, off);
                uint32_t code_sz = dump_u32(data, size, off + 4);
                uint32_t loc_sz = dump_u32(data, size, off + 8);
                uint32_t param_sz = dump_u32(data, size, off + 12);
                uint32_t max_stk = dump_u32(data, size, off + 16);
                uint32_t frame_func = dump_u32(data, size, off + 20);
                uint16_t prolog_sz = dump_u16(data, size, off + 24);
                uint16_t regs_sz = dump_u16(data, size, off + 26);
                uint32_t flg = dump_u32(data, size, off + 28);

                dump_begin(ctx, "entry", strm, (int64_t)off);
                dump_field_hex(ctx, "rva_start", rva);
                dump_field_uint(ctx, "code_size", code_sz);
                dump_field_uint(ctx, "locals_size", loc_sz);
                dump_field_uint(ctx, "params_size", param_sz);
                dump_field_uint(ctx, "max_stack_size", max_stk);
                dump_field_uint(ctx, "frame_func", frame_func);
                if (ctx->names) {
                    const char *p = xx_ms_pdb_names_get(ctx->names, frame_func);
                    if (p) dump_field_string(ctx, "program", p);
                }
                dump_field_uint(ctx, "prolog_size", prolog_sz);
                dump_field_uint(ctx, "saved_regs_size", regs_sz);
                dump_field_hex(ctx, "flags", flg);
                dump_end(ctx, "entry", strm, (int64_t)off);
                off += 32;
            }
            dump_end(ctx, "frame_data", strm, 0);
        }
    }

    /* 10: Original section headers */
    if (ctx->dbi_info.dbg_stream_count > 10) {
        dump_walk_dbg_section_headers(ctx, ctx->dbi_info.dbg_streams[10], "original_section_headers");
    }

    dump_end(ctx, "dbg_streams", -1, -1);
}

/* ---- 8. MODULES ----------------------------------------------------------- */
static void dump_walk_c13_subsection(dump_ctx *ctx, int32_t strm_idx, uint32_t kind, const uint8_t *sub_data, size_t sub_len, int64_t sub_off)
{
    dump_begin(ctx, "subsection", strm_idx, sub_off);
    dump_field_enum(ctx, "kind", kind, dump_subsection_kind_str(kind));
    dump_field_uint(ctx, "length", sub_len);

    uint32_t k = kind & 0x7FFFFFFFu;
    if (k == 0xF1) { /* DEBUG_S_SYMBOLS */
        size_t cur = 0;
        while (cur + 4 <= sub_len && !ctx->stopped) {
            uint16_t len = dump_u16(sub_data, sub_len, cur);
            uint16_t skind = dump_u16(sub_data, sub_len, cur + 2);
            size_t rec_sz = (size_t)len + 2;
            if (cur + rec_sz > sub_len) break;
            dump_begin(ctx, "symbol", strm_idx, sub_off + 8 + (int64_t)cur);
            dump_field_enum(ctx, "kind", skind, xx_ms_pdb_symbol_kind_to_string(skind));
            dump_field_uint(ctx, "length", len);
            if (len >= 2) {
                xx_ms_pdb_decode_symbol(skind, sub_data + cur + 4, (size_t)len - 2, ctx->dbi_info.machine, ctx->depth, ctx->fn, ctx->user);
            }
            dump_end(ctx, "symbol", strm_idx, sub_off + 8 + (int64_t)cur);
            cur += rec_sz;
        }
    } else if (k == 0xF2 || k == 0xF9) { /* LINES / IL_LINES */
        if (sub_len >= 12) {
            uint32_t offCon = dump_u32(sub_data, sub_len, 0);
            uint16_t segCon = dump_u16(sub_data, sub_len, 4);
            uint16_t flags = dump_u16(sub_data, sub_len, 6);
            uint32_t cbCon = dump_u32(sub_data, sub_len, 8);

            dump_begin(ctx, "lines_header", strm_idx, sub_off + 8);
            dump_field_hex(ctx, "off_con", offCon);
            dump_field_uint(ctx, "seg_con", segCon);
            dump_field_hex(ctx, "flags", flags);
            dump_field_uint(ctx, "cb_con", cbCon);
            dump_end(ctx, "lines_header", strm_idx, sub_off + 8);

            size_t cur = 12;
            while (cur + 12 <= sub_len && !ctx->stopped) {
                uint32_t file_id = dump_u32(sub_data, sub_len, cur);
                uint32_t nLines = dump_u32(sub_data, sub_len, cur + 4);
                uint32_t cbFileBlock = dump_u32(sub_data, sub_len, cur + 8);

                dump_begin(ctx, "block", strm_idx, sub_off + 8 + (int64_t)cur);
                dump_field_uint(ctx, "file_id", file_id);
                dump_field_uint(ctx, "line_count", nLines);
                dump_field_uint(ctx, "block_bytes", cbFileBlock);

                size_t lines_off = cur + 12;
                size_t cols_off = lines_off + (size_t)nLines * 8;
                bool has_cols = (flags & 1) != 0 && (cols_off + (size_t)nLines * 4 <= sub_len);

                dump_begin(ctx, "lines", strm_idx, sub_off + 8 + (int64_t)lines_off);
                for (uint32_t l = 0; l < nLines && lines_off + 8 <= sub_len && !ctx->stopped; l++) {
                    uint32_t l_off = dump_u32(sub_data, sub_len, lines_off);
                    uint32_t l_num = dump_u32(sub_data, sub_len, lines_off + 4);
                    lines_off += 8;

                    uint32_t line_start = l_num & 0x00FFFFFFu;
                    uint32_t delta = (l_num >> 24) & 0x7Fu;
                    bool is_stmt = (l_num & 0x80000000u) != 0;

                    dump_begin(ctx, "line", strm_idx, -1);
                    dump_field_hex(ctx, "offset", l_off);
                    dump_field_uint(ctx, "line_start", line_start);
                    dump_field_uint(ctx, "line_end", line_start + delta);
                    dump_field_bool(ctx, "is_statement", is_stmt);

                    if (has_cols) {
                        uint16_t cStart = dump_u16(sub_data, sub_len, cols_off + (size_t)l * 4);
                        uint16_t cEnd = dump_u16(sub_data, sub_len, cols_off + (size_t)l * 4 + 2);
                        dump_field_uint(ctx, "column_start", cStart);
                        dump_field_uint(ctx, "column_end", cEnd);
                    }
                    dump_end(ctx, "line", strm_idx, -1);
                }
                dump_end(ctx, "lines", strm_idx, -1);

                dump_end(ctx, "block", strm_idx, sub_off + 8 + (int64_t)cur);
                cur += cbFileBlock;
            }
        }
    } else if (k == 0xF4) { /* FILECHKSMS */
        size_t cur = 0;
        dump_begin(ctx, "checksums", strm_idx, sub_off + 8);
        while (cur + 6 <= sub_len && !ctx->stopped) {
            uint32_t name_off = dump_u32(sub_data, sub_len, cur);
            uint8_t c_sz = dump_u8(sub_data, sub_len, cur + 4);
            uint8_t c_type = dump_u8(sub_data, sub_len, cur + 5);

            dump_begin(ctx, "entry", strm_idx, sub_off + 8 + (int64_t)cur);
            dump_field_uint(ctx, "name_offset", name_off);
            if (ctx->names) {
                const char *s = xx_ms_pdb_names_get(ctx->names, name_off);
                if (s) dump_field_string(ctx, "name", s);
            }
            dump_field_enum(ctx, "checksum_kind", c_type, dump_checksum_kind_str(c_type));
            if (cur + 6 + c_sz <= sub_len) {
                dump_field_bytes(ctx, "checksum", sub_data + cur + 6, c_sz);
            }
            dump_end(ctx, "entry", strm_idx, sub_off + 8 + (int64_t)cur);

            size_t entry_total = (6 + (size_t)c_sz + 3) & ~3;
            cur += entry_total;
        }
        dump_end(ctx, "checksums", strm_idx, sub_off + 8);
    } else if (k == 0xF5) { /* FRAMEDATA */
        size_t cur = 0;
        if (sub_len >= 4) {
            dump_field_hex(ctx, "reloc_ptr", dump_u32(sub_data, sub_len, 0));
            cur = 4;
        }
        dump_begin(ctx, "frame_data", strm_idx, sub_off + 8 + (int64_t)cur);
        while (cur + 32 <= sub_len && !ctx->stopped) {
            dump_begin(ctx, "entry", strm_idx, sub_off + 8 + (int64_t)cur);
            dump_field_hex(ctx, "rva_start", dump_u32(sub_data, sub_len, cur));
            dump_field_uint(ctx, "code_size", dump_u32(sub_data, sub_len, cur + 4));
            dump_field_uint(ctx, "locals_size", dump_u32(sub_data, sub_len, cur + 8));
            dump_field_uint(ctx, "params_size", dump_u32(sub_data, sub_len, cur + 12));
            dump_field_uint(ctx, "max_stack_size", dump_u32(sub_data, sub_len, cur + 16));
            uint32_t ff = dump_u32(sub_data, sub_len, cur + 20);
            dump_field_uint(ctx, "frame_func", ff);
            if (ctx->names) {
                const char *p = xx_ms_pdb_names_get(ctx->names, ff);
                if (p) dump_field_string(ctx, "program", p);
            }
            dump_field_uint(ctx, "prolog_size", dump_u16(sub_data, sub_len, cur + 24));
            dump_field_uint(ctx, "saved_regs_size", dump_u16(sub_data, sub_len, cur + 26));
            dump_field_hex(ctx, "flags", dump_u32(sub_data, sub_len, cur + 28));
            dump_end(ctx, "entry", strm_idx, sub_off + 8 + (int64_t)cur);
            cur += 32;
        }
        dump_end(ctx, "frame_data", strm_idx, sub_off + 8 + (int64_t)cur);
    } else if (k == 0xF6) { /* INLINEELINES */
        if (sub_len >= 4) {
            uint32_t sig = dump_u32(sub_data, sub_len, 0);
            dump_field_uint(ctx, "signature", sig);
            size_t cur = 4;
            dump_begin(ctx, "inlinees", strm_idx, sub_off + 8 + 4);
            while (cur + 12 <= sub_len && !ctx->stopped) {
                uint32_t inlinee = dump_u32(sub_data, sub_len, cur);
                uint32_t file_id = dump_u32(sub_data, sub_len, cur + 4);
                uint32_t line = dump_u32(sub_data, sub_len, cur + 8);
                cur += 12;

                dump_begin(ctx, "inlinee_line", strm_idx, -1);
                dump_field_uint(ctx, "inlinee", inlinee);
                dump_field_uint(ctx, "file_id", file_id);
                dump_field_uint(ctx, "source_line", line);

                if (sig == 1 && cur + 4 <= sub_len) {
                    uint32_t extra_cnt = dump_u32(sub_data, sub_len, cur);
                    cur += 4;
                    dump_begin(ctx, "extra_files", strm_idx, -1);
                    for (uint32_t e = 0; e < extra_cnt && cur + 4 <= sub_len; e++) {
                        dump_field_uint(ctx, "file_id", dump_u32(sub_data, sub_len, cur));
                        cur += 4;
                    }
                    dump_end(ctx, "extra_files", strm_idx, -1);
                }
                dump_end(ctx, "inlinee_line", strm_idx, -1);
            }
            dump_end(ctx, "inlinees", strm_idx, sub_off + 8 + 4);
        }
    } else if (k == 0xF7) { /* CROSSSCOPEIMPORTS */
        size_t cur = 0;
        dump_begin(ctx, "imports", strm_idx, sub_off + 8);
        while (cur + 8 <= sub_len && !ctx->stopped) {
            uint32_t mod_off = dump_u32(sub_data, sub_len, cur);
            uint32_t cnt = dump_u32(sub_data, sub_len, cur + 4);
            cur += 8;

            dump_begin(ctx, "import_scope", strm_idx, -1);
            dump_field_uint(ctx, "module_name_offset", mod_off);
            dump_field_uint(ctx, "count", cnt);
            dump_begin(ctx, "ids", strm_idx, -1);
            for (uint32_t c = 0; c < cnt && cur + 4 <= sub_len; c++) {
                dump_field_uint(ctx, "id", dump_u32(sub_data, sub_len, cur));
                cur += 4;
            }
            dump_end(ctx, "ids", strm_idx, -1);
            dump_end(ctx, "import_scope", strm_idx, -1);
        }
        dump_end(ctx, "imports", strm_idx, sub_off + 8);
    } else if (k == 0xF8) { /* CROSSSCOPEEXPORTS */
        size_t cur = 0;
        dump_begin(ctx, "exports", strm_idx, sub_off + 8);
        while (cur + 8 <= sub_len && !ctx->stopped) {
            dump_begin(ctx, "export", strm_idx, -1);
            dump_field_uint(ctx, "local_id", dump_u32(sub_data, sub_len, cur));
            dump_field_uint(ctx, "global_id", dump_u32(sub_data, sub_len, cur + 4));
            dump_end(ctx, "export", strm_idx, -1);
            cur += 8;
        }
        dump_end(ctx, "exports", strm_idx, sub_off + 8);
    } else if (k == 0xFA || k == 0xFB) { /* MDTOKEN_MAP */
        size_t cur = 0;
        dump_begin(ctx, "token_map", strm_idx, sub_off + 8);
        while (cur + 8 <= sub_len && !ctx->stopped) {
            dump_begin(ctx, "entry", strm_idx, -1);
            dump_field_uint(ctx, "index", dump_u32(sub_data, sub_len, cur));
            dump_field_hex(ctx, "token", dump_u32(sub_data, sub_len, cur + 4));
            dump_end(ctx, "entry", strm_idx, -1);
            cur += 8;
        }
        dump_end(ctx, "token_map", strm_idx, sub_off + 8);
    } else if (k == 0xFD) { /* COFF_SYMBOL_RVA */
        size_t count = sub_len / 4;
        dump_begin(ctx, "coff_symbol_rvas", strm_idx, sub_off + 8);
        for (size_t i = 0; i < count && !ctx->stopped; i++) {
            dump_field_hex(ctx, "rva", dump_u32(sub_data, sub_len, i * 4));
        }
        dump_end(ctx, "coff_symbol_rvas", strm_idx, sub_off + 8);
    } else {
        dump_field_bytes(ctx, "data", sub_data, sub_len);
    }

    dump_end(ctx, "subsection", strm_idx, sub_off);
}

static void dump_walk_modules(dump_ctx *ctx)
{
    xx_ms_pdb_module_list mods = {0};
    if (!xx_ms_pdb_get_modules(ctx->pdb, &mods)) return;

    dump_begin(ctx, "modules", -1, -1);
    for (size_t i = 0; i < mods.count && !ctx->stopped; i++) {
        xx_ms_pdb_module *m = &mods.items[i];
        if (m->stream_index == 0xFFFF) continue;

        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, m->stream_index, &size);
        if (!data || size < 4) continue;

        dump_begin(ctx, "module", m->stream_index, 0);
        dump_field_uint(ctx, "index", (uint64_t)m->index);
        dump_field_string(ctx, "name", m->module_name);
        dump_field_string(ctx, "object", m->object_name);

        uint16_t cur_machine = ctx->dbi_info.machine;

        /* 1. Symbols */
        if (m->sym_byte_size > 4 && m->sym_byte_size <= size) {
            size_t sym_off = 4;
            dump_begin(ctx, "symbols", m->stream_index, 4);
            while (sym_off + 4 <= m->sym_byte_size && !ctx->stopped) {
                uint16_t len = dump_u16(data, size, sym_off);
                uint16_t kind = dump_u16(data, size, sym_off + 2);
                size_t rec_sz = (size_t)len + 2;
                if (sym_off + rec_sz > m->sym_byte_size) break;

                /* Track machine */
                if ((kind == 0x110B || kind == 0x113C) && len >= 8) {
                    cur_machine = dump_u16(data, size, sym_off + 8);
                }

                dump_begin(ctx, "symbol", m->stream_index, (int64_t)sym_off);
                dump_field_enum(ctx, "kind", kind, xx_ms_pdb_symbol_kind_to_string(kind));
                dump_field_uint(ctx, "length", len);
                if (len >= 2) {
                    xx_ms_pdb_decode_symbol(kind, data + sym_off + 4, (size_t)len - 2, cur_machine, ctx->depth, ctx->fn, ctx->user);
                }
                dump_end(ctx, "symbol", m->stream_index, (int64_t)sym_off);
                sym_off += rec_sz;
            }
            dump_end(ctx, "symbols", m->stream_index, 4);
        }

        /* 2. C11 lines */
        if (m->c11_byte_size > 0 && (size_t)m->sym_byte_size + m->c11_byte_size <= size) {
            dump_begin(ctx, "c11_lines", m->stream_index, (int64_t)m->sym_byte_size);
            dump_field_bytes(ctx, "data", data + m->sym_byte_size, (size_t)m->c11_byte_size);
            dump_end(ctx, "c11_lines", m->stream_index, (int64_t)m->sym_byte_size);
        }

        /* 3. C13 subsections */
        if (m->c13_byte_size > 0) {
            size_t c13_start = (size_t)m->sym_byte_size + m->c11_byte_size;
            size_t c13_end = c13_start + m->c13_byte_size;
            if (c13_end <= size) {
                size_t sub_off = c13_start;
                dump_begin(ctx, "c13_subsections", m->stream_index, (int64_t)c13_start);
                while (sub_off + 8 <= c13_end && !ctx->stopped) {
                    uint32_t skind = dump_u32(data, size, sub_off);
                    uint32_t slen = dump_u32(data, size, sub_off + 4);
                    size_t aligned_len = ((size_t)slen + 3) & ~3;
                    if (sub_off + 8 + slen > c13_end) break;

                    dump_walk_c13_subsection(ctx, m->stream_index, skind, data + sub_off + 8, slen, (int64_t)sub_off);
                    sub_off += 8 + aligned_len;
                }
                dump_end(ctx, "c13_subsections", m->stream_index, (int64_t)c13_start);
            }
        }

        /* 4. Global refs */
        size_t total_covered = (size_t)m->sym_byte_size + m->c11_byte_size + m->c13_byte_size;
        if (total_covered + 4 <= size) {
            uint32_t gr_sz = dump_u32(data, size, total_covered);
            if (total_covered + 4 + gr_sz <= size) {
                size_t count = gr_sz / 4;
                dump_begin(ctx, "global_refs", m->stream_index, (int64_t)total_covered);
                dump_field_uint(ctx, "size", gr_sz);
                dump_begin(ctx, "offsets", m->stream_index, (int64_t)(total_covered + 4));
                for (size_t g = 0; g < count && !ctx->stopped; g++) {
                    dump_field_hex(ctx, "offset", dump_u32(data, size, total_covered + 4 + g * 4));
                }
                dump_end(ctx, "offsets", m->stream_index, (int64_t)(total_covered + 4));
                dump_end(ctx, "global_refs", m->stream_index, (int64_t)total_covered);
            }
        }

        dump_end(ctx, "module", m->stream_index, 0);
    }
    dump_end(ctx, "modules", -1, -1);
    xx_ms_pdb_module_list_free(&mods);
}

/* ---- 9. SYMBOLS (Global Symbol Records) ------------------------------------ */
static void dump_walk_symbols(dump_ctx *ctx)
{
    int32_t strm_idx = (int32_t)ctx->dbi_info.sym_record_stream;
    size_t size = 0;
    uint8_t *data = NULL;
    size_t off = 0;

    if (strm_idx < 0 || strm_idx == 0xFFFF) return;
    data = dump_read_stream(ctx, strm_idx, &size);
    if (!data) return;

    dump_begin(ctx, "symbols", strm_idx, 0);
    while (off + 4 <= size && !ctx->stopped) {
        uint16_t len = dump_u16(data, size, off);
        uint16_t kind = dump_u16(data, size, off + 2);
        size_t rec_sz = (size_t)len + 2;
        if (off + rec_sz > size) {
            dump_field_error(ctx, "Symbol record truncated");
            break;
        }

        dump_begin(ctx, "symbol", strm_idx, (int64_t)off);
        dump_field_enum(ctx, "kind", kind, xx_ms_pdb_symbol_kind_to_string(kind));
        dump_field_uint(ctx, "length", len);
        if (len >= 2) {
            xx_ms_pdb_decode_symbol(kind, data + off + 4, (size_t)len - 2, ctx->dbi_info.machine, ctx->depth, ctx->fn, ctx->user);
        }
        dump_end(ctx, "symbol", strm_idx, (int64_t)off);
        off += rec_sz;
    }
    dump_end(ctx, "symbols", strm_idx, 0);
}

/* ---- 10. GLOBALS ---------------------------------------------------------- */
static void dump_walk_globals(dump_ctx *ctx)
{
    int32_t strm_idx = (int32_t)ctx->dbi_info.global_stream_index;
    size_t size = 0;
    uint8_t *data = NULL;

    if (strm_idx < 0 || strm_idx == 0xFFFF) return;
    data = dump_read_stream(ctx, strm_idx, &size);
    if (!data || size < 16) return;

    dump_begin(ctx, "globals", strm_idx, 0);

    uint32_t ver_sig = dump_u32(data, size, 0);
    uint32_t ver_hdr = dump_u32(data, size, 4);
    uint32_t hr_sz = dump_u32(data, size, 8);
    uint32_t num_buckets = dump_u32(data, size, 12);

    dump_begin(ctx, "header", strm_idx, 0);
    dump_field_hex(ctx, "ver_signature", ver_sig);
    dump_field_hex(ctx, "ver_hdr", ver_hdr);
    dump_field_uint(ctx, "hr_size", hr_sz);
    dump_field_uint(ctx, "num_buckets", num_buckets);
    dump_end(ctx, "header", strm_idx, 0);

    size_t off = 16;
    if (off + hr_sz <= size) {
        size_t count = hr_sz / 8;
        dump_begin(ctx, "hash_records", strm_idx, (int64_t)off);
        for (size_t i = 0; i < count && !ctx->stopped; i++) {
            uint32_t o = dump_u32(data, size, off + i * 8);
            uint32_t c = dump_u32(data, size, off + i * 8 + 4);
            dump_begin(ctx, "record", strm_idx, (int64_t)(off + i * 8));
            dump_field_hex(ctx, "offset", (o > 0) ? (o - 1) : 0);
            dump_field_uint(ctx, "cref", c);
            dump_end(ctx, "record", strm_idx, (int64_t)(off + i * 8));
        }
        dump_end(ctx, "hash_records", strm_idx, (int64_t)off);
        off += hr_sz;

        /* Bitmap (129 uint32 words) */
        if (off + 516 <= size) {
            uint32_t set_bits = 0;
            dump_begin(ctx, "bitmap", strm_idx, (int64_t)off);
            for (size_t w = 0; w < 129; w++) {
                uint32_t word = dump_u32(data, size, off + w * 4);
                dump_field_hex(ctx, "word", word);
                while (word) {
                    set_bits += (word & 1);
                    word >>= 1;
                }
            }
            dump_end(ctx, "bitmap", strm_idx, (int64_t)off);
            off += 516;

            /* Bucket offsets */
            if (off + (size_t)set_bits * 4 <= size) {
                dump_begin(ctx, "bucket_offsets", strm_idx, (int64_t)off);
                for (uint32_t b = 0; b < set_bits && !ctx->stopped; b++) {
                    dump_field_hex(ctx, "offset", dump_u32(data, size, off + (size_t)b * 4));
                }
                dump_end(ctx, "bucket_offsets", strm_idx, (int64_t)off);
            }
        }
    }

    dump_end(ctx, "globals", strm_idx, 0);
}

/* ---- 11. PUBLICS ---------------------------------------------------------- */
static void dump_walk_publics(dump_ctx *ctx)
{
    int32_t strm_idx = (int32_t)ctx->dbi_info.public_stream_index;
    size_t size = 0;
    uint8_t *data = NULL;

    if (strm_idx < 0 || strm_idx == 0xFFFF) return;
    data = dump_read_stream(ctx, strm_idx, &size);
    if (!data || size < 28) return;

    dump_begin(ctx, "publics", strm_idx, 0);

    uint32_t sym_hash = dump_u32(data, size, 0);
    uint32_t addr_map = dump_u32(data, size, 4);
    uint32_t num_thunks = dump_u32(data, size, 8);
    uint32_t sz_thunk = dump_u32(data, size, 12);
    uint16_t isect_thunk = dump_u16(data, size, 16);
    uint16_t padding = dump_u16(data, size, 18);
    uint32_t off_thunk = dump_u32(data, size, 20);
    uint32_t num_sec = dump_u32(data, size, 24);

    dump_begin(ctx, "header", strm_idx, 0);
    dump_field_uint(ctx, "sym_hash", sym_hash);
    dump_field_uint(ctx, "addr_map", addr_map);
    dump_field_uint(ctx, "num_thunks", num_thunks);
    dump_field_uint(ctx, "size_of_thunk", sz_thunk);
    dump_field_uint(ctx, "isect_thunk_table", isect_thunk);
    dump_field_uint(ctx, "padding", padding);
    dump_field_hex(ctx, "off_thunk_table", off_thunk);
    dump_field_uint(ctx, "num_sections", num_sec);
    dump_end(ctx, "header", strm_idx, 0);

    size_t off = 28;

    /* GSI hash inside publics */
    if (sym_hash >= 16 && off + sym_hash <= size) {
        uint32_t ver_sig = dump_u32(data, size, off);
        uint32_t ver_hdr = dump_u32(data, size, off + 4);
        uint32_t hr_sz = dump_u32(data, size, off + 8);
        uint32_t num_buckets = dump_u32(data, size, off + 12);

        dump_begin(ctx, "gsi_hash", strm_idx, (int64_t)off);
        dump_field_hex(ctx, "ver_signature", ver_sig);
        dump_field_hex(ctx, "ver_hdr", ver_hdr);
        dump_field_uint(ctx, "hr_size", hr_sz);
        dump_field_uint(ctx, "num_buckets", num_buckets);

        size_t h_off = off + 16;
        if ((h_off + hr_sz <= off + sym_hash) && (h_off + hr_sz <= size)) {
            size_t count = hr_sz / 8;
            dump_begin(ctx, "hash_records", strm_idx, (int64_t)h_off);
            for (size_t i = 0; i < count && !ctx->stopped; i++) {
                uint32_t o = dump_u32(data, size, h_off + i * 8);
                uint32_t c = dump_u32(data, size, h_off + i * 8 + 4);
                dump_begin(ctx, "record", strm_idx, (int64_t)(h_off + i * 8));
                dump_field_hex(ctx, "offset", (o > 0) ? (o - 1) : 0);
                dump_field_uint(ctx, "cref", c);
                dump_end(ctx, "record", strm_idx, (int64_t)(h_off + i * 8));
            }
            dump_end(ctx, "hash_records", strm_idx, (int64_t)h_off);
            h_off += hr_sz;

            if (h_off + 516 <= off + sym_hash) {
                uint32_t set_bits = 0;
                dump_begin(ctx, "bitmap", strm_idx, (int64_t)h_off);
                for (size_t w = 0; w < 129; w++) {
                    uint32_t word = dump_u32(data, size, h_off + w * 4);
                    dump_field_hex(ctx, "word", word);
                    while (word) {
                        set_bits += (word & 1);
                        word >>= 1;
                    }
                }
                dump_end(ctx, "bitmap", strm_idx, (int64_t)h_off);
                h_off += 516;

                if (h_off + (size_t)set_bits * 4 <= off + sym_hash) {
                    dump_begin(ctx, "bucket_offsets", strm_idx, (int64_t)h_off);
                    for (uint32_t b = 0; b < set_bits && !ctx->stopped; b++) {
                        dump_field_hex(ctx, "offset", dump_u32(data, size, h_off + (size_t)b * 4));
                    }
                    dump_end(ctx, "bucket_offsets", strm_idx, (int64_t)h_off);
                }
            }
        }
        dump_end(ctx, "gsi_hash", strm_idx, (int64_t)off);
        off += sym_hash;
    }

    /* Address map */
    if (addr_map > 0 && off + addr_map <= size) {
        size_t count = addr_map / 4;
        dump_begin(ctx, "address_map", strm_idx, (int64_t)off);
        for (size_t i = 0; i < count && !ctx->stopped; i++) {
            dump_field_hex(ctx, "offset", dump_u32(data, size, off + i * 4));
        }
        dump_end(ctx, "address_map", strm_idx, (int64_t)off);
        off += addr_map;
    }

    /* Thunk map */
    size_t thunk_sz = (size_t)num_thunks * sz_thunk;
    if (thunk_sz > 0 && off + thunk_sz <= size) {
        dump_begin(ctx, "thunk_map", strm_idx, (int64_t)off);
        dump_field_bytes(ctx, "data", data + off, thunk_sz);
        dump_end(ctx, "thunk_map", strm_idx, (int64_t)off);
        off += thunk_sz;
    }

    /* Section map */
    if (off < size) {
        dump_begin(ctx, "section_map", strm_idx, (int64_t)off);
        dump_field_bytes(ctx, "data", data + off, size - off);
        dump_end(ctx, "section_map", strm_idx, (int64_t)off);
    }

    dump_end(ctx, "publics", strm_idx, 0);
}

/* ---- 12. OTHER (Other named and orphan streams) --------------------------- */
static bool dump_is_text(const uint8_t *data, size_t size)
{
    if (size == 0) return true;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c == 0) return false;
        if (c < 9 || (c > 13 && c < 32)) return false;
    }
    return true;
}

static void dump_walk_other(dump_ctx *ctx)
{
    dump_begin(ctx, "other", -1, -1);

    for (int32_t i = 0; i < ctx->stream_count && !ctx->stopped; i++) {
        const char *role = (i < ctx->roles_count) ? ctx->stream_roles[i] : NULL;
        bool is_orphan = (role == NULL);
        bool is_named = (role && strncmp(role, "Named Stream \"", 14) == 0);

        if (!is_orphan && !is_named) {
            continue; /* already covered */
        }

        size_t size = 0;
        uint8_t *data = dump_read_stream(ctx, i, &size);

        if (is_named) {
            const char *name = role + 14;
            char clean_name[256] = {0};
            strncpy_s(clean_name, sizeof(clean_name), name, strlen(name) - 1);

            dump_begin(ctx, "named_stream", i, 0);
            dump_field_string(ctx, "name", clean_name);
            dump_field_uint(ctx, "size", size);

            if (strcmp(clean_name, "/names") == 0) {
                /* Handled in NAMES section */
            } else if (strcmp(clean_name, "/LinkInfo") == 0) {
                if (data && size >= 8) {
                    dump_field_uint(ctx, "cb", dump_u32(data, size, 0));
                    dump_field_uint(ctx, "version", dump_u32(data, size, 4));
                    dump_field_bytes(ctx, "data", data + 8, size - 8);
                }
            } else if (strcmp(clean_name, "/UDTSRCLINEUNDONE") == 0) {
                if (data && size >= 4) {
                    size_t count = size / 4;
                    dump_begin(ctx, "ids", i, 0);
                    for (size_t c = 0; c < count && !ctx->stopped; c++) {
                        dump_field_uint(ctx, "id", dump_u32(data, size, c * 4));
                    }
                    dump_end(ctx, "ids", i, 0);
                }
            } else if (strcmp(clean_name, "/src/headerblock") == 0) {
                if (data && size >= 64) {
                    dump_begin(ctx, "src_header_block", i, 0);
                    dump_field_uint(ctx, "version", dump_u32(data, size, 0));
                    dump_field_uint(ctx, "size", dump_u32(data, size, 4));
                    dump_field_uint(ctx, "filetime", dump_u64(data, size, 8));
                    dump_field_uint(ctx, "age", dump_u32(data, size, 16));
                    dump_end(ctx, "src_header_block", i, 0);
                }
            } else if (data && dump_is_text(data, size)) {
                dump_field_string(ctx, "content", (const char *)data);
            } else if (data) {
                dump_field_bytes(ctx, "content", data, size);
            }
            dump_end(ctx, "named_stream", i, 0);
        } else if (is_orphan && data && size > 0) {
            dump_begin(ctx, "orphan_stream", i, 0);
            dump_field_uint(ctx, "index", (uint64_t)i);
            dump_field_uint(ctx, "size", size);
            dump_field_bytes(ctx, "data", data, (size > DUMP_BLOB_CAP) ? DUMP_BLOB_CAP : size);
            dump_end(ctx, "orphan_stream", i, 0);
        }
    }

    dump_end(ctx, "other", -1, -1);
}

/* ===========================================================================
 * Public API
 * =========================================================================== */

const char *xx_ms_pdb_dump_section_name(uint32_t what_bit)
{
    switch (what_bit) {
        case XX_MS_PDB_DUMP_MSF: return "msf";
        case XX_MS_PDB_DUMP_INFO: return "info";
        case XX_MS_PDB_DUMP_NAMES: return "names";
        case XX_MS_PDB_DUMP_TPI: return "tpi";
        case XX_MS_PDB_DUMP_IPI: return "ipi";
        case XX_MS_PDB_DUMP_TPI_HASH: return "tpi_hash";
        case XX_MS_PDB_DUMP_DBI: return "dbi";
        case XX_MS_PDB_DUMP_DBG_STREAMS: return "dbg_streams";
        case XX_MS_PDB_DUMP_MODULES: return "modules";
        case XX_MS_PDB_DUMP_SYMBOLS: return "symbols";
        case XX_MS_PDB_DUMP_GLOBALS: return "globals";
        case XX_MS_PDB_DUMP_PUBLICS: return "publics";
        case XX_MS_PDB_DUMP_OTHER: return "other";
        default: return "";
    }
}

bool xx_ms_pdb_dump(xx_ms_pdb *pdb, uint32_t what, const xx_ms_pdb_names *names, xx_ms_pdb_dump_fn fn, void *user)
{
    dump_ctx ctx = {0};

    if (!pdb || !fn) {
        return false;
    }

    ctx.pdb = pdb;
    ctx.names = names;
    ctx.fn = fn;
    ctx.user = user;
    ctx.depth = 0;
    ctx.stopped = false;

    ctx.msf = xx_ms_pdb_get_msf_header(pdb);
    ctx.stream_count = xx_ms_pdb_get_number_of_streams(pdb);
    ctx.pdb_info = xx_ms_pdb_get_pdb_info(pdb);
    ctx.tpi_info = xx_ms_pdb_get_tpi_info(pdb);
    ctx.ipi_info = xx_ms_pdb_get_ipi_info(pdb);
    ctx.dbi_info = xx_ms_pdb_get_dbi_info(pdb);

    dump_init_roles(&ctx);

    if ((what & XX_MS_PDB_DUMP_MSF) && !ctx.stopped) {
        dump_walk_msf(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_INFO) && !ctx.stopped) {
        dump_walk_info(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_NAMES) && !ctx.stopped) {
        dump_walk_names(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_TPI) && !ctx.stopped) {
        dump_walk_types_stream(&ctx, XX_MS_PDB_STREAM_TPI, "tpi", &ctx.tpi_info);
    }
    if ((what & XX_MS_PDB_DUMP_IPI) && !ctx.stopped) {
        dump_walk_types_stream(&ctx, XX_MS_PDB_STREAM_IPI, "ipi", &ctx.ipi_info);
    }
    if ((what & XX_MS_PDB_DUMP_TPI_HASH) && !ctx.stopped) {
        dump_walk_tpi_hash(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_DBI) && !ctx.stopped) {
        dump_walk_dbi(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_DBG_STREAMS) && !ctx.stopped) {
        dump_walk_dbg_streams(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_MODULES) && !ctx.stopped) {
        dump_walk_modules(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_SYMBOLS) && !ctx.stopped) {
        dump_walk_symbols(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_GLOBALS) && !ctx.stopped) {
        dump_walk_globals(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_PUBLICS) && !ctx.stopped) {
        dump_walk_publics(&ctx);
    }
    if ((what & XX_MS_PDB_DUMP_OTHER) && !ctx.stopped) {
        dump_walk_other(&ctx);
    }

    dump_cleanup_roles(&ctx);
    if (ctx.buf) {
        xx_mem_free(ctx.buf);
    }

    return !ctx.stopped;
}
