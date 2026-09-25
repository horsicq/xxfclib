/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PyInstaller one-file executable (CArchive).  xx_pyinstaller_one_executable.h
 * carries the cookie and TOC field tables.
 *
 * Written from PyInstaller's documented cookie and TOC layout.  The
 * acceptance rules (the TOC ends exactly at the cookie, the cookie is 88 or
 * 24 bytes, every member lies inside the data area in front of the TOC,
 * stored members have equal lengths, the compression flag is 0 or 1) follow
 * XArchive's archives/xpyinstallercarchive.cpp (MIT).  No code is taken
 * from it.
 *
 * The executable itself is parsed only as far as it takes to find the
 * cookie: the last bytes of the file, or - in a signed PE - the bytes in
 * front of the Authenticode blob named by the security data directory.
 * Nothing in the bootloader is executed or emulated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pyinstaller_one_executable/xx_pyinstaller_one_executable.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef PYINSTALLER_ONE_EXECUTABLE
#define XX_PYINSTALLER_ONE_EXECUTABLE_FILE_TYPE \
    XX_FILE_TYPE_PYINSTALLER_ONE_EXECUTABLE
#else
#define XX_PYINSTALLER_ONE_EXECUTABLE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PYI_MAGIC_SIZE 8
#define PYI_COOKIE_V20 24
#define PYI_COOKIE_V21 88
#define PYI_LIBNAME_SIZE 64
/* The cookie is searched in the last PYI_TAIL_WINDOW bytes of the file, and
 * in the PYI_CERT_WINDOW bytes in front of a PE certificate table.  A
 * PyInstaller build puts nothing behind the cookie but that table (and at
 * most the 8-byte alignment padding in front of it), so the windows only
 * have to absorb padding; they also bound the probe to two small reads. */
#define PYI_TAIL_WINDOW 4096
#define PYI_CERT_WINDOW 512
/* Magic hits tried per window before the search gives up. */
#define PYI_MAX_CANDIDATES 16
#define PYI_ENTRY_FIXED 18
/* 1023 entries take 57 KiB of TOC; 16 MiB is far beyond any real build and
 * is the most the probe will ever allocate. */
#define PYI_MAX_TOC (16 * 1024 * 1024)
#define PYI_MAX_ENTRIES 262144
/* Names longer than this are listed but never written. */
#define PYI_MAX_NAME 4096
#define PYI_COPY_CHUNK 65536U
#define PYI_POLL_MASK 0x3ffU
/* A zlib stream is at least its 2-byte header, one Deflate block (2 bytes
 * for an empty fixed block) and the 4-byte Adler-32. */
#define PYI_MIN_ZLIB 8
/* Suffixes tried on a duplicate name before the member is refused. */
#define PYI_MAX_RENAMES 16
#define PYI_METHOD_STORE 0U
#define PYI_METHOD_DEFLATE 8U

static const uint8_t pyi_magic[PYI_MAGIC_SIZE] = {'M',  'E',  'I',  0x0c,
                                                  0x0b, 0x0a, 0x0b, 0x0e};

typedef struct pyi_layout_s {
    int64_t size;          /**< Device bytes from base_address. */
    int64_t cookie;        /**< Cookie start, relative to base. */
    int64_t cookie_size;   /**< 88 or 24. */
    int64_t package;       /**< Package start, relative to base. */
    int64_t data_size;     /**< Member data area: package .. TOC. */
    int64_t toc;           /**< TOC start, relative to base. */
    int64_t toc_size;
    int64_t format_size;   /**< Cookie end, or the certificate end. */
    uint32_t python_version;
    char python_library[PYI_LIBNAME_SIZE + 1];
} pyi_layout;

typedef struct pyi_member_s {
    int64_t entry_offset;  /**< Absolute offset of the TOC entry. */
    int64_t entry_size;
    int64_t data_offset;   /**< Absolute offset of the stored bytes. */
    int64_t packed_size;
    int64_t size;
    uint32_t entry_index;
    uint8_t compressed;
    char type;
    bool unsafe;           /**< Listed, never written. */
    char *name;            /**< Final ('/'-separated, de-duplicated) name. */
} pyi_member;

typedef struct pyi_stream_s {
    pyi_member *items;
    size_t count;
    size_t index;
    pyi_layout layout;
} pyi_stream;

/* A write-only device that counts, checksums and caps what the Deflate
 * decoder produces, forwarding it to the real destination (or nowhere). */
typedef struct pyi_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t limit;
    uint64_t count;
    uint32_t adler;
    bool failed;
} pyi_sink;

static void pyi_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t pyi_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint32_t pyi_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint16_t pyi_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static bool pyi_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool pyi_write_all(xx_io_device *device, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static char pyi_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* ----------------------------------------------------- cookie location -- */

/* The 64-byte library name of a 2.1+ cookie: a printable name that ends
 * inside the field. */
static bool pyi_library_name(const uint8_t *field, char *out) {
    size_t index;
    for (index = 0U; index < PYI_LIBNAME_SIZE; ++index) {
        if (field[index] == 0U) break;
        if (field[index] < 0x20U || field[index] > 0x7eU) return false;
        out[index] = (char)field[index];
    }
    if (index == 0U || index == PYI_LIBNAME_SIZE) return false;
    out[index] = 0;
    return true;
}

/* Validate a cookie at @p relative (from base), with @p available bytes of
 * it in @p cookie before the searched region ends.  The TOC ends where the
 * cookie begins, so the package length fixes the cookie size. */
static bool pyi_try_cookie(const uint8_t *cookie, size_t available,
                           int64_t relative, pyi_layout *out) {
    uint32_t package_length, toc_offset, toc_length, python_version;
    uint64_t cookie_size;
    pyi_layout layout;
    if (available < PYI_COOKIE_V20 ||
        xx_rt_memcmp(cookie, pyi_magic, PYI_MAGIC_SIZE) != 0)
        return false;
    package_length = pyi_be32(cookie + 8);
    toc_offset = pyi_be32(cookie + 12);
    toc_length = pyi_be32(cookie + 16);
    python_version = pyi_be32(cookie + 20);
    if ((uint64_t)toc_offset + toc_length > package_length) return false;
    cookie_size = (uint64_t)package_length - toc_offset - toc_length;
    if (cookie_size != PYI_COOKIE_V21 && cookie_size != PYI_COOKIE_V20)
        return false;
    if (cookie_size > available) return false;
    /* One entry at least, and a TOC this reader is willing to hold. */
    if (toc_length < PYI_ENTRY_FIXED || toc_length > PYI_MAX_TOC) return false;
    /* 20..39 in the old major*10+minor form, 300.. in major*100+minor. */
    if (python_version < 10U || python_version > 9999U) return false;
    xx_mem_zero(&layout, sizeof(layout));
    if (cookie_size == PYI_COOKIE_V21 &&
        !pyi_library_name(cookie + PYI_COOKIE_V20, layout.python_library))
        return false;
    if ((int64_t)package_length > relative + (int64_t)cookie_size)
        return false;
    layout.cookie = relative;
    layout.cookie_size = (int64_t)cookie_size;
    layout.package = relative + (int64_t)cookie_size - (int64_t)package_length;
    layout.data_size = (int64_t)toc_offset;
    layout.toc = layout.package + (int64_t)toc_offset;
    layout.toc_size = (int64_t)toc_length;
    layout.python_version = python_version;
    if (layout.package < 0 || layout.toc + layout.toc_size != relative)
        return false;
    *out = layout;
    return true;
}

/* Search [end - window, end) backwards for a cookie that ends inside it. */
static bool pyi_search(xx_io_device *device, int64_t base, int64_t end,
                       size_t window, pyi_layout *out) {
    uint8_t *buffer;
    int64_t start;
    size_t length, position;
    unsigned tries = 0U;
    bool found = false;
    if (end < PYI_COOKIE_V20) return false;
    length = end < (int64_t)window ? (size_t)end : window;
    start = end - (int64_t)length;
    buffer = (uint8_t *)xx_mem_alloc(length);
    if (!buffer) return false;
    if (pyi_read_at(device, base + start, buffer, length)) {
        position = length - PYI_COOKIE_V20 + 1U;
        while (position-- > 0U && tries < PYI_MAX_CANDIDATES) {
            if (buffer[position] != 'M' ||
                xx_rt_memcmp(buffer + position, pyi_magic, PYI_MAGIC_SIZE) !=
                    0)
                continue;
            ++tries;
            if (pyi_try_cookie(buffer + position, length - position,
                               start + (int64_t)position, out)) {
                found = true;
                break;
            }
        }
    }
    xx_mem_free(buffer);
    return found;
}

/* In a signed PE the certificate table follows the package.  Returns its
 * file offset and size from the security data directory, or false. */
static bool pyi_pe_certificate(xx_io_device *device, int64_t base,
                               int64_t size, int64_t *offset,
                               int64_t *length) {
    uint8_t header[64];
    uint8_t nt[24 + 2];
    uint8_t directory[8];
    uint32_t pe_offset, rva_count_at, security_at, count, cert_offset,
        cert_size;
    uint16_t optional_size, optional_magic;
    if (size < 0x40 || !pyi_read_at(device, base, header, sizeof(header)) ||
        header[0] != 'M' || header[1] != 'Z')
        return false;
    pe_offset = pyi_le32(header + 0x3c);
    if (pe_offset < 0x40U || (int64_t)pe_offset > size - (int64_t)sizeof(nt) ||
        !pyi_read_at(device, base + pe_offset, nt, sizeof(nt)) ||
        xx_rt_memcmp(nt, "PE\0\0", 4U) != 0)
        return false;
    optional_size = pyi_le16(nt + 20);
    optional_magic = pyi_le16(nt + 24);
    if (optional_magic == 0x10bU) {
        rva_count_at = 92U;
        security_at = 128U;
    } else if (optional_magic == 0x20bU) {
        rva_count_at = 108U;
        security_at = 144U;
    } else {
        return false;
    }
    if (optional_size < security_at + 8U ||
        (int64_t)pe_offset + 24 + security_at + 8 > size ||
        !pyi_read_at(device, base + pe_offset + 24 + rva_count_at, directory,
                     4U))
        return false;
    count = pyi_le32(directory);
    if (count < 5U ||
        !pyi_read_at(device, base + pe_offset + 24 + security_at, directory,
                     8U))
        return false;
    cert_offset = pyi_le32(directory);
    cert_size = pyi_le32(directory + 4);
    if (cert_offset == 0U || cert_size == 0U ||
        (int64_t)cert_offset + (int64_t)cert_size > size)
        return false;
    *offset = (int64_t)cert_offset;
    *length = (int64_t)cert_size;
    return true;
}

static bool pyi_locate(Abstractformat *format, pyi_layout *out) {
    int64_t total, size, cert_offset = 0, cert_size = 0;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < PYI_COOKIE_V20 + PYI_ENTRY_FIXED) return false;
    if (pyi_search(format->device, format->base_address, size,
                   PYI_TAIL_WINDOW, out)) {
        out->size = size;
        out->format_size = out->cookie + out->cookie_size;
        return true;
    }
    if (pyi_pe_certificate(format->device, format->base_address, size,
                           &cert_offset, &cert_size) &&
        pyi_search(format->device, format->base_address, cert_offset,
                   PYI_CERT_WINDOW, out)) {
        out->size = size;
        out->format_size = cert_offset + cert_size;
        return true;
    }
    return false;
}

/* --------------------------------------------------------------- TOC ---- */

static bool pyi_load_toc(Abstractformat *format, const pyi_layout *layout,
                         uint8_t **toc) {
    uint8_t *buffer = (uint8_t *)xx_mem_alloc((size_t)layout->toc_size);
    if (!buffer) return false;
    if (!pyi_read_at(format->device, format->base_address + layout->toc,
                     buffer, (size_t)layout->toc_size)) {
        xx_mem_free(buffer);
        return false;
    }
    *toc = buffer;
    return true;
}

/* 'o' runtime options and 'd' dependency references are bootloader
 * settings, not files. */
static bool pyi_is_file_type(char type) { return type != 'o' && type != 'd'; }

/* Walk the whole TOC.  Counts all entries and the file entries; with
 * @p items it also fills one pyi_member per file entry (names excluded). */
static bool pyi_walk(Abstractformat *format, const pyi_layout *layout,
                     const uint8_t *toc, pyi_member *items, size_t *entries,
                     size_t *files, xx_pd_struct *pd) {
    int64_t position = 0;
    size_t count = 0U, kept = 0U;
    while (position < layout->toc_size) {
        const uint8_t *entry = toc + position;
        uint32_t entry_size, data_offset, packed, size;
        uint8_t flag;
        char type;
        int64_t name_field;
        if ((count & PYI_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        if (layout->toc_size - position < PYI_ENTRY_FIXED ||
            count >= PYI_MAX_ENTRIES)
            return false;
        entry_size = pyi_be32(entry);
        data_offset = pyi_be32(entry + 4);
        packed = pyi_be32(entry + 8);
        size = pyi_be32(entry + 12);
        flag = entry[16];
        type = (char)entry[17];
        if (entry_size < PYI_ENTRY_FIXED + 1U ||
            (int64_t)entry_size > layout->toc_size - position)
            return false;
        if (flag > 1U ||
            !((type >= 'a' && type <= 'z') || (type >= 'A' && type <= 'Z')))
            return false;
        if ((int64_t)data_offset > layout->data_size ||
            (int64_t)packed > layout->data_size - (int64_t)data_offset)
            return false;
        if (flag == 0U ? packed != size : packed < PYI_MIN_ZLIB) return false;
        /* The name is NUL-terminated inside its field. */
        name_field = (int64_t)entry_size - PYI_ENTRY_FIXED;
        if (xx_rt_memchr(entry + PYI_ENTRY_FIXED, 0, (size_t)name_field) ==
            NULL)
            return false;
        if (pyi_is_file_type(type)) {
            if (items) {
                pyi_member *member = &items[kept];
                member->entry_offset =
                    format->base_address + layout->toc + position;
                member->entry_size = (int64_t)entry_size;
                member->data_offset =
                    format->base_address + layout->package + data_offset;
                member->packed_size = (int64_t)packed;
                member->size = (int64_t)size;
                member->entry_index = (uint32_t)count;
                member->compressed = flag;
                member->type = type;
                member->unsafe = false;
                member->name = NULL;
            }
            ++kept;
        }
        ++count;
        position += (int64_t)entry_size;
    }
    if (count == 0U) return false;
    if (entries) *entries = count;
    if (files) *files = kept;
    return true;
}

/* ------------------------------------------------------------- names ---- */

/* Well-formed UTF-8 without overlongs, surrogates or code points past
 * U+10FFFF. */
static bool pyi_utf8_valid(const uint8_t *text, size_t length) {
    size_t index = 0U;
    while (index < length) {
        uint8_t lead = text[index];
        size_t extra, k;
        uint32_t cp;
        if (lead < 0x80U) {
            ++index;
            continue;
        }
        if (lead >= 0xc2U && lead <= 0xdfU) {
            extra = 1U;
            cp = lead & 0x1fU;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            extra = 2U;
            cp = lead & 0x0fU;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            extra = 3U;
            cp = lead & 0x07U;
        } else {
            return false;
        }
        if (extra > length - index - 1U) return false;
        for (k = 1U; k <= extra; ++k) {
            if ((text[index + k] & 0xc0U) != 0x80U) return false;
            cp = (cp << 6U) | (text[index + k] & 0x3fU);
        }
        if ((extra == 2U && (cp < 0x800U || (cp >= 0xd800U && cp <= 0xdfffU))) ||
            (extra == 3U && (cp < 0x10000U || cp > 0x10ffffU)))
            return false;
        index += extra + 1U;
    }
    return true;
}

/* True when the component's stem (up to the first '.', trailing spaces
 * dropped) is a Windows device name: CON, PRN, AUX, NUL, CONIN$, CONOUT$,
 * CLOCK$, COM0-9 and LPT0-9, the last two also with a superscript 1-3. */
static bool pyi_is_device(const char *component, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index, k;
    while (stem < length && component[stem] != '.') ++stem;
    while (stem > 0U && component[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        const char *word = devices[index];
        for (k = 0U; k < stem && word[k]; ++k)
            if (pyi_upper(component[k]) != word[k]) break;
        if (k == stem && word[k] == 0) return true;
    }
    if (stem >= 4U &&
        ((pyi_upper(component[0]) == 'C' && pyi_upper(component[1]) == 'O' &&
          pyi_upper(component[2]) == 'M') ||
         (pyi_upper(component[0]) == 'L' && pyi_upper(component[1]) == 'P' &&
          pyi_upper(component[2]) == 'T'))) {
        if (stem == 4U && component[3] >= '0' && component[3] <= '9')
            return true;
        if (stem == 5U && (uint8_t)component[3] == 0xc2U &&
            ((uint8_t)component[4] == 0xb9U ||
             (uint8_t)component[4] == 0xb2U ||
             (uint8_t)component[4] == 0xb3U))
            return true;
    }
    return false;
}

/* A '/'-separated relative path that stays inside the extraction directory
 * on every host: no root, no drive or stream colon, no empty, "." or ".."
 * component, no control or reserved character, no component Windows would
 * silently strip ("name." / "name ") and no device name. */
static bool pyi_path_safe(const char *name) {
    size_t length, start = 0U, index;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (length > PYI_MAX_NAME || name[0] == '/') return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c < 0x20U || c == 0x7fU || c == ':' || c == '\\' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    for (index = 0U; index <= length; ++index) {
        if (index == length || name[index] == '/') {
            size_t size = index - start;
            const char *component = name + start;
            if (size == 0U) return false;
            if (component[size - 1U] == '.' || component[size - 1U] == ' ')
                return false; /* also refuses "." and ".." */
            if (pyi_is_device(component, size)) return false;
            start = index + 1U;
        }
    }
    return true;
}

/* The TOC name as a '/'-separated string; a name that is empty, too long
 * or not UTF-8 comes back as "entry_<index>_<type>.bin" instead. */
static char *pyi_convert_name(const uint8_t *raw, size_t length,
                              const pyi_member *member) {
    char *name;
    size_t index;
    if (length == 0U || length > PYI_MAX_NAME ||
        !pyi_utf8_valid(raw, length)) {
        char synthetic[48];
        (void)xx_rt_snprintf(synthetic, sizeof(synthetic),
                             "entry_%05u_%c.bin", member->entry_index,
                             member->type);
        return xx_str_dup(synthetic);
    }
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index)
        name[index] = raw[index] == '\\' ? '/' : (char)raw[index];
    name[length] = 0;
    return name;
}

static uint64_t pyi_hash(const char *name) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (; *name; ++name) {
        hash ^= (uint8_t)pyi_upper(*name);
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

static bool pyi_same(const char *left, const char *right) {
    while (*left && pyi_upper(*left) == pyi_upper(*right)) {
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

typedef struct pyi_names_s {
    const char **slots;
    size_t mask;
} pyi_names;

static bool pyi_names_contains(const pyi_names *table, const char *name) {
    size_t slot = (size_t)pyi_hash(name) & table->mask;
    while (table->slots[slot]) {
        if (pyi_same(table->slots[slot], name)) return true;
        slot = (slot + 1U) & table->mask;
    }
    return false;
}

static void pyi_names_add(pyi_names *table, const char *name) {
    size_t slot = (size_t)pyi_hash(name) & table->mask;
    while (table->slots[slot]) slot = (slot + 1U) & table->mask;
    table->slots[slot] = name;
}

/* "<stem>_<entry index>[_k]<extension>", the extension being the last
 * component's final ".xxx" when it has one that is not its first byte. */
static char *pyi_suffixed(const char *name, uint32_t index, unsigned round) {
    size_t length = xx_str_len(name), dot = length, cut;
    char suffix[32];
    char *result;
    size_t suffix_length;
    const char *slash = xx_str_rchr(name, '/');
    size_t component = slash ? (size_t)(slash - name) + 1U : 0U;
    for (cut = length; cut > component + 1U; --cut) {
        if (name[cut - 1U] == '.') {
            dot = cut - 1U;
            break;
        }
    }
    if (round == 0U)
        (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u", index);
    else
        (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u_%u", index, round);
    suffix_length = xx_str_len(suffix);
    result = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, name, dot);
    xx_rt_memcpy(result + dot, suffix, suffix_length);
    xx_rt_memcpy(result + dot + suffix_length, name + dot, length - dot);
    result[length + suffix_length] = 0;
    return result;
}

/* Give every member its final name.  The first holder of a name (ASCII
 * case folded, as on Windows) keeps it; each later one gets its entry index
 * inserted, so no two members can ever write the same file. */
static bool pyi_assign_names(Abstractformat *format, pyi_stream *stream,
                             const uint8_t *toc) {
    pyi_names table;
    size_t capacity = 16U, index;
    bool ok = true;
    while (capacity < stream->count * 2U + 2U) capacity <<= 1U;
    table.slots = (const char **)xx_mem_calloc(capacity, sizeof(char *));
    if (!table.slots) return false;
    table.mask = capacity - 1U;
    for (index = 0U; index < stream->count && ok; ++index) {
        pyi_member *member = &stream->items[index];
        const uint8_t *raw = toc + (member->entry_offset -
                                    format->base_address - stream->layout.toc) +
                             PYI_ENTRY_FIXED;
        size_t field = (size_t)member->entry_size - PYI_ENTRY_FIXED;
        const uint8_t *end = (const uint8_t *)xx_rt_memchr(raw, 0, field);
        size_t length = end ? (size_t)(end - raw) : field;
        bool clash = false;
        unsigned round;
        char *name = pyi_convert_name(raw, length, member);
        if (!name) {
            ok = false;
            break;
        }
        if (pyi_names_contains(&table, name)) {
            /* Every retry is built from the name as the TOC gives it. */
            clash = true;
            for (round = 0U; round < PYI_MAX_RENAMES; ++round) {
                char *renamed = pyi_suffixed(name, member->entry_index, round);
                if (!renamed) {
                    ok = false;
                    break;
                }
                if (!pyi_names_contains(&table, renamed)) {
                    xx_str_free(name);
                    name = renamed;
                    clash = false;
                    break;
                }
                xx_str_free(renamed);
            }
            if (!ok) {
                xx_str_free(name);
                break;
            }
        }
        /* A name still clashing is listed but never written, so nothing is
         * ever overwritten. */
        member->name = name;
        member->unsafe = clash || !pyi_path_safe(name);
        if (!clash) pyi_names_add(&table, name);
    }
    xx_mem_free((void *)table.slots);
    return ok;
}

/* ------------------------------------------------------------ parsing --- */

static void pyi_stream_free(void *opaque) {
    pyi_stream *stream = (pyi_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Locate and walk.  With @p names false this is the probe: nothing is kept
 * but the counts. */
static bool pyi_parse(Abstractformat *format, pyi_layout *layout,
                      size_t *entries, size_t *files, pyi_stream **result,
                      xx_pd_struct *pd) {
    uint8_t *toc = NULL;
    pyi_stream *stream = NULL;
    size_t entry_count = 0U, file_count = 0U;
    bool ok = false;
    if (!pyi_locate(format, layout) || !pyi_load_toc(format, layout, &toc))
        return false;
    if (!pyi_walk(format, layout, toc, NULL, &entry_count, &file_count, pd))
        goto done;
    if (result) {
        stream = (pyi_stream *)xx_mem_calloc(1U, sizeof(*stream));
        if (!stream) goto done;
        stream->layout = *layout;
        if (file_count != 0U) {
            stream->items =
                (pyi_member *)xx_mem_calloc(file_count, sizeof(pyi_member));
            if (!stream->items) goto done;
            if (!pyi_walk(format, layout, toc, stream->items, NULL, NULL, pd))
                goto done;
            stream->count = file_count;
            if (!pyi_assign_names(format, stream, toc)) goto done;
        }
        *result = stream;
        stream = NULL;
    }
    if (entries) *entries = entry_count;
    if (files) *files = file_count;
    ok = true;
done:
    if (stream) pyi_stream_free(stream);
    xx_mem_free(toc);
    return ok;
}

/* ------------------------------------------------------------ decoding -- */

static ssize_t pyi_sink_write(xx_io_device *self, const void *buffer,
                              size_t size) {
    pyi_sink *sink = (pyi_sink *)self->priv;
    if (!sink || sink->failed || (!buffer && size != 0U)) return -1;
    if ((uint64_t)size > sink->limit - sink->count) {
        sink->failed = true; /* more than the TOC promised */
        return -1;
    }
    sink->adler = xx_adler32_update(sink->adler, buffer, size);
    if (sink->target &&
        !pyi_write_all(sink->target, (const uint8_t *)buffer, size)) {
        sink->failed = true;
        return -1;
    }
    sink->count += (uint64_t)size;
    return (ssize_t)size;
}

static bool pyi_copy(xx_io_device *source, int64_t offset, int64_t size,
                     xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0;
    bool ok = true;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(PYI_COPY_CHUNK);
    if (!buffer) return false;
    while (done < size) {
        size_t chunk = size - done > (int64_t)PYI_COPY_CHUNK
                           ? (size_t)PYI_COPY_CHUNK
                           : (size_t)(size - done);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !pyi_read_at(source, offset + done, buffer, chunk) ||
            (destination && !pyi_write_all(destination, buffer, chunk))) {
            ok = false;
            break;
        }
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* Stream one member to @p destination (NULL: decode and discard).  A zlib
 * member must produce exactly the TOC's unpacked length and match its own
 * Adler-32 trailer; the decoder never gets to write past that length. */
static bool pyi_decode(Abstractformat *format, const pyi_member *member,
                       xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t header[2], trailer[4];
    pyi_sink sink;
    bool ok;
    if (member->compressed == 0U)
        return pyi_copy(format->device, member->data_offset, member->size,
                        destination, pd);
    if (member->packed_size < PYI_MIN_ZLIB ||
        !pyi_read_at(format->device, member->data_offset, header, 2U) ||
        !xx_zlib_stream_header_is_valid(header, 2U) ||
        !pyi_read_at(format->device,
                     member->data_offset + member->packed_size - 4, trailer,
                     4U))
        return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device.write = pyi_sink_write;
    sink.device.priv = &sink;
    sink.target = destination;
    sink.limit = (uint64_t)member->size;
    sink.adler = XX_ADLER32_INIT;
    ok = xx_deflate_unpack_device(format->device, member->data_offset + 2,
                                  member->packed_size - 2, &sink.device,
                                  false, pd);
    return ok && !sink.failed && sink.count == (uint64_t)member->size &&
           sink.adler == pyi_be32(trailer);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_pyinstaller_one_executable_init(xx_pyinstaller_one_executable *archive,
                                        xx_io_device *device,
                                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_PYINSTALLER_ONE_EXECUTABLE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pyinstaller");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_pyinstaller_one_executable_check_is_valid;
    archive->format.handle_base_info =
        xx_pyinstaller_one_executable_handle_base_info;
    archive->format.get_format_size =
        xx_pyinstaller_one_executable_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pyinstaller_one_executable_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pyinstaller_one_executable_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pyinstaller_one_executable_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pyinstaller_one_executable_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pyinstaller_one_executable_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pyinstaller_one_executable_free_archive_records_reading;
    archive->format.destroy = pyi_vtable_destroy;
    archive->package_offset = -1;
    archive->cookie_offset = -1;
}

xx_pyinstaller_one_executable *xx_pyinstaller_one_executable_create(
    xx_io_device *device, int64_t base_address) {
    xx_pyinstaller_one_executable *archive =
        (xx_pyinstaller_one_executable *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_pyinstaller_one_executable_init(archive, device,
                                                    base_address);
    return archive;
}

void xx_pyinstaller_one_executable_destroy(
    xx_pyinstaller_one_executable *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_pyinstaller_one_executable_free(
    xx_pyinstaller_one_executable *archive) {
    if (!archive) return;
    xx_pyinstaller_one_executable_destroy(archive);
    xx_mem_free(archive);
}

static void pyi_vtable_destroy(Abstractformat *self) {
    xx_pyinstaller_one_executable_destroy(
        (xx_pyinstaller_one_executable *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_pyinstaller_one_executable_check_is_valid(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    pyi_layout layout;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return pyi_parse(self, &layout, NULL, NULL, NULL, pd);
}

bool xx_pyinstaller_one_executable_handle_base_info(Abstractformat *self,
                                                    xx_pd_struct *pd) {
    xx_pyinstaller_one_executable *archive =
        (xx_pyinstaller_one_executable *)self;
    pyi_layout layout;
    size_t entries = 0U, files = 0U;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    if (!pyi_parse(self, &layout, &entries, &files, NULL, pd)) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = layout.format_size;
    self->number_of_archive_records = files;
    archive->number_of_records = files;
    archive->number_of_entries = entries;
    archive->package_offset = layout.package;
    archive->cookie_offset = layout.cookie;
    archive->cookie_size = (uint32_t)layout.cookie_size;
    archive->python_version = layout.python_version;
    xx_rt_memcpy(archive->python_library, layout.python_library,
                 sizeof(archive->python_library));
    return true;
}

int64_t xx_pyinstaller_one_executable_get_format_size(Abstractformat *self,
                                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled &&
         !xx_pyinstaller_one_executable_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pyinstaller_one_executable_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled &&
         !xx_pyinstaller_one_executable_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid
               ? ((xx_pyinstaller_one_executable *)self)->number_of_records
               : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool pyi_set_record(xx_archive_record *record,
                           const pyi_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->entry_offset;
    record->header_size = member->entry_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSION_METHOD,
               member->compressed ? PYI_METHOD_DEFLATE : PYI_METHOD_STORE) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool pyi_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *pyi_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *
xx_pyinstaller_one_executable_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    pyi_layout layout;
    pyi_stream *stream = NULL;
    xx_archive_record_state *state;
    if (!self || !self->device ||
        !pyi_parse(self, &layout, NULL, NULL, &stream, pd))
        return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pyi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = pyi_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!pyi_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !pyi_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *
xx_pyinstaller_one_executable_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pyinstaller_one_executable_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    pyi_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (pyi_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        pyi_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pyinstaller_one_executable_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    pyi_stream *stream;
    const pyi_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (pyi_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    path_option = pyi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return pyi_decode(self, member, NULL, pd);
    if (member->unsafe || !member->name) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = pyi_decode(self, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(path);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_pyinstaller_one_executable_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
