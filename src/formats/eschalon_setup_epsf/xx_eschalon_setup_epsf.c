/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Eschalon Setup 3 "EPSF" self-extractor.  xx_eschalon_setup_epsf.h has the
 * header layout.
 *
 * The locating rules follow XArchive sfx/xepsfsfx.cpp (MIT, same author):
 * the 18-byte header is accepted only at the PE overlay offset, never at an
 * incidental "EPSF" (the stub itself carries that string in its code), and
 * the ARCV 4.00 container is found by searching for "ARCV" 00 04 behind the
 * runtime, at most 16 candidates tried.  Nothing in the executable is run or
 * emulated; only the DOS header, the COFF header and the section table are
 * read.
 *
 * Each candidate first gets a constant-cost gate (the fixed ARCV4 header
 * fill, the first FILE prologue and the first DATA header).  Only the first
 * candidate that passes it is parsed in full by the ARCV4 reader, whose walk
 * can cost seconds on a hostile 100,000-record chain; it is never repeated
 * for later candidates.  If that full parse fails, the container is taken as
 * truncated or damaged and is listed as PRODUCT_ARCHIVE.ARCV, a member that
 * cannot be extracted, instead of being dropped silently.  The layout found
 * by handle_base_info is reused when the records are created.
 *
 * Unlike the XArchive module this reader also publishes the setup script.
 * Its size is recorded nowhere, but the ARCV4 method-2 stream carries an
 * end symbol: decoding into a buffer that is large enough stops there, and a
 * second decode into a buffer of exactly that size proves it (the codec
 * reports success only when the end symbol lands on the last output byte).
 *
 * The ARCV4 members are enumerated through xx_arcv4 on the same device at
 * the container's offset, so every gate, name rule, method and CRC is the
 * ARCV4 reader's.  They are extracted here rather than through that reader
 * so that names can be kept unique across the whole carrier: SETUPMN.DLL and
 * the script come first and any later duplicate (ASCII case-insensitive)
 * gets a "_<n>" suffix (the taken names are kept in a balanced tree, so the
 * check costs O(log n) whatever the names are).  A setup-script stream that
 * does not decode is listed as a SETUP_SCRIPT.DFM that cannot be extracted.
 * A name that aliases a Windows device is listed but
 * not extracted.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/formats/eschalon_setup_epsf/xx_eschalon_setup_epsf.h"

#include "xxfclib/algo/arcv4/xx_arcv4.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/formats/arcv4/xx_arcv4.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef ESCHALON_SETUP_EPSF

#define XX_ESCHALON_SETUP_EPSF_FILE_TYPE XX_FILE_TYPE_ESCHALON_SETUP_EPSF
#else
#define XX_ESCHALON_SETUP_EPSF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define EPSF_HEADER_SIZE 18U
#define EPSF_VERSION 3U
#define EPSF_DOS_HEADER_SIZE 64U
#define EPSF_NT_HEADER_SIZE 26U /* "PE\0\0", COFF header, optional magic */
#define EPSF_SECTION_SIZE 40U
#define EPSF_MAX_SECTIONS 96U
/* SETUPMN.DLL is 512-528 KiB on every known build; the ceiling only stops a
 * corrupt header from asking for an unbounded buffer. */
#define EPSF_MAX_RUNTIME UINT32_C(0x4000000)
/* One method-2 match emits at most 500 bytes and costs at least five bits,
 * so no stream expands by more than 800:1. */
#define EPSF_MAX_RATIO 1024U
#define EPSF_MAX_MATCH 500U
#define EPSF_MAX_MEMBER UINT64_C(0x10000000)
/* The script is 4-16 KiB packed on every known carrier. */
#define EPSF_SEARCH_WINDOW INT64_C(0x1000000)
#define EPSF_SEARCH_CHUNK 65536U
#define EPSF_TAG_SIZE 6U
#define EPSF_MAX_CANDIDATES 16U
#define EPSF_SCRIPT_MAX_INPUT UINT32_C(0x400000)
#define EPSF_SCRIPT_FIRST_OUTPUT ((size_t)0x40000)
#define EPSF_SCRIPT_MAX_OUTPUT ((size_t)0x1000000)
#define EPSF_MAX_ARCHIVE_RECORDS 100000U
#define EPSF_COPY_CHUNK 65536U

#define EPSF_KIND_RUNTIME 0U
#define EPSF_KIND_SCRIPT 1U
#define EPSF_KIND_ARCV 2U
#define EPSF_KIND_DAMAGED 3U
#define EPSF_KIND_SCRIPT_DAMAGED 4U

/* The fixed parts of an ARCV 4.00 container, used by the cheap gate. */
#define EPSF_ARCV_HEADER_SIZE 0x79cU
#define EPSF_ARCV_ZERO_OFFSET 0x08U
#define EPSF_ARCV_ZERO_SIZE 0x84U
#define EPSF_ARCV_AA_OFFSET 0x9cU
#define EPSF_ARCV_AA_SIZE 64U
#define EPSF_ARCV_TAIL_ZERO_OFFSET 0x59cU
#define EPSF_ARCV_TAIL_ZERO_SIZE 16U
#define EPSF_ARCV_PROLOGUE_SIZE 16U
#define EPSF_ARCV_DATA_SIZE 32U
#define EPSF_ARCV_MAX_BODY 65536U

static const char epsf_runtime_name[] = "SETUPMN.DLL";
static const char epsf_script_name[] = "SETUP_SCRIPT.DFM";
static const char epsf_damaged_name[] = "PRODUCT_ARCHIVE.ARCV";
static const uint8_t epsf_arcv4_tag[EPSF_TAG_SIZE] = { 'A', 'R', 'C', 'V',
                                                       0x00U, 0x04U };

typedef struct epsf_layout_s {
    int64_t end;             /* device size */
    int64_t header_offset;   /* absolute offsets from here on */
    uint32_t runtime_size;
    uint32_t runtime_packed;
    uint32_t runtime_sum;
    int64_t runtime_offset;
    int64_t script_offset;
    int64_t script_region;
    uint64_t script_size;
    int64_t archive_offset;
    int64_t archive_size;
    uint64_t archive_records;
    int64_t damaged_offset;  /* gated but unparsable container, or -1 */
} epsf_layout;

typedef struct epsf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    uint64_t packed_size;
    uint64_t original_size;
    uint64_t timestamp;
    uint32_t kind;
    uint32_t method;
    uint32_t check;
    uint32_t attributes;
    bool safe;
} epsf_member;

typedef struct epsf_stream_s {
    epsf_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    epsf_layout layout;
} epsf_stream;

/* Names already taken, in an AVL tree ordered by xx_str_icmp.  A hash table
 * with a fixed, public hash lets a hostile carrier make every name collide
 * (O(n^2) compares over 100,000 records); the tree is O(log n) per lookup
 * whatever the names are.  Node 0 is the empty link. */
typedef struct epsf_name_node_s {
    const char *name;
    uint32_t left;
    uint32_t right;
    uint32_t height;
} epsf_name_node;

typedef struct epsf_names_s {
    epsf_name_node *nodes;
    uint32_t root;
    uint32_t count;
    uint32_t capacity; /* usable nodes, 1..capacity */
} epsf_names;

#include "xxfclib/global/xx_global.h"
static size_t gb_eschalon_setup_epsf_capacity(void) {
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}
static ssize_t gb_eschalon_setup_epsf_read(xx_io_device *device, void *buffer, size_t size, size_t capacity) {
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}
static ssize_t gb_eschalon_setup_epsf_write(xx_io_device *device, const void *buffer, size_t size, size_t capacity) {
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_write(device, (const uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}


static uint16_t epsf_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t epsf_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool epsf_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    const size_t file_io_capacity = gb_eschalon_setup_epsf_capacity();
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = gb_eschalon_setup_epsf_read(device, (uint8_t *)buffer + done,
                                    size - done, file_io_capacity);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Carrier: PE overlay and the EPSF header                                */
/* ---------------------------------------------------------------------- */

static bool epsf_locate(Abstractformat *format, epsf_layout *out) {
    uint8_t dos[EPSF_DOS_HEADER_SIZE];
    uint8_t nt[EPSF_NT_HEADER_SIZE];
    uint8_t sections[EPSF_MAX_SECTIONS * EPSF_SECTION_SIZE];
    uint8_t header[EPSF_HEADER_SIZE];
    xx_io_device *device;
    int64_t base, total, size;
    uint64_t table, raw_end = 0U;
    uint32_t lfanew, runtime_size, runtime_packed, runtime_sum;
    uint16_t section_count, optional_size, optional_magic, index;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    device = format->device;
    base = format->base_address;
    total = xx_io_total_size(device);
    if (total < base ||
        total - base < (int64_t)(EPSF_DOS_HEADER_SIZE + EPSF_HEADER_SIZE))
        return false;
    size = total - base;
    if (!epsf_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    lfanew = epsf_le32(dos + 0x3cU);
    if ((uint64_t)lfanew + EPSF_NT_HEADER_SIZE > (uint64_t)size ||
        !epsf_read_at(device, base + (int64_t)lfanew, nt, sizeof(nt)) ||
        nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U)
        return false;
    section_count = epsf_le16(nt + 6U);
    optional_size = epsf_le16(nt + 20U);
    optional_magic = epsf_le16(nt + 24U);
    if (section_count == 0U || section_count > EPSF_MAX_SECTIONS ||
        optional_size < 2U ||
        (optional_magic != 0x10bU && optional_magic != 0x20bU))
        return false;
    table = (uint64_t)lfanew + 24U + optional_size;
    if (table > (uint64_t)size ||
        (uint64_t)section_count * EPSF_SECTION_SIZE > (uint64_t)size - table ||
        !epsf_read_at(device, base + (int64_t)table, sections,
                      (size_t)section_count * EPSF_SECTION_SIZE))
        return false;
    for (index = 0U; index < section_count; ++index) {
        const uint8_t *section = sections + (size_t)index * EPSF_SECTION_SIZE;
        uint32_t raw_size = epsf_le32(section + 16U);
        uint32_t raw_pointer = epsf_le32(section + 20U);
        if (raw_size != 0U &&
            (uint64_t)raw_pointer + raw_size > raw_end)
            raw_end = (uint64_t)raw_pointer + raw_size;
    }
    /* The header sits exactly at the overlay: the end of the furthest raw
     * section data. */
    if (raw_end == 0U || raw_end > (uint64_t)size - EPSF_HEADER_SIZE ||
        !epsf_read_at(device, base + (int64_t)raw_end, header,
                      sizeof(header)) ||
        xx_rt_memcmp(header, "EPSF", 4U) != 0 ||
        epsf_le16(header + 4U) != EPSF_VERSION)
        return false;
    runtime_size = epsf_le32(header + 6U);
    runtime_packed = epsf_le32(header + 10U);
    runtime_sum = epsf_le32(header + 14U);
    if (runtime_size == 0U || runtime_packed == 0U ||
        runtime_size > EPSF_MAX_RUNTIME || runtime_packed > EPSF_MAX_RUNTIME ||
        (uint64_t)runtime_packed >
            (uint64_t)size - raw_end - EPSF_HEADER_SIZE ||
        (uint64_t)runtime_size >
            (uint64_t)runtime_packed * EPSF_MAX_RATIO + EPSF_MAX_RATIO ||
        (uint64_t)runtime_sum > (uint64_t)runtime_size * 255U)
        return false;
    xx_rt_memset(out, 0, sizeof(*out));
    out->end = total;
    out->header_offset = base + (int64_t)raw_end;
    out->runtime_size = runtime_size;
    out->runtime_packed = runtime_packed;
    out->runtime_sum = runtime_sum;
    out->runtime_offset = out->header_offset + (int64_t)EPSF_HEADER_SIZE;
    out->script_offset = out->runtime_offset + (int64_t)runtime_packed;
    out->script_region = total - out->script_offset;
    out->archive_offset = -1;
    out->damaged_offset = -1;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Region 3: the ARCV 4.00 container                                      */
/* ---------------------------------------------------------------------- */

static bool epsf_filled(const uint8_t *data, size_t size, uint8_t value) {
    size_t index;
    for (index = 0U; index < size; ++index)
        if (data[index] != value) return false;
    return true;
}

/* Constant-cost gate: the fixed ARCV4 header fill, then the first FILE
 * prologue and the DATA header behind its body.  Three reads, no walk. */
static bool epsf_gate_archive(xx_io_device *device, const epsf_layout *layout,
                              int64_t candidate) {
    uint8_t header[EPSF_ARCV_HEADER_SIZE];
    uint8_t prologue[EPSF_ARCV_PROLOGUE_SIZE];
    uint8_t data[EPSF_ARCV_DATA_SIZE];
    uint32_t record_size, data_type;
    uint16_t subvariant;
    int64_t room = layout->end - candidate;
    if (candidate < 0 ||
        room < (int64_t)(EPSF_ARCV_HEADER_SIZE + EPSF_ARCV_PROLOGUE_SIZE) ||
        !epsf_read_at(device, candidate, header, sizeof(header)) ||
        xx_rt_memcmp(header, epsf_arcv4_tag, EPSF_TAG_SIZE) != 0)
        return false;
    subvariant = epsf_le16(header + 6U);
    if ((subvariant != 1U && subvariant != 5U) ||
        !epsf_filled(header + EPSF_ARCV_ZERO_OFFSET, EPSF_ARCV_ZERO_SIZE, 0U) ||
        !epsf_filled(header + EPSF_ARCV_AA_OFFSET, EPSF_ARCV_AA_SIZE, 0xaaU) ||
        !epsf_filled(header + EPSF_ARCV_TAIL_ZERO_OFFSET,
                     EPSF_ARCV_TAIL_ZERO_SIZE, 0U) ||
        !epsf_read_at(device, candidate + (int64_t)EPSF_ARCV_HEADER_SIZE,
                      prologue, sizeof(prologue)) ||
        xx_rt_memcmp(prologue, "FILE", 4U) != 0 ||
        (epsf_le32(prologue + 4U) & 0xffU) != 1U ||
        epsf_le32(prologue + 8U) != EPSF_ARCV_PROLOGUE_SIZE)
        return false;
    record_size = epsf_le32(prologue + 12U);
    if (record_size < 40U || record_size > EPSF_ARCV_MAX_BODY ||
        (int64_t)(EPSF_ARCV_HEADER_SIZE + EPSF_ARCV_PROLOGUE_SIZE +
                  EPSF_ARCV_DATA_SIZE) + (int64_t)record_size > room ||
        !epsf_read_at(device,
                      candidate + (int64_t)(EPSF_ARCV_HEADER_SIZE +
                                            EPSF_ARCV_PROLOGUE_SIZE) +
                          (int64_t)record_size,
                      data, sizeof(data)))
        return false;
    data_type = epsf_le32(data + 4U) & 0xffU;
    return xx_rt_memcmp(data, "DATA", 4U) == 0 &&
           (data_type == 1U || data_type == 5U) &&
           epsf_le32(data + 8U) == EPSF_ARCV_DATA_SIZE &&
           epsf_le32(data + 16U) == 0U && epsf_le32(data + 24U) == 0U &&
           epsf_le32(data + 28U) == 0U;
}

static bool epsf_try_archive(Abstractformat *format, epsf_layout *layout,
                             int64_t candidate, xx_pd_struct *pd) {
    xx_arcv4 inner;
    bool valid;
    xx_arcv4_init(&inner, format->device, candidate);
    valid = xx_arcv4_handle_base_info(&inner.format, pd) &&
            inner.format.format_size > 0 &&
            inner.format.format_size <= layout->end - candidate &&
            inner.number_of_records != 0U &&
            inner.number_of_records <= EPSF_MAX_ARCHIVE_RECORDS;
    if (valid) {
        layout->archive_offset = candidate;
        layout->archive_size = inner.format.format_size;
        layout->archive_records = inner.number_of_records;
    }
    xx_arcv4_destroy(&inner);
    return valid;
}

static void epsf_find_archive(Abstractformat *format, epsf_layout *layout,
                              xx_pd_struct *pd) {
    const size_t file_io_capacity = gb_eschalon_setup_epsf_capacity();
    int64_t position = layout->script_offset;
    int64_t limit = layout->end;
    unsigned candidates = 0U;
    if (limit - position > EPSF_SEARCH_WINDOW)
        limit = position + EPSF_SEARCH_WINDOW;
    while (limit - position >= (int64_t)EPSF_TAG_SIZE &&
           candidates < EPSF_MAX_CANDIDATES && !xx_pd_is_stopped(pd)) {
        int64_t candidate = xx_io_find_bytes_buffer_optimize_ex(
            format->device, position, limit - position, epsf_arcv4_tag,
            EPSF_TAG_SIZE, file_io_capacity, pd);
        if (candidate < 0) break;
        ++candidates;
        if (epsf_gate_archive(format->device, layout, candidate)) {
            /* The one full parse.  A container this well formed at its start
             * is the product container; if its chain does not parse it is
             * damaged, and later hits (which may well lie inside it) are not
             * worth another multi-second walk. */
            if (epsf_try_archive(format, layout, candidate, pd))
                layout->script_region =
                    layout->archive_offset - layout->script_offset;
            else if (!xx_pd_is_stopped(pd)) {
                layout->damaged_offset = candidate;
                layout->script_region = candidate - layout->script_offset;
            }
            return;
        }
        position = candidate + 1;
    }
}

/* ---------------------------------------------------------------------- */
/* Region 2: the setup script                                             */
/* ---------------------------------------------------------------------- */

/* Decode a method-2 stream that ends at its end symbol.  On success the
 * output holds exactly the decoded bytes; *output may be NULL to only
 * measure. */
static bool epsf_decode_open_stream(const uint8_t *packed, size_t packed_size,
                                    uint8_t **output, size_t *output_size) {
    size_t capacity = EPSF_SCRIPT_FIRST_OUTPUT;
    if (output) *output = NULL;
    if (output_size) *output_size = 0U;
    if (!packed || packed_size == 0U) return false;
    for (;;) {
        uint8_t *buffer = (uint8_t *)xx_mem_alloc(capacity);
        size_t produced = 0U, check = 0U;
        if (!buffer) return false;
        (void)xx_arcv4_decode_memory(packed, packed_size, buffer, capacity,
                                     &produced);
        if (produced != 0U && produced < capacity &&
            xx_arcv4_decode_memory(packed, packed_size, buffer, produced,
                                   &check) &&
            check == produced) {
            if (output)
                *output = buffer;
            else
                xx_mem_free(buffer);
            if (output_size) *output_size = produced;
            return true;
        }
        xx_mem_free(buffer);
        /* Only a stream that ran into the end of the buffer earns a larger
         * one; anything else stopped on a real error. */
        if (produced + EPSF_MAX_MATCH <= capacity ||
            capacity >= EPSF_SCRIPT_MAX_OUTPUT)
            return false;
        capacity *= 4U;
        if (capacity > EPSF_SCRIPT_MAX_OUTPUT)
            capacity = EPSF_SCRIPT_MAX_OUTPUT;
    }
}

static size_t epsf_script_input_size(const epsf_layout *layout) {
    if (layout->script_region <= 0) return 0U;
    return layout->script_region > (int64_t)EPSF_SCRIPT_MAX_INPUT
               ? (size_t)EPSF_SCRIPT_MAX_INPUT
               : (size_t)layout->script_region;
}

static void epsf_measure_script(Abstractformat *format, epsf_layout *layout) {
    size_t input_size = epsf_script_input_size(layout);
    size_t decoded = 0U;
    uint8_t *packed;
    layout->script_size = 0U;
    if (input_size == 0U) return;
    packed = (uint8_t *)xx_mem_alloc(input_size);
    if (!packed) return;
    if (epsf_read_at(format->device, layout->script_offset, packed,
                     input_size) &&
        epsf_decode_open_stream(packed, input_size, NULL, &decoded))
        layout->script_size = decoded;
    xx_mem_free(packed);
}

static bool epsf_parse_layout(Abstractformat *format, epsf_layout *layout,
                              xx_pd_struct *pd) {
    if (!epsf_locate(format, layout)) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    epsf_find_archive(format, layout, pd);
    if (pd && xx_pd_is_stopped(pd)) return false;
    epsf_measure_script(format, layout);
    return true;
}

/* A script region that does not decode is still listed (as a member that
 * cannot be extracted), so that its loss shows. */
static bool epsf_script_listed(const epsf_layout *layout) {
    return layout->script_size != 0U || layout->script_region > 0;
}

static uint64_t epsf_record_count(const epsf_layout *layout) {
    return 1U + (epsf_script_listed(layout) ? 1U : 0U) +
           (layout->archive_offset >= 0 ? layout->archive_records : 0U) +
           (layout->damaged_offset >= 0 ? 1U : 0U);
}

/* ---------------------------------------------------------------------- */
/* Names                                                                  */
/* ---------------------------------------------------------------------- */

static char epsf_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool epsf_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index) {
        if (!word[index] || epsf_upper(name[index]) != word[index])
            return false;
    }
    return word[stem] == 0;
}

/* One path component: printable ASCII, not only dots and spaces (so never
 * "." or ".."), and not a Windows device name.  Ported from xx_xpak.c's
 * xpak_safe_output_name (xxfclib, MIT). */
static bool epsf_safe_component(const char *name, size_t length) {
    static const char *const devices[] = { "CON",    "PRN",     "AUX",
                                           "NUL",    "CONIN$",  "CONOUT$",
                                           "CLOCK$" };
    size_t stem = 0U, index;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (epsf_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((epsf_upper(name[0]) == 'C' && epsf_upper(name[1]) == 'O' &&
          epsf_upper(name[2]) == 'M') ||
         (epsf_upper(name[0]) == 'L' && epsf_upper(name[1]) == 'P' &&
          epsf_upper(name[2]) == 'T')))
        return false;
    return true;
}

/* ARCV4 member names are relative paths: the reader turns "E:\SETUP\X.DLL"
 * into "E_/SETUP/X.DLL".  '/' separates components; each component must be
 * safe on its own, so no absolute path, empty component, "." or ".." and no
 * device name can reach the file system. */
static bool epsf_safe_output_name(const char *name) {
    size_t start = 0U, index = 0U;
    if (!name || !name[0]) return false;
    for (;;) {
        char c = name[index];
        if (c == '/' || c == 0) {
            if (!epsf_safe_component(name + start, index - start))
                return false;
            if (c == 0) return true;
            start = index + 1U;
        }
        ++index;
    }
}

static uint32_t epsf_node_height(const epsf_names *set, uint32_t node) {
    return node ? set->nodes[node].height : 0U;
}

static void epsf_node_update(epsf_names *set, uint32_t node) {
    uint32_t left = epsf_node_height(set, set->nodes[node].left);
    uint32_t right = epsf_node_height(set, set->nodes[node].right);
    set->nodes[node].height = (left > right ? left : right) + 1U;
}

static uint32_t epsf_node_rotate_right(epsf_names *set, uint32_t node) {
    uint32_t pivot = set->nodes[node].left;
    set->nodes[node].left = set->nodes[pivot].right;
    set->nodes[pivot].right = node;
    epsf_node_update(set, node);
    epsf_node_update(set, pivot);
    return pivot;
}

static uint32_t epsf_node_rotate_left(epsf_names *set, uint32_t node) {
    uint32_t pivot = set->nodes[node].right;
    set->nodes[node].right = set->nodes[pivot].left;
    set->nodes[pivot].left = node;
    epsf_node_update(set, node);
    epsf_node_update(set, pivot);
    return pivot;
}

static uint32_t epsf_node_balance(epsf_names *set, uint32_t node) {
    uint32_t left, right;
    epsf_node_update(set, node);
    left = epsf_node_height(set, set->nodes[node].left);
    right = epsf_node_height(set, set->nodes[node].right);
    if (left > right + 1U) {
        uint32_t child = set->nodes[node].left;
        if (epsf_node_height(set, set->nodes[child].right) >
            epsf_node_height(set, set->nodes[child].left))
            set->nodes[node].left = epsf_node_rotate_left(set, child);
        return epsf_node_rotate_right(set, node);
    }
    if (right > left + 1U) {
        uint32_t child = set->nodes[node].right;
        if (epsf_node_height(set, set->nodes[child].left) >
            epsf_node_height(set, set->nodes[child].right))
            set->nodes[node].right = epsf_node_rotate_right(set, child);
        return epsf_node_rotate_left(set, node);
    }
    return node;
}

/* Recursion depth is the tree height: at most ~1.44 log2(n), 25 levels for
 * the 100,002 names a carrier can have. */
static uint32_t epsf_node_insert(epsf_names *set, uint32_t root,
                                 uint32_t node) {
    if (!root) return node;
    if (xx_str_icmp(set->nodes[node].name, set->nodes[root].name) < 0)
        set->nodes[root].left = epsf_node_insert(set, set->nodes[root].left,
                                                 node);
    else
        set->nodes[root].right = epsf_node_insert(set, set->nodes[root].right,
                                                  node);
    return epsf_node_balance(set, root);
}

static bool epsf_names_has(const epsf_names *set, const char *name) {
    uint32_t node = set->root;
    uint32_t steps;
    for (steps = 0U; node && steps <= set->count; ++steps) {
        int order = xx_str_icmp(name, set->nodes[node].name);
        if (order == 0) return true;
        node = order < 0 ? set->nodes[node].left : set->nodes[node].right;
    }
    return false;
}

static bool epsf_names_put(epsf_names *set, const char *name) {
    uint32_t node;
    if (set->count >= set->capacity) return false;
    node = ++set->count;
    set->nodes[node].name = name;
    set->nodes[node].left = 0U;
    set->nodes[node].right = 0U;
    set->nodes[node].height = 1U;
    set->root = epsf_node_insert(set, set->root, node);
    return true;
}

/* "DIR/NAME.EXT" -> "DIR/NAME_<n>.EXT"; only the last path component's
 * extension is looked at, and the dot of a leading-dot name is kept. */
static char *epsf_suffixed(const char *name, uint32_t number) {
    char digits[16];
    size_t digit_count = 0U, length, dot, total, index, at = 0U;
    char *result;
    uint32_t value = number;
    do {
        digits[digit_count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && digit_count < sizeof(digits));
    length = xx_str_len(name);
    dot = length;
    for (index = length; index > 1U; --index) {
        if (name[index - 1U] == '/' || name[index - 2U] == '/') break;
        if (name[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    total = length + 1U + digit_count;
    result = (char *)xx_mem_alloc(total + 1U);
    if (!result) return NULL;
    for (index = 0U; index < dot; ++index) result[at++] = name[index];
    result[at++] = '_';
    while (digit_count != 0U) result[at++] = digits[--digit_count];
    for (index = dot; index < length; ++index) result[at++] = name[index];
    result[at] = 0;
    return result;
}

/* ---------------------------------------------------------------------- */
/* Member list                                                            */
/* ---------------------------------------------------------------------- */

static void epsf_stream_free(void *opaque) {
    epsf_stream *stream = (epsf_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Takes ownership of member->name. */
static bool epsf_add_member(epsf_stream *stream, epsf_names *set,
                            epsf_member *member, uint32_t *suffix) {
    if (stream->count >= stream->capacity) {
        xx_mem_free(member->name);
        member->name = NULL;
        return false;
    }
    if (epsf_names_has(set, member->name)) {
        char *alternative = NULL;
        size_t tries;
        for (tries = 0U; tries <= stream->capacity + 1U; ++tries) {
            alternative = epsf_suffixed(member->name, (*suffix)++);
            if (!alternative || !epsf_names_has(set, alternative)) break;
            xx_mem_free(alternative);
            alternative = NULL;
        }
        xx_mem_free(member->name);
        member->name = alternative;
        if (!alternative) return false;
    }
    member->safe = epsf_safe_output_name(member->name);
    if (!epsf_names_put(set, member->name)) {
        xx_mem_free(member->name);
        member->name = NULL;
        return false;
    }
    stream->items[stream->count] = *member;
    ++stream->count;
    member->name = NULL;
    return true;
}

static bool epsf_collect(Abstractformat *format, epsf_stream *stream,
                         xx_pd_struct *pd) {
    const epsf_layout *layout = &stream->layout;
    epsf_names set;
    epsf_member member;
    uint32_t suffix = 2U;
    uint64_t wanted = epsf_record_count(layout);
    bool ok = false;
    if (wanted > EPSF_MAX_ARCHIVE_RECORDS + 2U) return false;
    stream->capacity = (size_t)wanted;
    stream->items =
        (epsf_member *)xx_mem_calloc(stream->capacity, sizeof(*stream->items));
    if (!stream->items) return false;
    set.nodes = (epsf_name_node *)xx_mem_calloc(stream->capacity + 1U,
                                                sizeof(*set.nodes));
    set.root = 0U;
    set.count = 0U;
    set.capacity = (uint32_t)stream->capacity;
    if (!set.nodes) return false;

    xx_rt_memset(&member, 0, sizeof(member));
    member.name = xx_str_dup(epsf_runtime_name);
    member.kind = EPSF_KIND_RUNTIME;
    member.header_offset = layout->header_offset;
    member.header_size = EPSF_HEADER_SIZE;
    member.data_offset = layout->runtime_offset;
    member.packed_size = layout->runtime_packed;
    member.original_size = layout->runtime_size;
    member.method = 2U;
    member.check = layout->runtime_sum;
    if (!member.name || !epsf_add_member(stream, &set, &member, &suffix))
        goto done;

    if (epsf_script_listed(layout)) {
        xx_rt_memset(&member, 0, sizeof(member));
        member.name = xx_str_dup(epsf_script_name);
        member.kind = layout->script_size != 0U ? EPSF_KIND_SCRIPT
                                                : EPSF_KIND_SCRIPT_DAMAGED;
        member.header_offset = -1;
        member.data_offset = layout->script_offset;
        member.packed_size = (uint64_t)layout->script_region;
        member.original_size = layout->script_size;
        member.method = layout->script_size != 0U ? 2U : UINT32_MAX;
        if (!member.name || !epsf_add_member(stream, &set, &member, &suffix))
            goto done;
    }

    if (layout->damaged_offset >= 0) {
        xx_rt_memset(&member, 0, sizeof(member));
        member.name = xx_str_dup(epsf_damaged_name);
        member.kind = EPSF_KIND_DAMAGED;
        member.header_offset = layout->damaged_offset;
        member.header_size = EPSF_ARCV_HEADER_SIZE;
        member.data_offset = layout->damaged_offset;
        member.packed_size = (uint64_t)(layout->end - layout->damaged_offset);
        member.original_size = member.packed_size;
        member.method = UINT32_MAX;
        if (!member.name || !epsf_add_member(stream, &set, &member, &suffix))
            goto done;
    }

    if (layout->archive_offset >= 0) {
        xx_arcv4 inner;
        xx_archive_record_state *state;
        bool inner_ok = true;
        xx_arcv4_init(&inner, format->device, layout->archive_offset);
        state = xx_arcv4_create_archive_records_reading(&inner.format, NULL,
                                                        pd);
        if (!state) inner_ok = false;
        while (inner_ok) {
            const xx_archive_record *record =
                xx_arcv4_get_current_archive_record(&inner.format, state);
            const char *name;
            if (!record) break;
            if (pd && xx_pd_is_stopped(pd)) {
                inner_ok = false;
                break;
            }
            name = xx_archive_record_get_original_name(record);
            xx_rt_memset(&member, 0, sizeof(member));
            member.name = name ? xx_str_dup(name) : NULL;
            member.kind = EPSF_KIND_ARCV;
            member.header_offset = record->header_offset;
            member.header_size = record->header_size;
            member.data_offset = record->data_offset;
            member.packed_size = record->compressed_size < 0
                                     ? 0U : (uint64_t)record->compressed_size;
            member.original_size = xx_archive_record_get_meta_u64(
                record, XX_META_ID_UNCOMPRESSED_SIZE, 0U);
            member.method = (uint32_t)xx_archive_record_get_meta_u64(
                record, XX_META_ID_COMPRESSION_METHOD, UINT32_MAX);
            member.check = (uint32_t)xx_archive_record_get_meta_u64(
                record, XX_META_ID_CRC32, 0U);
            member.attributes = (uint32_t)xx_archive_record_get_meta_u64(
                record, XX_META_ID_ATTRIBUTES, 0U);
            member.timestamp = xx_archive_record_get_meta_u64(
                record, XX_META_ID_TIMESTAMP, 0U);
            if (!member.name || record->data_offset < layout->archive_offset ||
                record->compressed_size < 0 ||
                record->data_offset >
                    layout->archive_offset + layout->archive_size ||
                record->compressed_size >
                    layout->archive_offset + layout->archive_size -
                        record->data_offset ||
                !epsf_add_member(stream, &set, &member, &suffix)) {
                if (member.name) xx_mem_free(member.name);
                inner_ok = false;
                break;
            }
            if (!xx_arcv4_archive_record_move_to_next(&inner.format, state,
                                                      pd))
                break;
        }
        if (state) xx_arcv4_free_archive_records_reading(&inner.format, state);
        xx_arcv4_destroy(&inner);
        if (!inner_ok || stream->count != stream->capacity) goto done;
    }
    ok = stream->count == stream->capacity;
done:
    xx_mem_free(set.nodes);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                             */
/* ---------------------------------------------------------------------- */

static bool epsf_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *epsf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool epsf_set_record(xx_archive_record *record,
                            const epsf_member *member) {
    bool ok;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->packed_size;
    ok = xx_archive_record_set_original_name(record, member->name) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        member->packed_size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        member->original_size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        member->method) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (ok && member->kind == EPSF_KIND_ARCV) {
        ok = xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                            member->attributes) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                            member->timestamp) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                            member->check);
    }
    return ok;
}

static bool epsf_write_all(xx_io_device *destination, const uint8_t *data,
                           size_t size) {
    const size_t file_io_capacity = gb_eschalon_setup_epsf_capacity();
    size_t written = 0U;
    while (written < size) {
        ssize_t amount = gb_eschalon_setup_epsf_write(destination, data + written,
                                     size - written, file_io_capacity);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

/* Decode a member completely in memory and verify it. */
static bool epsf_decode_member(Abstractformat *format,
                               const epsf_stream *stream,
                               const epsf_member *member, uint64_t limit,
                               uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t input_size, written = 0U;
    bool ok = false;
    *plain = NULL;
    *plain_size = 0U;
    if (member->original_size > limit ||
        member->original_size > EPSF_MAX_MEMBER ||
        member->original_size > (uint64_t)SIZE_MAX - 1U)
        return false;
    if (member->kind == EPSF_KIND_SCRIPT) {
        size_t decoded = 0U;
        input_size = epsf_script_input_size(&stream->layout);
        if (input_size == 0U) return false;
        packed = (uint8_t *)xx_mem_alloc(input_size);
        if (!packed ||
            !epsf_read_at(format->device, member->data_offset, packed,
                          input_size) ||
            !epsf_decode_open_stream(packed, input_size, &output, &decoded) ||
            decoded != member->original_size) {
            if (output) xx_mem_free(output);
            output = NULL;
            goto done;
        }
        written = decoded;
        ok = true;
        goto done;
    }
    if (member->packed_size > EPSF_MAX_MEMBER ||
        member->packed_size > (uint64_t)SIZE_MAX ||
        (member->method == 2U &&
         member->original_size >
             member->packed_size * EPSF_MAX_RATIO + EPSF_MAX_RATIO) ||
        (member->method == 0U && member->original_size != member->packed_size) ||
        (member->method != 0U && member->method != 2U) ||
        (member->method == 2U && member->packed_size == 0U))
        return false;
    input_size = (size_t)member->packed_size;
    packed = (uint8_t *)xx_mem_alloc(input_size != 0U ? input_size : 1U);
    output = (uint8_t *)xx_mem_alloc(member->original_size != 0U
                                         ? (size_t)member->original_size : 1U);
    if (!packed || !output ||
        (input_size != 0U &&
         !epsf_read_at(format->device, member->data_offset, packed,
                       input_size)))
        goto done;
    if (member->method == 0U) {
        if (input_size != 0U) xx_rt_memcpy(output, packed, input_size);
        written = input_size;
    } else if (!xx_arcv4_decode_memory(packed, input_size, output,
                                       (size_t)member->original_size,
                                       &written)) {
        goto done;
    }
    if (written != member->original_size) goto done;
    if (member->kind == EPSF_KIND_RUNTIME) {
        uint32_t sum = 0U;
        size_t index;
        for (index = 0U; index < written; ++index) sum += output[index];
        ok = sum == member->check;
    } else {
        ok = xx_crc32_calc(0U, output, written) == member->check;
    }
done:
    if (packed) xx_mem_free(packed);
    if (!ok) {
        if (output) xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = written;
    return true;
}

/* Stored ARCV4 members are streamed so their size never becomes a buffer. */
static bool epsf_copy_stored(Abstractformat *format, const epsf_member *member,
                             xx_io_device *destination) {
    const size_t file_io_capacity = gb_eschalon_setup_epsf_capacity();
    uint8_t *chunk;
    uint64_t done = 0U;
    uint32_t crc = 0U;
    bool ok = true;
    chunk = (uint8_t *)xx_mem_alloc(file_io_capacity);
    if (!chunk) return false;
    while (done < member->packed_size) {
        size_t want = member->packed_size - done > file_io_capacity
                          ? file_io_capacity
                          : (size_t)(member->packed_size - done);
        if (!epsf_read_at(format->device, member->data_offset + (int64_t)done,
                          chunk, want)) {
            ok = false;
            break;
        }
        crc = xx_crc32_calc(crc, chunk, want);
        if (destination && !epsf_write_all(destination, chunk, want)) {
            ok = false;
            break;
        }
        done += want;
    }
    xx_mem_free(chunk);
    return ok && crc == member->check;
}

/* The caller's XX_META_ID_OPT_MAX_MEMBER_SIZE, or no limit.  Decoded
 * members are additionally held to EPSF_MAX_MEMBER because they need a
 * buffer; stored ones are streamed and need none. */
static uint64_t epsf_member_limit(const xx_list_s *options) {
    const xx_var *limit = epsf_option(options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    return limit ? xx_var_get_u64(limit) : UINT64_MAX;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

void xx_eschalon_setup_epsf_init(xx_eschalon_setup_epsf *archive,
                                 xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_ESCHALON_SETUP_EPSF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-eschalon-setup");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_eschalon_setup_epsf_check_is_valid;
    archive->format.handle_base_info = xx_eschalon_setup_epsf_handle_base_info;
    archive->format.get_format_size = xx_eschalon_setup_epsf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_eschalon_setup_epsf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_eschalon_setup_epsf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_eschalon_setup_epsf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_eschalon_setup_epsf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_eschalon_setup_epsf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_eschalon_setup_epsf_free_archive_records_reading;
    archive->header_offset = -1;
    archive->script_offset = -1;
    archive->archive_offset = -1;
    archive->damaged_archive_offset = -1;
}

xx_eschalon_setup_epsf *xx_eschalon_setup_epsf_create(xx_io_device *device,
                                                      int64_t base_address) {
    xx_eschalon_setup_epsf *archive =
        (xx_eschalon_setup_epsf *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_eschalon_setup_epsf_init(archive, device, base_address);
    return archive;
}

void xx_eschalon_setup_epsf_destroy(xx_eschalon_setup_epsf *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_eschalon_setup_epsf_free(xx_eschalon_setup_epsf *archive) {
    if (!archive) return;
    xx_eschalon_setup_epsf_destroy(archive);
    xx_mem_free(archive);
}

bool xx_eschalon_setup_epsf_check_is_valid(Abstractformat *format,
                                           xx_pd_struct *pd) {
    epsf_layout layout;
    (void)pd;
    return epsf_locate(format, &layout);
}

bool xx_eschalon_setup_epsf_handle_base_info(Abstractformat *format,
                                             xx_pd_struct *pd) {
    epsf_layout layout;
    xx_eschalon_setup_epsf *archive;
    int64_t format_end;
    if (!format || !epsf_parse_layout(format, &layout, pd)) return false;
    archive = (xx_eschalon_setup_epsf *)format;
    archive->number_of_records = epsf_record_count(&layout);
    archive->header_offset = layout.header_offset;
    archive->runtime_size = layout.runtime_size;
    archive->runtime_packed_size = layout.runtime_packed;
    archive->runtime_checksum = layout.runtime_sum;
    archive->script_offset = layout.script_offset;
    archive->script_region_size = layout.script_region;
    archive->script_size = layout.script_size;
    archive->archive_offset = layout.archive_offset;
    archive->archive_size = layout.archive_offset >= 0 ? layout.archive_size : 0;
    archive->archive_records =
        layout.archive_offset >= 0 ? layout.archive_records : 0U;
    archive->damaged_archive_offset = layout.damaged_offset;
    /* Without a product container the script runs to the end of the file,
     * and nothing records where its stream stops.  A damaged container also
     * runs to the end. */
    format_end = layout.archive_offset >= 0
                     ? layout.archive_offset + layout.archive_size
                     : layout.end;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = format_end - format->base_address;
    format->overlay_offset = format_end < layout.end ? format_end : -1;
    format->overlay_size = format_end < layout.end ? layout.end - format_end : 0;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_eschalon_setup_epsf_get_format_size(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_eschalon_setup_epsf_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_eschalon_setup_epsf_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_eschalon_setup_epsf_handle_base_info(format, pd))
               ? ((xx_eschalon_setup_epsf *)format)->number_of_records : 0U;
}

/* Rebuild the layout handle_base_info found, so the ARCV4 search and its
 * full parse are not repeated.  Falls back to a fresh parse (returns false)
 * when base info was never handled or the device no longer matches. */
static bool epsf_cached_layout(Abstractformat *format, epsf_layout *layout) {
    const xx_eschalon_setup_epsf *archive =
        (const xx_eschalon_setup_epsf *)format;
    int64_t total;
    if (!format->base_info_handled || !format->is_valid ||
        archive->header_offset < 0 || archive->script_offset < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total != format->base_address + format->format_size +
                     format->overlay_size ||
        archive->script_offset > total)
        return false;
    xx_rt_memset(layout, 0, sizeof(*layout));
    layout->end = total;
    layout->header_offset = archive->header_offset;
    layout->runtime_size = archive->runtime_size;
    layout->runtime_packed = archive->runtime_packed_size;
    layout->runtime_sum = archive->runtime_checksum;
    layout->runtime_offset = archive->header_offset + (int64_t)EPSF_HEADER_SIZE;
    layout->script_offset = archive->script_offset;
    layout->script_region = archive->script_region_size;
    layout->script_size = archive->script_size;
    layout->archive_offset = archive->archive_offset;
    layout->archive_size = archive->archive_size;
    layout->archive_records = archive->archive_records;
    layout->damaged_offset = archive->damaged_archive_offset;
    return true;
}

xx_archive_record_state *xx_eschalon_setup_epsf_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    epsf_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (epsf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!epsf_cached_layout(format, &stream->layout) &&
        !epsf_parse_layout(format, &stream->layout, pd)) {
        epsf_stream_free(stream);
        return NULL;
    }
    if (!epsf_collect(format, stream, pd) || stream->count == 0U) {
        epsf_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        epsf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = epsf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!epsf_copy_options(&state->options, options) ||
        !epsf_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_eschalon_setup_epsf_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_eschalon_setup_epsf_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    epsf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (epsf_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = epsf_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_eschalon_setup_epsf_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    epsf_stream *stream;
    const epsf_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    uint64_t limit;
    bool result = false;
    bool created = false;
    bool stored;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (epsf_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    /* A damaged product container is listed so that its loss shows; there is
     * nothing trustworthy to write for it. */
    if (!member->safe || member->kind == EPSF_KIND_DAMAGED ||
        member->kind == EPSF_KIND_SCRIPT_DAMAGED)
        return false;
    limit = epsf_member_limit(&state->options);
    stored = member->kind == EPSF_KIND_ARCV && member->method == 0U;
    if (stored) {
        if (member->original_size != member->packed_size ||
            member->original_size > limit)
            return false;
    } else if (!epsf_decode_member(format, stream, member, limit, &plain,
                                   &plain_size)) {
        return false;
    }
    path_option = epsf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = stored ? epsf_copy_stored(format, member, NULL) : true;
        goto done;
    }
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
        result = stored ? epsf_copy_stored(format, member, destination)
                        : epsf_write_all(destination, plain, plain_size);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_eschalon_setup_epsf_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
