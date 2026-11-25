/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft program database reader: MSF 7.00 ("DS"), the old
 * "program database 2.00" layout ("JG") and portable PDB detection.
 * The 16t / ST type and symbol records of a JG file, which XPDB does not
 * decode, are normalised to the current ones (pdb_legacy_record); that code
 * runs for JG files only.
 * C port of _mylibs/XPDB (xpdb.cpp); the function order and the corner cases
 * follow it so the two can be compared line by line. Where XPDB does 32 bit
 * arithmetic on an offset, the same width is used here on purpose.
 *
 * Bounded and lazy: stream pages are read on demand through the block map,
 * the whole file is never loaded. Payloads are never executed.
 *
 * Layout reference: https://github.com/Microsoft/microsoft-pdb
 *
 * xx_ms_pdb::strict (xx_ms_pdb_set_strict) switches off the XPDB decoding
 * quirks; every such place tests the flag and is marked "strict:". The list
 * is in the header. With the flag clear nothing here behaves differently.
 */
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

/* XPDB caps its reads at 0x7FFFFFF0 (QByteArray's limit). Offsets inside a
 * buffer are int32_t and a parse step can advance past the buffer end by a
 * name (<= 8 KiB) plus a few fixed fields before the next bounds check, so the
 * cap keeps 1 MiB of headroom below INT32_MAX: no int32_t offset can overflow.
 * Only streams of nearly 2 GiB are affected, and they are cut either way. */
#define PDB_MAX_BUFFER 0x7FF00000

/* XPDB cuts a name at 4096 / 8192 bytes. strict: a name ends at its zero byte
 * or at the end of its record (a record holds less than 0x10002 bytes). */
#define PDB_NAME_MAX(pdb, quirk_size) ((pdb)->strict ? 0x10002 : (quirk_size))

/* The numeric leaves XPDB does not know are stepped over by their real size
 * in strict mode and, in both modes, in a JG file (XPDB decodes no JG record,
 * so there is nothing to stay identical to). */
#define PDB_NUMERIC_EXACT(pdb) ((pdb)->strict || (pdb)->is_legacy)

/* Leaves only the strict mode decodes (cvinfo.h) */
#define PDB_LF_FRIENDFCN 0x150C
#define PDB_LF_NESTTYPEEX 0x1512
#define PDB_LF_MEMBERMODIFY 0x1513
#define PDB_LF_REAL48 0x800B
#define PDB_LF_COMPLEX32 0x800C
#define PDB_LF_COMPLEX64 0x800D
#define PDB_LF_COMPLEX80 0x800E
#define PDB_LF_COMPLEX128 0x800F
#define PDB_LF_VARSTRING 0x8010
#define PDB_LF_OCTWORD 0x8017
#define PDB_LF_UOCTWORD 0x8018
#define PDB_LF_DECIMAL 0x8019
#define PDB_LF_DATE 0x801A
#define PDB_LF_UTF8STRING 0x801B
#define PDB_LF_REAL16 0x801C

/* ---- small helpers -------------------------------------------------------- */

typedef struct pdb_chunk {
    struct pdb_chunk *next;
    size_t used;
    size_t capacity;
} pdb_chunk;

typedef struct pdb_istr {
    const char *string;
    uint32_t hash;
    uint32_t length;
} pdb_istr;

typedef struct pdb_pmap_entry {
    const char *key; /* interned string */
    uint32_t value;
} pdb_pmap_entry;

typedef struct pdb_pmap {
    pdb_pmap_entry *entries;
    size_t capacity;
    size_t count;
} pdb_pmap;

typedef struct pdb_stream_rec {
    uint32_t size; /* a nil stream holds 0, as in XPDB */
    uint32_t block_count;
    uint32_t dir_offset; /* offset of the block numbers inside the directory */
    bool is_nil;
} pdb_stream_rec;

typedef struct pdb_named {
    const char *name;
    uint32_t index;
} pdb_named;

struct xx_ms_pdb {
    xx_io_device *device;
    xx_pd_struct *pd;
    int64_t file_size;
    xx_ms_pdb_type type;
    bool is_legacy; /* JG: the 16t / ST records are normalised, see pdb_legacy_record */
    bool strict;    /* xx_ms_pdb_set_strict: decode correctly instead of the XPDB way */
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

    /* cached stream headers */
    bool pdb_info_loaded;
    xx_ms_pdb_info pdb_info;
    uint32_t *features;
    pdb_named *named;
    size_t named_count;
    bool named_sorted;
    pdb_pmap named_map; /* name -> slot of `named` */

    bool tpi_loaded;
    xx_ms_pdb_tpi_info tpi_info;
    bool ipi_loaded;
    xx_ms_pdb_tpi_info ipi_info;

    bool dbi_loaded;
    xx_ms_pdb_dbi_info dbi_info;
    uint16_t *dbg_streams;

    bool modules_loaded;
    xx_ms_pdb_module *modules;
    size_t module_count;

    bool sections_loaded;
    xx_ms_pdb_section_header *sections;
    size_t section_count;

    bool portable_loaded;
    xx_ms_pdb_portable_info portable_info;
    xx_ms_pdb_md_stream *md_streams;

    /* type record cache, filled by xx_ms_pdb_load_types */
    bool types_loaded;
    uint8_t *type_records;
    int32_t type_records_size;
    int32_t *type_offsets;
    int32_t *type_sizes; /* legacy (JG) only: sizes of the normalised records */
    uint32_t type_offset_count;
    uint32_t type_index_begin;
    uint32_t type_index_end;
    pdb_pmap unique_names;
    pdb_pmap names;
    pdb_pmap hash_adjusters;
    uint8_t *names_stream;
    int32_t names_stream_size;
    bool names_stream_loaded;

    /* IPI record offsets, filled by the first xx_ms_pdb_get_id_by_index */
    bool ids_loaded;
    uint32_t *id_offsets; /* stream offsets of the records (header included) */
    uint32_t id_offset_count;
    uint32_t id_index_begin;
    uint8_t *id_record; /* one record, 0x10002 bytes at most */
};

static const char g_pdb_empty[1] = {0};

static bool pdb_not_canceled(const xx_ms_pdb *pdb)
{
    return !(pdb->pd && xx_pd_is_stopped(pdb->pd));
}

static size_t pdb_strlen(const char *string)
{
    size_t length = 0;

    while (string[length]) {
        length++;
    }

    return length;
}

/* XBinary::_read_uintN_safe: an out of range read gives 0 */
static uint8_t pdb_u8(const uint8_t *data, int32_t data_size, int32_t offset)
{
    if ((offset >= 0) && (offset < data_size)) {
        return data[offset];
    }

    return 0;
}

static uint16_t pdb_u16(const uint8_t *data, int32_t data_size, int32_t offset)
{
    if ((offset >= 0) && ((int64_t)offset + 1 < data_size)) {
        return xx_data_get_le16(data + offset);
    }

    return 0;
}

static uint32_t pdb_u32(const uint8_t *data, int32_t data_size, int32_t offset)
{
    if ((offset >= 0) && ((int64_t)offset + 3 < data_size)) {
        return xx_data_get_le32(data + offset);
    }

    return 0;
}

static uint64_t pdb_u64(const uint8_t *data, int32_t data_size, int32_t offset)
{
    if ((offset >= 0) && ((int64_t)offset + 7 < data_size)) {
        return xx_data_get_le64(data + offset);
    }

    return 0;
}

/* qint32 += quint32, the way the XPDB offset walks wrap */
static int32_t pdb_add32(int32_t offset, uint32_t value)
{
    return (int32_t)((uint32_t)offset + value);
}

/* ---- arena / interning ---------------------------------------------------- */

static void *pdb_arena_alloc(xx_ms_pdb *pdb, size_t size)
{
    pdb_chunk *chunk = pdb->chunks;
    void *result = NULL;

    if (!chunk || ((chunk->capacity - chunk->used) < size)) {
        size_t capacity = 0x10000;

        if (size > capacity) {
            capacity = size;
        }

        chunk = (pdb_chunk *)xx_mem_alloc(sizeof(pdb_chunk) + capacity);

        if (!chunk) {
            return NULL;
        }

        chunk->used = 0;
        chunk->capacity = capacity;

        /* A big string gets a chunk of its own; keep filling the current one */
        if (pdb->chunks && (size >= 0x10000)) {
            chunk->next = pdb->chunks->next;
            pdb->chunks->next = chunk;
        } else {
            chunk->next = pdb->chunks;
            pdb->chunks = chunk;
        }
    }

    result = (uint8_t *)(chunk + 1) + chunk->used;
    chunk->used += size;

    return result;
}

static uint32_t pdb_hash(const char *string, size_t length)
{
    uint32_t hash = 2166136261u;
    size_t i = 0;

    for (i = 0; i < length; i++) {
        hash ^= (uint8_t)string[i];
        hash *= 16777619u;
    }

    return hash;
}

static bool pdb_intern_grow(xx_ms_pdb *pdb)
{
    size_t capacity = pdb->intern_capacity ? (pdb->intern_capacity * 2) : 1024;
    pdb_istr *table = (pdb_istr *)xx_mem_calloc(capacity, sizeof(pdb_istr));
    size_t i = 0;

    if (!table) {
        return false;
    }

    for (i = 0; i < pdb->intern_capacity; i++) {
        if (pdb->intern[i].string) {
            size_t slot = pdb->intern[i].hash & (capacity - 1);

            while (table[slot].string) {
                slot = (slot + 1) & (capacity - 1);
            }

            table[slot] = pdb->intern[i];
        }
    }

    if (pdb->intern) {
        xx_mem_free(pdb->intern);
    }

    pdb->intern = table;
    pdb->intern_capacity = capacity;

    return true;
}

/* Returns the object owned copy of a string; with insert == false only an
 * already known string is found (NULL otherwise). */
static const char *pdb_intern(xx_ms_pdb *pdb, const char *string, size_t length, bool insert)
{
    uint32_t hash = 0;
    size_t slot = 0;
    char *copy = NULL;

    if (length == 0) {
        return g_pdb_empty;
    }

    hash = pdb_hash(string, length);

    if (pdb->intern_capacity) {
        slot = hash & (pdb->intern_capacity - 1);

        while (pdb->intern[slot].string) {
            if ((pdb->intern[slot].hash == hash) && (pdb->intern[slot].length == length) && (xx_mem_compare(pdb->intern[slot].string, string, length) == 0)) {
                return pdb->intern[slot].string;
            }

            slot = (slot + 1) & (pdb->intern_capacity - 1);
        }
    }

    if (!insert || (length > 0x7FFFFFFF)) {
        return NULL;
    }

    if ((pdb->intern_count + 1) * 4 >= pdb->intern_capacity * 3) {
        if (!pdb_intern_grow(pdb)) {
            return NULL;
        }

        slot = hash & (pdb->intern_capacity - 1);

        while (pdb->intern[slot].string) {
            slot = (slot + 1) & (pdb->intern_capacity - 1);
        }
    }

    copy = (char *)pdb_arena_alloc(pdb, length + 1);

    if (!copy) {
        return NULL;
    }

    xx_mem_copy(copy, string, length);
    copy[length] = 0;

    pdb->intern[slot].string = copy;
    pdb->intern[slot].hash = hash;
    pdb->intern[slot].length = (uint32_t)length;
    pdb->intern_count++;

    return copy;
}

static const char *pdb_intern_z(xx_ms_pdb *pdb, const char *string)
{
    const char *result = pdb_intern(pdb, string, pdb_strlen(string), true);

    return result ? result : g_pdb_empty;
}

static size_t pdb_pmap_slot(const pdb_pmap *map, const char *key)
{
    uint64_t hash = (uint64_t)(uintptr_t)key * UINT64_C(0x9E3779B97F4A7C15);
    size_t slot = (size_t)(hash >> 20) & (map->capacity - 1);

    while (map->entries[slot].key && (map->entries[slot].key != key)) {
        slot = (slot + 1) & (map->capacity - 1);
    }

    return slot;
}

static bool pdb_pmap_get(const pdb_pmap *map, const char *key, uint32_t *value)
{
    size_t slot = 0;

    if (!map->capacity || !key) {
        return false;
    }

    slot = pdb_pmap_slot(map, key);

    if (!map->entries[slot].key) {
        return false;
    }

    *value = map->entries[slot].value;

    return true;
}

/* QMap::insert: a later value replaces an earlier one */
static bool pdb_pmap_set(pdb_pmap *map, const char *key, uint32_t value)
{
    size_t slot = 0;

    if ((map->count + 1) * 4 >= map->capacity * 3) {
        pdb_pmap grown = {0};
        size_t i = 0;

        grown.capacity = map->capacity ? (map->capacity * 2) : 256;
        grown.entries = (pdb_pmap_entry *)xx_mem_calloc(grown.capacity, sizeof(pdb_pmap_entry));

        if (!grown.entries) {
            return false;
        }

        for (i = 0; i < map->capacity; i++) {
            if (map->entries[i].key) {
                grown.entries[pdb_pmap_slot(&grown, map->entries[i].key)] = map->entries[i];
            }
        }

        grown.count = map->count;

        if (map->entries) {
            xx_mem_free(map->entries);
        }

        *map = grown;
    }

    slot = pdb_pmap_slot(map, key);

    if (!map->entries[slot].key) {
        map->entries[slot].key = key;
        map->count++;
    }

    map->entries[slot].value = value;

    return true;
}

static void pdb_pmap_free(pdb_pmap *map)
{
    if (map->entries) {
        xx_mem_free(map->entries);
    }

    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

static bool pdb_scratch(xx_ms_pdb *pdb, size_t size)
{
    if (pdb->scratch_size < size) {
        uint8_t *buffer = (uint8_t *)xx_mem_realloc(pdb->scratch, size);

        if (!buffer) {
            return false;
        }

        pdb->scratch = buffer;
        pdb->scratch_size = size;
    }

    return true;
}

/* Grows a result array so that one more element fits */
static bool pdb_list_reserve(void **items, size_t *capacity, size_t count, size_t element_size)
{
    if (count >= *capacity) {
        size_t new_capacity = *capacity ? (*capacity * 2) : 16;
        void *new_items = NULL;

        if (new_capacity > ((size_t)-1) / element_size) {
            return false;
        }

        new_items = xx_mem_realloc(*items, new_capacity * element_size);

        if (!new_items) {
            return false;
        }

        *items = new_items;
        *capacity = new_capacity;
    }

    return true;
}

/* ---- strings ---------------------------------------------------------------- */

static size_t pdb_put_utf8(uint8_t *out, uint32_t code_point)
{
    if (code_point < 0x80) {
        out[0] = (uint8_t)code_point;
        return 1;
    } else if (code_point < 0x800) {
        out[0] = (uint8_t)(0xC0 | (code_point >> 6));
        out[1] = (uint8_t)(0x80 | (code_point & 0x3F));
        return 2;
    } else if (code_point < 0x10000) {
        out[0] = (uint8_t)(0xE0 | (code_point >> 12));
        out[1] = (uint8_t)(0x80 | ((code_point >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (code_point & 0x3F));
        return 3;
    }

    out[0] = (uint8_t)(0xF0 | (code_point >> 18));
    out[1] = (uint8_t)(0x80 | ((code_point >> 12) & 0x3F));
    out[2] = (uint8_t)(0x80 | ((code_point >> 6) & 0x3F));
    out[3] = (uint8_t)(0x80 | (code_point & 0x3F));
    return 4;
}

/* XBinary::_read_ansiString_safe. XPDB builds the QString with
 * QString::append(const char *), which is QString::fromUtf8: the bytes are
 * decoded as UTF-8, a leading BOM is dropped and every byte that does not
 * start a valid sequence becomes U+FFFD. The result is that QString encoded
 * back to UTF-8. *utf16_length receives QString::size(), which is what XPDB
 * advances its offsets by (not the byte length).
 * strict: *utf16_length receives the number of bytes the name occupies, so
 * what follows a name is read from the right place, and a leading BOM is kept
 * (it is U+FEFF, part of the name). The text is still UTF-8 with U+FFFD for
 * every byte that is not: the code page of an old ANSI name is not recorded. */
static const char *pdb_string(xx_ms_pdb *pdb, const uint8_t *data, int32_t data_size, int32_t offset, int32_t max_size, int32_t *utf16_length)
{
    const uint8_t *source = NULL;
    const char *result = NULL;
    int32_t length = 0;
    bool is_ascii = true;

    if (utf16_length) {
        *utf16_length = 0;
    }

    if ((offset < 0) || (offset >= data_size)) {
        return g_pdb_empty;
    }

    if ((int64_t)offset + max_size > data_size) {
        max_size = data_size - offset;
    }

    source = data + offset;

    while ((length < max_size) && source[length]) {
        if (source[length] & 0x80) {
            is_ascii = false;
        }

        length++;
    }

    if (length == 0) {
        return g_pdb_empty;
    }

    if (is_ascii) {
        result = pdb_intern(pdb, (const char *)source, (size_t)length, true);

        if (result && utf16_length) {
            *utf16_length = length;
        }
    } else {
        int32_t position = 0;
        int32_t units = 0;
        size_t out_size = 0;
        uint8_t *out = NULL;

        if (!pdb_scratch(pdb, (size_t)length * 3 + 4)) {
            return g_pdb_empty;
        }

        out = pdb->scratch;

        if (!pdb->strict && (length >= 3) && (source[0] == 0xEF) && (source[1] == 0xBB) && (source[2] == 0xBF)) {
            position = 3;
        }

        while (position < length) {
            uint8_t byte = source[position++];
            uint32_t code_point = 0;
            uint32_t min_code_point = 0;
            int32_t needed = 0;
            int32_t available = length - position;
            bool is_error = false;

            if (byte < 0x80) {
                out[out_size++] = byte;
                units++;
                continue;
            }

            if (byte <= 0xC1) {
                is_error = true;
            } else if (byte < 0xE0) {
                needed = 2;
                min_code_point = 0x80;
                code_point = byte & 0x1F;
            } else if (byte < 0xF0) {
                needed = 3;
                min_code_point = 0x800;
                code_point = byte & 0x0F;
            } else if (byte < 0xF5) {
                needed = 4;
                min_code_point = 0x10000;
                code_point = byte & 0x07;
            } else {
                is_error = true;
            }

            if (!is_error) {
                if (available < (needed - 1)) {
                    is_error = true;
                } else {
                    int32_t i = 0;

                    for (i = 0; i < (needed - 1); i++) {
                        uint8_t next = source[position + i];

                        if ((next & 0xC0) != 0x80) {
                            is_error = true;
                            break;
                        }

                        code_point = (code_point << 6) | (next & 0x3F);
                    }
                }
            }

            if (!is_error) {
                if ((code_point < min_code_point) || ((code_point >= 0xD800) && (code_point <= 0xDFFF)) || (code_point > 0x10FFFF)) {
                    is_error = true;
                }
            }

            if (is_error) {
                /* Only the lead byte is consumed */
                out[out_size++] = 0xEF;
                out[out_size++] = 0xBF;
                out[out_size++] = 0xBD;
                units++;
            } else {
                out_size += pdb_put_utf8(out + out_size, code_point);
                units += (code_point >= 0x10000) ? 2 : 1;
                position += needed - 1;
            }
        }

        result = pdb_intern(pdb, (const char *)out, out_size, true);

        if (result && utf16_length) {
            *utf16_length = pdb->strict ? length : units;
        }
    }

    return result ? result : g_pdb_empty;
}

/* XBinary::read_ansiString: Latin-1, up to the first zero byte */
static const char *pdb_latin1(xx_ms_pdb *pdb, const uint8_t *data, int32_t data_size, int32_t *length)
{
    const char *result = NULL;
    int32_t count = 0;
    size_t out_size = 0;
    int32_t i = 0;

    while ((count < data_size) && data[count]) {
        count++;
    }

    if (length) {
        *length = count;
    }

    if ((count == 0) || !pdb_scratch(pdb, (size_t)count * 2 + 1)) {
        return g_pdb_empty;
    }

    for (i = 0; i < count; i++) {
        out_size += pdb_put_utf8(pdb->scratch + out_size, data[i]);
    }

    result = pdb_intern(pdb, (const char *)pdb->scratch, out_size, true);

    return result ? result : g_pdb_empty;
}

static void pdb_hex(char *out, uint64_t value, int32_t width, bool is_upper)
{
    const char *digits = is_upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buffer[17];
    int32_t count = 0;
    int32_t i = 0;

    do {
        buffer[count++] = digits[value & 0xF];
        value >>= 4;
    } while (value);

    while (count < width) {
        buffer[count++] = '0';
    }

    for (i = 0; i < count; i++) {
        out[i] = buffer[count - 1 - i];
    }

    out[count] = 0;
}

static void pdb_decimal(char *out, uint64_t value)
{
    char buffer[21];
    int32_t count = 0;
    int32_t i = 0;

    do {
        buffer[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value);

    for (i = 0; i < count; i++) {
        out[i] = buffer[count - 1 - i];
    }

    out[count] = 0;
}

/* ---- tables ----------------------------------------------------------------- */

typedef struct pdb_id_string {
    uint32_t id;
    const char *string;
} pdb_id_string;

static const pdb_id_string g_pdb_symkind[] = {
    {XX_MS_PDB_S_COMPILE, "S_COMPILE"},
    {XX_MS_PDB_S_END, "S_END"},
    {XX_MS_PDB_S_OBJNAME, "S_OBJNAME"},
    {XX_MS_PDB_S_THUNK32, "S_THUNK32"},
    {XX_MS_PDB_S_BLOCK32, "S_BLOCK32"},
    {XX_MS_PDB_S_LABEL32, "S_LABEL32"},
    {XX_MS_PDB_S_REGISTER, "S_REGISTER"},
    {XX_MS_PDB_S_CONSTANT, "S_CONSTANT"},
    {XX_MS_PDB_S_UDT, "S_UDT"},
    {XX_MS_PDB_S_LDATA32, "S_LDATA32"},
    {XX_MS_PDB_S_GDATA32, "S_GDATA32"},
    {XX_MS_PDB_S_PUB32, "S_PUB32"},
    {XX_MS_PDB_S_LPROC32, "S_LPROC32"},
    {XX_MS_PDB_S_GPROC32, "S_GPROC32"},
    {XX_MS_PDB_S_REGREL32, "S_REGREL32"},
    {XX_MS_PDB_S_LTHREAD32, "S_LTHREAD32"},
    {XX_MS_PDB_S_GTHREAD32, "S_GTHREAD32"},
    {XX_MS_PDB_S_PROCREF, "S_PROCREF"},
    {XX_MS_PDB_S_DATAREF, "S_DATAREF"},
    {XX_MS_PDB_S_LPROCREF, "S_LPROCREF"},
    {XX_MS_PDB_S_TRAMPOLINE, "S_TRAMPOLINE"},
    {XX_MS_PDB_S_SEPCODE, "S_SEPCODE"},
    {XX_MS_PDB_S_LOCAL, "S_LOCAL"},
    {XX_MS_PDB_S_DEFRANGE, "S_DEFRANGE"},
    {XX_MS_PDB_S_SECTION, "S_SECTION"},
    {XX_MS_PDB_S_COFFGROUP, "S_COFFGROUP"},
    {XX_MS_PDB_S_EXPORT, "S_EXPORT"},
    {XX_MS_PDB_S_CALLSITEINFO, "S_CALLSITEINFO"},
    {XX_MS_PDB_S_FRAMECOOKIE, "S_FRAMECOOKIE"},
    {XX_MS_PDB_S_COMPILE3, "S_COMPILE3"},
    {XX_MS_PDB_S_ENVBLOCK, "S_ENVBLOCK"},
    {XX_MS_PDB_S_FRAMEPROC, "S_FRAMEPROC"},
    {XX_MS_PDB_S_CALLEES, "S_CALLEES"},
    {XX_MS_PDB_S_CALLERS, "S_CALLERS"},
    {XX_MS_PDB_S_HEAPALLOCSITE, "S_HEAPALLOCSITE"},
    {XX_MS_PDB_S_FILESTATIC, "S_FILESTATIC"},
    {XX_MS_PDB_S_LPROC32_ID, "S_LPROC32_ID"},
    {XX_MS_PDB_S_GPROC32_ID, "S_GPROC32_ID"},
    {XX_MS_PDB_S_BUILDINFO, "S_BUILDINFO"},
    {XX_MS_PDB_S_INLINESITE, "S_INLINESITE"},
    {XX_MS_PDB_S_INLINESITE_END, "S_INLINESITE_END"},
    {XX_MS_PDB_S_PROC_ID_END, "S_PROC_ID_END"},
    {XX_MS_PDB_S_UNAMESPACE, "S_UNAMESPACE"},
    {XX_MS_PDB_S_ANNOTATION, "S_ANNOTATION"},
};

static const pdb_id_string g_pdb_leafkind[] = {
    {XX_MS_PDB_LF_VTSHAPE, "LF_VTSHAPE"},
    {XX_MS_PDB_LF_MODIFIER, "LF_MODIFIER"},
    {XX_MS_PDB_LF_POINTER, "LF_POINTER"},
    {XX_MS_PDB_LF_PROCEDURE, "LF_PROCEDURE"},
    {XX_MS_PDB_LF_MFUNCTION, "LF_MFUNCTION"},
    {XX_MS_PDB_LF_ARGLIST, "LF_ARGLIST"},
    {XX_MS_PDB_LF_FIELDLIST, "LF_FIELDLIST"},
    {XX_MS_PDB_LF_BITFIELD, "LF_BITFIELD"},
    {XX_MS_PDB_LF_METHODLIST, "LF_METHODLIST"},
    {XX_MS_PDB_LF_BCLASS, "LF_BCLASS"},
    {XX_MS_PDB_LF_VBCLASS, "LF_VBCLASS"},
    {XX_MS_PDB_LF_IVBCLASS, "LF_IVBCLASS"},
    {XX_MS_PDB_LF_INDEX, "LF_INDEX"},
    {XX_MS_PDB_LF_VFUNCTAB, "LF_VFUNCTAB"},
    {XX_MS_PDB_LF_ENUMERATE, "LF_ENUMERATE"},
    {XX_MS_PDB_LF_ARRAY, "LF_ARRAY"},
    {XX_MS_PDB_LF_CLASS, "LF_CLASS"},
    {XX_MS_PDB_LF_STRUCTURE, "LF_STRUCTURE"},
    {XX_MS_PDB_LF_UNION, "LF_UNION"},
    {XX_MS_PDB_LF_ENUM, "LF_ENUM"},
    {XX_MS_PDB_LF_MEMBER, "LF_MEMBER"},
    {XX_MS_PDB_LF_STMEMBER, "LF_STMEMBER"},
    {XX_MS_PDB_LF_METHOD, "LF_METHOD"},
    {XX_MS_PDB_LF_NESTTYPE, "LF_NESTTYPE"},
    {XX_MS_PDB_LF_ONEMETHOD, "LF_ONEMETHOD"},
    {XX_MS_PDB_LF_VFTABLE, "LF_VFTABLE"},
    {XX_MS_PDB_LF_TYPESERVER2, "LF_TYPESERVER2"},
    {XX_MS_PDB_LF_INTERFACE, "LF_INTERFACE"},
    {XX_MS_PDB_LF_FUNC_ID, "LF_FUNC_ID"},
    {XX_MS_PDB_LF_MFUNC_ID, "LF_MFUNC_ID"},
    {XX_MS_PDB_LF_BUILDINFO, "LF_BUILDINFO"},
    {XX_MS_PDB_LF_SUBSTR_LIST, "LF_SUBSTR_LIST"},
    {XX_MS_PDB_LF_STRING_ID, "LF_STRING_ID"},
    {XX_MS_PDB_LF_UDT_SRC_LINE, "LF_UDT_SRC_LINE"},
    {XX_MS_PDB_LF_UDT_MOD_SRC_LINE, "LF_UDT_MOD_SRC_LINE"},
    {XX_MS_PDB_LF_CLASS2, "LF_CLASS2"},
    {XX_MS_PDB_LF_STRUCTURE2, "LF_STRUCTURE2"},
    {XX_MS_PDB_LF_UNION2, "LF_UNION2"},
    {XX_MS_PDB_LF_INTERFACE2, "LF_INTERFACE2"},
};

static const pdb_id_string g_pdb_machine[] = {
    {0x0000, "UNKNOWN"}, {0x014C, "I386"},      {0x0162, "R3000"},   {0x0166, "R4000"},    {0x0169, "R10000"},  {0x0184, "ALPHA"},   {0x01A2, "SH3"},
    {0x01A3, "SH3DSP"},  {0x01A6, "SH4"},       {0x01A8, "SH5"},     {0x01C0, "ARM"},      {0x01C2, "THUMB"},   {0x01C4, "ARMNT"},   {0x01D3, "AM33"},
    {0x01F0, "POWERPC"}, {0x01F1, "POWERPCFP"}, {0x0200, "IA64"},    {0x0266, "MIPS16"},   {0x0284, "ALPHA64"}, {0x0366, "MIPSFPU"}, {0x0466, "MIPSFPU16"},
    {0x0EBC, "EBC"},     {0x5032, "RISCV32"},   {0x5064, "RISCV64"}, {0x5128, "RISCV128"}, {0x8664, "AMD64"},   {0x9041, "M32R"},    {0xAA64, "ARM64"},
};

static const pdb_id_string g_pdb_pdbversion[] = {
    {XX_MS_PDB_PDB_IMPL_VC2, "VC2"},     {XX_MS_PDB_PDB_IMPL_VC4, "VC4"},         {XX_MS_PDB_PDB_IMPL_VC41, "VC41"}, {XX_MS_PDB_PDB_IMPL_VC50, "VC50"},
    {XX_MS_PDB_PDB_IMPL_VC98, "VC98"},   {XX_MS_PDB_PDB_IMPL_VC70DEP, "VC70Dep"}, {XX_MS_PDB_PDB_IMPL_VC70, "VC70"}, {XX_MS_PDB_PDB_IMPL_VC80, "VC80"},
    {XX_MS_PDB_PDB_IMPL_VC110, "VC110"}, {XX_MS_PDB_PDB_IMPL_VC140, "VC140"},
};

static const pdb_id_string g_pdb_tpiversion[] = {
    {XX_MS_PDB_TPI_V40, "V40"}, {XX_MS_PDB_TPI_V41, "V41"}, {XX_MS_PDB_TPI_V50, "V50"}, {XX_MS_PDB_TPI_V70, "V70"}, {XX_MS_PDB_TPI_V80, "V80"},
};

static const pdb_id_string g_pdb_dbiversion[] = {
    {XX_MS_PDB_DBI_V41, "V41"}, {XX_MS_PDB_DBI_V50, "V50"}, {XX_MS_PDB_DBI_V60, "V60"}, {XX_MS_PDB_DBI_V70, "V70"}, {XX_MS_PDB_DBI_V110, "V110"},
};

static const char *const g_pdb_wellknown_stream[] = {"Old MSF Directory", "PDB", "TPI", "DBI", "IPI"};

#define PDB_COUNT_OF(table) ((int32_t)(sizeof(table) / sizeof((table)[0])))

static const char *pdb_lookup(uint32_t id, const pdb_id_string *table, int32_t count)
{
    int32_t i = 0;

    for (i = 0; i < count; i++) {
        if (table[i].id == id) {
            return table[i].string;
        }
    }

    return NULL;
}

/* "0x%04x" for the kinds that have no name. The strings have to outlive the
 * call and there is no object to own them, so the 16 bit range lives in a
 * static table that is filled on demand (every writer stores the same bytes). */
static char g_pdb_hex16[0x10000][8];
static char g_pdb_hex_wide[8][12];
static uint32_t g_pdb_hex_wide_next;

static const char *pdb_unknown_kind(uint32_t kind)
{
    char *slot = NULL;

    if (kind <= 0xFFFF) {
        slot = g_pdb_hex16[kind];

        if (!slot[0]) {
            char buffer[8];

            buffer[0] = '0';
            buffer[1] = 'x';
            pdb_hex(buffer + 2, kind, 4, false);
            xx_mem_copy(slot + 1, buffer + 1, 6);
            slot[0] = '0';
        }

        return slot;
    }

    /* Wider values never come out of a record (the kinds are 16 bit); a small ring serves a direct caller */
    slot = g_pdb_hex_wide[(g_pdb_hex_wide_next++) & 7];
    slot[0] = '0';
    slot[1] = 'x';
    pdb_hex(slot + 2, kind, 4, false);

    return slot;
}

const char *xx_ms_pdb_symbol_kind_to_string(uint32_t kind)
{
    const char *result = pdb_lookup(kind, g_pdb_symkind, PDB_COUNT_OF(g_pdb_symkind));

    return result ? result : pdb_unknown_kind(kind);
}

const char *xx_ms_pdb_leaf_kind_to_string(uint32_t kind)
{
    const char *result = pdb_lookup(kind, g_pdb_leafkind, PDB_COUNT_OF(g_pdb_leafkind));

    return result ? result : pdb_unknown_kind(kind);
}

const char *xx_ms_pdb_machine_to_string(uint32_t machine)
{
    const char *result = pdb_lookup(machine, g_pdb_machine, PDB_COUNT_OF(g_pdb_machine));

    return result ? result : pdb_unknown_kind(machine);
}

/* XPDB::pdbVersionToString and friends: the number itself if it has no name */
static const char *pdb_version_string(xx_ms_pdb *pdb, uint32_t version, const pdb_id_string *table, int32_t count)
{
    const char *result = pdb_lookup(version, table, count);
    char buffer[24];

    if (result) {
        return result;
    }

    pdb_decimal(buffer, version);

    return pdb_intern_z(pdb, buffer);
}

/* ---- static primitive type helpers ---------------------------------------------- */

bool xx_ms_pdb_is_udt_leaf(uint16_t kind)
{
    return ((kind == XX_MS_PDB_LF_CLASS) || (kind == XX_MS_PDB_LF_STRUCTURE) || (kind == XX_MS_PDB_LF_INTERFACE) || (kind == XX_MS_PDB_LF_UNION) ||
            (kind == XX_MS_PDB_LF_CLASS2) || (kind == XX_MS_PDB_LF_STRUCTURE2) || (kind == XX_MS_PDB_LF_INTERFACE2) || (kind == XX_MS_PDB_LF_UNION2));
}

bool xx_ms_pdb_is_primitive_type(uint32_t type_index)
{
    return (type_index < XX_MS_PDB_FIRST_TYPE_INDEX);
}

bool xx_ms_pdb_is_primitive_pointer(uint32_t type_index)
{
    return (xx_ms_pdb_is_primitive_type(type_index) && ((type_index & XX_MS_PDB_PRIMITIVE_MODE_MASK) != 0));
}

uint32_t xx_ms_pdb_primitive_type_strip_pointer(uint32_t type_index)
{
    return (type_index & XX_MS_PDB_PRIMITIVE_TYPE_MASK);
}

int32_t xx_ms_pdb_primitive_pointer_size(uint32_t type_index)
{
    int32_t result = 0;
    uint32_t mode = type_index & XX_MS_PDB_PRIMITIVE_MODE_MASK;

    if (mode == XX_MS_PDB_PRIMITIVE_PTR_NEAR) {
        result = 2;
    } else if ((mode == XX_MS_PDB_PRIMITIVE_PTR_FAR) || (mode == XX_MS_PDB_PRIMITIVE_PTR_HUGE)) {
        result = 4;
    } else if (mode == XX_MS_PDB_PRIMITIVE_PTR_NEAR32) {
        result = 4;
    } else if (mode == XX_MS_PDB_PRIMITIVE_PTR_FAR32) {
        result = 6;
    } else if (mode == XX_MS_PDB_PRIMITIVE_PTR_NEAR64) {
        result = 8;
    }

    return result;
}

int32_t xx_ms_pdb_primitive_type_to_basic_type(uint32_t type_index)
{
    /* Maps a CodeView primitive type to the DIA BasicType value that DIA
     * reports for the same type, so both readers can share the naming tables. */
    int32_t result = XX_MS_PDB_btNoType;
    uint32_t type = xx_ms_pdb_primitive_type_strip_pointer(type_index);

    if (type == XX_MS_PDB_T_VOID) {
        result = XX_MS_PDB_btVoid;
    } else if (type == XX_MS_PDB_T_RCHAR) {
        /* Only the "real char" is the char type, T_CHAR is the signed 8 bit
         * integer and DIA reports it as btInt with length 1 */
        result = XX_MS_PDB_btChar;
    } else if (type == XX_MS_PDB_T_WCHAR) {
        result = XX_MS_PDB_btWChar;
    } else if (type == XX_MS_PDB_T_CHAR8) {
        result = XX_MS_PDB_btChar8;
    } else if (type == XX_MS_PDB_T_CHAR16) {
        result = XX_MS_PDB_btChar16;
    } else if (type == XX_MS_PDB_T_CHAR32) {
        result = XX_MS_PDB_btChar32;
    } else if ((type == XX_MS_PDB_T_CHAR) || (type == XX_MS_PDB_T_SHORT) || (type == XX_MS_PDB_T_INT1) || (type == XX_MS_PDB_T_INT2) || (type == XX_MS_PDB_T_INT4) ||
               (type == XX_MS_PDB_T_INT8) || (type == XX_MS_PDB_T_INT16) || (type == XX_MS_PDB_T_QUAD) || (type == XX_MS_PDB_T_OCT)) {
        /* T_QUAD/T_OCT are __int64/__int128, DIA reports them as btInt with the matching length */
        result = XX_MS_PDB_btInt;
    } else if ((type == XX_MS_PDB_T_UCHAR) || (type == XX_MS_PDB_T_USHORT) || (type == XX_MS_PDB_T_UINT1) || (type == XX_MS_PDB_T_UINT2) || (type == XX_MS_PDB_T_UINT4) ||
               (type == XX_MS_PDB_T_UINT8) || (type == XX_MS_PDB_T_UINT16) || (type == XX_MS_PDB_T_UQUAD) || (type == XX_MS_PDB_T_UOCT)) {
        result = XX_MS_PDB_btUInt;
    } else if (type == XX_MS_PDB_T_LONG) {
        result = XX_MS_PDB_btLong;
    } else if (type == XX_MS_PDB_T_ULONG) {
        result = XX_MS_PDB_btULong;
    } else if ((type == XX_MS_PDB_T_BOOL08) || (type == XX_MS_PDB_T_BOOL16) || (type == XX_MS_PDB_T_BOOL32) || (type == XX_MS_PDB_T_BOOL64)) {
        result = XX_MS_PDB_btBool;
    } else if ((type == XX_MS_PDB_T_REAL16) || (type == XX_MS_PDB_T_REAL32) || (type == XX_MS_PDB_T_REAL32PP) || (type == XX_MS_PDB_T_REAL48) ||
               (type == XX_MS_PDB_T_REAL64) || (type == XX_MS_PDB_T_REAL80) || (type == XX_MS_PDB_T_REAL128)) {
        result = XX_MS_PDB_btFloat;
    } else if ((type == XX_MS_PDB_T_CPLX32) || (type == XX_MS_PDB_T_CPLX64) || (type == XX_MS_PDB_T_CPLX80) || (type == XX_MS_PDB_T_CPLX128)) {
        result = XX_MS_PDB_btComplex;
    } else if (type == XX_MS_PDB_T_HRESULT) {
        result = XX_MS_PDB_btHresult;
    } else if (type == XX_MS_PDB_T_CURRENCY) {
        result = XX_MS_PDB_btCurrency;
    } else if (type == XX_MS_PDB_T_BIT) {
        result = XX_MS_PDB_btBit;
    } else if ((type == XX_MS_PDB_T_NBASICSTR) || (type == XX_MS_PDB_T_FBASICSTR)) {
        result = XX_MS_PDB_btBSTR;
    }

    return result;
}

int32_t xx_ms_pdb_primitive_type_size(uint32_t type_index)
{
    int32_t result = 0;
    uint32_t type = 0;

    if (xx_ms_pdb_is_primitive_pointer(type_index)) {
        return xx_ms_pdb_primitive_pointer_size(type_index);
    }

    type = xx_ms_pdb_primitive_type_strip_pointer(type_index);

    if ((type == XX_MS_PDB_T_CHAR) || (type == XX_MS_PDB_T_RCHAR) || (type == XX_MS_PDB_T_UCHAR) || (type == XX_MS_PDB_T_INT1) || (type == XX_MS_PDB_T_UINT1) ||
        (type == XX_MS_PDB_T_BOOL08) || (type == XX_MS_PDB_T_CHAR8) || (type == XX_MS_PDB_T_BIT) || (type == XX_MS_PDB_T_PASCHAR)) {
        result = 1;
    } else if ((type == XX_MS_PDB_T_SHORT) || (type == XX_MS_PDB_T_USHORT) || (type == XX_MS_PDB_T_INT2) || (type == XX_MS_PDB_T_UINT2) || (type == XX_MS_PDB_T_WCHAR) ||
               (type == XX_MS_PDB_T_CHAR16) || (type == XX_MS_PDB_T_BOOL16) || (type == XX_MS_PDB_T_REAL16)) {
        result = 2;
    } else if ((type == XX_MS_PDB_T_LONG) || (type == XX_MS_PDB_T_ULONG) || (type == XX_MS_PDB_T_INT4) || (type == XX_MS_PDB_T_UINT4) || (type == XX_MS_PDB_T_CHAR32) ||
               (type == XX_MS_PDB_T_BOOL32) || (type == XX_MS_PDB_T_REAL32) || (type == XX_MS_PDB_T_REAL32PP) || (type == XX_MS_PDB_T_HRESULT) ||
               (type == XX_MS_PDB_T_BOOL32FF)) {
        result = 4;
    } else if (type == XX_MS_PDB_T_REAL48) {
        result = 6;
    } else if ((type == XX_MS_PDB_T_QUAD) || (type == XX_MS_PDB_T_UQUAD) || (type == XX_MS_PDB_T_INT8) || (type == XX_MS_PDB_T_UINT8) || (type == XX_MS_PDB_T_BOOL64) ||
               (type == XX_MS_PDB_T_REAL64) || (type == XX_MS_PDB_T_CPLX32) || (type == XX_MS_PDB_T_CURRENCY)) {
        result = 8;
    } else if (type == XX_MS_PDB_T_REAL80) {
        result = 10;
    } else if ((type == XX_MS_PDB_T_OCT) || (type == XX_MS_PDB_T_UOCT) || (type == XX_MS_PDB_T_INT16) || (type == XX_MS_PDB_T_UINT16) || (type == XX_MS_PDB_T_REAL128) ||
               (type == XX_MS_PDB_T_CPLX64)) {
        result = 16;
    } else if (type == XX_MS_PDB_T_CPLX80) {
        result = 20;
    } else if (type == XX_MS_PDB_T_CPLX128) {
        result = 32;
    }

    return result;
}

/* [is pointer][type] -> name, filled on demand (every writer stores the same bytes) */
static char g_pdb_primitive_names[2][256][24];

const char *xx_ms_pdb_primitive_type_to_string(uint32_t type_index)
{
    uint32_t type = xx_ms_pdb_primitive_type_strip_pointer(type_index);
    bool is_pointer = xx_ms_pdb_is_primitive_pointer(type_index);
    char *slot = g_pdb_primitive_names[is_pointer ? 1 : 0][type];

    if (!slot[0]) {
        const char *name = NULL;
        char buffer[24];
        size_t length = 0;

        if (type == XX_MS_PDB_T_NOTYPE) name = "<no type>";
        else if (type == XX_MS_PDB_T_VOID) name = "void";
        else if (type == XX_MS_PDB_T_HRESULT) name = "HRESULT";
        else if (type == XX_MS_PDB_T_CHAR) name = "signed char";
        else if (type == XX_MS_PDB_T_RCHAR) name = "char";
        else if (type == XX_MS_PDB_T_UCHAR) name = "unsigned char";
        else if (type == XX_MS_PDB_T_WCHAR) name = "wchar_t";
        else if (type == XX_MS_PDB_T_CHAR8) name = "char8_t";
        else if (type == XX_MS_PDB_T_CHAR16) name = "char16_t";
        else if (type == XX_MS_PDB_T_CHAR32) name = "char32_t";
        else if (type == XX_MS_PDB_T_SHORT) name = "short";
        else if (type == XX_MS_PDB_T_USHORT) name = "unsigned short";
        else if (type == XX_MS_PDB_T_LONG) name = "long";
        else if (type == XX_MS_PDB_T_ULONG) name = "unsigned long";
        else if (type == XX_MS_PDB_T_QUAD) name = "__int64";
        else if (type == XX_MS_PDB_T_UQUAD) name = "unsigned __int64";
        else if (type == XX_MS_PDB_T_INT1) name = "__int8";
        else if (type == XX_MS_PDB_T_UINT1) name = "unsigned __int8";
        else if (type == XX_MS_PDB_T_INT2) name = "__int16";
        else if (type == XX_MS_PDB_T_UINT2) name = "unsigned __int16";
        else if (type == XX_MS_PDB_T_INT4) name = "int";
        else if (type == XX_MS_PDB_T_UINT4) name = "unsigned int";
        else if (type == XX_MS_PDB_T_INT8) name = "__int64";
        else if (type == XX_MS_PDB_T_UINT8) name = "unsigned __int64";
        else if (type == XX_MS_PDB_T_INT16) name = "__int128";
        else if (type == XX_MS_PDB_T_UINT16) name = "unsigned __int128";
        else if ((type == XX_MS_PDB_T_BOOL08) || (type == XX_MS_PDB_T_BOOL16) || (type == XX_MS_PDB_T_BOOL32) || (type == XX_MS_PDB_T_BOOL64)) name = "bool";
        else if (type == XX_MS_PDB_T_REAL32) name = "float";
        else if (type == XX_MS_PDB_T_REAL64) name = "double";
        else if (type == XX_MS_PDB_T_REAL80) name = "long double";

        if (name) {
            length = pdb_strlen(name);
            xx_mem_copy(buffer, name, length);
        } else {
            buffer[0] = '0';
            buffer[1] = 'x';
            pdb_hex(buffer + 2, type, 4, false);
            length = 6;
        }

        if (is_pointer) {
            buffer[length++] = ' ';
            buffer[length++] = '*';
        }

        buffer[length] = 0;

        /* The first byte goes last, it is the "filled" mark */
        xx_mem_copy(slot + 1, buffer + 1, length);
        slot[0] = buffer[0];
    }

    return slot;
}

uint8_t xx_ms_pdb_field_attr_to_access(uint16_t attr)
{
    return (uint8_t)(attr & XX_MS_PDB_FLD_ATTR_ACCESS_MASK);
}

/* ---- device ----------------------------------------------------------------- */

/* XBinary::read_array into a caller buffer: the range is cut at the end of
 * the file, the result is the number of bytes read */
static int64_t pdb_file_read(xx_ms_pdb *pdb, int64_t offset, void *buffer, int64_t size)
{
    if ((offset < 0) || (offset >= pdb->file_size) || (size <= 0)) {
        return 0;
    }

    if (size > (pdb->file_size - offset)) {
        size = pdb->file_size - offset;
    }

    if (!xx_io_read_at(pdb->device, offset, buffer, (size_t)size)) {
        return 0;
    }

    return size;
}

static uint16_t pdb_file_u16(xx_ms_pdb *pdb, int64_t offset)
{
    uint8_t buffer[2] = {0};

    if (pdb_file_read(pdb, offset, buffer, 2) != 2) {
        return 0;
    }

    return xx_data_get_le16(buffer);
}

static uint32_t pdb_file_u32(xx_ms_pdb *pdb, int64_t offset)
{
    uint8_t buffer[4] = {0};

    if (pdb_file_read(pdb, offset, buffer, 4) != 4) {
        return 0;
    }

    return xx_data_get_le32(buffer);
}

/* ---- MSF container ---------------------------------------------------------- */

static xx_ms_pdb_type pdb_detect_type(xx_ms_pdb *pdb)
{
    xx_ms_pdb_type result = XX_MS_PDB_TYPE_UNKNOWN;

    /* A portable PDB is an ECMA-335 metadata file, its root starts with "BSJB"
     * and, for a PDB, carries the "PDB v1.0" version string */
    if (pdb->file_size >= 32) {
        if (pdb_file_u32(pdb, 0) == XX_MS_PDB_PORTABLE_SIGNATURE) {
            uint32_t version_length = pdb_file_u32(pdb, 12);

            if ((version_length <= 256) && ((int64_t)XX_MS_PDB_PORTABLE_VERSION_OFFSET + version_length <= pdb->file_size)) {
                uint8_t version[256];
                int64_t count = pdb_file_read(pdb, XX_MS_PDB_PORTABLE_VERSION_OFFSET, version, version_length);
                int64_t i = 0;

                for (i = 0; (i < count) && version[i]; i++) {
                    if ((i + 2 < count) && (version[i] == 'P') && (version[i + 1] == 'D') && (version[i + 2] == 'B')) {
                        return XX_MS_PDB_TYPE_PORTABLE;
                    }
                }
            }
        }
    }

    if (pdb->file_size >= 56) {
        static const char magic7[] = "Microsoft C/C++ MSF 7.00";
        static const char magic2[] = "Microsoft C/C++ program database 2.00";
        uint8_t header[XX_MS_PDB_MAGIC_SIZE_PDB2];

        if (pdb_file_read(pdb, 0, header, XX_MS_PDB_MAGIC_SIZE_PDB2) >= XX_MS_PDB_MAGIC_SIZE_PDB7) {
            if (xx_mem_compare(header, magic7, 24) == 0) {
                if (xx_mem_compare(header + 24,
                                   "\x0D\x0A\x1A"
                                   "DS",
                                   5) == 0) {
                    result = XX_MS_PDB_TYPE_PDB7;
                }
            } else if (xx_mem_compare(header, magic2, 37) == 0) {
                if (xx_mem_compare(header + 37,
                                   "\x0D\x0A\x1A"
                                   "JG",
                                   5) == 0) {
                    result = XX_MS_PDB_TYPE_PDB2;
                }
            }
        }
    }

    return result;
}

static void pdb_load_msf_header(xx_ms_pdb *pdb)
{
    xx_ms_pdb_msf_header result = {0};

    pdb->type = pdb_detect_type(pdb);

    if (pdb->type == XX_MS_PDB_TYPE_PDB7) {
        result.block_size = pdb_file_u32(pdb, 32);
        result.free_block_map_block = pdb_file_u32(pdb, 36);
        result.num_blocks = pdb_file_u32(pdb, 40);
        result.num_directory_bytes = pdb_file_u32(pdb, 44);
        /* 48: reserved */
        result.block_map_addr = pdb_file_u32(pdb, 52);
    } else if (pdb->type == XX_MS_PDB_TYPE_PDB2) {
        result.block_size = pdb_file_u32(pdb, 44);
        result.free_block_map_block = pdb_file_u16(pdb, 48);
        result.num_blocks = pdb_file_u16(pdb, 50);
        result.num_directory_bytes = pdb_file_u32(pdb, 52);
        result.block_map_addr = 0; /* The root page list is stored inline, at offset 0x3C */
    }

    if ((pdb->type == XX_MS_PDB_TYPE_PDB7) || (pdb->type == XX_MS_PDB_TYPE_PDB2)) {
        bool is_block_size_valid = (result.block_size == 512) || (result.block_size == 1024) || (result.block_size == 2048) || (result.block_size == 4096) ||
                                   (result.block_size == 8192) || (result.block_size == 16384) || (result.block_size == 32768);

        if (is_block_size_valid && (result.num_blocks > 0) && (result.num_directory_bytes > 0)) {
            result.is_valid = true;
        }
    }

    pdb->msf = result;
}

/* XPDB::_readBlocks for the stream directory: stops at the first block that
 * is not inside the file. A directory can never be larger than the file that
 * holds it, so the read is capped there (a hostile block list could repeat
 * one block for ever otherwise). */
static uint8_t *pdb_read_directory(xx_ms_pdb *pdb, const uint8_t *block_list, int64_t block_count, int32_t width, int32_t *result_size)
{
    uint32_t block_size = pdb->msf.block_size;
    int64_t size = pdb->msf.num_directory_bytes;
    int64_t done = 0;
    int64_t i = 0;
    uint8_t *result = NULL;

    *result_size = 0;

    if (size > pdb->file_size) {
        size = pdb->file_size;
    }

    if (size > PDB_MAX_BUFFER) {
        size = PDB_MAX_BUFFER;
    }

    if (size <= 0) {
        return NULL;
    }

    result = (uint8_t *)xx_mem_alloc((size_t)size);

    if (!result) {
        return NULL;
    }

    for (i = 0; (i < block_count) && (done < size) && pdb_not_canceled(pdb); i++) {
        uint32_t block = (width == 4) ? xx_data_get_le32(block_list + i * 4) : xx_data_get_le16(block_list + i * 2);
        int64_t block_offset = (int64_t)block * block_size;
        int64_t part = (int64_t)pdb->msf.num_directory_bytes - done;

        if (part > block_size) {
            part = block_size;
        }

        if ((block_offset + part) > pdb->file_size) {
            break;
        }

        if (part > (size - done)) {
            part = size - done;
        }

        if (pdb_file_read(pdb, block_offset, result + done, part) != part) {
            break;
        }

        done += part;
    }

    if (done == 0) {
        xx_mem_free(result);
        return NULL;
    }

    *result_size = (int32_t)done;

    return result;
}

/* XPDB::_parseStreamDirectory. A directory that is cut short keeps the
 * streams in front of the cut, as XPDB does. */
static void pdb_parse_stream_directory(xx_ms_pdb *pdb)
{
    const uint8_t *data = pdb->directory;
    int32_t data_size = pdb->directory_size;
    uint32_t block_size = pdb->msf.block_size;
    uint32_t number_of_streams = 0;
    int32_t offset = 0;
    uint32_t i = 0;

    if (data_size < 4) {
        return;
    }

    number_of_streams = pdb_u32(data, data_size, 0);

    /* A directory can never describe more streams than it has room for the sizes */
    if ((number_of_streams == 0) || (number_of_streams == 0xFFFFFFFF) || (((int64_t)number_of_streams + 1) * 4 > data_size)) {
        return;
    }

    pdb->streams = (pdb_stream_rec *)xx_mem_calloc(number_of_streams, sizeof(pdb_stream_rec));

    if (!pdb->streams) {
        return;
    }

    offset = (int32_t)(4 + number_of_streams * 4);

    for (i = 0; (i < number_of_streams) && pdb_not_canceled(pdb); i++) {
        pdb_stream_rec *stream = &pdb->streams[i];
        uint32_t stream_size = pdb_u32(data, data_size, (int32_t)(4 + i * 4));
        int64_t number_of_blocks = 0;

        if (stream_size == XX_MS_PDB_INVALID_STREAM_SIZE) {
            stream_size = 0; /* Nil stream */
            stream->is_nil = true;
        }

        /* XPDB rounds up in quint32: a size near 4 GiB wraps to a few blocks
         * (or none) and the block numbers of the next streams are then taken
         * from the wrong place. Such a stream cannot be in the directory; the
         * check below ends the directory at it. */
        number_of_blocks = ((int64_t)stream_size + block_size - 1) / block_size;

        if ((offset + number_of_blocks * 4) > data_size) {
            break;
        }

        stream->size = stream_size;
        stream->block_count = (uint32_t)number_of_blocks;
        stream->dir_offset = (uint32_t)offset;

        offset += (int32_t)(number_of_blocks * 4);
        pdb->stream_count = (int32_t)(i + 1);
    }
}

/* XPDB::_parseStreamDirectory2. Old (JG) directory: the stream count is
 * followed by an array of {size, reserved} pairs and then by the 16 bit page
 * numbers. Nothing is kept unless the whole directory parses. */
static bool pdb_parse_stream_directory2(xx_ms_pdb *pdb, int32_t count_size)
{
    const uint8_t *data = pdb->directory;
    int32_t data_size = pdb->directory_size;
    uint32_t block_size = pdb->msf.block_size;
    uint32_t number_of_streams = 0;
    pdb_stream_rec *streams = NULL;
    int32_t offset = 0;
    uint32_t i = 0;
    bool result = true;

    if (data_size < count_size) {
        return false;
    }

    if (count_size == 4) {
        number_of_streams = pdb_u32(data, data_size, 0);
    } else {
        number_of_streams = pdb_u16(data, data_size, 0);
    }

    if ((number_of_streams == 0) || ((int64_t)count_size + (int64_t)number_of_streams * 8 > data_size)) {
        return false;
    }

    streams = (pdb_stream_rec *)xx_mem_calloc(number_of_streams, sizeof(pdb_stream_rec));

    if (!streams) {
        return false;
    }

    offset = (int32_t)(count_size + number_of_streams * 8);

    for (i = 0; (i < number_of_streams) && pdb_not_canceled(pdb); i++) {
        pdb_stream_rec *stream = &streams[i];
        uint32_t stream_size = pdb_u32(data, data_size, (int32_t)(count_size + i * 8));
        int64_t number_of_blocks = 0;
        int64_t j = 0;

        if (stream_size == XX_MS_PDB_INVALID_STREAM_SIZE) {
            stream_size = 0;
            stream->is_nil = true;
        }

        number_of_blocks = ((int64_t)stream_size + block_size - 1) / block_size; /* 64 bit: no wrap near 4 GiB */

        if ((offset + number_of_blocks * 2) > data_size) {
            result = false;
            break;
        }

        stream->size = stream_size;
        stream->block_count = (uint32_t)number_of_blocks;
        stream->dir_offset = (uint32_t)offset;

        for (j = 0; j < number_of_blocks; j++) {
            uint32_t page = pdb_u16(data, data_size, offset);

            if (page >= pdb->msf.num_blocks) {
                result = false;
            }

            offset += 2;
        }

        if (!result) {
            break;
        }
    }

    if (result) {
        pdb->streams = streams;
        pdb->stream_count = (int32_t)number_of_streams;
    } else {
        xx_mem_free(streams);
    }

    return result;
}

static bool pdb_load_streams(xx_ms_pdb *pdb)
{
    uint32_t block_size = pdb->msf.block_size;

    if (pdb->streams_loaded) {
        return (pdb->stream_count > 0);
    }

    pdb->streams_loaded = true;

    if (pdb->msf.is_valid) {
        int64_t directory_blocks = ((int64_t)pdb->msf.num_directory_bytes + block_size - 1) / block_size;
        uint8_t *block_list = NULL;

        if (pdb->type == XX_MS_PDB_TYPE_PDB7) {
            int64_t block_map_offset = (int64_t)pdb->msf.block_map_addr * block_size;

            if ((block_map_offset > 0) && ((block_map_offset + directory_blocks * 4) <= pdb->file_size)) {
                block_list = (uint8_t *)xx_mem_alloc((size_t)(directory_blocks * 4));

                if (block_list && (pdb_file_read(pdb, block_map_offset, block_list, directory_blocks * 4) == directory_blocks * 4)) {
                    pdb->block_width = 4;
                    pdb->directory = pdb_read_directory(pdb, block_list, directory_blocks, 4, &pdb->directory_size);

                    if (pdb->directory) {
                        pdb_parse_stream_directory(pdb);
                    }
                }
            }
        } else if (pdb->type == XX_MS_PDB_TYPE_PDB2) {
            if ((0x3C + directory_blocks * 2) <= block_size) {
                block_list = (uint8_t *)xx_mem_calloc((size_t)directory_blocks, 2);

                if (block_list) {
                    /* A page number behind the end of the file reads as 0, as XBinary::read_uint16 does */
                    pdb_file_read(pdb, 0x3C, block_list, directory_blocks * 2);

                    pdb->block_width = 2;
                    pdb->directory = pdb_read_directory(pdb, block_list, directory_blocks, 2, &pdb->directory_size);

                    if (pdb->directory) {
                        /* The width of the stream counter is not documented; try both variants */
                        if (!pdb_parse_stream_directory2(pdb, 4)) {
                            pdb_parse_stream_directory2(pdb, 2);
                        }
                    }
                }
            }
        }

        if (block_list) {
            xx_mem_free(block_list);
        }
    }

    return (pdb->stream_count > 0);
}

static uint32_t pdb_stream_block(const xx_ms_pdb *pdb, const pdb_stream_rec *stream, uint32_t index)
{
    const uint8_t *entry = pdb->directory + stream->dir_offset + (size_t)index * (size_t)pdb->block_width;

    return (pdb->block_width == 4) ? xx_data_get_le32(entry) : xx_data_get_le16(entry);
}

/* XPDB::readStreamRegion into a caller buffer. The read stops at the first
 * block that lies outside the file. Returns the number of bytes read. */
static int64_t pdb_stream_read(xx_ms_pdb *pdb, int32_t index, int64_t offset, int64_t size, uint8_t *buffer)
{
    const pdb_stream_rec *stream = NULL;
    uint32_t block_size = pdb->msf.block_size;
    uint32_t block_index = 0;
    int64_t block_offset = 0;
    int64_t done = 0;

    if (!pdb->msf.is_valid || !pdb_load_streams(pdb) || (index < 0) || (index >= pdb->stream_count) || (offset < 0) || (size <= 0)) {
        return 0;
    }

    stream = &pdb->streams[index];

    if (offset >= stream->size) {
        return 0;
    }

    if (size > ((int64_t)stream->size - offset)) {
        size = (int64_t)stream->size - offset;
    }

    block_index = (uint32_t)(offset / block_size);
    block_offset = offset % block_size;

    while ((done < size) && (block_index < stream->block_count) && pdb_not_canceled(pdb)) {
        uint32_t block = pdb_stream_block(pdb, stream, block_index);
        int64_t file_offset = (int64_t)block * block_size + block_offset;
        int64_t run = (int64_t)block_size - block_offset;
        uint32_t next_index = block_index + 1;

        if (run > (size - done)) {
            run = size - done;
        }

        if ((file_offset + run) > pdb->file_size) {
            break;
        }

        /* Blocks that follow each other in the file are read in one go */
        while (((done + run) < size) && (next_index < stream->block_count)) {
            uint32_t next_block = pdb_stream_block(pdb, stream, next_index);
            int64_t part = size - done - run;

            if (part > block_size) {
                part = block_size;
            }

            if (((uint64_t)next_block != (uint64_t)block + (next_index - block_index)) || (((int64_t)next_block * block_size + part) > pdb->file_size)) {
                break;
            }

            run += part;
            next_index++;
        }

        if (!xx_io_read_at(pdb->device, file_offset, buffer + done, (size_t)run)) {
            break;
        }

        done += run;
        block_offset = 0;
        block_index = next_index;
    }

    return done;
}

/* XPDB::readStreamRegion as a new buffer (NULL when nothing was read). The
 * blocks of one stream are distinct blocks of the file, so the result is
 * never larger than the file; the allocation is capped there. */
static uint8_t *pdb_stream_region(xx_ms_pdb *pdb, int32_t index, int64_t offset, int64_t size, int32_t *result_size)
{
    uint8_t *result = NULL;
    int64_t done = 0;

    *result_size = 0;

    if (!pdb->msf.is_valid || !pdb_load_streams(pdb) || (index < 0) || (index >= pdb->stream_count) || (offset < 0) || (size <= 0)) {
        return NULL;
    }

    if (offset >= pdb->streams[index].size) {
        return NULL;
    }

    if (size > ((int64_t)pdb->streams[index].size - offset)) {
        size = (int64_t)pdb->streams[index].size - offset;
    }

    if (size > pdb->file_size) {
        size = pdb->file_size;
    }

    if (size > PDB_MAX_BUFFER) {
        size = PDB_MAX_BUFFER;
    }

    if (size <= 0) {
        return NULL;
    }

    result = (uint8_t *)xx_mem_alloc((size_t)size);

    if (!result) {
        return NULL;
    }

    done = pdb_stream_read(pdb, index, offset, size, result);

    if (done <= 0) {
        xx_mem_free(result);
        return NULL;
    }

    if (done < (size / 2)) {
        uint8_t *smaller = (uint8_t *)xx_mem_realloc(result, (size_t)done);

        if (smaller) {
            result = smaller;
        }
    }

    *result_size = (int32_t)done;

    return result;
}

/* XPDB::readStream(nIndex) */
static uint8_t *pdb_stream_whole(xx_ms_pdb *pdb, int32_t index, int32_t *result_size)
{
    *result_size = 0;

    if (!pdb_load_streams(pdb) || (index < 0) || (index >= pdb->stream_count)) {
        return NULL;
    }

    return pdb_stream_region(pdb, index, 0, pdb->streams[index].size, result_size);
}

/* ---- lifetime ------------------------------------------------------------------ */

static const xx_ms_pdb_portable_info *pdb_portable_info(xx_ms_pdb *pdb);

xx_ms_pdb *xx_ms_pdb_open(xx_io_device *device, xx_pd_struct *pd)
{
    xx_ms_pdb *pdb = NULL;
    bool is_valid = false;

    if (!device) {
        return NULL;
    }

    pdb = (xx_ms_pdb *)xx_mem_calloc(1, sizeof(xx_ms_pdb));

    if (!pdb) {
        return NULL;
    }

    pdb->device = device;
    pdb->pd = pd;
    pdb->file_size = xx_io_total_size(device);

    if (pdb->file_size > 0) {
        pdb_load_msf_header(pdb);

        pdb->is_legacy = (pdb->type == XX_MS_PDB_TYPE_PDB2);

        if (pdb->type == XX_MS_PDB_TYPE_PORTABLE) {
            is_valid = pdb_portable_info(pdb)->is_valid;
        } else {
            is_valid = pdb->msf.is_valid;
        }
    }

    /* The progress object is only borrowed for the open itself, see xx_ms_pdb_set_pd */
    pdb->pd = NULL;

    if (!is_valid) {
        xx_ms_pdb_close(pdb);
        pdb = NULL;
    }

    return pdb;
}

void xx_ms_pdb_set_pd(xx_ms_pdb *pdb, xx_pd_struct *pd)
{
    if (pdb) {
        pdb->pd = pd;
    }
}

void xx_ms_pdb_set_strict(xx_ms_pdb *pdb, bool strict)
{
    if (!pdb || (pdb->strict == strict)) {
        return;
    }

    pdb->strict = strict;

    /* What was decoded in the other mode is read again on demand */
    xx_ms_pdb_unload_types(pdb);

    pdb->modules_loaded = false;

    if (pdb->pdb_info_loaded) {
        if (pdb->features) {
            xx_mem_free(pdb->features);
            pdb->features = NULL;
        }

        pdb->named_count = 0;
        pdb->named_sorted = false;
        pdb_pmap_free(&pdb->named_map);
        xx_mem_zero(&pdb->pdb_info, sizeof(pdb->pdb_info));
        pdb->pdb_info_loaded = false;
    }
}

bool xx_ms_pdb_is_strict(const xx_ms_pdb *pdb)
{
    return pdb ? pdb->strict : false;
}

void xx_ms_pdb_close(xx_ms_pdb *pdb)
{
    pdb_chunk *chunk = NULL;

    if (!pdb) {
        return;
    }

    xx_ms_pdb_unload_types(pdb);

    chunk = pdb->chunks;

    while (chunk) {
        pdb_chunk *next = chunk->next;

        xx_mem_free(chunk);
        chunk = next;
    }

    pdb_pmap_free(&pdb->named_map);

    if (pdb->intern) xx_mem_free(pdb->intern);
    if (pdb->scratch) xx_mem_free(pdb->scratch);
    if (pdb->directory) xx_mem_free(pdb->directory);
    if (pdb->streams) xx_mem_free(pdb->streams);
    if (pdb->features) xx_mem_free(pdb->features);
    if (pdb->named) xx_mem_free(pdb->named);
    if (pdb->dbg_streams) xx_mem_free(pdb->dbg_streams);
    if (pdb->modules) xx_mem_free(pdb->modules);
    if (pdb->sections) xx_mem_free(pdb->sections);
    if (pdb->md_streams) xx_mem_free(pdb->md_streams);
    if (pdb->id_offsets) xx_mem_free(pdb->id_offsets);
    if (pdb->id_record) xx_mem_free(pdb->id_record);

    xx_mem_free(pdb);
}

bool xx_ms_pdb_is_valid_device(xx_io_device *device)
{
    xx_ms_pdb *pdb = xx_ms_pdb_open(device, NULL);

    if (!pdb) {
        return false;
    }

    xx_ms_pdb_close(pdb);

    return true;
}

xx_ms_pdb_type xx_ms_pdb_get_type(const xx_ms_pdb *pdb)
{
    return pdb ? pdb->type : XX_MS_PDB_TYPE_UNKNOWN;
}

/* ---- MSF API --------------------------------------------------------------------- */

xx_ms_pdb_msf_header xx_ms_pdb_get_msf_header(xx_ms_pdb *pdb)
{
    xx_ms_pdb_msf_header result = {0};

    if (pdb) {
        result = pdb->msf;
    }

    return result;
}

int32_t xx_ms_pdb_get_number_of_streams(xx_ms_pdb *pdb)
{
    if (!pdb) {
        return 0;
    }

    pdb_load_streams(pdb);

    return pdb->stream_count;
}

bool xx_ms_pdb_get_streams(xx_ms_pdb *pdb, xx_ms_pdb_stream_list *out)
{
    int32_t i = 0;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    pdb_load_streams(pdb);

    if (pdb->stream_count == 0) {
        return true;
    }

    out->items = (xx_ms_pdb_stream *)xx_mem_calloc((size_t)pdb->stream_count, sizeof(xx_ms_pdb_stream));

    if (!out->items) {
        return false;
    }

    for (i = 0; i < pdb->stream_count; i++) {
        out->items[i].index = i;
        out->items[i].size = pdb->streams[i].is_nil ? -1 : (int64_t)pdb->streams[i].size;
        out->items[i].name = (i < PDB_COUNT_OF(g_pdb_wellknown_stream)) ? g_pdb_wellknown_stream[i] : g_pdb_empty;
    }

    out->count = (size_t)pdb->stream_count;

    return true;
}

int64_t xx_ms_pdb_get_stream_size(xx_ms_pdb *pdb, int32_t index)
{
    if (!pdb || !pdb_load_streams(pdb) || (index < 0) || (index >= pdb->stream_count) || pdb->streams[index].is_nil) {
        return -1;
    }

    return pdb->streams[index].size;
}

int64_t xx_ms_pdb_read_stream(xx_ms_pdb *pdb, int32_t index, int64_t offset, void *buffer, int64_t size)
{
    if (!pdb || !buffer || (size < 0) || (offset < 0) || !pdb->msf.is_valid || !pdb_load_streams(pdb) || (index < 0) || (index >= pdb->stream_count)) {
        return -1;
    }

    return pdb_stream_read(pdb, index, offset, size, (uint8_t *)buffer);
}

/* ---- portable PDB -------------------------------------------------------------------- */

static void pdb_guid_string(char *out, const uint8_t *data)
{
    /* The GUID is stored as {DWORD, WORD, WORD, BYTE[8]}, the first three fields are little endian */
    uint32_t data1 = xx_data_get_le32(data);
    uint16_t data2 = xx_data_get_le16(data + 4);
    uint16_t data3 = xx_data_get_le16(data + 6);
    int32_t i = 0;

    *out++ = '{';
    pdb_hex(out, data1, 8, true);
    out += 8;
    *out++ = '-';
    pdb_hex(out, data2, 4, true);
    out += 4;
    *out++ = '-';
    pdb_hex(out, data3, 4, true);
    out += 4;
    *out++ = '-';

    for (i = 8; i < 16; i++) {
        pdb_hex(out, data[i], 2, true);
        out += 2;

        if (i == 9) {
            *out++ = '-';
        }
    }

    *out++ = '}';
    *out = 0;
}

static bool pdb_name_equals(const char *name, const char *text)
{
    size_t i = 0;

    for (i = 0; name[i] && (name[i] == text[i]); i++) {
    }

    return (name[i] == text[i]);
}

/* XPDB::getPortableInfo without the row counts of the metadata tables */
static const xx_ms_pdb_portable_info *pdb_portable_info(xx_ms_pdb *pdb)
{
    xx_ms_pdb_portable_info *result = &pdb->portable_info;
    xx_ms_pdb_md_stream pdb_stream = {0};
    xx_ms_pdb_md_stream table_stream = {0};
    size_t capacity = 0;
    uint32_t version_length = 0;
    int64_t total_size = pdb->file_size;
    int64_t offset = 0;
    uint8_t buffer[260];
    int64_t count = 0;
    uint32_t i = 0;
    size_t j = 0;

    if (pdb->portable_loaded) {
        return result;
    }

    pdb->portable_loaded = true;

    result->version = g_pdb_empty;
    result->pdb_id = g_pdb_empty;

    if (pdb->type != XX_MS_PDB_TYPE_PORTABLE) {
        return result;
    }

    if (pdb_file_u32(pdb, 0) != XX_MS_PDB_PORTABLE_SIGNATURE) {
        return result;
    }

    result->major_version = pdb_file_u16(pdb, 4);
    result->minor_version = pdb_file_u16(pdb, 6);

    version_length = pdb_file_u32(pdb, 12);

    if ((version_length > 256) || ((int64_t)XX_MS_PDB_PORTABLE_VERSION_OFFSET + version_length + 4 > total_size)) {
        return result;
    }

    count = pdb_file_read(pdb, XX_MS_PDB_PORTABLE_VERSION_OFFSET, buffer, version_length);
    result->version = pdb_latin1(pdb, buffer, (int32_t)count, NULL);

    offset = XX_MS_PDB_PORTABLE_VERSION_OFFSET + version_length;

    result->flags = pdb_file_u16(pdb, offset);
    result->number_of_streams = pdb_file_u16(pdb, offset + 2);
    offset += 4;

    result->is_valid = true;

    /* Stream headers: offset, size and a name padded to a 4 byte boundary */
    for (i = 0; (i < result->number_of_streams) && ((offset + 8) < total_size) && pdb_not_canceled(pdb); i++) {
        xx_ms_pdb_md_stream record = {0};
        int32_t name_length = 0;
        int64_t padded = 0;

        record.header_offset = offset;
        record.offset = pdb_file_u32(pdb, offset);
        record.size = pdb_file_u32(pdb, offset + 4);

        count = pdb_file_read(pdb, offset + 8, buffer, 64);
        record.name = pdb_latin1(pdb, buffer, (int32_t)count, &name_length);

        padded = (int64_t)name_length + 1;

        if (padded & 3) {
            padded = (padded & ~(int64_t)3) + 4;
        }

        offset += 8 + padded;

        if ((record.offset + (int64_t)record.size) <= total_size) {
            if (!pdb_list_reserve((void **)&pdb->md_streams, &capacity, result->stream_count, sizeof(xx_ms_pdb_md_stream))) {
                break;
            }

            pdb->md_streams[result->stream_count++] = record;
        }
    }

    result->streams = pdb->md_streams;

    /* #Pdb: the id of the file */
    for (j = 0; j < result->stream_count; j++) {
        if (pdb_name_equals(pdb->md_streams[j].name, "#Pdb")) {
            pdb_stream = pdb->md_streams[j];
        }
    }

    if (pdb_stream.size >= 32) {
        count = pdb_file_read(pdb, pdb_stream.offset, buffer, 32);

        if (count >= 32) {
            char guid[40];

            pdb_guid_string(guid, buffer);
            result->pdb_id = pdb_intern_z(pdb, guid);
            result->pdb_stamp = xx_data_get_le32(buffer + 16);
            result->entry_point = xx_data_get_le32(buffer + 20);
            result->referenced_type_system_tables = xx_data_get_le64(buffer + 24);
            result->pdb_stream_valid = true;
        }
    }

    /* #~ (or #-): the metadata tables */
    for (j = 0; j < result->stream_count; j++) {
        if (pdb_name_equals(pdb->md_streams[j].name, "#~") || pdb_name_equals(pdb->md_streams[j].name, "#-")) {
            table_stream = pdb->md_streams[j];
        }
    }

    if (table_stream.size >= 24) {
        count = pdb_file_read(pdb, table_stream.offset, buffer, 24);

        if (count >= 24) {
            result->tables_major_version = buffer[4];
            result->tables_minor_version = buffer[5];
            result->heap_sizes = buffer[6];
            result->valid_tables = xx_data_get_le64(buffer + 8);
            result->sorted_tables = xx_data_get_le64(buffer + 16);
            result->tables_valid = true;
        }
    }

    return result;
}

xx_ms_pdb_portable_info xx_ms_pdb_get_portable_info(xx_ms_pdb *pdb)
{
    xx_ms_pdb_portable_info result = {0};

    result.version = g_pdb_empty;
    result.pdb_id = g_pdb_empty;

    if (pdb) {
        result = *pdb_portable_info(pdb);
    }

    return result;
}

/* ---- PDB info stream ----------------------------------------------------------------- */

static void pdb_named_insert(xx_ms_pdb *pdb, size_t *capacity, const char *name, uint32_t stream_index)
{
    uint32_t slot = 0;

    if (pdb_pmap_get(&pdb->named_map, name, &slot)) {
        pdb->named[slot].index = stream_index;
        return;
    }

    if (!pdb_list_reserve((void **)&pdb->named, capacity, pdb->named_count, sizeof(pdb_named))) {
        return;
    }

    if (!pdb_pmap_set(&pdb->named_map, name, (uint32_t)pdb->named_count)) {
        return;
    }

    pdb->named[pdb->named_count].name = name;
    pdb->named[pdb->named_count].index = stream_index;
    pdb->named_count++;
}

/* An entry walk whose offset went negative (a wrapped word count) reads zeros
 * until the offset is back inside the data. XPDB walks every such entry, they
 * all do the same thing; this gives the number that can be skipped at once. */
static uint32_t pdb_zero_entries(int32_t offset, uint32_t remaining)
{
    uint32_t steps = (uint32_t)((-5 - (int64_t)offset) / 8 + 1);

    return (steps < remaining) ? steps : remaining;
}

static const xx_ms_pdb_info *pdb_get_pdb_info(xx_ms_pdb *pdb)
{
    xx_ms_pdb_info *result = &pdb->pdb_info;
    uint8_t *data = NULL;
    int32_t data_size = 0;
    size_t feature_capacity = 0;
    size_t named_capacity = 0;

    if (pdb->pdb_info_loaded) {
        return result;
    }

    result->version_string = g_pdb_empty;
    result->guid = g_pdb_empty;

    data = pdb_stream_whole(pdb, XX_MS_PDB_STREAM_PDB, &data_size);

    if (data_size >= 12) {
        int32_t offset = 12;
        bool is_guid_present = false;

        result->version = pdb_u32(data, data_size, 0);
        result->signature = pdb_u32(data, data_size, 4);
        result->age = pdb_u32(data, data_size, 8);
        result->version_string = pdb_version_string(pdb, result->version, g_pdb_pdbversion, PDB_COUNT_OF(g_pdb_pdbversion));

        is_guid_present = (pdb->type == XX_MS_PDB_TYPE_PDB7) && (result->version >= XX_MS_PDB_PDB_IMPL_VC70);

        if (is_guid_present && (data_size >= 28)) {
            char guid[40];

            pdb_guid_string(guid, data + 12);
            result->guid = pdb_intern_z(pdb, guid);
            offset = 28;
        }

        result->is_valid = true;

        /* Named stream map */
        if ((offset + 4) <= data_size) {
            uint32_t string_buffer_size = pdb_u32(data, data_size, offset);
            int32_t string_buffer_offset = 0;

            offset += 4;
            string_buffer_offset = offset;

            if ((string_buffer_size <= (uint32_t)data_size) && ((offset + (int64_t)string_buffer_size + 12) <= data_size)) {
                uint32_t number_of_entries = 0;
                uint32_t present_words = 0;

                offset = pdb_add32(offset, string_buffer_size);

                number_of_entries = pdb_u32(data, data_size, offset);
                offset += 4;
                offset += 4; /* Capacity */

                present_words = pdb_u32(data, data_size, offset);
                offset += 4;
                offset = pdb_add32(offset, present_words * 4);

                if (((int64_t)offset + 4) <= data_size) {
                    uint32_t deleted_words = pdb_u32(data, data_size, offset);
                    uint32_t i = 0;

                    offset = pdb_add32(offset, 4);
                    offset = pdb_add32(offset, deleted_words * 4);

                    while ((i < number_of_entries) && (((int64_t)offset + 8) <= data_size) && pdb_not_canceled(pdb)) {
                        uint32_t name_offset = pdb_u32(data, data_size, offset);
                        uint32_t stream_index = pdb_u32(data, data_size, pdb_add32(offset, 4));
                        uint32_t steps = (offset <= -5) ? pdb_zero_entries(offset, number_of_entries - i) : 1;

                        if (name_offset < string_buffer_size) {
                            const char *name = pdb_string(pdb, data, data_size, pdb_add32(string_buffer_offset, name_offset), 512, NULL);

                            if (name[0]) {
                                pdb_named_insert(pdb, &named_capacity, name, stream_index);
                            }
                        }

                        i += steps;
                        offset = pdb_add32(offset, steps * 8);
                    }

                    /* strict: the name table ends with the largest name index in
                     * use (niMac); XPDB lists it as the first feature code */
                    if (pdb->strict && (offset >= 0) && (((int64_t)offset + 4) <= data_size)) {
                        offset += 4;
                    }

                    /* Feature codes. An offset that went negative is a wrapped
                     * word count of a damaged map: XPDB then stores one zero
                     * feature for every 4 bytes below zero and goes on to read
                     * the whole stream as features. Neither is a feature list,
                     * so such a file has none. */
                    while ((offset >= 0) && (((int64_t)offset + 4) <= data_size) && pdb_not_canceled(pdb)) {
                        uint32_t feature = pdb_u32(data, data_size, offset);

                        offset += 4;

                        if (!pdb_list_reserve((void **)&pdb->features, &feature_capacity, result->feature_count, sizeof(uint32_t))) {
                            break;
                        }

                        pdb->features[result->feature_count++] = feature;

                        if (feature == XX_MS_PDB_PDB_FEATURE_MINIMALDEBUGINFO) {
                            result->minimal_debug_info = true;
                        } else if (feature == XX_MS_PDB_PDB_FEATURE_NOTYPEMERGE) {
                            result->no_type_merge = true;
                        } else if ((feature == XX_MS_PDB_PDB_FEATURE_VC110) || (feature == XX_MS_PDB_PDB_FEATURE_VC140)) {
                            result->has_id_stream = true;
                        }
                    }
                }
            }
        }
    }

    result->features = pdb->features;

    if (data) {
        xx_mem_free(data);
    }

    pdb->pdb_info_loaded = pdb_not_canceled(pdb);

    return result;
}

xx_ms_pdb_info xx_ms_pdb_get_pdb_info(xx_ms_pdb *pdb)
{
    xx_ms_pdb_info result = {0};

    result.version_string = g_pdb_empty;
    result.guid = g_pdb_empty;

    if (pdb) {
        if (!pdb->pdb_info_loaded) {
            /* A canceled parse is done again; drop what it left */
            xx_mem_zero(&pdb->pdb_info, sizeof(pdb->pdb_info));
            pdb->named_count = 0;
            pdb->named_sorted = false;
            pdb_pmap_free(&pdb->named_map);

            if (pdb->features) {
                xx_mem_free(pdb->features);
                pdb->features = NULL;
            }

            if (pdb->named) {
                xx_mem_free(pdb->named);
                pdb->named = NULL;
            }
        }

        result = *pdb_get_pdb_info(pdb);
    }

    return result;
}

/* QString::operator<: the order of the UTF-16 code units */
static int32_t pdb_compare_utf16(const char *left, const char *right)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    uint32_t code_points[2] = {0, 0};
    int32_t side = 0;

    while (*a && (*a == *b)) {
        a++;
        b++;
    }

    if (*a == *b) {
        return 0;
    }

    if (!*a) return -1;
    if (!*b) return 1;

    /* Back to the start of the sequence the difference is in */
    while ((a > (const uint8_t *)left) && ((*a & 0xC0) == 0x80)) {
        a--;
        b--;
    }

    for (side = 0; side < 2; side++) {
        const uint8_t *p = side ? b : a;
        uint32_t code_point = p[0];
        uint8_t tail[3] = {0, 0, 0};
        int32_t needed = (code_point >= 0xF0) ? 3 : ((code_point >= 0xE0) ? 2 : ((code_point >= 0xC0) ? 1 : 0));
        int32_t i = 0;

        /* The names come out of pdb_string and are well formed; a sequence
         * that is cut short all the same is not read past its end */
        for (i = 0; (i < needed) && ((p[1 + i] & 0xC0) == 0x80); i++) {
            tail[i] = p[1 + i];
        }

        if (code_point >= 0xF0) {
            /* A surrogate pair sorts by its high surrogate first */
            code_point = ((code_point & 0x07) << 18) | ((uint32_t)(tail[0] & 0x3F) << 12) | ((uint32_t)(tail[1] & 0x3F) << 6) | (tail[2] & 0x3F);
            code_point = 0xD800 + ((code_point - 0x10000) >> 10);
            code_point = (code_point << 16) | 1;
        } else if (code_point >= 0xE0) {
            code_point = ((code_point & 0x0F) << 12) | ((uint32_t)(tail[0] & 0x3F) << 6) | (tail[1] & 0x3F);
            code_point <<= 16;
        } else if (code_point >= 0xC0) {
            code_point = ((code_point & 0x1F) << 6) | (tail[0] & 0x3F);
            code_point <<= 16;
        } else {
            code_point <<= 16;
        }

        code_points[side] = code_point;
    }

    if (code_points[0] != code_points[1]) {
        return (code_points[0] < code_points[1]) ? -1 : 1;
    }

    /* Two supplementary characters with the same high surrogate */
    while (*a && (*a == *b)) {
        a++;
        b++;
    }

    return (*a < *b) ? -1 : ((*a > *b) ? 1 : 0);
}

static void pdb_sort_named(pdb_named *items, size_t count)
{
    /* Heap sort: no recursion, no worst case */
    size_t start = count / 2;
    size_t end = count;

    while (end > 1) {
        size_t root = 0;
        pdb_named value;

        if (start > 0) {
            start--;
        } else {
            end--;
            value = items[end];
            items[end] = items[0];
            items[0] = value;
        }

        root = start;
        value = items[root];

        for (;;) {
            size_t child = root * 2 + 1;

            if (child >= end) {
                break;
            }

            if (((child + 1) < end) && (pdb_compare_utf16(items[child].name, items[child + 1].name) < 0)) {
                child++;
            }

            if (pdb_compare_utf16(value.name, items[child].name) >= 0) {
                break;
            }

            items[root] = items[child];
            root = child;
        }

        items[root] = value;
    }
}

bool xx_ms_pdb_get_named_streams(xx_ms_pdb *pdb, xx_ms_pdb_stream_list *out)
{
    size_t i = 0;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    xx_ms_pdb_get_pdb_info(pdb);

    if (pdb->named_count == 0) {
        return true;
    }

    if (!pdb->named_sorted) {
        /* The map only holds slots, it is rebuilt after the sort */
        pdb_sort_named(pdb->named, pdb->named_count);

        for (i = 0; i < pdb->named_count; i++) {
            pdb_pmap_set(&pdb->named_map, pdb->named[i].name, (uint32_t)i);
        }

        pdb->named_sorted = true;
    }

    out->items = (xx_ms_pdb_stream *)xx_mem_calloc(pdb->named_count, sizeof(xx_ms_pdb_stream));

    if (!out->items) {
        return false;
    }

    for (i = 0; i < pdb->named_count; i++) {
        out->items[i].index = (int32_t)pdb->named[i].index;
        out->items[i].size = xx_ms_pdb_get_stream_size(pdb, out->items[i].index);
        out->items[i].name = pdb->named[i].name;
    }

    out->count = pdb->named_count;

    return true;
}

int32_t xx_ms_pdb_get_named_stream_index(xx_ms_pdb *pdb, const char *name)
{
    const char *key = NULL;
    uint32_t slot = 0;

    if (!pdb || !name) {
        return -1;
    }

    xx_ms_pdb_get_pdb_info(pdb);

    key = pdb_intern(pdb, name, pdb_strlen(name), false);

    if (!key || !pdb_pmap_get(&pdb->named_map, key, &slot)) {
        return -1;
    }

    return (int32_t)pdb->named[slot].index;
}

/* ---- TPI / IPI header ------------------------------------------------------------------ */

/* The type stream header of the 16 bit era (HDR_16t): {version, u16 first
 * index, u16 end index, record bytes, u16 hash stream}, 14 bytes that the
 * compiler pads to 16. The header size is not stored, so it is accepted only
 * when the records that follow it add up exactly. */
static void pdb_read_tpi_info_16t(xx_ms_pdb *pdb, int32_t stream_index, const uint8_t *header, int32_t header_size, xx_ms_pdb_tpi_info *result)
{
    static const uint32_t sizes[2] = {16, 14};
    uint32_t version = pdb_u32(header, header_size, 0);
    uint32_t index_begin = pdb_u16(header, header_size, 4);
    uint32_t index_end = pdb_u16(header, header_size, 6);
    uint32_t record_bytes = pdb_u32(header, header_size, 8);
    uint16_t hash_stream_index = pdb_u16(header, header_size, 12);
    int32_t i = 0;

    if ((header_size < 14) || (index_begin < XX_MS_PDB_FIRST_TYPE_INDEX) || (index_end < index_begin) || (record_bytes > (uint32_t)PDB_MAX_BUFFER)) {
        return;
    }

    for (i = 0; (i < 2) && !result->is_valid; i++) {
        int32_t data_size = 0;
        uint8_t *data = (record_bytes > 0) ? pdb_stream_region(pdb, stream_index, sizes[i], record_bytes, &data_size) : NULL;
        uint32_t count = 0;
        int32_t offset = 0;

        if ((uint32_t)data_size == record_bytes) {
            while ((((int64_t)offset + 4) <= data_size) && (count <= (index_end - index_begin))) {
                uint16_t record_length = pdb_u16(data, data_size, offset);

                if ((record_length < 2) || (((int64_t)offset + record_length + 2) > data_size)) {
                    break;
                }

                offset += (int32_t)record_length + 2;
                count++;
            }

            if ((offset == data_size) && (count == (index_end - index_begin))) {
                xx_mem_zero(result, sizeof(*result));

                result->version = version;
                result->version_string = pdb_version_string(pdb, version, g_pdb_tpiversion, PDB_COUNT_OF(g_pdb_tpiversion));
                result->header_size = sizes[i];
                result->type_index_begin = index_begin;
                result->type_index_end = index_end;
                result->type_record_bytes = record_bytes;
                result->number_of_types = count;
                result->hash_stream_index = hash_stream_index;
                result->hash_aux_stream_index = XX_MS_PDB_INVALID_STREAM_INDEX;
                result->is_valid = true;
            }
        }

        if (data) {
            xx_mem_free(data);
        }
    }
}

static xx_ms_pdb_tpi_info pdb_read_tpi_info(xx_ms_pdb *pdb, int32_t stream_index)
{
    xx_ms_pdb_tpi_info result = {0};
    uint8_t data[56];
    int32_t data_size = (int32_t)pdb_stream_read(pdb, stream_index, 0, 56, data);

    result.version_string = g_pdb_empty;

    if (data_size >= 56) {
        result.version = pdb_u32(data, data_size, 0);
        result.header_size = pdb_u32(data, data_size, 4);
        result.type_index_begin = pdb_u32(data, data_size, 8);
        result.type_index_end = pdb_u32(data, data_size, 12);
        result.type_record_bytes = pdb_u32(data, data_size, 16);
        result.hash_stream_index = pdb_u16(data, data_size, 20);
        result.hash_aux_stream_index = pdb_u16(data, data_size, 22);
        result.hash_key_size = pdb_u32(data, data_size, 24);
        result.num_hash_buckets = pdb_u32(data, data_size, 28);
        result.hash_value_buffer_offset = (int32_t)pdb_u32(data, data_size, 32);
        result.hash_value_buffer_length = pdb_u32(data, data_size, 36);
        result.index_offset_buffer_offset = (int32_t)pdb_u32(data, data_size, 40);
        result.index_offset_buffer_length = pdb_u32(data, data_size, 44);
        result.hash_adj_buffer_offset = (int32_t)pdb_u32(data, data_size, 48);
        result.hash_adj_buffer_length = pdb_u32(data, data_size, 52);

        result.version_string = pdb_version_string(pdb, result.version, g_pdb_tpiversion, PDB_COUNT_OF(g_pdb_tpiversion));

        if (result.type_index_end >= result.type_index_begin) {
            result.number_of_types = result.type_index_end - result.type_index_begin;
        }

        result.is_valid = (result.header_size == 56) && (result.type_index_begin >= XX_MS_PDB_FIRST_TYPE_INDEX);
    }

    if (!result.is_valid && pdb->is_legacy && (stream_index == XX_MS_PDB_STREAM_TPI) && (data_size >= 14)) {
        pdb_read_tpi_info_16t(pdb, stream_index, data, data_size, &result);
    }

    return result;
}

static const xx_ms_pdb_tpi_info *pdb_get_tpi_info(xx_ms_pdb *pdb, int32_t stream_index)
{
    bool *is_loaded = (stream_index == XX_MS_PDB_STREAM_TPI) ? &pdb->tpi_loaded : &pdb->ipi_loaded;
    xx_ms_pdb_tpi_info *info = (stream_index == XX_MS_PDB_STREAM_TPI) ? &pdb->tpi_info : &pdb->ipi_info;

    if (!*is_loaded) {
        *info = pdb_read_tpi_info(pdb, stream_index);
        *is_loaded = pdb_not_canceled(pdb);
    }

    return info;
}

xx_ms_pdb_tpi_info xx_ms_pdb_get_tpi_info(xx_ms_pdb *pdb)
{
    xx_ms_pdb_tpi_info result = {0};

    result.version_string = g_pdb_empty;

    return pdb ? *pdb_get_tpi_info(pdb, XX_MS_PDB_STREAM_TPI) : result;
}

xx_ms_pdb_tpi_info xx_ms_pdb_get_ipi_info(xx_ms_pdb *pdb)
{
    xx_ms_pdb_tpi_info result = {0};

    result.version_string = g_pdb_empty;

    return pdb ? *pdb_get_tpi_info(pdb, XX_MS_PDB_STREAM_IPI) : result;
}

/* ---- DBI stream ---------------------------------------------------------------------------- */

static const xx_ms_pdb_dbi_info *pdb_get_dbi_info(xx_ms_pdb *pdb)
{
    xx_ms_pdb_dbi_info *result = &pdb->dbi_info;
    uint8_t data[64];
    int32_t data_size = 0;

    if (pdb->dbi_loaded) {
        return result;
    }

    xx_mem_zero(result, sizeof(*result));

    if (pdb->dbg_streams) {
        xx_mem_free(pdb->dbg_streams);
        pdb->dbg_streams = NULL;
    }

    result->version_string = g_pdb_empty;
    result->machine_string = g_pdb_empty;

    data_size = (int32_t)pdb_stream_read(pdb, XX_MS_PDB_STREAM_DBI, 0, 64, data);

    if (data_size >= 64) {
        result->version_signature = (int32_t)pdb_u32(data, data_size, 0);
        result->version_header = pdb_u32(data, data_size, 4);
        result->age = pdb_u32(data, data_size, 8);
        result->global_stream_index = pdb_u16(data, data_size, 12);
        result->build_number = pdb_u16(data, data_size, 14);
        result->public_stream_index = pdb_u16(data, data_size, 16);
        result->pdb_dll_version = pdb_u16(data, data_size, 18);
        result->sym_record_stream = pdb_u16(data, data_size, 20);
        result->pdb_dll_rbld = pdb_u16(data, data_size, 22);
        result->mod_info_size = (int32_t)pdb_u32(data, data_size, 24);
        result->section_contribution_size = (int32_t)pdb_u32(data, data_size, 28);
        result->section_map_size = (int32_t)pdb_u32(data, data_size, 32);
        result->source_info_size = (int32_t)pdb_u32(data, data_size, 36);
        result->type_server_map_size = (int32_t)pdb_u32(data, data_size, 40);
        result->mfc_type_server_index = pdb_u32(data, data_size, 44);
        result->optional_dbg_header_size = (int32_t)pdb_u32(data, data_size, 48);
        result->ec_substream_size = (int32_t)pdb_u32(data, data_size, 52);
        result->flags = pdb_u16(data, data_size, 56);
        result->machine = pdb_u16(data, data_size, 58);

        result->version_string = pdb_version_string(pdb, result->version_header, g_pdb_dbiversion, PDB_COUNT_OF(g_pdb_dbiversion));
        result->machine_string = xx_ms_pdb_machine_to_string(result->machine);

        result->is_incremental = (result->flags & XX_MS_PDB_DBI_FLAG_INCREMENTAL) != 0;
        result->is_stripped = (result->flags & XX_MS_PDB_DBI_FLAG_STRIPPED) != 0;
        result->is_ctypes = (result->flags & XX_MS_PDB_DBI_FLAG_CTYPES) != 0;

        result->new_version_format = (result->build_number & 0x8000) != 0;
        result->toolchain_major = (uint16_t)((result->build_number >> 8) & 0x7F);
        result->toolchain_minor = (uint16_t)(result->build_number & 0xFF);

        result->is_valid = (result->version_signature == -1) && (result->mod_info_size >= 0) && (result->section_contribution_size >= 0) &&
                           (result->section_map_size >= 0) && (result->source_info_size >= 0) && (result->type_server_map_size >= 0) &&
                           (result->optional_dbg_header_size >= 0) && (result->ec_substream_size >= 0);

        if (result->is_valid && (result->optional_dbg_header_size > 0)) {
            int64_t dbg_offset = 64;
            uint8_t *dbg_data = NULL;
            int32_t dbg_data_size = 0;

            dbg_offset += result->mod_info_size;
            dbg_offset += result->section_contribution_size;
            dbg_offset += result->section_map_size;
            dbg_offset += result->source_info_size;
            dbg_offset += result->type_server_map_size;
            dbg_offset += result->ec_substream_size;

            dbg_data = pdb_stream_region(pdb, XX_MS_PDB_STREAM_DBI, dbg_offset, result->optional_dbg_header_size, &dbg_data_size);

            if (dbg_data && (dbg_data_size >= 2)) {
                size_t count = (size_t)dbg_data_size / 2;

                pdb->dbg_streams = (uint16_t *)xx_mem_alloc(count * sizeof(uint16_t));

                if (pdb->dbg_streams) {
                    size_t i = 0;

                    for (i = 0; i < count; i++) {
                        pdb->dbg_streams[i] = xx_data_get_le16(dbg_data + i * 2);
                    }

                    result->dbg_stream_count = count;
                }
            }

            if (dbg_data) {
                xx_mem_free(dbg_data);
            }
        }
    }

    result->dbg_streams = pdb->dbg_streams;

    pdb->dbi_loaded = pdb_not_canceled(pdb);

    return result;
}

xx_ms_pdb_dbi_info xx_ms_pdb_get_dbi_info(xx_ms_pdb *pdb)
{
    xx_ms_pdb_dbi_info result = {0};

    result.version_string = g_pdb_empty;
    result.machine_string = g_pdb_empty;

    return pdb ? *pdb_get_dbi_info(pdb) : result;
}

static int32_t pdb_align_up4(int32_t value)
{
    return (int32_t)(((uint32_t)value + 3) & ~(uint32_t)3);
}

static void pdb_load_modules(xx_ms_pdb *pdb)
{
    const xx_ms_pdb_dbi_info *dbi_info = NULL;
    size_t capacity = 0;

    if (pdb->modules_loaded) {
        return;
    }

    if (pdb->modules) {
        xx_mem_free(pdb->modules);
        pdb->modules = NULL;
    }

    pdb->module_count = 0;

    dbi_info = pdb_get_dbi_info(pdb);

    if (dbi_info->is_valid && (dbi_info->mod_info_size > 0)) {
        int32_t data_size = 0;
        uint8_t *data = pdb_stream_region(pdb, XX_MS_PDB_STREAM_DBI, 64, dbi_info->mod_info_size, &data_size);
        int32_t offset = 0;
        int32_t index = 0;

        while ((((int64_t)offset + XX_MS_PDB_MODINFO_HEADER_SIZE) <= data_size) && pdb_not_canceled(pdb)) {
            xx_ms_pdb_module record = {0};
            int32_t name_offset = 0;
            int32_t length = 0;

            record.index = index;

            record.section_contrib.section = pdb_u16(data, data_size, offset + 4);
            record.section_contrib.offset = (int32_t)pdb_u32(data, data_size, offset + 8);
            record.section_contrib.size = (int32_t)pdb_u32(data, data_size, offset + 12);
            record.section_contrib.characteristics = pdb_u32(data, data_size, offset + 16);
            record.section_contrib.module_index = pdb_u16(data, data_size, offset + 20);
            record.section_contrib.data_crc = pdb_u32(data, data_size, offset + 24);
            record.section_contrib.reloc_crc = pdb_u32(data, data_size, offset + 28);

            record.flags = pdb_u16(data, data_size, offset + 32);
            record.stream_index = pdb_u16(data, data_size, offset + 34);
            record.sym_byte_size = pdb_u32(data, data_size, offset + 36);
            record.c11_byte_size = pdb_u32(data, data_size, offset + 40);
            record.c13_byte_size = pdb_u32(data, data_size, offset + 44);
            record.source_file_count = pdb_u16(data, data_size, offset + 48);

            name_offset = offset + XX_MS_PDB_MODINFO_HEADER_SIZE;

            record.module_name = pdb_string(pdb, data, data_size, name_offset, PDB_NAME_MAX(pdb, 4096), &length);
            name_offset += length + 1;

            record.object_name = pdb_string(pdb, data, data_size, name_offset, PDB_NAME_MAX(pdb, 4096), &length);
            name_offset += length + 1;

            if (!pdb_list_reserve((void **)&pdb->modules, &capacity, pdb->module_count, sizeof(xx_ms_pdb_module))) {
                break;
            }

            pdb->modules[pdb->module_count++] = record;

            offset = pdb_align_up4(name_offset);
            index++;
        }

        if (data) {
            xx_mem_free(data);
        }
    }

    pdb->modules_loaded = pdb_not_canceled(pdb);
}

bool xx_ms_pdb_get_modules(xx_ms_pdb *pdb, xx_ms_pdb_module_list *out)
{
    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    pdb_load_modules(pdb);

    if (pdb->module_count == 0) {
        return true;
    }

    out->items = (xx_ms_pdb_module *)xx_mem_alloc(pdb->module_count * sizeof(xx_ms_pdb_module));

    if (!out->items) {
        return false;
    }

    xx_mem_copy(out->items, pdb->modules, pdb->module_count * sizeof(xx_ms_pdb_module));
    out->count = pdb->module_count;

    return true;
}

bool xx_ms_pdb_get_section_contribs(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_section_contrib_list *out)
{
    const xx_ms_pdb_dbi_info *dbi_info = NULL;
    size_t capacity = 0;
    bool result = true;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    dbi_info = pdb_get_dbi_info(pdb);

    if (dbi_info->is_valid && (dbi_info->section_contribution_size > 4)) {
        int64_t substream_offset = (int64_t)64 + dbi_info->mod_info_size;
        int32_t data_size = 0;
        uint8_t *data = pdb_stream_region(pdb, XX_MS_PDB_STREAM_DBI, substream_offset, dbi_info->section_contribution_size, &data_size);
        uint32_t version = pdb_u32(data, data_size, 0);
        int32_t entry_size = 28;
        int32_t offset = 4;

        if (version == (uint32_t)(XX_MS_PDB_DBI_SECCONTRIB_V2)) {
            entry_size = 32;
        }

        /* strict: a V2 entry (32 bytes) has to be inside the substream as a whole */
        while ((((int64_t)offset + (pdb->strict ? entry_size : 28)) <= data_size) && pdb_not_canceled(pdb)) {
            xx_ms_pdb_section_contrib record = {0};

            if ((limit >= 0) && (out->count >= (size_t)limit)) {
                break;
            }

            record.section = pdb_u16(data, data_size, offset + 0);
            record.offset = (int32_t)pdb_u32(data, data_size, offset + 4);
            record.size = (int32_t)pdb_u32(data, data_size, offset + 8);
            record.characteristics = pdb_u32(data, data_size, offset + 12);
            record.module_index = pdb_u16(data, data_size, offset + 16);
            record.data_crc = pdb_u32(data, data_size, offset + 20);
            record.reloc_crc = pdb_u32(data, data_size, offset + 24);

            if (!pdb_list_reserve((void **)&out->items, &capacity, out->count, sizeof(xx_ms_pdb_section_contrib))) {
                xx_ms_pdb_section_contrib_list_free(out);
                result = false;
                break;
            }

            out->items[out->count++] = record;

            offset += entry_size;
        }

        if (data) {
            xx_mem_free(data);
        }
    }

    return result;
}

static void pdb_load_sections(xx_ms_pdb *pdb)
{
    const xx_ms_pdb_dbi_info *dbi_info = NULL;
    size_t capacity = 0;

    if (pdb->sections_loaded) {
        return;
    }

    if (pdb->sections) {
        xx_mem_free(pdb->sections);
        pdb->sections = NULL;
    }

    pdb->section_count = 0;

    dbi_info = pdb_get_dbi_info(pdb);

    if (dbi_info->is_valid && (dbi_info->dbg_stream_count > XX_MS_PDB_DBG_INDEX_SECTIONHDR)) {
        uint16_t stream_index = dbi_info->dbg_streams[XX_MS_PDB_DBG_INDEX_SECTIONHDR];

        if (stream_index != XX_MS_PDB_INVALID_STREAM_INDEX) {
            int32_t data_size = 0;
            uint8_t *data = pdb_stream_whole(pdb, (int32_t)stream_index, &data_size);
            int32_t offset = 0;

            while ((((int64_t)offset + XX_MS_PDB_IMAGE_SECTION_HEADER_SIZE) <= data_size) && pdb_not_canceled(pdb)) {
                xx_ms_pdb_section_header record = {0};

                record.name = pdb_string(pdb, data, data_size, offset, 8, NULL);
                record.virtual_size = pdb_u32(data, data_size, offset + 8);
                record.virtual_address = pdb_u32(data, data_size, offset + 12);
                record.size_of_raw_data = pdb_u32(data, data_size, offset + 16);
                record.pointer_to_raw_data = pdb_u32(data, data_size, offset + 20);
                record.characteristics = pdb_u32(data, data_size, offset + 36);

                if (!pdb_list_reserve((void **)&pdb->sections, &capacity, pdb->section_count, sizeof(xx_ms_pdb_section_header))) {
                    break;
                }

                pdb->sections[pdb->section_count++] = record;

                offset += XX_MS_PDB_IMAGE_SECTION_HEADER_SIZE;
            }

            if (data) {
                xx_mem_free(data);
            }
        }
    }

    pdb->sections_loaded = pdb_not_canceled(pdb);
}

bool xx_ms_pdb_get_section_headers(xx_ms_pdb *pdb, xx_ms_pdb_section_header_list *out)
{
    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    pdb_load_sections(pdb);

    if (pdb->section_count == 0) {
        return true;
    }

    out->items = (xx_ms_pdb_section_header *)xx_mem_alloc(pdb->section_count * sizeof(xx_ms_pdb_section_header));

    if (!out->items) {
        return false;
    }

    xx_mem_copy(out->items, pdb->sections, pdb->section_count * sizeof(xx_ms_pdb_section_header));
    out->count = pdb->section_count;

    return true;
}

/* ---- numeric leaves ------------------------------------------------------------------------ */

/* strict (and JG): the numeric leaves XPDB takes for 2 bytes. The value is the low
 * 64 bits of an integer and 0 for everything else. */
static int32_t pdb_strict_numeric_leaf(const uint8_t *data, int32_t data_size, int32_t offset, uint16_t leaf, uint64_t *value)
{
    int32_t result = 2;

    *value = 0;

    if ((leaf == PDB_LF_OCTWORD) || (leaf == PDB_LF_UOCTWORD)) {
        *value = pdb_u64(data, data_size, offset + 2);
        result = 18;
    } else if ((leaf == PDB_LF_COMPLEX64) || (leaf == PDB_LF_DECIMAL)) {
        result = 18;
    } else if (leaf == PDB_LF_REAL48) {
        result = 8;
    } else if ((leaf == PDB_LF_COMPLEX32) || (leaf == PDB_LF_DATE)) {
        result = 10;
    } else if (leaf == PDB_LF_COMPLEX80) {
        result = 22;
    } else if (leaf == PDB_LF_COMPLEX128) {
        result = 34;
    } else if (leaf == PDB_LF_REAL16) {
        result = 4;
    } else if (leaf == PDB_LF_VARSTRING) {
        result = 4 + (int32_t)pdb_u16(data, data_size, offset + 2);
    } else if (leaf == PDB_LF_UTF8STRING) {
        int32_t end = offset + 2;

        while ((end >= 0) && (end < data_size) && data[end]) {
            end++;
        }

        result = end + 1 - offset;
    }

    return result;
}

/* Returns the number of bytes occupied by a numeric leaf at offset */
static int32_t pdb_skip_numeric_leaf(bool strict, const uint8_t *data, int32_t data_size, int32_t offset, uint64_t *value)
{
    int32_t result = 0;
    uint16_t leaf = pdb_u16(data, data_size, offset);

    if (leaf < XX_MS_PDB_LF_NUMERIC) {
        *value = leaf;
        result = 2;
    } else if (leaf == XX_MS_PDB_LF_CHAR) {
        *value = pdb_u8(data, data_size, offset + 2);
        result = 3;
    } else if ((leaf == XX_MS_PDB_LF_SHORT) || (leaf == XX_MS_PDB_LF_USHORT)) {
        *value = pdb_u16(data, data_size, offset + 2);
        result = 4;
    } else if ((leaf == XX_MS_PDB_LF_LONG) || (leaf == XX_MS_PDB_LF_ULONG) || (leaf == XX_MS_PDB_LF_REAL32)) {
        *value = pdb_u32(data, data_size, offset + 2);
        result = 6;
    } else if ((leaf == XX_MS_PDB_LF_REAL64) || (leaf == XX_MS_PDB_LF_QUADWORD) || (leaf == XX_MS_PDB_LF_UQUADWORD)) {
        *value = pdb_u64(data, data_size, offset + 2);
        result = 10;
    } else if (leaf == XX_MS_PDB_LF_REAL80) {
        *value = 0;
        result = 12;
    } else if (leaf == XX_MS_PDB_LF_REAL128) {
        *value = 0;
        result = 18;
    } else if (strict) {
        result = pdb_strict_numeric_leaf(data, data_size, offset, leaf, value);
    } else {
        *value = 0;
        result = 2;
    }

    return result;
}

/* Same layout as pdb_skip_numeric_leaf but the value keeps its sign */
static int32_t pdb_read_numeric_leaf(bool strict, const uint8_t *data, int32_t data_size, int32_t offset, int64_t *value)
{
    int32_t result = 0;
    uint16_t leaf = pdb_u16(data, data_size, offset);

    if (leaf < XX_MS_PDB_LF_NUMERIC) {
        *value = leaf;
        result = 2;
    } else if (leaf == XX_MS_PDB_LF_CHAR) {
        *value = (int8_t)pdb_u8(data, data_size, offset + 2);
        result = 3;
    } else if (leaf == XX_MS_PDB_LF_SHORT) {
        *value = (int16_t)pdb_u16(data, data_size, offset + 2);
        result = 4;
    } else if (leaf == XX_MS_PDB_LF_USHORT) {
        *value = pdb_u16(data, data_size, offset + 2);
        result = 4;
    } else if (leaf == XX_MS_PDB_LF_LONG) {
        *value = (int32_t)pdb_u32(data, data_size, offset + 2);
        result = 6;
    } else if (leaf == XX_MS_PDB_LF_ULONG) {
        *value = pdb_u32(data, data_size, offset + 2);
        result = 6;
    } else if (leaf == XX_MS_PDB_LF_QUADWORD) {
        *value = (int64_t)pdb_u64(data, data_size, offset + 2);
        result = 10;
    } else if (leaf == XX_MS_PDB_LF_UQUADWORD) {
        *value = (int64_t)pdb_u64(data, data_size, offset + 2);
        result = 10;
    } else {
        uint64_t unsigned_value = 0;

        result = pdb_skip_numeric_leaf(strict, data, data_size, offset, &unsigned_value);
        *value = (int64_t)unsigned_value;
    }

    return result;
}

/* ---- symbols --------------------------------------------------------------------------------- */

static uint64_t pdb_segment_offset_to_address(const xx_ms_pdb *pdb, uint16_t segment, uint32_t offset)
{
    uint64_t result = (uint64_t)-1;
    int32_t index = (int32_t)segment - 1; /* Segments are 1 based */

    if ((index >= 0) && ((size_t)index < pdb->section_count)) {
        result = (uint64_t)pdb->sections[index].virtual_address + offset;
    }

    return result;
}

/* PROCSYM32 {len, kind, pParent, pEnd, pNext, len, DbgStart, DbgEnd, ...}: the
 * part every S_*PROC32* form (current, 16t, ST) has in the same place */
static void pdb_parse_proc_links(const uint8_t *data, int32_t data_size, xx_ms_pdb_symbol *record)
{
    record->parent = pdb_u32(data, data_size, 4);
    record->end = pdb_u32(data, data_size, 8);
    record->next = pdb_u32(data, data_size, 12);
    record->code_size = pdb_u32(data, data_size, 16);
    record->debug_start = pdb_u32(data, data_size, 20);
    record->debug_end = pdb_u32(data, data_size, 24);
}

/* data points to the beginning of the record (length field), the payload starts at offset 4 */
static void pdb_parse_symbol(xx_ms_pdb *pdb, const uint8_t *data, int32_t data_size, uint16_t kind, xx_ms_pdb_symbol *record)
{
    xx_mem_zero(record, sizeof(*record));

    record->kind = kind;
    record->kind_string = xx_ms_pdb_symbol_kind_to_string(kind);
    record->name = g_pdb_empty;
    record->address = (uint64_t)-1;
    record->module_index = -1;

    if (kind == XX_MS_PDB_S_PUB32) {
        record->flags = pdb_u32(data, data_size, 4);
        record->offset = pdb_u32(data, data_size, 8);
        record->segment = pdb_u16(data, data_size, 12);
        record->name = pdb_string(pdb, data, data_size, 14, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if ((kind == XX_MS_PDB_S_LDATA32) || (kind == XX_MS_PDB_S_GDATA32) || (kind == XX_MS_PDB_S_LTHREAD32) || (kind == XX_MS_PDB_S_GTHREAD32)) {
        record->type_index = pdb_u32(data, data_size, 4);
        record->offset = pdb_u32(data, data_size, 8);
        record->segment = pdb_u16(data, data_size, 12);
        record->name = pdb_string(pdb, data, data_size, 14, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if ((kind == XX_MS_PDB_S_LPROC32) || (kind == XX_MS_PDB_S_GPROC32) || (kind == XX_MS_PDB_S_LPROC32_ID) || (kind == XX_MS_PDB_S_GPROC32_ID)) {
        pdb_parse_proc_links(data, data_size, record);
        record->type_index = pdb_u32(data, data_size, 28);
        record->offset = pdb_u32(data, data_size, 32);
        record->segment = pdb_u16(data, data_size, 36);
        record->flags = pdb_u8(data, data_size, 38);
        record->name = pdb_string(pdb, data, data_size, 39, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_UDT) {
        record->type_index = pdb_u32(data, data_size, 4);
        record->name = pdb_string(pdb, data, data_size, 8, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_CONSTANT) {
        int32_t value_size = 0;

        record->type_index = pdb_u32(data, data_size, 4);
        record->value_leaf = pdb_u16(data, data_size, 8);
        value_size = pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), data, data_size, 8, &record->value);
        record->is_value_valid = true;
        record->name = pdb_string(pdb, data, data_size, 8 + value_size, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_OBJNAME) {
        record->flags = pdb_u32(data, data_size, 4); /* Signature */
        record->name = pdb_string(pdb, data, data_size, 8, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_UNAMESPACE) {
        record->name = pdb_string(pdb, data, data_size, 4, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if ((kind == XX_MS_PDB_S_PROCREF) || (kind == XX_MS_PDB_S_LPROCREF) || (kind == XX_MS_PDB_S_DATAREF)) {
        /* The offset points into the symbol stream of the module, the module index is 1 based */
        record->offset = pdb_u32(data, data_size, 8);
        record->module_index = (int32_t)pdb_u16(data, data_size, 12) - 1;
        record->name = pdb_string(pdb, data, data_size, 14, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_COMPILE3) {
        record->flags = pdb_u32(data, data_size, 4);
        record->name = pdb_string(pdb, data, data_size, 26, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_ENVBLOCK) {
        record->name = pdb_string(pdb, data, data_size, 5, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_THUNK32) {
        record->offset = pdb_u32(data, data_size, 16);
        record->segment = pdb_u16(data, data_size, 20);
        record->name = pdb_string(pdb, data, data_size, 25, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_LABEL32) {
        record->offset = pdb_u32(data, data_size, 4);
        record->segment = pdb_u16(data, data_size, 8);
        record->flags = pdb_u8(data, data_size, 10);
        record->name = pdb_string(pdb, data, data_size, 11, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_SECTION) {
        /* {isec, align, reserved, rva, cb, characteristics, name}, the address is the start of the section */
        record->segment = pdb_u16(data, data_size, 4);
        record->name = pdb_string(pdb, data, data_size, 20, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_COFFGROUP) {
        record->offset = pdb_u32(data, data_size, 12);
        record->segment = pdb_u16(data, data_size, 16);
        record->name = pdb_string(pdb, data, data_size, 18, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (kind == XX_MS_PDB_S_EXPORT) {
        record->flags = pdb_u16(data, data_size, 6);
        record->name = pdb_string(pdb, data, data_size, 8, PDB_NAME_MAX(pdb, 4096), NULL);
    }

    if (record->segment > 0) {
        record->address = pdb_segment_offset_to_address(pdb, record->segment, record->offset);
    }
}

/* ---- legacy (JG) symbol records ---------------------------------------------------------------- */

/* The current kind of a 16t / ST symbol record, 0 if the kind is not one of them */
static uint16_t pdb_legacy_symbol_kind(uint16_t kind)
{
    if ((kind == XX_MS_PDB_S_PUB32_ST) || (kind == XX_MS_PDB_S_PUB32_16T)) return XX_MS_PDB_S_PUB32;
    if ((kind == XX_MS_PDB_S_LDATA32_ST) || (kind == XX_MS_PDB_S_LDATA32_16T)) return XX_MS_PDB_S_LDATA32;
    if ((kind == XX_MS_PDB_S_GDATA32_ST) || (kind == XX_MS_PDB_S_GDATA32_16T)) return XX_MS_PDB_S_GDATA32;
    if ((kind == XX_MS_PDB_S_LTHREAD32_ST) || (kind == XX_MS_PDB_S_LTHREAD32_16T)) return XX_MS_PDB_S_LTHREAD32;
    if ((kind == XX_MS_PDB_S_GTHREAD32_ST) || (kind == XX_MS_PDB_S_GTHREAD32_16T)) return XX_MS_PDB_S_GTHREAD32;
    if ((kind == XX_MS_PDB_S_LPROC32_ST) || (kind == XX_MS_PDB_S_LPROC32_16T)) return XX_MS_PDB_S_LPROC32;
    if ((kind == XX_MS_PDB_S_GPROC32_ST) || (kind == XX_MS_PDB_S_GPROC32_16T)) return XX_MS_PDB_S_GPROC32;
    if ((kind == XX_MS_PDB_S_UDT_ST) || (kind == XX_MS_PDB_S_UDT_16T)) return XX_MS_PDB_S_UDT;
    if ((kind == XX_MS_PDB_S_CONSTANT_ST) || (kind == XX_MS_PDB_S_CONSTANT_16T)) return XX_MS_PDB_S_CONSTANT;
    if (kind == XX_MS_PDB_S_OBJNAME_ST) return XX_MS_PDB_S_OBJNAME;
    if (kind == XX_MS_PDB_S_THUNK32_ST) return XX_MS_PDB_S_THUNK32;
    if (kind == XX_MS_PDB_S_LABEL32_ST) return XX_MS_PDB_S_LABEL32;
    if (kind == XX_MS_PDB_S_PROCREF_ST) return XX_MS_PDB_S_PROCREF;
    if (kind == XX_MS_PDB_S_DATAREF_ST) return XX_MS_PDB_S_DATAREF;
    if (kind == XX_MS_PDB_S_LPROCREF_ST) return XX_MS_PDB_S_LPROCREF;

    return 0;
}

/* A length prefixed name */
static const char *pdb_string_st(xx_ms_pdb *pdb, const uint8_t *data, int32_t data_size, int32_t offset)
{
    return pdb_string(pdb, data, data_size, offset + 1, pdb_u8(data, data_size, offset), NULL);
}

/* The reference records of a JG file (S_PROCREF_ST, S_DATAREF_ST,
 * S_LPROCREF_ST) end before their name: the length prefixed name follows the
 * record and the next record starts at the next 4 byte boundary of the
 * stream. Returns the size of that tail, 0 if what follows is not a name. */
static int64_t pdb_legacy_symbol_tail(const uint8_t *data, int64_t record_end, int64_t max_offset)
{
    int64_t length = 0;
    int64_t tail = 0;
    int64_t i = 0;

    if (record_end >= max_offset) {
        return 0;
    }

    length = data[record_end];

    if ((length == 0) || ((record_end + 1 + length) > max_offset)) {
        return 0;
    }

    /* The header of a record would have a zero byte in it */
    for (i = 0; i < length; i++) {
        if (!data[record_end + 1 + i]) {
            return 0;
        }
    }

    tail = ((record_end + 1 + length + 3) & ~(int64_t)3) - record_end;

    if ((record_end + tail) > max_offset) {
        tail = max_offset - record_end;
    }

    return tail;
}

/* A 16t / ST symbol record, stored under its current kind. The layouts
 * differ from the current ones in the place of the 16 bit type index and in
 * the length prefixed name. raw_kind is one pdb_legacy_symbol_kind knows. */
static void pdb_parse_symbol_legacy(xx_ms_pdb *pdb, const uint8_t *data, int32_t data_size, uint16_t raw_kind, xx_ms_pdb_symbol *record)
{
    uint16_t kind = pdb_legacy_symbol_kind(raw_kind);

    xx_mem_zero(record, sizeof(*record));

    record->kind = kind;
    record->kind_string = xx_ms_pdb_symbol_kind_to_string(kind);
    record->name = g_pdb_empty;
    record->address = (uint64_t)-1;
    record->module_index = -1;

    if ((raw_kind == XX_MS_PDB_S_PUB32_ST) || (raw_kind == XX_MS_PDB_S_LDATA32_ST) || (raw_kind == XX_MS_PDB_S_GDATA32_ST) || (raw_kind == XX_MS_PDB_S_LTHREAD32_ST) ||
        (raw_kind == XX_MS_PDB_S_GTHREAD32_ST)) {
        /* An old public symbol carries a type index where the flags are today */
        record->type_index = pdb_u32(data, data_size, 4);
        record->offset = pdb_u32(data, data_size, 8);
        record->segment = pdb_u16(data, data_size, 12);
        record->name = pdb_string_st(pdb, data, data_size, 14);
    } else if ((raw_kind == XX_MS_PDB_S_PUB32_16T) || (raw_kind == XX_MS_PDB_S_LDATA32_16T) || (raw_kind == XX_MS_PDB_S_GDATA32_16T) ||
               (raw_kind == XX_MS_PDB_S_LTHREAD32_16T) || (raw_kind == XX_MS_PDB_S_GTHREAD32_16T)) {
        record->offset = pdb_u32(data, data_size, 4);
        record->segment = pdb_u16(data, data_size, 8);
        record->type_index = pdb_u16(data, data_size, 10);
        record->name = pdb_string_st(pdb, data, data_size, 12);
    } else if ((raw_kind == XX_MS_PDB_S_LPROC32_ST) || (raw_kind == XX_MS_PDB_S_GPROC32_ST)) {
        pdb_parse_proc_links(data, data_size, record);
        record->type_index = pdb_u32(data, data_size, 28);
        record->offset = pdb_u32(data, data_size, 32);
        record->segment = pdb_u16(data, data_size, 36);
        record->flags = pdb_u8(data, data_size, 38);
        record->name = pdb_string_st(pdb, data, data_size, 39);
    } else if ((raw_kind == XX_MS_PDB_S_LPROC32_16T) || (raw_kind == XX_MS_PDB_S_GPROC32_16T)) {
        pdb_parse_proc_links(data, data_size, record);
        record->offset = pdb_u32(data, data_size, 28);
        record->segment = pdb_u16(data, data_size, 32);
        record->type_index = pdb_u16(data, data_size, 34);
        record->flags = pdb_u8(data, data_size, 36);
        record->name = pdb_string_st(pdb, data, data_size, 37);
    } else if (raw_kind == XX_MS_PDB_S_UDT_ST) {
        record->type_index = pdb_u32(data, data_size, 4);
        record->name = pdb_string_st(pdb, data, data_size, 8);
    } else if (raw_kind == XX_MS_PDB_S_UDT_16T) {
        record->type_index = pdb_u16(data, data_size, 4);
        record->name = pdb_string_st(pdb, data, data_size, 6);
    } else if ((raw_kind == XX_MS_PDB_S_CONSTANT_ST) || (raw_kind == XX_MS_PDB_S_CONSTANT_16T)) {
        int32_t value_offset = (raw_kind == XX_MS_PDB_S_CONSTANT_ST) ? 8 : 6;
        int32_t value_size = 0;

        record->type_index = (raw_kind == XX_MS_PDB_S_CONSTANT_ST) ? pdb_u32(data, data_size, 4) : pdb_u16(data, data_size, 4);
        record->value_leaf = pdb_u16(data, data_size, value_offset);
        value_size = pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), data, data_size, value_offset, &record->value);
        record->is_value_valid = true;
        record->name = pdb_string_st(pdb, data, data_size, value_offset + value_size);
    } else if (raw_kind == XX_MS_PDB_S_OBJNAME_ST) {
        record->flags = pdb_u32(data, data_size, 4); /* Signature */
        record->name = pdb_string_st(pdb, data, data_size, 8);
    } else if ((raw_kind == XX_MS_PDB_S_PROCREF_ST) || (raw_kind == XX_MS_PDB_S_DATAREF_ST) || (raw_kind == XX_MS_PDB_S_LPROCREF_ST)) {
        /* {checksum, offset, module, fill}, the name sits behind the record, see pdb_legacy_symbol_tail */
        int32_t record_size = (int32_t)pdb_u16(data, data_size, 0) + 2;

        record->offset = pdb_u32(data, data_size, 8);
        record->module_index = (int32_t)pdb_u16(data, data_size, 12) - 1;

        if (record_size < data_size) {
            record->name = pdb_string_st(pdb, data, data_size, record_size);
        }
    } else if (raw_kind == XX_MS_PDB_S_THUNK32_ST) {
        record->offset = pdb_u32(data, data_size, 16);
        record->segment = pdb_u16(data, data_size, 20);
        record->name = pdb_string_st(pdb, data, data_size, 25);
    } else if (raw_kind == XX_MS_PDB_S_LABEL32_ST) {
        record->offset = pdb_u32(data, data_size, 4);
        record->segment = pdb_u16(data, data_size, 8);
        record->flags = pdb_u8(data, data_size, 10);
        record->name = pdb_string_st(pdb, data, data_size, 11);
    }

    if (record->segment > 0) {
        record->address = pdb_segment_offset_to_address(pdb, record->segment, record->offset);
    }
}

/* XPDB::_parseSymbols. only_kind != 0 keeps the records of one kind (the
 * limit then counts the kept ones, as XPDB::getPublicSymbols does). */
static bool pdb_parse_symbols(xx_ms_pdb *pdb, const uint8_t *data, int32_t data_size, int64_t start_offset, int64_t size, int32_t module_index, int32_t limit,
                              uint16_t only_kind, xx_ms_pdb_symbol_list *out)
{
    size_t capacity = 0;
    int64_t max_offset = start_offset + size;
    int64_t offset = start_offset;

    if (max_offset > data_size) {
        max_offset = data_size;
    }

    while (((offset + 4) <= max_offset) && pdb_not_canceled(pdb)) {
        uint16_t record_length = 0;
        uint16_t kind = 0;
        uint16_t raw_kind = 0; /* legacy (JG) only: the stored kind of a 16t / ST record */
        int64_t record_size = 0;
        int64_t tail_size = 0; /* legacy (JG) only: the name behind a reference record */

        if ((limit >= 0) && (out->count >= (size_t)limit)) {
            break;
        }

        record_length = pdb_u16(data, data_size, (int32_t)offset);
        kind = pdb_u16(data, data_size, (int32_t)offset + 2);

        if (record_length < 2) {
            break;
        }

        record_size = (int64_t)record_length + 2;

        if ((offset + record_size) > max_offset) {
            break;
        }

        if (pdb->is_legacy) {
            raw_kind = kind;

            if (pdb_legacy_symbol_kind(raw_kind)) {
                kind = pdb_legacy_symbol_kind(raw_kind);
            } else {
                raw_kind = 0; /* Not a 16t / ST record: decoded as it is */
            }

            if ((raw_kind == XX_MS_PDB_S_PROCREF_ST) || (raw_kind == XX_MS_PDB_S_DATAREF_ST) || (raw_kind == XX_MS_PDB_S_LPROCREF_ST)) {
                tail_size = pdb_legacy_symbol_tail(data, offset + record_size, max_offset);
            }
        }

        if (!only_kind || (kind == only_kind)) {
            xx_ms_pdb_symbol *record = NULL;

            if (!pdb_list_reserve((void **)&out->items, &capacity, out->count, sizeof(xx_ms_pdb_symbol))) {
                xx_ms_pdb_symbol_list_free(out);

                return false;
            }

            record = &out->items[out->count++];

            if (raw_kind) {
                pdb_parse_symbol_legacy(pdb, data + offset, (int32_t)(record_size + tail_size), raw_kind, record);
            } else {
                pdb_parse_symbol(pdb, data + offset, (int32_t)record_size, kind, record);
            }

            record->record_offset = offset;
            record->record_size = (int32_t)record_size;

            if (record->module_index == -1) {
                record->module_index = module_index;
            }
        }

        offset += record_size + tail_size;
    }

    return true;
}

static bool pdb_get_symbols(xx_ms_pdb *pdb, int32_t read_limit, int32_t limit, uint16_t only_kind, xx_ms_pdb_symbol_list *out)
{
    const xx_ms_pdb_dbi_info *dbi_info = NULL;
    bool result = true;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    dbi_info = pdb_get_dbi_info(pdb);

    if (dbi_info->is_valid && (dbi_info->sym_record_stream != XX_MS_PDB_INVALID_STREAM_INDEX)) {
        int32_t stream_index = (int32_t)dbi_info->sym_record_stream;
        int64_t size = 0;
        uint8_t *data = NULL;
        int32_t data_size = 0;

        if (pdb_load_streams(pdb) && (stream_index < pdb->stream_count)) {
            size = pdb->streams[stream_index].size;
        }

        /* For a limited request only the beginning of the stream is read */
        if (read_limit >= 0) {
            int64_t limited = (int64_t)4096 + (int64_t)read_limit * 1024;

            if (size > limited) {
                size = limited;
            }
        }

        data = pdb_stream_region(pdb, stream_index, 0, size, &data_size);

        pdb_load_sections(pdb);

        if (data) {
            result = pdb_parse_symbols(pdb, data, data_size, 0, data_size, -1, limit, only_kind, out);
            xx_mem_free(data);
        }
    }

    return result;
}

bool xx_ms_pdb_get_symbols(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_symbol_list *out)
{
    return pdb_get_symbols(pdb, limit, limit, 0, out);
}

bool xx_ms_pdb_get_public_symbols(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_symbol_list *out)
{
    /* The publics stream is a hash table over the records of the symbol record
     * stream, so the public symbols are collected from the record stream itself */
    return pdb_get_symbols(pdb, -1, limit, XX_MS_PDB_S_PUB32, out);
}

bool xx_ms_pdb_get_module_symbols(xx_ms_pdb *pdb, int32_t module_index, int32_t limit, xx_ms_pdb_symbol_list *out)
{
    bool result = true;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    pdb_load_modules(pdb);

    if ((module_index >= 0) && ((size_t)module_index < pdb->module_count)) {
        const xx_ms_pdb_module *module = &pdb->modules[module_index];

        if ((module->stream_index != XX_MS_PDB_INVALID_STREAM_INDEX) && (module->sym_byte_size > 4)) {
            int32_t data_size = 0;
            uint8_t *data = pdb_stream_region(pdb, (int32_t)module->stream_index, 0, module->sym_byte_size, &data_size);

            pdb_load_sections(pdb);

            if (data) {
                /* The symbol substream starts with a 4 byte CodeView signature */
                result = pdb_parse_symbols(pdb, data, data_size, 4, (int64_t)data_size - 4, module_index, limit, 0, out);
                xx_mem_free(data);
            }
        }
    }

    return result;
}

/* ---- type record lists ---------------------------------------------------------------------- */

static void pdb_decode_type(xx_ms_pdb *pdb, const uint8_t *record, int32_t record_size, uint32_t type_index, xx_ms_pdb_cvtype *result);

static bool pdb_get_types(xx_ms_pdb *pdb, int32_t stream_index, int32_t limit, xx_ms_pdb_type_record_list *out)
{
    const xx_ms_pdb_tpi_info *tpi_info = NULL;
    size_t capacity = 0;
    bool result = true;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    tpi_info = pdb_get_tpi_info(pdb, stream_index);

    if (pdb->is_legacy && (stream_index == XX_MS_PDB_STREAM_TPI)) {
        /* The records of a JG file are listed in their normalised form: the
         * kinds are the current ones, the offsets and sizes are those of the
         * rewritten records */
        uint32_t i = 0;

        if (!pdb->types_loaded) {
            xx_ms_pdb_load_types(pdb);
        }

        for (i = 0; (i < pdb->type_offset_count) && pdb_not_canceled(pdb); i++) {
            xx_ms_pdb_type_record record = {0};
            xx_ms_pdb_cvtype cvtype;

            if ((limit >= 0) && (out->count >= (size_t)limit)) {
                break;
            }

            pdb_decode_type(pdb, pdb->type_records + pdb->type_offsets[i], pdb->type_sizes[i], pdb->type_index_begin + i, &cvtype);

            record.type_index = cvtype.type_index;
            record.kind = cvtype.kind;
            record.kind_string = xx_ms_pdb_leaf_kind_to_string(cvtype.kind);
            record.name = cvtype.name;
            record.record_offset = (int64_t)tpi_info->header_size + pdb->type_offsets[i];
            record.record_size = pdb->type_sizes[i];

            if (!pdb_list_reserve((void **)&out->items, &capacity, out->count, sizeof(xx_ms_pdb_type_record))) {
                xx_ms_pdb_type_record_list_free(out);

                return false;
            }

            out->items[out->count++] = record;
        }

        return true;
    }

    if (tpi_info->is_valid && (tpi_info->type_record_bytes > 0)) {
        int64_t size = tpi_info->type_record_bytes;
        uint8_t *data = NULL;
        int32_t data_size = 0;
        int32_t offset = 0;
        uint32_t type_index = tpi_info->type_index_begin;

        /* For a limited request only the beginning of the record region is read */
        if (limit >= 0) {
            int64_t limited = (int64_t)4096 + (int64_t)limit * 1024;

            if (size > limited) {
                size = limited;
            }
        }

        data = pdb_stream_region(pdb, stream_index, tpi_info->header_size, size, &data_size);

        while ((((int64_t)offset + 4) <= data_size) && pdb_not_canceled(pdb)) {
            xx_ms_pdb_type_record record = {0};
            const uint8_t *type_record = NULL;
            uint16_t record_length = 0;
            uint16_t kind = 0;
            int32_t record_size = 0;
            uint64_t value = 0;
            int32_t value_size = 0;

            if ((limit >= 0) && (out->count >= (size_t)limit)) {
                break;
            }

            record_length = pdb_u16(data, data_size, offset);
            kind = pdb_u16(data, data_size, offset + 2);

            if (record_length < 2) {
                break;
            }

            record_size = (int32_t)record_length + 2;

            if (((int64_t)offset + record_size) > data_size) {
                break;
            }

            record.type_index = type_index;
            record.kind = kind;
            record.kind_string = xx_ms_pdb_leaf_kind_to_string(kind);
            record.name = g_pdb_empty;
            record.record_offset = (int64_t)(uint32_t)(tpi_info->header_size + (uint32_t)offset);
            record.record_size = record_size;

            type_record = data + offset;

            if ((kind == XX_MS_PDB_LF_CLASS) || (kind == XX_MS_PDB_LF_STRUCTURE) || (kind == XX_MS_PDB_LF_INTERFACE)) {
                value_size = pdb_skip_numeric_leaf(PDB_NUMERIC_EXACT(pdb), type_record, record_size, 20, &value);
                record.name = pdb_string(pdb, type_record, record_size, 20 + value_size, PDB_NAME_MAX(pdb, 4096), NULL);
            } else if (kind == XX_MS_PDB_LF_UNION) {
                value_size = pdb_skip_numeric_leaf(PDB_NUMERIC_EXACT(pdb), type_record, record_size, 12, &value);
                record.name = pdb_string(pdb, type_record, record_size, 12 + value_size, PDB_NAME_MAX(pdb, 4096), NULL);
            } else if ((kind == XX_MS_PDB_LF_CLASS2) || (kind == XX_MS_PDB_LF_STRUCTURE2) || (kind == XX_MS_PDB_LF_INTERFACE2)) {
                /* The newer records carry a 2 byte member count before the size */
                value_size = pdb_skip_numeric_leaf(PDB_NUMERIC_EXACT(pdb), type_record, record_size, 22, &value);
                record.name = pdb_string(pdb, type_record, record_size, 22 + value_size, PDB_NAME_MAX(pdb, 4096), NULL);
            } else if (kind == XX_MS_PDB_LF_UNION2) {
                value_size = pdb_skip_numeric_leaf(PDB_NUMERIC_EXACT(pdb), type_record, record_size, 14, &value);
                record.name = pdb_string(pdb, type_record, record_size, 14 + value_size, PDB_NAME_MAX(pdb, 4096), NULL);
            } else if (kind == XX_MS_PDB_LF_ENUM) {
                record.name = pdb_string(pdb, type_record, record_size, 16, PDB_NAME_MAX(pdb, 4096), NULL);
            } else if (kind == XX_MS_PDB_LF_TYPESERVER2) {
                record.name = pdb_string(pdb, type_record, record_size, 24, PDB_NAME_MAX(pdb, 4096), NULL);
            } else if (kind == XX_MS_PDB_LF_STRING_ID) {
                record.name = pdb_string(pdb, type_record, record_size, 8, PDB_NAME_MAX(pdb, 4096), NULL);
            } else if ((kind == XX_MS_PDB_LF_FUNC_ID) || (kind == XX_MS_PDB_LF_MFUNC_ID)) {
                record.name = pdb_string(pdb, type_record, record_size, 12, PDB_NAME_MAX(pdb, 4096), NULL);
            }

            if (!pdb_list_reserve((void **)&out->items, &capacity, out->count, sizeof(xx_ms_pdb_type_record))) {
                xx_ms_pdb_type_record_list_free(out);
                result = false;
                break;
            }

            out->items[out->count++] = record;

            offset += record_size;
            type_index++;
        }

        if (data) {
            xx_mem_free(data);
        }
    }

    return result;
}

bool xx_ms_pdb_get_types(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_type_record_list *out)
{
    return pdb_get_types(pdb, XX_MS_PDB_STREAM_TPI, limit, out);
}

bool xx_ms_pdb_get_ids(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_type_record_list *out)
{
    return pdb_get_types(pdb, XX_MS_PDB_STREAM_IPI, limit, out);
}

/* ---- legacy (JG) type records ---------------------------------------------------------------- */

/* A "program database 2.00" file stores its types with the record kinds of
 * its time: LF_*_16t (16 bit type indices, C7 .. VC 4.x) or the 32 bit kinds
 * with a length prefixed name (LF_*_ST, VC 5 / VC 6). XPDB decodes neither.
 * For a JG file every record is rewritten into the current layout when the
 * types are loaded, so the decoders below see one format only:
 *   - the kind becomes the current LF_ value;
 *   - 16 bit type indices are widened (the primitive indices are the same);
 *   - a length prefixed name becomes a zero terminated one;
 *   - friend / vfuncoff / membermodify entries of a field list, which nothing
 *     here decodes, are left out (they would stop the walk of the list).
 * The rewritten records are 4 byte aligned with LF_PAD bytes. A widened
 * record can outgrow its 16 bit length field, so the real sizes are kept in
 * xx_ms_pdb::type_sizes. Nothing of this runs for an MSF 7.00 file. */

typedef struct pdb_wbuf {
    uint8_t *data;
    size_t size;
    size_t capacity;
    bool failed;
} pdb_wbuf;

typedef struct pdb_rsrc {
    const uint8_t *data;
    int32_t size;
    int32_t offset;
    bool strict; /* exact numeric leaves; always set, see PDB_NUMERIC_EXACT */
} pdb_rsrc;

static bool pdb_wbuf_reserve(pdb_wbuf *buffer, size_t extra)
{
    if (buffer->failed) {
        return false;
    }

    if ((buffer->size + extra) > (size_t)PDB_MAX_BUFFER) {
        buffer->failed = true;

        return false;
    }

    if ((buffer->size + extra) > buffer->capacity) {
        size_t new_capacity = buffer->capacity ? buffer->capacity : 4096;
        uint8_t *new_data = NULL;

        while (new_capacity < (buffer->size + extra)) {
            new_capacity *= 2;
        }

        new_data = (uint8_t *)xx_mem_realloc(buffer->data, new_capacity);

        if (!new_data) {
            buffer->failed = true;

            return false;
        }

        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }

    return true;
}

static void pdb_wbuf_u8(pdb_wbuf *buffer, uint8_t value)
{
    if (pdb_wbuf_reserve(buffer, 1)) {
        buffer->data[buffer->size++] = value;
    }
}

static void pdb_wbuf_u16(pdb_wbuf *buffer, uint16_t value)
{
    if (pdb_wbuf_reserve(buffer, 2)) {
        buffer->data[buffer->size++] = (uint8_t)(value & 0xFF);
        buffer->data[buffer->size++] = (uint8_t)(value >> 8);
    }
}

static void pdb_wbuf_u32(pdb_wbuf *buffer, uint32_t value)
{
    pdb_wbuf_u16(buffer, (uint16_t)(value & 0xFFFF));
    pdb_wbuf_u16(buffer, (uint16_t)(value >> 16));
}

static int32_t pdb_rsrc_left(const pdb_rsrc *source)
{
    return (source->offset < source->size) ? (source->size - source->offset) : 0;
}

static void pdb_rsrc_skip(pdb_rsrc *source, int32_t count)
{
    /* The offset never runs far past the end, so it cannot wrap */
    if (source->offset < source->size) {
        source->offset += count;
    }
}

static uint8_t pdb_rsrc_u8(pdb_rsrc *source)
{
    uint8_t result = pdb_u8(source->data, source->size, source->offset);

    pdb_rsrc_skip(source, 1);

    return result;
}

static uint16_t pdb_rsrc_u16(pdb_rsrc *source)
{
    uint16_t result = pdb_u16(source->data, source->size, source->offset);

    pdb_rsrc_skip(source, 2);

    return result;
}

static uint32_t pdb_rsrc_u32(pdb_rsrc *source)
{
    uint32_t result = pdb_u32(source->data, source->size, source->offset);

    pdb_rsrc_skip(source, 4);

    return result;
}

/* A type index: 2 bytes in a 16t record, 4 bytes otherwise */
static uint32_t pdb_rsrc_index(pdb_rsrc *source, bool is_16t)
{
    return is_16t ? (uint32_t)pdb_rsrc_u16(source) : pdb_rsrc_u32(source);
}

/* Copies a numeric leaf as it is */
static void pdb_legacy_numeric(pdb_wbuf *out, pdb_rsrc *source)
{
    uint64_t value = 0;
    int32_t size = pdb_skip_numeric_leaf(source->strict, source->data, source->size, source->offset, &value);
    int32_t i = 0;

    for (i = 0; i < size; i++) {
        pdb_wbuf_u8(out, pdb_u8(source->data, source->size, source->offset + i));
    }

    pdb_rsrc_skip(source, size);
}

/* Writes a name zero terminated. out == NULL only skips it. */
static void pdb_legacy_name(pdb_wbuf *out, pdb_rsrc *source, bool is_st)
{
    int32_t left = pdb_rsrc_left(source);
    const uint8_t *name = source->data;
    int32_t length = 0;
    int32_t i = 0;

    if (left > 0) {
        name += source->offset;

        if (is_st) {
            length = name[0];
            name++;
            left--;

            if (length > left) {
                length = left;
            }

            source->offset += 1 + length;
        } else {
            while ((length < left) && name[length]) {
                length++;
            }

            source->offset += length + ((length < left) ? 1 : 0);
        }
    }

    if (out) {
        /* A length prefixed name has no terminator of its own: it ends at the first zero byte, if it has one */
        for (i = 0; (i < length) && name[i]; i++) {
            pdb_wbuf_u8(out, name[i]);
        }

        pdb_wbuf_u8(out, 0);
    }
}

/* LF_PAD bytes up to the next 4 byte boundary of the record */
static void pdb_legacy_pad(pdb_wbuf *out, size_t record_start)
{
    while (!out->failed && ((out->size - record_start) & 3)) {
        pdb_wbuf_u8(out, (uint8_t)(XX_MS_PDB_LF_PAD_MASK | (4 - ((out->size - record_start) & 3))));
    }
}

/* The property word of a 16t / ST record ends at the "scoped" bit: there is no
 * unique name behind the name, whatever the later bits say */
static uint16_t pdb_legacy_property(uint16_t property)
{
    return (uint16_t)(property & ~XX_MS_PDB_UDT_PROPERTY_HASUNIQUENAME);
}

static bool pdb_legacy_is_intro(uint16_t attr)
{
    uint16_t method_property = (uint16_t)((attr & XX_MS_PDB_FLD_ATTR_MPROP_MASK) >> XX_MS_PDB_FLD_ATTR_MPROP_SHIFT);

    return (method_property == XX_MS_PDB_FLD_MPROP_INTRO) || (method_property == XX_MS_PDB_FLD_MPROP_PUREINTRO);
}

/* The entries of a field list. An entry of an unknown kind ends the list, as it does in pdb_read_field. */
static void pdb_legacy_fields(pdb_wbuf *out, size_t record_start, pdb_rsrc *source)
{
    while (!out->failed && (pdb_rsrc_left(source) >= 2)) {
        uint16_t kind = 0;
        uint16_t attr = 0;
        uint32_t index = 0;
        bool is_16t = false;
        bool is_st = false;

        if ((source->data[source->offset] & XX_MS_PDB_LF_PAD_MASK) == XX_MS_PDB_LF_PAD_MASK) {
            source->offset++;
            continue;
        }

        kind = pdb_rsrc_u16(source);

        if ((kind == XX_MS_PDB_LF_BCLASS_16T) || (kind == XX_MS_PDB_LF_BCLASS)) {
            if (kind == XX_MS_PDB_LF_BCLASS_16T) {
                index = pdb_rsrc_u16(source);
                attr = pdb_rsrc_u16(source);
            } else {
                attr = pdb_rsrc_u16(source);
                index = pdb_rsrc_u32(source);
            }

            pdb_wbuf_u16(out, XX_MS_PDB_LF_BCLASS);
            pdb_wbuf_u16(out, attr);
            pdb_wbuf_u32(out, index);
            pdb_legacy_numeric(out, source);
        } else if ((kind == XX_MS_PDB_LF_VBCLASS_16T) || (kind == XX_MS_PDB_LF_IVBCLASS_16T) || (kind == XX_MS_PDB_LF_VBCLASS) || (kind == XX_MS_PDB_LF_IVBCLASS)) {
            uint32_t vbptr_index = 0;

            if ((kind == XX_MS_PDB_LF_VBCLASS_16T) || (kind == XX_MS_PDB_LF_IVBCLASS_16T)) {
                index = pdb_rsrc_u16(source);
                vbptr_index = pdb_rsrc_u16(source);
                attr = pdb_rsrc_u16(source);
            } else {
                attr = pdb_rsrc_u16(source);
                index = pdb_rsrc_u32(source);
                vbptr_index = pdb_rsrc_u32(source);
            }

            pdb_wbuf_u16(out, ((kind == XX_MS_PDB_LF_VBCLASS_16T) || (kind == XX_MS_PDB_LF_VBCLASS)) ? XX_MS_PDB_LF_VBCLASS : XX_MS_PDB_LF_IVBCLASS);
            pdb_wbuf_u16(out, attr);
            pdb_wbuf_u32(out, index);
            pdb_wbuf_u32(out, vbptr_index);
            pdb_legacy_numeric(out, source);
            pdb_legacy_numeric(out, source);
        } else if ((kind == XX_MS_PDB_LF_ENUMERATE_ST) || (kind == XX_MS_PDB_LF_ENUMERATE)) {
            attr = pdb_rsrc_u16(source);

            pdb_wbuf_u16(out, XX_MS_PDB_LF_ENUMERATE);
            pdb_wbuf_u16(out, attr);
            pdb_legacy_numeric(out, source);
            pdb_legacy_name(out, source, kind == XX_MS_PDB_LF_ENUMERATE_ST);
        } else if ((kind == XX_MS_PDB_LF_INDEX_16T) || (kind == XX_MS_PDB_LF_INDEX) || (kind == XX_MS_PDB_LF_VFUNCTAB_16T) || (kind == XX_MS_PDB_LF_VFUNCTAB)) {
            is_16t = (kind == XX_MS_PDB_LF_INDEX_16T) || (kind == XX_MS_PDB_LF_VFUNCTAB_16T);

            if (!is_16t) {
                pdb_rsrc_skip(source, 2); /* padding */
            }

            index = pdb_rsrc_index(source, is_16t);

            pdb_wbuf_u16(out, ((kind == XX_MS_PDB_LF_INDEX_16T) || (kind == XX_MS_PDB_LF_INDEX)) ? XX_MS_PDB_LF_INDEX : XX_MS_PDB_LF_VFUNCTAB);
            pdb_wbuf_u16(out, 0);
            pdb_wbuf_u32(out, index);
        } else if ((kind == XX_MS_PDB_LF_MEMBER_16T) || (kind == XX_MS_PDB_LF_MEMBER_ST) || (kind == XX_MS_PDB_LF_MEMBER) || (kind == XX_MS_PDB_LF_STMEMBER_16T) ||
                   (kind == XX_MS_PDB_LF_STMEMBER_ST) || (kind == XX_MS_PDB_LF_STMEMBER)) {
            bool is_static = (kind == XX_MS_PDB_LF_STMEMBER_16T) || (kind == XX_MS_PDB_LF_STMEMBER_ST) || (kind == XX_MS_PDB_LF_STMEMBER);

            is_16t = (kind == XX_MS_PDB_LF_MEMBER_16T) || (kind == XX_MS_PDB_LF_STMEMBER_16T);
            is_st = (kind != XX_MS_PDB_LF_MEMBER) && (kind != XX_MS_PDB_LF_STMEMBER);

            if (is_16t) {
                index = pdb_rsrc_u16(source);
                attr = pdb_rsrc_u16(source);
            } else {
                attr = pdb_rsrc_u16(source);
                index = pdb_rsrc_u32(source);
            }

            pdb_wbuf_u16(out, is_static ? XX_MS_PDB_LF_STMEMBER : XX_MS_PDB_LF_MEMBER);
            pdb_wbuf_u16(out, attr);
            pdb_wbuf_u32(out, index);

            if (!is_static) {
                pdb_legacy_numeric(out, source);
            }

            pdb_legacy_name(out, source, is_st);
        } else if ((kind == XX_MS_PDB_LF_METHOD_16T) || (kind == XX_MS_PDB_LF_METHOD_ST) || (kind == XX_MS_PDB_LF_METHOD)) {
            uint16_t count = pdb_rsrc_u16(source);

            index = pdb_rsrc_index(source, kind == XX_MS_PDB_LF_METHOD_16T);

            pdb_wbuf_u16(out, XX_MS_PDB_LF_METHOD);
            pdb_wbuf_u16(out, count);
            pdb_wbuf_u32(out, index);
            pdb_legacy_name(out, source, kind != XX_MS_PDB_LF_METHOD);
        } else if ((kind == XX_MS_PDB_LF_NESTTYPE_16T) || (kind == XX_MS_PDB_LF_NESTTYPE_ST) || (kind == XX_MS_PDB_LF_NESTTYPEEX_ST) || (kind == XX_MS_PDB_LF_NESTTYPE)) {
            if (kind == XX_MS_PDB_LF_NESTTYPE_16T) {
                index = pdb_rsrc_u16(source);
            } else {
                attr = pdb_rsrc_u16(source); /* padding, the attributes of LF_NESTTYPEEX */
                index = pdb_rsrc_u32(source);
            }

            pdb_wbuf_u16(out, XX_MS_PDB_LF_NESTTYPE);
            pdb_wbuf_u16(out, attr);
            pdb_wbuf_u32(out, index);
            pdb_legacy_name(out, source, kind != XX_MS_PDB_LF_NESTTYPE);
        } else if ((kind == XX_MS_PDB_LF_ONEMETHOD_16T) || (kind == XX_MS_PDB_LF_ONEMETHOD_ST) || (kind == XX_MS_PDB_LF_ONEMETHOD)) {
            attr = pdb_rsrc_u16(source);
            index = pdb_rsrc_index(source, kind == XX_MS_PDB_LF_ONEMETHOD_16T);

            pdb_wbuf_u16(out, XX_MS_PDB_LF_ONEMETHOD);
            pdb_wbuf_u16(out, attr);
            pdb_wbuf_u32(out, index);

            if (pdb_legacy_is_intro(attr)) {
                pdb_wbuf_u32(out, pdb_rsrc_u32(source)); /* vtable offset */
            }

            pdb_legacy_name(out, source, kind != XX_MS_PDB_LF_ONEMETHOD);
        } else if ((kind == XX_MS_PDB_LF_FRIENDFCN_16T) || (kind == XX_MS_PDB_LF_FRIENDFCN_ST) || (kind == XX_MS_PDB_LF_MEMBERMODIFY_ST)) {
            /* Dropped: {index, name} / {padding, index, name} / {attr, index, name} */
            pdb_rsrc_skip(source, (kind == XX_MS_PDB_LF_FRIENDFCN_16T) ? 2 : 6);
            pdb_legacy_name(NULL, source, true);
        } else if ((kind == XX_MS_PDB_LF_FRIENDCLS_16T) || (kind == XX_MS_PDB_LF_FRIENDCLS)) {
            /* Dropped: {index} / {padding, index} */
            pdb_rsrc_skip(source, (kind == XX_MS_PDB_LF_FRIENDCLS_16T) ? 2 : 6);
        } else if ((kind == XX_MS_PDB_LF_VFUNCOFF_16T) || (kind == XX_MS_PDB_LF_VFUNCOFF)) {
            /* Dropped: {index, offset} / {padding, index, offset} */
            pdb_rsrc_skip(source, (kind == XX_MS_PDB_LF_VFUNCOFF_16T) ? 6 : 10);
        } else {
            break;
        }

        pdb_legacy_pad(out, record_start);
    }
}

/* record points to the record start: {u16 length, u16 kind}. Appends the normalised record. */
static void pdb_legacy_record(pdb_wbuf *out, const uint8_t *record, int32_t record_size, bool strict)
{
    size_t record_start = out->size;
    pdb_rsrc reader = {0};
    pdb_rsrc *source = &reader;
    uint16_t kind = pdb_u16(record, record_size, 2);
    bool is_16t = false;
    size_t size = 0;

    reader.data = record;
    reader.size = record_size;
    reader.offset = 4;
    reader.strict = strict;

    pdb_wbuf_u16(out, 0); /* The length, set at the end */

    if ((kind == XX_MS_PDB_LF_CLASS_16T) || (kind == XX_MS_PDB_LF_STRUCTURE_16T) || (kind == XX_MS_PDB_LF_CLASS_ST) || (kind == XX_MS_PDB_LF_STRUCTURE_ST)) {
        uint16_t count = pdb_rsrc_u16(source);
        uint16_t property = 0;
        uint32_t field_list = 0;
        uint32_t derived = 0;
        uint32_t vshape = 0;

        is_16t = (kind == XX_MS_PDB_LF_CLASS_16T) || (kind == XX_MS_PDB_LF_STRUCTURE_16T);

        if (is_16t) {
            field_list = pdb_rsrc_u16(source);
            property = pdb_rsrc_u16(source);
            derived = pdb_rsrc_u16(source);
            vshape = pdb_rsrc_u16(source);
        } else {
            property = pdb_rsrc_u16(source);
            field_list = pdb_rsrc_u32(source);
            derived = pdb_rsrc_u32(source);
            vshape = pdb_rsrc_u32(source);
        }

        pdb_wbuf_u16(out, ((kind == XX_MS_PDB_LF_CLASS_16T) || (kind == XX_MS_PDB_LF_CLASS_ST)) ? XX_MS_PDB_LF_CLASS : XX_MS_PDB_LF_STRUCTURE);
        pdb_wbuf_u16(out, count);
        pdb_wbuf_u16(out, pdb_legacy_property(property));
        pdb_wbuf_u32(out, field_list);
        pdb_wbuf_u32(out, derived);
        pdb_wbuf_u32(out, vshape);
        pdb_legacy_numeric(out, source); /* Size */
        pdb_legacy_name(out, source, true);
    } else if ((kind == XX_MS_PDB_LF_UNION_16T) || (kind == XX_MS_PDB_LF_UNION_ST)) {
        uint16_t count = pdb_rsrc_u16(source);
        uint16_t property = 0;
        uint32_t field_list = 0;

        if (kind == XX_MS_PDB_LF_UNION_16T) {
            field_list = pdb_rsrc_u16(source);
            property = pdb_rsrc_u16(source);
        } else {
            property = pdb_rsrc_u16(source);
            field_list = pdb_rsrc_u32(source);
        }

        pdb_wbuf_u16(out, XX_MS_PDB_LF_UNION);
        pdb_wbuf_u16(out, count);
        pdb_wbuf_u16(out, pdb_legacy_property(property));
        pdb_wbuf_u32(out, field_list);
        pdb_legacy_numeric(out, source); /* Size */
        pdb_legacy_name(out, source, true);
    } else if ((kind == XX_MS_PDB_LF_ENUM_16T) || (kind == XX_MS_PDB_LF_ENUM_ST)) {
        uint16_t count = pdb_rsrc_u16(source);
        uint16_t property = 0;
        uint32_t underlying = 0;
        uint32_t field_list = 0;

        if (kind == XX_MS_PDB_LF_ENUM_16T) {
            underlying = pdb_rsrc_u16(source);
            field_list = pdb_rsrc_u16(source);
            property = pdb_rsrc_u16(source);
        } else {
            property = pdb_rsrc_u16(source);
            underlying = pdb_rsrc_u32(source);
            field_list = pdb_rsrc_u32(source);
        }

        pdb_wbuf_u16(out, XX_MS_PDB_LF_ENUM);
        pdb_wbuf_u16(out, count);
        pdb_wbuf_u16(out, pdb_legacy_property(property));
        pdb_wbuf_u32(out, underlying);
        pdb_wbuf_u32(out, field_list);
        pdb_legacy_name(out, source, true);
    } else if ((kind == XX_MS_PDB_LF_ARRAY_16T) || (kind == XX_MS_PDB_LF_ARRAY_ST)) {
        uint32_t element = 0;
        uint32_t index = 0;

        is_16t = (kind == XX_MS_PDB_LF_ARRAY_16T);
        element = pdb_rsrc_index(source, is_16t);
        index = pdb_rsrc_index(source, is_16t);

        pdb_wbuf_u16(out, XX_MS_PDB_LF_ARRAY);
        pdb_wbuf_u32(out, element);
        pdb_wbuf_u32(out, index);
        pdb_legacy_numeric(out, source); /* Size in bytes */
        pdb_legacy_name(out, source, true);
    } else if (kind == XX_MS_PDB_LF_MODIFIER_16T) {
        /* {attr, type}, the other way round than the current record */
        uint16_t attr = pdb_rsrc_u16(source);
        uint32_t index = pdb_rsrc_u16(source);

        pdb_wbuf_u16(out, XX_MS_PDB_LF_MODIFIER);
        pdb_wbuf_u32(out, index);
        pdb_wbuf_u16(out, attr);
    } else if (kind == XX_MS_PDB_LF_POINTER_16T) {
        /* {attr(16 bit), type, variant}: the attribute bits kept their places, the size bits did not exist yet */
        uint16_t attr = pdb_rsrc_u16(source);
        uint32_t index = pdb_rsrc_u16(source);
        uint32_t mode = ((uint32_t)attr & XX_MS_PDB_PTR_ATTR_MODE_MASK) >> XX_MS_PDB_PTR_ATTR_MODE_SHIFT;

        pdb_wbuf_u16(out, XX_MS_PDB_LF_POINTER);
        pdb_wbuf_u32(out, index);
        pdb_wbuf_u32(out, attr);

        if ((mode == XX_MS_PDB_PTR_MODE_MEMBER_DATA_POINTER) || (mode == XX_MS_PDB_PTR_MODE_MEMBER_FUNCTION_POINTER)) {
            /* {class, representation} */
            pdb_wbuf_u32(out, pdb_rsrc_u16(source));
            pdb_wbuf_u16(out, pdb_rsrc_u16(source));
        }
    } else if (kind == XX_MS_PDB_LF_PROCEDURE_16T) {
        uint32_t return_type = pdb_rsrc_u16(source);
        uint8_t calling_convention = pdb_rsrc_u8(source);
        uint8_t reserved = pdb_rsrc_u8(source);
        uint16_t count = pdb_rsrc_u16(source);
        uint32_t arg_list = pdb_rsrc_u16(source);

        pdb_wbuf_u16(out, XX_MS_PDB_LF_PROCEDURE);
        pdb_wbuf_u32(out, return_type);
        pdb_wbuf_u8(out, calling_convention);
        pdb_wbuf_u8(out, reserved);
        pdb_wbuf_u16(out, count);
        pdb_wbuf_u32(out, arg_list);
    } else if (kind == XX_MS_PDB_LF_MFUNCTION_16T) {
        uint32_t return_type = pdb_rsrc_u16(source);
        uint32_t class_type = pdb_rsrc_u16(source);
        uint32_t this_type = pdb_rsrc_u16(source);
        uint8_t calling_convention = pdb_rsrc_u8(source);
        uint8_t reserved = pdb_rsrc_u8(source);
        uint16_t count = pdb_rsrc_u16(source);
        uint32_t arg_list = pdb_rsrc_u16(source);
        uint32_t this_adjust = pdb_rsrc_u32(source);

        pdb_wbuf_u16(out, XX_MS_PDB_LF_MFUNCTION);
        pdb_wbuf_u32(out, return_type);
        pdb_wbuf_u32(out, class_type);
        pdb_wbuf_u32(out, this_type);
        pdb_wbuf_u8(out, calling_convention);
        pdb_wbuf_u8(out, reserved);
        pdb_wbuf_u16(out, count);
        pdb_wbuf_u32(out, arg_list);
        pdb_wbuf_u32(out, this_adjust);
    } else if (kind == XX_MS_PDB_LF_ARGLIST_16T) {
        uint16_t count = pdb_rsrc_u16(source);
        uint16_t i = 0;

        pdb_wbuf_u16(out, XX_MS_PDB_LF_ARGLIST);
        pdb_wbuf_u32(out, count);

        for (i = 0; (i < count) && (pdb_rsrc_left(source) >= 2); i++) {
            pdb_wbuf_u32(out, pdb_rsrc_u16(source));
        }
    } else if (kind == XX_MS_PDB_LF_BITFIELD_16T) {
        /* {length, position, type} */
        uint8_t bit_size = pdb_rsrc_u8(source);
        uint8_t bit_position = pdb_rsrc_u8(source);
        uint32_t index = pdb_rsrc_u16(source);

        pdb_wbuf_u16(out, XX_MS_PDB_LF_BITFIELD);
        pdb_wbuf_u32(out, index);
        pdb_wbuf_u8(out, bit_size);
        pdb_wbuf_u8(out, bit_position);
    } else if (kind == XX_MS_PDB_LF_METHODLIST_16T) {
        /* {attr, type, [vtable offset]} */
        pdb_wbuf_u16(out, XX_MS_PDB_LF_METHODLIST);

        while (!out->failed && (pdb_rsrc_left(source) >= 4) && ((record[reader.offset] & XX_MS_PDB_LF_PAD_MASK) != XX_MS_PDB_LF_PAD_MASK)) {
            uint16_t attr = pdb_rsrc_u16(source);
            uint32_t index = pdb_rsrc_u16(source);

            pdb_wbuf_u16(out, attr);
            pdb_wbuf_u16(out, 0);
            pdb_wbuf_u32(out, index);

            if (pdb_legacy_is_intro(attr)) {
                pdb_wbuf_u32(out, pdb_rsrc_u32(source));
            }
        }
    } else if ((kind == XX_MS_PDB_LF_FIELDLIST_16T) || (kind == XX_MS_PDB_LF_FIELDLIST)) {
        pdb_wbuf_u16(out, XX_MS_PDB_LF_FIELDLIST);
        pdb_legacy_fields(out, record_start, source);
    } else {
        /* The same layout as today (LF_MODIFIER, LF_POINTER, LF_PROCEDURE,
         * LF_MFUNCTION, LF_ARGLIST, LF_BITFIELD, LF_METHODLIST, LF_VTSHAPE) or
         * a kind nothing here decodes: kept as it is */
        if (pdb_wbuf_reserve(out, (size_t)record_size)) {
            xx_mem_copy(out->data + out->size, record + 2, (size_t)record_size - 2);
            out->size += (size_t)record_size - 2;
        }
    }

    pdb_legacy_pad(out, record_start);

    if (!out->failed) {
        size = out->size - record_start - 2;

        if (size > 0xFFFF) {
            size = 0xFFFF; /* The real size is in xx_ms_pdb::type_sizes */
        }

        out->data[record_start] = (uint8_t)(size & 0xFF);
        out->data[record_start + 1] = (uint8_t)(size >> 8);
    }
}

/* Rewrites the type records of a JG file (pdb->type_records) and fills
 * type_offsets / type_sizes. On a failed allocation the records that were
 * converted so far are kept. */
static void pdb_legacy_convert_types(xx_ms_pdb *pdb)
{
    const uint8_t *data = pdb->type_records;
    int32_t data_size = pdb->type_records_size;
    pdb_wbuf out = {0};
    size_t offsets_capacity = 0;
    size_t sizes_capacity = 0;
    int32_t offset = 0;

    pdb->type_offset_count = 0;

    while ((((int64_t)offset + 4) <= data_size) && pdb_not_canceled(pdb)) {
        uint16_t record_length = pdb_u16(data, data_size, offset);
        int32_t record_size = (int32_t)record_length + 2;
        size_t record_start = out.size;

        if ((record_length < 2) || (((int64_t)offset + record_size) > data_size)) {
            break;
        }

        if (!pdb_list_reserve((void **)&pdb->type_offsets, &offsets_capacity, pdb->type_offset_count, sizeof(int32_t)) ||
            !pdb_list_reserve((void **)&pdb->type_sizes, &sizes_capacity, pdb->type_offset_count, sizeof(int32_t))) {
            break;
        }

        pdb_legacy_record(&out, data + offset, record_size, true);

        if (out.failed) {
            out.size = record_start;
            break;
        }

        pdb->type_offsets[pdb->type_offset_count] = (int32_t)record_start;
        pdb->type_sizes[pdb->type_offset_count] = (int32_t)(out.size - record_start);
        pdb->type_offset_count++;

        offset += record_size;
    }

    if (pdb->type_records) {
        xx_mem_free(pdb->type_records);
    }

    pdb->type_records = out.data;
    pdb->type_records_size = (int32_t)out.size;
}

/* Size of the cached record of a type that pdb_type_record_offset found */
static int32_t pdb_type_record_size(const xx_ms_pdb *pdb, uint32_t type_index, int32_t record_offset)
{
    if (pdb->type_sizes) {
        return pdb->type_sizes[type_index - pdb->type_index_begin];
    }

    return (int32_t)pdb_u16(pdb->type_records, pdb->type_records_size, record_offset) + 2;
}

/* ---- CodeView types ------------------------------------------------------------------------- */

bool xx_ms_pdb_is_types_loaded(const xx_ms_pdb *pdb)
{
    return pdb ? pdb->types_loaded : false;
}

void xx_ms_pdb_unload_types(xx_ms_pdb *pdb)
{
    if (!pdb) {
        return;
    }

    pdb->types_loaded = false;

    if (pdb->type_records) xx_mem_free(pdb->type_records);
    if (pdb->type_offsets) xx_mem_free(pdb->type_offsets);
    if (pdb->type_sizes) xx_mem_free(pdb->type_sizes);
    if (pdb->names_stream) xx_mem_free(pdb->names_stream);

    pdb->type_records = NULL;
    pdb->type_records_size = 0;
    pdb->type_offsets = NULL;
    pdb->type_sizes = NULL;
    pdb->type_offset_count = 0;
    pdb->names_stream = NULL;
    pdb->names_stream_size = 0;
    pdb->names_stream_loaded = false;
    pdb->type_index_begin = 0;
    pdb->type_index_end = 0;

    pdb_pmap_free(&pdb->unique_names);
    pdb_pmap_free(&pdb->names);
    pdb_pmap_free(&pdb->hash_adjusters);
}

uint32_t xx_ms_pdb_get_type_index_begin(xx_ms_pdb *pdb)
{
    return pdb ? pdb->type_index_begin : 0;
}

uint32_t xx_ms_pdb_get_type_index_end(xx_ms_pdb *pdb)
{
    return pdb ? pdb->type_index_end : 0;
}

static int32_t pdb_type_record_offset(const xx_ms_pdb *pdb, uint32_t type_index);

/* strict: the size of the underlying type of an enum that is not a primitive
 * one (a modifier or another enum in front of it). 0 if it leads nowhere. */
static uint64_t pdb_strict_enum_size(const xx_ms_pdb *pdb, uint32_t type_index)
{
    int32_t depth = 0;

    for (depth = 0; depth < 16; depth++) {
        const uint8_t *data = pdb->type_records;
        int32_t data_size = pdb->type_records_size;
        int32_t offset = 0;
        uint16_t kind = 0;

        if (xx_ms_pdb_is_primitive_type(type_index)) {
            return (uint64_t)(int64_t)xx_ms_pdb_primitive_type_size(type_index);
        }

        offset = pdb_type_record_offset(pdb, type_index);

        if ((offset < 0) || (((int64_t)offset + 12) > data_size)) {
            break;
        }

        kind = pdb_u16(data, data_size, offset + 2);

        if (kind == XX_MS_PDB_LF_MODIFIER) {
            type_index = pdb_u32(data, data_size, offset + 4);
        } else if (kind == XX_MS_PDB_LF_ENUM) {
            type_index = pdb_u32(data, data_size, offset + 8);
        } else {
            break;
        }
    }

    return 0;
}

/* record points to the record start: {u16 length, u16 kind}, the payload starts at 4 */
static void pdb_decode_type(xx_ms_pdb *pdb, const uint8_t *record, int32_t record_size, uint32_t type_index, xx_ms_pdb_cvtype *result)
{
    uint16_t kind = 0;
    int64_t size = 0;
    int32_t size_length = 0;
    int32_t name_offset = 0;
    int32_t name_length = 0;

    xx_mem_zero(result, sizeof(*result));

    result->name = g_pdb_empty;
    result->unique_name = g_pdb_empty;
    result->type_index = type_index;
    result->kind = pdb_u16(record, record_size, 2);
    result->record_size = record_size;
    result->is_valid = true;

    kind = result->kind;

    if ((kind == XX_MS_PDB_LF_CLASS) || (kind == XX_MS_PDB_LF_STRUCTURE) || (kind == XX_MS_PDB_LF_INTERFACE)) {
        result->count = pdb_u16(record, record_size, 4);
        result->property = pdb_u16(record, record_size, 6);
        result->field_list_index = pdb_u32(record, record_size, 8);
        /* 12: derived, 16: vshape */
        size_length = pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), record, record_size, 20, &size);
        result->size = (uint64_t)size;

        name_offset = 20 + size_length;
        result->name = pdb_string(pdb, record, record_size, name_offset, PDB_NAME_MAX(pdb, 8192), &name_length);

        if (result->property & XX_MS_PDB_UDT_PROPERTY_HASUNIQUENAME) {
            result->unique_name = pdb_string(pdb, record, record_size, name_offset + name_length + 1, PDB_NAME_MAX(pdb, 8192), NULL);
        }

        if (kind == XX_MS_PDB_LF_CLASS) {
            result->udt_kind = XX_MS_PDB_UDT_KIND_CLASS;
        } else if (kind == XX_MS_PDB_LF_INTERFACE) {
            result->udt_kind = XX_MS_PDB_UDT_KIND_INTERFACE;
        } else {
            result->udt_kind = XX_MS_PDB_UDT_KIND_STRUCT;
        }
    } else if ((kind == XX_MS_PDB_LF_CLASS2) || (kind == XX_MS_PDB_LF_STRUCTURE2) || (kind == XX_MS_PDB_LF_INTERFACE2)) {
        /* Newer layout: the property is 4 bytes and the member count sits after
         * vshape, just before the size, so the size is 2 bytes further along
         * than in the classic record. field @8, derived @12, vshape @16. */
        result->property = (uint16_t)pdb_u32(record, record_size, 4);
        result->field_list_index = pdb_u32(record, record_size, 8);
        result->count = pdb_u16(record, record_size, 20);

        size_length = pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), record, record_size, 22, &size);
        result->size = (uint64_t)size;

        name_offset = 22 + size_length;
        result->name = pdb_string(pdb, record, record_size, name_offset, PDB_NAME_MAX(pdb, 8192), &name_length);

        if (result->property & XX_MS_PDB_UDT_PROPERTY_HASUNIQUENAME) {
            result->unique_name = pdb_string(pdb, record, record_size, name_offset + name_length + 1, PDB_NAME_MAX(pdb, 8192), NULL);
        }

        if (kind == XX_MS_PDB_LF_CLASS2) {
            result->udt_kind = XX_MS_PDB_UDT_KIND_CLASS;
        } else if (kind == XX_MS_PDB_LF_INTERFACE2) {
            result->udt_kind = XX_MS_PDB_UDT_KIND_INTERFACE;
        } else {
            result->udt_kind = XX_MS_PDB_UDT_KIND_STRUCT;
        }
    } else if (kind == XX_MS_PDB_LF_UNION2) {
        /* property @4(4 bytes), field @8, count @12(2 bytes), size @14 */
        result->property = (uint16_t)pdb_u32(record, record_size, 4);
        result->field_list_index = pdb_u32(record, record_size, 8);
        result->count = pdb_u16(record, record_size, 12);

        size_length = pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), record, record_size, 14, &size);
        result->size = (uint64_t)size;

        name_offset = 14 + size_length;
        result->name = pdb_string(pdb, record, record_size, name_offset, PDB_NAME_MAX(pdb, 8192), &name_length);

        if (result->property & XX_MS_PDB_UDT_PROPERTY_HASUNIQUENAME) {
            result->unique_name = pdb_string(pdb, record, record_size, name_offset + name_length + 1, PDB_NAME_MAX(pdb, 8192), NULL);
        }

        result->udt_kind = XX_MS_PDB_UDT_KIND_UNION;
    } else if (kind == XX_MS_PDB_LF_UNION) {
        result->count = pdb_u16(record, record_size, 4);
        result->property = pdb_u16(record, record_size, 6);
        result->field_list_index = pdb_u32(record, record_size, 8);

        size_length = pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), record, record_size, 12, &size);
        result->size = (uint64_t)size;

        name_offset = 12 + size_length;
        result->name = pdb_string(pdb, record, record_size, name_offset, PDB_NAME_MAX(pdb, 8192), &name_length);

        if (result->property & XX_MS_PDB_UDT_PROPERTY_HASUNIQUENAME) {
            result->unique_name = pdb_string(pdb, record, record_size, name_offset + name_length + 1, PDB_NAME_MAX(pdb, 8192), NULL);
        }

        result->udt_kind = XX_MS_PDB_UDT_KIND_UNION;
    } else if (kind == XX_MS_PDB_LF_ENUM) {
        result->count = pdb_u16(record, record_size, 4);
        result->property = pdb_u16(record, record_size, 6);
        result->element_type_index = pdb_u32(record, record_size, 8); /* Underlying type */
        result->field_list_index = pdb_u32(record, record_size, 12);
        result->name = pdb_string(pdb, record, record_size, 16, PDB_NAME_MAX(pdb, 8192), &name_length);

        if (result->property & XX_MS_PDB_UDT_PROPERTY_HASUNIQUENAME) {
            result->unique_name = pdb_string(pdb, record, record_size, 16 + name_length + 1, PDB_NAME_MAX(pdb, 8192), NULL);
        }

        /* XPDB sizes the enum by the low byte of the underlying index whatever
         * the index is. strict: an index of the type stream is followed. */
        if (pdb->strict && !xx_ms_pdb_is_primitive_type(result->element_type_index)) {
            result->size = pdb_strict_enum_size(pdb, result->element_type_index);
        } else {
            result->size = (uint64_t)(int64_t)xx_ms_pdb_primitive_type_size(result->element_type_index);
        }
    } else if (kind == XX_MS_PDB_LF_POINTER) {
        uint32_t attr = 0;

        result->element_type_index = pdb_u32(record, record_size, 4);

        attr = pdb_u32(record, record_size, 8);

        result->pointer_mode = (uint8_t)((attr & XX_MS_PDB_PTR_ATTR_MODE_MASK) >> XX_MS_PDB_PTR_ATTR_MODE_SHIFT);
        result->size = (attr & XX_MS_PDB_PTR_ATTR_SIZE_MASK) >> XX_MS_PDB_PTR_ATTR_SIZE_SHIFT;
        result->is_const = (attr & XX_MS_PDB_PTR_ATTR_CONST) != 0;
        result->is_volatile = (attr & XX_MS_PDB_PTR_ATTR_VOLATILE) != 0;
        result->is_unaligned = (attr & XX_MS_PDB_PTR_ATTR_UNALIGNED) != 0;

        /* Only an lvalue reference is a reference here: this matches what DIA
         * reports through get_reference, an rvalue reference counts as a pointer */
        result->is_reference = (result->pointer_mode == XX_MS_PDB_PTR_MODE_LVALUE_REFERENCE);
        result->is_pointer = (!result->is_reference);

        if (result->size == 0) {
            /* Older records do not carry the size, derive it from the pointer kind */
            uint32_t pointer_kind = attr & XX_MS_PDB_PTR_ATTR_KIND_MASK;

            if (pointer_kind == XX_MS_PDB_CV_PTR_NEAR) {
                result->size = 2;
            } else if ((pointer_kind == XX_MS_PDB_CV_PTR_FAR) || (pointer_kind == XX_MS_PDB_CV_PTR_HUGE)) {
                result->size = 4;
            } else if (pointer_kind == XX_MS_PDB_CV_PTR_FAR32) {
                result->size = 6;
            } else if (pointer_kind == XX_MS_PDB_CV_PTR_64) {
                result->size = 8;
            } else {
                result->size = 4; /* CV_PTR_NEAR32 and everything else */
            }
        }
    } else if (kind == XX_MS_PDB_LF_ARRAY) {
        result->element_type_index = pdb_u32(record, record_size, 4);
        result->index_type_index = pdb_u32(record, record_size, 8);

        pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), record, record_size, 12, &size);
        result->size = (uint64_t)size; /* In bytes */
    } else if (kind == XX_MS_PDB_LF_MODIFIER) {
        uint16_t attr = 0;

        result->element_type_index = pdb_u32(record, record_size, 4);

        attr = pdb_u16(record, record_size, 8);

        result->is_const = (attr & XX_MS_PDB_MOD_ATTR_CONST) != 0;
        result->is_volatile = (attr & XX_MS_PDB_MOD_ATTR_VOLATILE) != 0;
        result->is_unaligned = (attr & XX_MS_PDB_MOD_ATTR_UNALIGNED) != 0;
    } else if (kind == XX_MS_PDB_LF_BITFIELD) {
        result->element_type_index = pdb_u32(record, record_size, 4);
        result->bit_size = pdb_u8(record, record_size, 8);
        result->bit_position = pdb_u8(record, record_size, 9);
    } else if (kind == XX_MS_PDB_LF_PROCEDURE) {
        result->element_type_index = pdb_u32(record, record_size, 4); /* Return type */
        result->calling_convention = pdb_u8(record, record_size, 8);
        result->count = pdb_u16(record, record_size, 10);
        result->arg_list_index = pdb_u32(record, record_size, 12);
    } else if (kind == XX_MS_PDB_LF_MFUNCTION) {
        result->element_type_index = pdb_u32(record, record_size, 4); /* Return type */
        result->class_type_index = pdb_u32(record, record_size, 8);
        result->this_type_index = pdb_u32(record, record_size, 12);
        result->calling_convention = pdb_u8(record, record_size, 16);
        result->count = pdb_u16(record, record_size, 18);
        result->arg_list_index = pdb_u32(record, record_size, 20);
    } else if (kind == XX_MS_PDB_LF_ARGLIST) {
        result->count = pdb_u32(record, record_size, 4);
    } else if (kind == XX_MS_PDB_LF_VTSHAPE) {
        result->count = pdb_u16(record, record_size, 4);
    }

    result->is_forward_ref = (result->property & XX_MS_PDB_UDT_PROPERTY_FWDREF) != 0;
    result->is_nested = (result->property & XX_MS_PDB_UDT_PROPERTY_ISNESTED) != 0;
    result->is_scoped = (result->property & XX_MS_PDB_UDT_PROPERTY_SCOPED) != 0;
}

/* The string table of the /names stream, cached: the hash adjusters name
 * their types by an offset into it */
static const char *pdb_names_string(xx_ms_pdb *pdb, uint32_t offset)
{
    const uint8_t *data = NULL;
    int32_t data_size = 0;

    if (!pdb->names_stream_loaded) {
        int32_t stream_index = xx_ms_pdb_get_named_stream_index(pdb, "/names");

        pdb->names_stream_loaded = true;

        if (stream_index != -1) {
            pdb->names_stream = pdb_stream_whole(pdb, stream_index, &pdb->names_stream_size);
        }
    }

    data = pdb->names_stream;
    data_size = pdb->names_stream_size;

    if (data_size >= 12) {
        uint32_t signature = pdb_u32(data, data_size, 0);
        uint32_t byte_size = pdb_u32(data, data_size, 8);

        if ((signature == XX_MS_PDB_NAMES_SIGNATURE) && (offset < byte_size)) {
            /* XPDB reads up to 8192 bytes whatever the table size is; a string
             * of a sound table ends inside it, one that does not is cut at its end */
            uint32_t max_size = byte_size - offset;
            uint32_t limit = (uint32_t)PDB_NAME_MAX(pdb, 8192);

            return pdb_string(pdb, data, data_size, (int32_t)(12u + offset), (max_size < limit) ? (int32_t)max_size : (int32_t)limit, NULL);
        }
    }

    return g_pdb_empty;
}

static void pdb_load_hash_adjusters(xx_ms_pdb *pdb, const xx_ms_pdb_tpi_info *tpi_info)
{
    uint8_t *data = NULL;
    int32_t data_size = 0;
    int32_t offset = 0;
    uint32_t number_of_entries = 0;
    uint32_t present_words = 0;
    uint32_t deleted_words = 0;
    uint32_t i = 0;

    if ((tpi_info->hash_stream_index == XX_MS_PDB_INVALID_STREAM_INDEX) || (tpi_info->hash_adj_buffer_length == 0)) {
        return;
    }

    data = pdb_stream_region(pdb, (int32_t)tpi_info->hash_stream_index, tpi_info->hash_adj_buffer_offset, tpi_info->hash_adj_buffer_length, &data_size);

    /* The same hash table layout the named stream map of the PDB stream uses */
    if (data_size >= 8) {
        number_of_entries = pdb_u32(data, data_size, offset);
        offset += 4;
        offset += 4; /* Capacity */

        if ((offset + 4) <= data_size) {
            present_words = pdb_u32(data, data_size, offset);
            offset += 4;
            offset = pdb_add32(offset, present_words * 4);

            if (((int64_t)offset + 4) <= data_size) {
                deleted_words = pdb_u32(data, data_size, offset);
                offset = pdb_add32(offset, 4);
                offset = pdb_add32(offset, deleted_words * 4);

                while ((i < number_of_entries) && (((int64_t)offset + 8) <= data_size) && pdb_not_canceled(pdb)) {
                    uint32_t name_offset = pdb_u32(data, data_size, offset);
                    uint32_t type_index = pdb_u32(data, data_size, pdb_add32(offset, 4));
                    uint32_t steps = (offset <= -5) ? pdb_zero_entries(offset, number_of_entries - i) : 1;
                    const char *name = pdb_names_string(pdb, name_offset);

                    if (name[0]) {
                        pdb_pmap_set(&pdb->hash_adjusters, name, type_index);
                    }

                    i += steps;
                    offset = pdb_add32(offset, steps * 8);
                }
            }
        }
    }

    if (data) {
        xx_mem_free(data);
    }
}

bool xx_ms_pdb_load_types(xx_ms_pdb *pdb)
{
    const xx_ms_pdb_tpi_info *tpi_info = NULL;
    const uint8_t *data = NULL;
    int32_t data_size = 0;
    size_t capacity = 0;
    int32_t offset = 0;
    uint32_t type_index = 0;

    if (!pdb) {
        return false;
    }

    if (pdb->types_loaded) {
        return true;
    }

    tpi_info = pdb_get_tpi_info(pdb, XX_MS_PDB_STREAM_TPI);

    if (!tpi_info->is_valid) {
        /* A portable PDB holds no CodeView types at all, and a stub PDB(one
         * written before the link step) has no type stream: both are valid
         * files with an empty type list */
        pdb->type_index_begin = 0;
        pdb->type_index_end = 0;
        pdb->types_loaded = true;

        return true;
    }

    pdb->type_index_begin = tpi_info->type_index_begin;
    pdb->type_index_end = tpi_info->type_index_end;

    pdb->type_records = pdb_stream_region(pdb, XX_MS_PDB_STREAM_TPI, tpi_info->header_size, tpi_info->type_record_bytes, &pdb->type_records_size);

    data = pdb->type_records;
    data_size = pdb->type_records_size;

    pdb->type_offset_count = 0;

    type_index = pdb->type_index_begin;

    if (pdb->is_legacy) {
        uint32_t i = 0;

        pdb_legacy_convert_types(pdb);

        data = pdb->type_records;
        data_size = pdb->type_records_size;

        /* The same walk as below over the normalised records */
        for (i = 0; (i < pdb->type_offset_count) && pdb_not_canceled(pdb); i++) {
            uint16_t kind = pdb_u16(data, data_size, pdb->type_offsets[i] + 2);

            if (xx_ms_pdb_is_udt_leaf(kind) || (kind == XX_MS_PDB_LF_ENUM)) {
                xx_ms_pdb_cvtype cvtype;

                pdb_decode_type(pdb, data + pdb->type_offsets[i], pdb->type_sizes[i], type_index + i, &cvtype);

                if (!cvtype.is_forward_ref) {
                    if (cvtype.unique_name[0]) {
                        pdb_pmap_set(&pdb->unique_names, cvtype.unique_name, type_index + i);
                    }

                    if (cvtype.name[0]) {
                        pdb_pmap_set(&pdb->names, cvtype.name, type_index + i);
                    }
                }
            }
        }

        type_index += pdb->type_offset_count;
        offset = data_size; /* Nothing is left for the walk below */
    }

    while ((((int64_t)offset + 4) <= data_size) && pdb_not_canceled(pdb)) {
        uint16_t record_length = pdb_u16(data, data_size, offset);
        int32_t record_size = 0;
        uint16_t kind = 0;

        if (record_length < 2) {
            break;
        }

        record_size = (int32_t)record_length + 2;

        if (((int64_t)offset + record_size) > data_size) {
            break;
        }

        if (!pdb_list_reserve((void **)&pdb->type_offsets, &capacity, pdb->type_offset_count, sizeof(int32_t))) {
            break;
        }

        pdb->type_offsets[pdb->type_offset_count++] = offset;

        kind = pdb_u16(data, data_size, offset + 2);

        if (xx_ms_pdb_is_udt_leaf(kind) || (kind == XX_MS_PDB_LF_ENUM)) {
            xx_ms_pdb_cvtype cvtype;

            pdb_decode_type(pdb, data + offset, record_size, type_index, &cvtype);

            /* The definition record is the one a forward reference resolves to */
            if (!cvtype.is_forward_ref) {
                /* A PDB can hold several different definitions under one name,
                 * built from translation units that did not agree. The last one
                 * is the one a forward reference resolves to. */
                if (cvtype.unique_name[0]) {
                    pdb_pmap_set(&pdb->unique_names, cvtype.unique_name, type_index);
                }

                /* Without a unique name several definitions of the same name
                 * cannot be told apart; the last one wins, which is what the
                 * TPI hash table of such a PDB resolves a forward reference to */
                if (cvtype.name[0]) {
                    pdb_pmap_set(&pdb->names, cvtype.name, type_index);
                }
            }
        }

        offset += record_size;
        type_index++;
    }

    pdb->type_index_end = type_index;

    pdb_load_hash_adjusters(pdb, tpi_info);

    pdb->types_loaded = true;

    /* A PDB without a single type record is still a valid PDB(a .NET one for
     * example), the type stream is simply empty */
    return true;
}

static int32_t pdb_type_record_offset(const xx_ms_pdb *pdb, uint32_t type_index)
{
    int32_t result = -1;

    if ((type_index >= pdb->type_index_begin) && (type_index < pdb->type_index_end)) {
        uint32_t index = type_index - pdb->type_index_begin;

        if (index < pdb->type_offset_count) {
            result = pdb->type_offsets[index];
        }
    }

    return result;
}

xx_ms_pdb_cvtype xx_ms_pdb_get_type_by_index(xx_ms_pdb *pdb, uint32_t type_index)
{
    xx_ms_pdb_cvtype result = {0};
    int32_t offset = -1;

    result.name = g_pdb_empty;
    result.unique_name = g_pdb_empty;
    result.type_index = type_index;

    if (!pdb) {
        return result;
    }

    if (!pdb->types_loaded) {
        xx_ms_pdb_load_types(pdb);
    }

    offset = pdb_type_record_offset(pdb, type_index);

    if (offset != -1) {
        const uint8_t *data = pdb->type_records;
        int32_t data_size = pdb->type_records_size;
        int32_t record_size = pdb_type_record_size(pdb, type_index, offset);

        if (((int64_t)offset + record_size) <= data_size) {
            pdb_decode_type(pdb, data + offset, record_size, type_index, &result);
            result.record_offset = offset;
        }
    }

    return result;
}

uint32_t xx_ms_pdb_resolve_forward_ref_type(xx_ms_pdb *pdb, const xx_ms_pdb_cvtype *cvtype)
{
    uint32_t result = 0;

    if (!cvtype) {
        return 0;
    }

    result = cvtype->type_index;

    if (pdb && cvtype->is_forward_ref) {
        /* The caller may hand in strings of its own, so the names are looked up by content */
        const char *unique_name = (cvtype->unique_name && cvtype->unique_name[0]) ? pdb_intern(pdb, cvtype->unique_name, pdb_strlen(cvtype->unique_name), false) : NULL;
        const char *name = (cvtype->name && cvtype->name[0]) ? pdb_intern(pdb, cvtype->name, pdb_strlen(cvtype->name), false) : NULL;
        uint32_t value = 0;

        /* An adjuster names the record to use and wins over the name lookup */
        if (unique_name && pdb_pmap_get(&pdb->hash_adjusters, unique_name, &value)) {
            return value;
        }

        if (name && pdb_pmap_get(&pdb->hash_adjusters, name, &value)) {
            return value;
        }

        if (unique_name && pdb_pmap_get(&pdb->unique_names, unique_name, &value)) {
            result = value;
        } else if (name && pdb_pmap_get(&pdb->names, name, &value)) {
            result = value;
        }
    }

    return result;
}

uint32_t xx_ms_pdb_resolve_forward_ref(xx_ms_pdb *pdb, uint32_t type_index)
{
    xx_ms_pdb_cvtype cvtype = xx_ms_pdb_get_type_by_index(pdb, type_index);

    return xx_ms_pdb_resolve_forward_ref_type(pdb, &cvtype);
}

bool xx_ms_pdb_get_hash_adjuster(xx_ms_pdb *pdb, const char *name, uint32_t *type_index)
{
    const char *key = NULL;
    uint32_t value = 0;

    if (!pdb || !name) {
        return false;
    }

    if (!pdb->types_loaded) {
        xx_ms_pdb_load_types(pdb);
    }

    key = pdb_intern(pdb, name, pdb_strlen(name), false);

    if (!key || !key[0] || !pdb_pmap_get(&pdb->hash_adjusters, key, &value)) {
        return false;
    }

    if (type_index) {
        *type_index = value;
    }

    return true;
}

size_t xx_ms_pdb_get_hash_adjuster_count(xx_ms_pdb *pdb)
{
    if (!pdb) {
        return 0;
    }

    if (!pdb->types_loaded) {
        xx_ms_pdb_load_types(pdb);
    }

    return pdb->hash_adjusters.count;
}

/* Decodes one entry of a field list. field_size receives the size of the
 * entry including the trailing padding leaves. */
static void pdb_read_field(xx_ms_pdb *pdb, const uint8_t *data, int32_t data_size, int32_t offset, int32_t *field_size, xx_ms_pdb_cvfield *result)
{
    int32_t start_offset = offset;
    int32_t name_length = 0;
    uint16_t kind = 0;

    xx_mem_zero(result, sizeof(*result));

    result->name = g_pdb_empty;
    result->kind = pdb_u16(data, data_size, offset);
    offset += 2;

    kind = result->kind;

    if (kind == XX_MS_PDB_LF_MEMBER) {
        result->attr = pdb_u16(data, data_size, offset);
        result->type_index = pdb_u32(data, data_size, offset + 2);
        offset += 6;
        offset += pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), data, data_size, offset, &result->offset);
        result->name = pdb_string(pdb, data, data_size, offset, PDB_NAME_MAX(pdb, 8192), &name_length);
        offset += name_length + 1;
    } else if (kind == XX_MS_PDB_LF_STMEMBER) {
        result->attr = pdb_u16(data, data_size, offset);
        result->type_index = pdb_u32(data, data_size, offset + 2);
        offset += 6;
        result->name = pdb_string(pdb, data, data_size, offset, PDB_NAME_MAX(pdb, 8192), &name_length);
        offset += name_length + 1;
        result->is_static = true;
    } else if (kind == XX_MS_PDB_LF_BCLASS) {
        result->attr = pdb_u16(data, data_size, offset);
        result->type_index = pdb_u32(data, data_size, offset + 2);
        offset += 6;
        offset += pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), data, data_size, offset, &result->offset);
    } else if ((kind == XX_MS_PDB_LF_VBCLASS) || (kind == XX_MS_PDB_LF_IVBCLASS)) {
        result->attr = pdb_u16(data, data_size, offset);
        result->type_index = pdb_u32(data, data_size, offset + 2);
        result->vbptr_type_index = pdb_u32(data, data_size, offset + 6);
        offset += 10;
        offset += pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), data, data_size, offset, &result->vbptr_offset);
        offset += pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), data, data_size, offset, &result->vbtable_index);
    } else if (kind == XX_MS_PDB_LF_ENUMERATE) {
        result->attr = pdb_u16(data, data_size, offset);
        offset += 2;
        result->value_leaf = pdb_u16(data, data_size, offset);
        offset += pdb_read_numeric_leaf(PDB_NUMERIC_EXACT(pdb), data, data_size, offset, &result->value);
        result->is_value_valid = true;
        result->name = pdb_string(pdb, data, data_size, offset, PDB_NAME_MAX(pdb, 8192), &name_length);
        offset += name_length + 1;
    } else if (kind == XX_MS_PDB_LF_NESTTYPE) {
        result->attr = pdb_u16(data, data_size, offset);
        result->type_index = pdb_u32(data, data_size, offset + 2);
        offset += 6;
        result->name = pdb_string(pdb, data, data_size, offset, PDB_NAME_MAX(pdb, 8192), &name_length);
        offset += name_length + 1;
    } else if (kind == XX_MS_PDB_LF_ONEMETHOD) {
        result->attr = pdb_u16(data, data_size, offset);
        result->type_index = pdb_u32(data, data_size, offset + 2);
        offset += 6;

        result->method_property = (uint8_t)((result->attr & XX_MS_PDB_FLD_ATTR_MPROP_MASK) >> XX_MS_PDB_FLD_ATTR_MPROP_SHIFT);

        if ((result->method_property == XX_MS_PDB_FLD_MPROP_INTRO) || (result->method_property == XX_MS_PDB_FLD_MPROP_PUREINTRO)) {
            result->vtable_offset = pdb_u32(data, data_size, offset);
            result->is_vtable_offset_valid = true;
            offset += 4;
        }

        result->name = pdb_string(pdb, data, data_size, offset, PDB_NAME_MAX(pdb, 8192), &name_length);
        offset += name_length + 1;
    } else if (kind == XX_MS_PDB_LF_METHOD) {
        result->method_count = pdb_u16(data, data_size, offset);
        result->method_list_index = pdb_u32(data, data_size, offset + 2);
        offset += 6;
        result->name = pdb_string(pdb, data, data_size, offset, PDB_NAME_MAX(pdb, 8192), &name_length);
        offset += name_length + 1;
    } else if (kind == XX_MS_PDB_LF_VFUNCTAB) {
        offset += 2; /* padding */
        result->type_index = pdb_u32(data, data_size, offset);
        offset += 4;
    } else if (kind == XX_MS_PDB_LF_INDEX) {
        offset += 2; /* padding */
        result->type_index = pdb_u32(data, data_size, offset);
        offset += 4;
    } else if (pdb->strict && ((kind == PDB_LF_NESTTYPEEX) || (kind == PDB_LF_MEMBERMODIFY) || (kind == PDB_LF_FRIENDFCN))) {
        /* strict: {attr (padding for a friend), type, name}. A nested type
         * with attributes is reported as LF_NESTTYPE; the other two are only
         * walked over, see xx_ms_pdb_get_field_list */
        result->attr = pdb_u16(data, data_size, offset);
        result->type_index = pdb_u32(data, data_size, offset + 2);
        offset += 6;
        result->name = pdb_string(pdb, data, data_size, offset, PDB_NAME_MAX(pdb, 8192), &name_length);
        offset += name_length + 1;

        if (kind == PDB_LF_NESTTYPEEX) {
            result->kind = XX_MS_PDB_LF_NESTTYPE;
        }
    } else if (pdb->strict && (kind == XX_MS_PDB_LF_FRIENDCLS)) {
        result->type_index = pdb_u32(data, data_size, offset + 2); /* strict: {padding, type} */
        offset += 6;
    } else if (pdb->strict && (kind == XX_MS_PDB_LF_VFUNCOFF)) {
        result->type_index = pdb_u32(data, data_size, offset + 2); /* strict: {padding, type, offset} */
        offset += 10;
    } else {
        /* Unknown entry: the walk cannot continue safely */
        *field_size = 0;
        return;
    }

    result->access = xx_ms_pdb_field_attr_to_access(result->attr);

    if (kind != XX_MS_PDB_LF_METHOD) {
        result->method_property = (uint8_t)((result->attr & XX_MS_PDB_FLD_ATTR_MPROP_MASK) >> XX_MS_PDB_FLD_ATTR_MPROP_SHIFT);

        if (result->method_property == XX_MS_PDB_FLD_MPROP_STATIC) {
            result->is_static = true;
        }
    }

    /* Skip the padding leaves (0xF1..0xF3) that align the next entry */
    while (offset < data_size) {
        uint8_t pad = pdb_u8(data, data_size, offset);

        if ((pad & XX_MS_PDB_LF_PAD_MASK) == XX_MS_PDB_LF_PAD_MASK) {
            offset++;
        } else {
            break;
        }
    }

    *field_size = offset - start_offset;
}

bool xx_ms_pdb_get_field_list(xx_ms_pdb *pdb, uint32_t type_index, xx_ms_pdb_cvfield_list *out)
{
    size_t capacity = 0;
    uint32_t current_index = type_index;
    uint32_t *visited = NULL;
    size_t visited_capacity = 0;
    size_t visited_count = 0;
    bool result = true;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    if (!pdb->types_loaded) {
        xx_ms_pdb_load_types(pdb);
    }

    /* LF_INDEX continues the list in another record */
    while ((current_index != 0) && result && pdb_not_canceled(pdb)) {
        int32_t record_offset = pdb_type_record_offset(pdb, current_index);
        const uint8_t *data = pdb->type_records;
        int32_t data_size = pdb->type_records_size;
        int32_t record_end = 0;
        int32_t offset = 0;
        size_t i = 0;

        if (record_offset == -1) {
            break;
        }

        record_end = record_offset + pdb_type_record_size(pdb, current_index, record_offset);

        if ((record_end > data_size) || (pdb_u16(data, data_size, record_offset + 2) != XX_MS_PDB_LF_FIELDLIST)) {
            break;
        }

        /* A chain of continuations that comes back to a record it already
         * went through never ends (XPDB does not return from it); stop there */
        for (i = 0; i < visited_count; i++) {
            if (visited[i] == current_index) {
                break;
            }
        }

        if ((i < visited_count) || !pdb_list_reserve((void **)&visited, &visited_capacity, visited_count, sizeof(uint32_t))) {
            break;
        }

        visited[visited_count++] = current_index;

        current_index = 0;

        offset = record_offset + 4;

        while ((offset < record_end) && pdb_not_canceled(pdb)) {
            xx_ms_pdb_cvfield field;
            int32_t field_size = 0;

            pdb_read_field(pdb, data, record_end, offset, &field_size, &field);

            if (field_size <= 0) {
                break;
            }

            if (field.kind == XX_MS_PDB_LF_INDEX) {
                current_index = field.type_index; /* Continuation record */
            } else if (pdb->strict && ((field.kind == PDB_LF_FRIENDFCN) || (field.kind == PDB_LF_MEMBERMODIFY) || (field.kind == XX_MS_PDB_LF_FRIENDCLS) ||
                                       (field.kind == XX_MS_PDB_LF_VFUNCOFF))) {
                /* strict: entries XPDB ends the list at. They are not members
                 * and are left out, as in a JG file (pdb_legacy_fields). */
            } else {
                if (!pdb_list_reserve((void **)&out->items, &capacity, out->count, sizeof(xx_ms_pdb_cvfield))) {
                    xx_ms_pdb_cvfield_list_free(out);
                    result = false;
                    break;
                }

                out->items[out->count++] = field;
            }

            offset += field_size;
        }
    }

    if (visited) {
        xx_mem_free(visited);
    }

    return result;
}

bool xx_ms_pdb_get_method_list(xx_ms_pdb *pdb, uint32_t type_index, int32_t count, xx_ms_pdb_cvfield_list *out)
{
    size_t capacity = 0;
    int32_t record_offset = -1;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    if (!pdb->types_loaded) {
        xx_ms_pdb_load_types(pdb);
    }

    record_offset = pdb_type_record_offset(pdb, type_index);

    if (record_offset != -1) {
        const uint8_t *data = pdb->type_records;
        int32_t data_size = pdb->type_records_size;
        int32_t record_end = record_offset + pdb_type_record_size(pdb, type_index, record_offset);

        if ((record_end <= data_size) && (pdb_u16(data, data_size, record_offset + 2) == XX_MS_PDB_LF_METHODLIST)) {
            int32_t offset = record_offset + 4;
            int32_t i = 0;

            for (i = 0; (i < count) && ((offset + 8) <= record_end); i++) {
                xx_ms_pdb_cvfield record = {0};

                record.name = g_pdb_empty;
                record.kind = XX_MS_PDB_LF_ONEMETHOD;
                record.attr = pdb_u16(data, data_size, offset);
                record.type_index = pdb_u32(data, data_size, offset + 4);
                record.access = xx_ms_pdb_field_attr_to_access(record.attr);
                record.method_property = (uint8_t)((record.attr & XX_MS_PDB_FLD_ATTR_MPROP_MASK) >> XX_MS_PDB_FLD_ATTR_MPROP_SHIFT);
                record.is_static = (record.method_property == XX_MS_PDB_FLD_MPROP_STATIC);

                offset += 8;

                if ((record.method_property == XX_MS_PDB_FLD_MPROP_INTRO) || (record.method_property == XX_MS_PDB_FLD_MPROP_PUREINTRO)) {
                    /* XPDB reads the offset wherever it is, also behind the
                     * record. strict: an entry cut off by the end of its
                     * record ends the list. */
                    if (pdb->strict && ((offset + 4) > record_end)) {
                        break;
                    }

                    record.vtable_offset = pdb_u32(data, data_size, offset);
                    record.is_vtable_offset_valid = true;
                    offset += 4;
                }

                if (!pdb_list_reserve((void **)&out->items, &capacity, out->count, sizeof(xx_ms_pdb_cvfield))) {
                    xx_ms_pdb_cvfield_list_free(out);

                    return false;
                }

                out->items[out->count++] = record;
            }
        }
    }

    return true;
}

bool xx_ms_pdb_get_arg_list(xx_ms_pdb *pdb, uint32_t type_index, xx_ms_pdb_u32_list *out)
{
    size_t capacity = 0;
    int32_t record_offset = -1;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    if (!pdb->types_loaded) {
        xx_ms_pdb_load_types(pdb);
    }

    record_offset = pdb_type_record_offset(pdb, type_index);

    if (record_offset != -1) {
        const uint8_t *data = pdb->type_records;
        int32_t data_size = pdb->type_records_size;
        int32_t record_end = record_offset + pdb_type_record_size(pdb, type_index, record_offset);

        if ((record_end <= data_size) && (pdb_u16(data, data_size, record_offset + 2) == XX_MS_PDB_LF_ARGLIST)) {
            uint32_t count = pdb_u32(data, data_size, record_offset + 4);
            int32_t offset = record_offset + 8;
            uint32_t i = 0;

            for (i = 0; (i < count) && ((offset + 4) <= record_end); i++) {
                if (!pdb_list_reserve((void **)&out->items, &capacity, out->count, sizeof(uint32_t))) {
                    xx_ms_pdb_u32_list_free(out);

                    return false;
                }

                out->items[out->count++] = pdb_u32(data, data_size, offset);
                offset += 4;
            }
        }
    }

    return true;
}

/* ---- IPI records by index ------------------------------------------------------------------- */

/* The offsets of the IPI records: the walk of pdb_get_types over stream 4 */
static void pdb_load_ids(xx_ms_pdb *pdb)
{
    const xx_ms_pdb_tpi_info *ipi_info = NULL;
    uint8_t *data = NULL;
    int32_t data_size = 0;
    int32_t offset = 0;
    size_t capacity = 0;

    if (pdb->ids_loaded) {
        return;
    }

    pdb->ids_loaded = true;
    ipi_info = pdb_get_tpi_info(pdb, XX_MS_PDB_STREAM_IPI);

    if ((!ipi_info->is_valid) || (ipi_info->type_record_bytes == 0)) {
        return;
    }

    pdb->id_index_begin = ipi_info->type_index_begin;
    data = pdb_stream_region(pdb, XX_MS_PDB_STREAM_IPI, ipi_info->header_size, ipi_info->type_record_bytes, &data_size);

    while (data && (((int64_t)offset + 4) <= data_size) && pdb_not_canceled(pdb)) {
        uint16_t record_length = pdb_u16(data, data_size, offset);
        int32_t record_size = 0;

        if (record_length < 2) {
            break;
        }

        record_size = (int32_t)record_length + 2;

        if (((int64_t)offset + record_size) > data_size) {
            break;
        }

        if ((pdb->id_offset_count == UINT32_MAX) || !pdb_list_reserve((void **)&pdb->id_offsets, &capacity, pdb->id_offset_count, sizeof(uint32_t))) {
            break;
        }

        pdb->id_offsets[pdb->id_offset_count++] = ipi_info->header_size + (uint32_t)offset;
        offset += record_size;
    }

    if (data) {
        xx_mem_free(data);
    }

    if (!pdb_not_canceled(pdb)) {
        /* A stopped walk is not the list; the next call walks again */
        pdb->ids_loaded = false;
        pdb->id_offset_count = 0;
    }
}

/* Reads the record of an id index into pdb->id_record; its size, 0 if none */
static int32_t pdb_read_id_record(xx_ms_pdb *pdb, uint32_t id_index, int64_t *record_offset)
{
    uint32_t slot = 0;
    uint8_t header[4];
    int32_t record_size = 0;

    pdb_load_ids(pdb);

    if ((id_index < pdb->id_index_begin) || ((id_index - pdb->id_index_begin) >= pdb->id_offset_count)) {
        return 0;
    }

    slot = id_index - pdb->id_index_begin;

    if (pdb_stream_read(pdb, XX_MS_PDB_STREAM_IPI, pdb->id_offsets[slot], 4, header) != 4) {
        return 0;
    }

    record_size = (int32_t)pdb_u16(header, 4, 0) + 2;

    if (!pdb->id_record) {
        pdb->id_record = (uint8_t *)xx_mem_alloc(0x10002);

        if (!pdb->id_record) {
            return 0;
        }
    }

    if (pdb_stream_read(pdb, XX_MS_PDB_STREAM_IPI, pdb->id_offsets[slot], record_size, pdb->id_record) != record_size) {
        return 0;
    }

    *record_offset = pdb->id_offsets[slot];

    return record_size;
}

bool xx_ms_pdb_get_id_by_index(xx_ms_pdb *pdb, uint32_t id_index, xx_ms_pdb_id_record *out)
{
    const uint8_t *data = NULL;
    int32_t data_size = 0;
    int64_t record_offset = 0;

    if (!out) {
        return false;
    }

    xx_mem_zero(out, sizeof(*out));
    out->id_index = id_index;
    out->name = g_pdb_empty;

    if (!pdb) {
        return false;
    }

    data_size = pdb_read_id_record(pdb, id_index, &record_offset);

    if (data_size < 4) {
        return false;
    }

    data = pdb->id_record;

    out->is_valid = true;
    out->kind = pdb_u16(data, data_size, 2);
    out->record_offset = record_offset;
    out->record_size = data_size;

    if (out->kind == XX_MS_PDB_LF_FUNC_ID) {
        out->scope_index = pdb_u32(data, data_size, 4);
        out->type_index = pdb_u32(data, data_size, 8);
        out->name = pdb_string(pdb, data, data_size, 12, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (out->kind == XX_MS_PDB_LF_MFUNC_ID) {
        out->parent_type_index = pdb_u32(data, data_size, 4);
        out->type_index = pdb_u32(data, data_size, 8);
        out->name = pdb_string(pdb, data, data_size, 12, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (out->kind == XX_MS_PDB_LF_STRING_ID) {
        out->list_index = pdb_u32(data, data_size, 4);
        out->name = pdb_string(pdb, data, data_size, 8, PDB_NAME_MAX(pdb, 4096), NULL);
    } else if (out->kind == XX_MS_PDB_LF_UDT_SRC_LINE) {
        out->type_index = pdb_u32(data, data_size, 4);
        out->source_index = pdb_u32(data, data_size, 8);
        out->line = pdb_u32(data, data_size, 12);
    } else if (out->kind == XX_MS_PDB_LF_UDT_MOD_SRC_LINE) {
        out->type_index = pdb_u32(data, data_size, 4);
        out->source_index = pdb_u32(data, data_size, 8);
        out->line = pdb_u32(data, data_size, 12);
        out->module = pdb_u16(data, data_size, 16);
        out->name = pdb_names_string(pdb, out->source_index);
    } else if (out->kind == XX_MS_PDB_LF_BUILDINFO) {
        out->count = pdb_u16(data, data_size, 4);
    } else if (out->kind == XX_MS_PDB_LF_SUBSTR_LIST) {
        out->count = pdb_u32(data, data_size, 4);
    }

    return true;
}

bool xx_ms_pdb_get_id_list(xx_ms_pdb *pdb, uint32_t id_index, xx_ms_pdb_u32_list *out)
{
    size_t capacity = 0;
    int64_t record_offset = 0;
    int32_t data_size = 0;

    if (!out) {
        return false;
    }

    out->items = NULL;
    out->count = 0;

    if (!pdb) {
        return false;
    }

    data_size = pdb_read_id_record(pdb, id_index, &record_offset);

    if (data_size >= 4) {
        const uint8_t *data = pdb->id_record;
        uint16_t kind = pdb_u16(data, data_size, 2);
        uint32_t count = 0;
        int32_t offset = 0;
        uint32_t i = 0;

        if (kind == XX_MS_PDB_LF_BUILDINFO) {
            count = pdb_u16(data, data_size, 4);
            offset = 6;
        } else if (kind == XX_MS_PDB_LF_SUBSTR_LIST) {
            count = pdb_u32(data, data_size, 4);
            offset = 8;
        }

        for (i = 0; (i < count) && ((offset + 4) <= data_size); i++) {
            if (!pdb_list_reserve((void **)&out->items, &capacity, out->count, sizeof(uint32_t))) {
                xx_ms_pdb_u32_list_free(out);

                return false;
            }

            out->items[out->count++] = pdb_u32(data, data_size, offset);
            offset += 4;
        }
    }

    return true;
}

/* ---- result arrays ------------------------------------------------------------------------- */

#define PDB_LIST_FREE(function_name, list_type) \
    void function_name(list_type *list)         \
    {                                           \
        if (list) {                             \
            if (list->items) {                  \
                xx_mem_free(list->items);       \
            }                                   \
            list->items = NULL;                 \
            list->count = 0;                    \
        }                                       \
    }

PDB_LIST_FREE(xx_ms_pdb_symbol_list_free, xx_ms_pdb_symbol_list)
PDB_LIST_FREE(xx_ms_pdb_module_list_free, xx_ms_pdb_module_list)
PDB_LIST_FREE(xx_ms_pdb_cvfield_list_free, xx_ms_pdb_cvfield_list)
PDB_LIST_FREE(xx_ms_pdb_u32_list_free, xx_ms_pdb_u32_list)
PDB_LIST_FREE(xx_ms_pdb_stream_list_free, xx_ms_pdb_stream_list)
PDB_LIST_FREE(xx_ms_pdb_type_record_list_free, xx_ms_pdb_type_record_list)
PDB_LIST_FREE(xx_ms_pdb_section_header_list_free, xx_ms_pdb_section_header_list)
PDB_LIST_FREE(xx_ms_pdb_section_contrib_list_free, xx_ms_pdb_section_contrib_list)
