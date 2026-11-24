/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft PDB: the /names string table and segment:offset -> rva mapping
 * (section headers, OMAP). Shared base of the xx_microsoft_pdb_debug.h parts.
 */
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_debug.h"

#include <stdlib.h>
#include <string.h>

#include "xxfclib/memory/xx_memory.h"

#define PDB_NAMES_MAGIC 0xEFFEEFFEu
#define PDB_DBG_OMAP_FROM_SRC 4
#define PDB_DBG_SECTION_HDR 5
#define PDB_DBG_SECTION_HDR_ORIG 10
#define PDB_NAMES_MAX (256u * 1024u * 1024u)
#define PDB_OMAP_MAX (64u * 1024u * 1024u)

struct xx_ms_pdb_names {
    char *buffer;     /* buffer_size bytes + a NUL */
    uint32_t buffer_size;
};

typedef struct pdb_omap_entry {
    uint32_t rva;
    uint32_t rva_to;
} pdb_omap_entry;

struct xx_ms_pdb_addr_map {
    uint32_t *section_va;      /* the sections rvas are computed with */
    uint32_t section_count;
    pdb_omap_entry *omap;      /* from source, sorted by rva */
    size_t omap_count;
    bool has_omap;
};

static uint32_t names_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Reads a whole stream (at most max bytes); NULL when missing / empty / too big. */
static uint8_t *names_read_stream(xx_ms_pdb *pdb, int32_t index, uint32_t max, uint32_t *size)
{
    int64_t stream_size = 0;
    uint8_t *data = NULL;

    *size = 0;
    if (index < 0) {
        return NULL;
    }
    stream_size = xx_ms_pdb_get_stream_size(pdb, index);
    if (stream_size <= 0 || stream_size > (int64_t)max) {
        return NULL;
    }
    data = (uint8_t *)xx_mem_alloc((size_t)stream_size + 1);
    if (!data) {
        return NULL;
    }
    if (xx_ms_pdb_read_stream(pdb, index, 0, data, stream_size) != stream_size) {
        xx_mem_free(data);
        return NULL;
    }
    data[stream_size] = 0;
    *size = (uint32_t)stream_size;
    return data;
}

xx_ms_pdb_names *xx_ms_pdb_names_open(xx_ms_pdb *pdb)
{
    uint8_t *data = NULL;
    uint32_t size = 0;
    uint32_t buffer_size = 0;
    xx_ms_pdb_names *names = NULL;

    if (!pdb || xx_ms_pdb_get_type(pdb) != XX_MS_PDB_TYPE_PDB7) {
        return NULL;
    }
    data = names_read_stream(pdb, xx_ms_pdb_get_named_stream_index(pdb, "/names"), PDB_NAMES_MAX, &size);
    if (!data) {
        return NULL;
    }
    if (size < 12 || names_u32(data) != PDB_NAMES_MAGIC) {
        xx_mem_free(data);
        return NULL;
    }
    buffer_size = names_u32(data + 8);
    if ((uint64_t)buffer_size > (uint64_t)size - 12) {
        buffer_size = size - 12; /* cut, never read past the stream */
    }
    names = (xx_ms_pdb_names *)xx_mem_calloc(1, sizeof(*names));
    if (!names) {
        xx_mem_free(data);
        return NULL;
    }
    names->buffer = (char *)xx_mem_alloc((size_t)buffer_size + 1);
    if (!names->buffer) {
        xx_mem_free(names);
        xx_mem_free(data);
        return NULL;
    }
    memcpy(names->buffer, data + 12, buffer_size);
    names->buffer[buffer_size] = 0;
    names->buffer_size = buffer_size;
    xx_mem_free(data);
    return names;
}

void xx_ms_pdb_names_close(xx_ms_pdb_names *names)
{
    if (!names) {
        return;
    }
    xx_mem_free(names->buffer);
    xx_mem_free(names);
}

const char *xx_ms_pdb_names_get(const xx_ms_pdb_names *names, uint32_t offset)
{
    if (!names || offset >= names->buffer_size) {
        return NULL;
    }
    return names->buffer + offset; /* the buffer ends in a NUL we added */
}

/* Section virtual addresses from an IMAGE_SECTION_HEADER array stream. */
static bool map_load_sections(xx_ms_pdb *pdb, int32_t stream, xx_ms_pdb_addr_map *map)
{
    uint32_t size = 0;
    uint32_t i = 0;
    uint8_t *data = names_read_stream(pdb, stream, 40u * 65536u, &size);

    if (!data) {
        return false;
    }
    map->section_count = size / 40;
    if (map->section_count) {
        map->section_va = (uint32_t *)xx_mem_alloc(sizeof(uint32_t) * map->section_count);
        if (!map->section_va) {
            map->section_count = 0;
            xx_mem_free(data);
            return false;
        }
        for (i = 0; i < map->section_count; i++) {
            map->section_va[i] = names_u32(data + (size_t)i * 40 + 12);
        }
    }
    xx_mem_free(data);
    return map->section_count != 0;
}

static int map_cmp_omap(const void *a, const void *b)
{
    const pdb_omap_entry *x = (const pdb_omap_entry *)a;
    const pdb_omap_entry *y = (const pdb_omap_entry *)b;
    return (x->rva > y->rva) - (x->rva < y->rva);
}

xx_ms_pdb_addr_map *xx_ms_pdb_addr_map_open(xx_ms_pdb *pdb)
{
    xx_ms_pdb_addr_map *map = (xx_ms_pdb_addr_map *)xx_mem_calloc(1, sizeof(*map));
    xx_ms_pdb_dbi_info dbi;
    int32_t omap_stream = -1;
    int32_t orig_stream = -1;
    int32_t sect_stream = -1;

    if (!map) {
        return NULL;
    }
    if (!pdb || xx_ms_pdb_get_type(pdb) != XX_MS_PDB_TYPE_PDB7) {
        return map;
    }
    dbi = xx_ms_pdb_get_dbi_info(pdb);
    if (!dbi.is_valid || !dbi.dbg_streams) {
        return map;
    }
    if (dbi.dbg_stream_count > PDB_DBG_SECTION_HDR && dbi.dbg_streams[PDB_DBG_SECTION_HDR] != 0xFFFF) {
        sect_stream = dbi.dbg_streams[PDB_DBG_SECTION_HDR];
    }
    if (dbi.dbg_stream_count > PDB_DBG_SECTION_HDR_ORIG && dbi.dbg_streams[PDB_DBG_OMAP_FROM_SRC] != 0xFFFF &&
        dbi.dbg_streams[PDB_DBG_SECTION_HDR_ORIG] != 0xFFFF) {
        omap_stream = dbi.dbg_streams[PDB_DBG_OMAP_FROM_SRC];
        orig_stream = dbi.dbg_streams[PDB_DBG_SECTION_HDR_ORIG];
    }
    if (omap_stream >= 0) {
        uint32_t size = 0;
        uint8_t *data = names_read_stream(pdb, omap_stream, PDB_OMAP_MAX, &size);
        if (data && size >= 8 && map_load_sections(pdb, orig_stream, map)) {
            size_t i = 0;
            map->omap_count = size / 8;
            map->omap = (pdb_omap_entry *)xx_mem_alloc(sizeof(pdb_omap_entry) * map->omap_count);
            if (map->omap) {
                for (i = 0; i < map->omap_count; i++) {
                    map->omap[i].rva = names_u32(data + i * 8);
                    map->omap[i].rva_to = names_u32(data + i * 8 + 4);
                }
                qsort(map->omap, map->omap_count, sizeof(pdb_omap_entry), map_cmp_omap);
                map->has_omap = true;
            } else {
                map->omap_count = 0;
            }
        }
        xx_mem_free(data);
        if (!map->has_omap) {
            xx_mem_free(map->section_va);
            map->section_va = NULL;
            map->section_count = 0;
        }
    }
    if (!map->has_omap && sect_stream >= 0) {
        map_load_sections(pdb, sect_stream, map);
    }
    return map;
}

void xx_ms_pdb_addr_map_close(xx_ms_pdb_addr_map *map)
{
    if (!map) {
        return;
    }
    xx_mem_free(map->section_va);
    xx_mem_free(map->omap);
    xx_mem_free(map);
}

bool xx_ms_pdb_addr_map_has_omap(const xx_ms_pdb_addr_map *map)
{
    return map && map->has_omap;
}

uint64_t xx_ms_pdb_addr_map_omap(const xx_ms_pdb_addr_map *map, uint32_t rva)
{
    size_t lo = 0;
    size_t hi = 0;
    const pdb_omap_entry *e = NULL;

    if (!map) {
        return XX_MS_PDB_NO_RVA;
    }
    if (!map->has_omap) {
        return rva;
    }
    /* the last entry with entry.rva <= rva */
    hi = map->omap_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (map->omap[mid].rva <= rva) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return XX_MS_PDB_NO_RVA;
    }
    e = &map->omap[lo - 1];
    if (e->rva_to == 0) {
        return XX_MS_PDB_NO_RVA;
    }
    return (uint64_t)e->rva_to + (rva - e->rva);
}

uint64_t xx_ms_pdb_addr_map_rva(const xx_ms_pdb_addr_map *map, uint16_t segment, uint32_t offset)
{
    uint64_t rva = 0;

    if (!map || segment == 0 || segment > map->section_count) {
        return XX_MS_PDB_NO_RVA;
    }
    rva = (uint64_t)map->section_va[segment - 1] + offset;
    if (map->has_omap) {
        if (rva > 0xFFFFFFFFu) {
            return XX_MS_PDB_NO_RVA;
        }
        return xx_ms_pdb_addr_map_omap(map, (uint32_t)rva);
    }
    return rva;
}
