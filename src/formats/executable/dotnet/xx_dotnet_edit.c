/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/executable/dotnet/xx_dotnet_edit.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet_reader.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

static uint16_t edit_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t edit_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void edit_put16(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static void edit_put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static int edit_overlap(const void *a, size_t a_size, const void *b, size_t b_size)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!a_size || !b_size) return 0;
    if (a_size > UINTPTR_MAX - x || b_size > UINTPTR_MAX - y) return 1;
    return x < y + b_size && y < x + a_size;
}

/* The first pass validates every scalar before a caller's buffer can change. */
static bool edit_utf8_pass(const uint8_t *input, size_t size, uint16_t *output, size_t *count)
{
    size_t i = 0, used = 0;
    while (i < size) {
        uint32_t cp = input[i++], minimum = 0;
        unsigned continuation = 0, j;
        if (cp >= 0xc2 && cp <= 0xdf) { cp &= 31; continuation = 1; minimum = 0x80; }
        else if (cp >= 0xe0 && cp <= 0xef) { cp &= 15; continuation = 2; minimum = 0x800; }
        else if (cp >= 0xf0 && cp <= 0xf4) { cp &= 7; continuation = 3; minimum = 0x10000; }
        else if (cp >= 0x80) return false;
        if (continuation > size - i) return false;
        for (j = 0; j < continuation; ++j) {
            uint32_t byte = input[i++];
            if (byte < 0x80 || byte > 0xbf) return false;
            cp = cp * 64 + (byte & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        if (used > SIZE_MAX - (cp < 0x10000 ? 1u : 2u)) return false;
        if (cp < 0x10000) {
            if (output) output[used] = (uint16_t)cp;
            ++used;
        } else {
            cp -= 0x10000;
            if (output) {
                output[used] = (uint16_t)(0xd800 + (cp >> 10));
                output[used + 1] = (uint16_t)(0xdc00 + (cp & 0x3ff));
            }
            used += 2;
        }
    }
    *count = used;
    return true;
}
bool xx_dotnet_utf8_to_utf16(const uint8_t *input, size_t input_size,
    uint16_t *output, size_t capacity, size_t *units)
{
    size_t count = 0;
    if (units) *units = 0;
    if (!units || (!input && input_size) || input_size > XX_DOTNET_US_MAX_HEAP || !edit_utf8_pass(input, input_size, NULL, &count)) return false;
    if (!output) { *units = count; return true; }
    if (capacity < count || count > SIZE_MAX / sizeof(*output) ||
        edit_overlap(input, input_size, output, count * sizeof(*output))) return false;
    if (!edit_utf8_pass(input, input_size, output, &count)) return false;
    *units = count;
    return true;
}
bool xx_dotnet_us_encode(const uint16_t *units, size_t count,
    uint8_t *output, size_t capacity, size_t *encoded_size)
{
    size_t payload, prefix, total, i;
    unsigned flag = 0;
    if (encoded_size) *encoded_size = 0;
    if (!encoded_size || (!units && count) || count > (XX_DOTNET_US_MAX_HEAP - 2u) / 2u) return false;
    payload = count * 2 + 1;
    prefix = payload < 0x80 ? 1 : payload < 0x4000 ? 2 : 4;
    total = prefix + payload;
    if (total > XX_DOTNET_US_MAX_HEAP) return false;
    if (!output) { *encoded_size = total; return true; }
    if (capacity < total || edit_overlap(units, count * sizeof(*units), output, total)) return false;
    if (prefix == 1) output[0] = (uint8_t)payload;
    else if (prefix == 2) {
        output[0] = (uint8_t)(0x80 + (payload >> 8)); output[1] = (uint8_t)payload;
    } else {
        output[0] = (uint8_t)(0xc0 + (payload >> 24)); output[1] = (uint8_t)(payload >> 16);
        output[2] = (uint8_t)(payload >> 8); output[3] = (uint8_t)payload;
    }
    for (i = 0; i < count; ++i) {
        uint32_t unit = units[i], low = unit & 0xff;
        if (unit > 0xff || (low >= 1 && low <= 8) || (low >= 0x0e && low <= 0x1f) ||
            low == 0x27 || low == 0x2d || low == 0x7f) flag = 1;
        edit_put16(output + prefix + i * 2, unit);
    }
    output[total - 1] = (uint8_t)flag;
    *encoded_size = total;
    return true;
}

typedef struct edit_section {
    uint32_t header, virtual_size, va, raw_size, raw, flags;
} edit_section;
typedef struct edit_stream {
    uint32_t header, offset, size;
    char name[33];
} edit_stream;
typedef struct edit_layout {
    uint32_t optional, cli, metadata, metadata_size, version_size, header_end;
    uint32_t file_alignment, section_alignment;
    edit_section sections[96];
    edit_stream streams[5];
    unsigned section_count, stream_count;
    int users;
} edit_layout;

static bool edit_range(size_t limit, uint64_t offset, uint64_t size)
{
    return offset <= limit && size <= limit - offset;
}
static uint64_t edit_align(uint64_t value, uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}
static bool edit_fail(const char **error, const char *message)
{
    if (error) *error = message;
    return false;
}
static bool edit_rva(const edit_layout *layout, size_t file_size, uint32_t rva, uint32_t size, uint32_t *offset)
{
    unsigned i;
    bool found = false;
    uint64_t result = 0;
    if ((uint64_t)rva + size > UINT32_MAX) return false;
    for (i = 0; i < layout->section_count; ++i) {
        const edit_section *section = &layout->sections[i];
        if (rva >= section->va) {
            uint32_t delta = rva - section->va;
            if (delta <= section->raw_size && size <= section->raw_size - delta) {
                if (found) return false;
                result = (uint64_t)section->raw + delta;
                found = true;
            }
        }
    }
    if (!found || !edit_range(file_size, result, size)) return false;
    *offset = (uint32_t)result;
    return true;
}

static bool edit_parse(const uint8_t *file, size_t size, edit_layout *layout, const char **error)
{
    uint32_t nt, optional_size, magic, directories, headers, table, max_raw, max_va;
    uint32_t at, metadata_end, version_end, cli_size, flags;
    uint64_t next;
    unsigned i, j, required = 0;
#define EDIT_REQUIRE(test,message) do { if (!(test)) return edit_fail(error,message); } while (0)
    xx_rt_memset(layout, 0, sizeof(*layout)); layout->users = -1;
    EDIT_REQUIRE(file && size >= 0x100 && size <= XX_DOTNET_EDIT_MAX_IMAGE, "input size outside supported bound");
    EDIT_REQUIRE(edit_u16(file) == 0x5a4d, "expected PE/CLI input");
    nt = edit_u32(file + 0x3c);
    EDIT_REQUIRE(edit_range(size, nt, 24) && edit_u32(file + nt) == 0x4550, "invalid PE signature");
    layout->section_count = edit_u16(file + nt + 6); optional_size = edit_u16(file + nt + 20); layout->optional = nt + 24;
    EDIT_REQUIRE(layout->section_count && layout->section_count <= 96 && optional_size >= 2 && edit_range(size, layout->optional, optional_size), "unsupported PE headers");
    magic = edit_u16(file + layout->optional);
    EDIT_REQUIRE(magic == 0x10b || magic == 0x20b, "unsupported optional header");
    directories = layout->optional + (magic == 0x10b ? 96u : 112u);
    EDIT_REQUIRE(optional_size >= directories - layout->optional + 15 * 8 && edit_u32(file + directories - 4) >= 15, "CLI directory missing");
    EDIT_REQUIRE(!edit_u32(file + directories + 4 * 8) && !edit_u32(file + directories + 4 * 8 + 4), "signed PE input is unsupported");
    layout->file_alignment = edit_u32(file + layout->optional + 36);
    layout->section_alignment = edit_u32(file + layout->optional + 32);
    EDIT_REQUIRE(layout->file_alignment >= 0x200 && layout->file_alignment <= 0x10000 && !(layout->file_alignment & (layout->file_alignment - 1)) &&
        layout->section_alignment >= layout->file_alignment && !(layout->section_alignment & (layout->section_alignment - 1)), "invalid PE alignments");
    headers = edit_u32(file + layout->optional + 60); table = layout->optional + optional_size;
    EDIT_REQUIRE((uint64_t)table + layout->section_count * 40 <= headers && headers <= size, "invalid PE header extent");
    max_raw = headers; max_va = 0;
    for (i = 0; i < layout->section_count; ++i) {
        edit_section *section = &layout->sections[i];
        const uint8_t *p = file + table + i * 40;
        section->header = table + i * 40; section->virtual_size = edit_u32(p + 8); section->va = edit_u32(p + 12);
        section->raw_size = edit_u32(p + 16); section->raw = edit_u32(p + 20); section->flags = edit_u32(p + 36);
        EDIT_REQUIRE(section->va >= max_va && section->va % layout->section_alignment == 0 && section->raw_size % layout->file_alignment == 0, "overlapping or unaligned PE sections");
        if (section->raw_size) {
            EDIT_REQUIRE(section->raw >= max_raw && section->raw % layout->file_alignment == 0 && edit_range(size, section->raw, section->raw_size), "invalid raw section range");
            max_raw = section->raw + section->raw_size;
        }
        next = (uint64_t)section->va + (section->virtual_size > section->raw_size ? section->virtual_size : section->raw_size);
        EDIT_REQUIRE(next <= UINT32_MAX, "PE virtual extent overflow"); max_va = (uint32_t)next;
    }
    EDIT_REQUIRE(max_raw == size && (uint64_t)layout->sections[layout->section_count - 1].raw + layout->sections[layout->section_count - 1].raw_size == size, "overlay or nonextendible final section");
    cli_size = edit_u32(file + directories + 14 * 8 + 4);
    EDIT_REQUIRE(cli_size >= 72 && edit_rva(layout, size, edit_u32(file + directories + 14 * 8), 72, &layout->cli) && edit_u32(file + layout->cli) >= 72, "invalid CLI directory");
    flags = edit_u32(file + layout->cli + 16);
    EDIT_REQUIRE((flags & 1) && !(flags & UINT32_C(0xfffdfffc)), "expected IL-only managed PE");
    EDIT_REQUIRE(!edit_u32(file + layout->cli + 32) && !edit_u32(file + layout->cli + 36), "strong-name signed input is unsupported");
    for (i = 40; i < 72; i += 4) EDIT_REQUIRE(!edit_u32(file + layout->cli + i), "native CLR directories are unsupported");
    layout->metadata_size = edit_u32(file + layout->cli + 12);
    EDIT_REQUIRE(layout->metadata_size >= 32 && layout->metadata_size <= XX_DOTNET_US_MAX_HEAP && edit_rva(layout, size, edit_u32(file + layout->cli + 8), layout->metadata_size, &layout->metadata), "invalid CLI metadata extent");
    EDIT_REQUIRE(edit_u32(file + layout->metadata) == UINT32_C(0x424a5342), "invalid metadata signature");
    layout->version_size = edit_u32(file + layout->metadata + 12);
    EDIT_REQUIRE(layout->version_size && layout->version_size <= 0x100 && layout->version_size % 4 == 0, "invalid metadata version extent");
    version_end = layout->metadata + 16 + layout->version_size; metadata_end = layout->metadata + layout->metadata_size;
    EDIT_REQUIRE((uint64_t)version_end + 4 <= metadata_end, "truncated metadata directory");
    layout->stream_count = edit_u16(file + version_end + 2); at = version_end + 4;
    EDIT_REQUIRE(layout->stream_count == 4 || layout->stream_count == 5, "expected standard metadata streams");
    for (i = 0; i < layout->stream_count; ++i) {
        edit_stream *stream = &layout->streams[i];
        uint32_t name_at, n = 0;
        unsigned kind;
        EDIT_REQUIRE(at <= metadata_end && metadata_end - at >= 9, "truncated metadata stream header");
        stream->header = at; stream->offset = edit_u32(file + at); stream->size = edit_u32(file + at + 4); name_at = at + 8;
        while (n < 32 && name_at + n < metadata_end && file[name_at + n]) ++n;
        EDIT_REQUIRE(n < 32 && name_at + n < metadata_end, "invalid metadata stream name");
        xx_rt_memcpy(stream->name, file + name_at, n); stream->name[n] = 0;
        EDIT_REQUIRE(stream->offset <= layout->metadata_size && stream->size <= layout->metadata_size - stream->offset, "invalid metadata stream range");
        if (!xx_rt_strcmp(stream->name, "#Strings")) kind = 1;
        else if (!xx_rt_strcmp(stream->name, "#Blob")) kind = 2;
        else if (!xx_rt_strcmp(stream->name, "#GUID")) kind = 4;
        else if (!xx_rt_strcmp(stream->name, "#~")) kind = 8;
        else if (!xx_rt_strcmp(stream->name, "#US")) { kind = 16; layout->users = (int)i; }
        else return edit_fail(error, "unexpected metadata stream");
        EDIT_REQUIRE(!(required & kind), "duplicate metadata stream"); required |= kind;
        next = (uint64_t)name_at + edit_align(n + 1, 4);
        EDIT_REQUIRE(next <= metadata_end, "truncated metadata stream name padding"); at = (uint32_t)next;
    }
    EDIT_REQUIRE((required & 15) == 15 && layout->stream_count == (layout->users < 0 ? 4u : 5u), "required metadata stream missing");
    for (i = 0; i < layout->stream_count; ++i) {
        const edit_stream *a = &layout->streams[i];
        uint64_t begin = (uint64_t)layout->metadata + a->offset;
        EDIT_REQUIRE(begin >= at, "metadata stream overlaps headers");
        for (j = i + 1; j < layout->stream_count; ++j) {
            const edit_stream *b = &layout->streams[j];
            EDIT_REQUIRE((uint64_t)a->offset + a->size <= b->offset || (uint64_t)b->offset + b->size <= a->offset, "metadata streams overlap");
        }
    }
    layout->header_end = at - layout->metadata;
    /* Share the canonical metadata schema/row-range validation with normal
     * inspection. Its device/cancellation borrows end before this returns. */
    {
        xx_dotnet_inspection inspection;
        xx_io_device *device = xx_io_mem_open_ro(file, size);
        xx_pd_struct pd = xx_pd_init();
        bool valid;
        xx_rt_memset(&inspection, 0, sizeof(inspection));
        if (!device) return edit_fail(error, "cannot allocate metadata view");
        valid = xx_dotnet_inspect_analyze_from_device(&inspection, device, 0, &pd) && inspection.cli.bValid;
        xx_dotnet_inspect_free(&inspection);
        xx_io_close(device);
        EDIT_REQUIRE(valid, "invalid .NET metadata tables");
    }
    return true;
#undef EDIT_REQUIRE
}

bool xx_dotnet_rebuild_user_strings(const uint8_t *file, size_t file_size,
    const uint8_t *pool, size_t pool_size, size_t max_output,
    uint8_t **output, size_t *output_size, const char **error)
{
    edit_layout layout;
    const edit_section *last;
    uint8_t *data;
    uint64_t us_offset, metadata_size, metadata_file, metadata_rva, virtual_size, raw_size, total, initialized, image_size;
    unsigned i, grow;
    if (output) *output = NULL;
    if (output_size) *output_size = 0;
    if (error) *error = NULL;
    if (!output || !output_size || !pool || !pool_size || pool_size > XX_DOTNET_US_MAX_HEAP || pool[0] || !max_output || max_output > XX_DOTNET_EDIT_MAX_IMAGE)
        return edit_fail(error, "invalid rebuild arguments or user-string pool");
    if (!edit_parse(file, file_size, &layout, error)) return false;
    last = &layout.sections[layout.section_count - 1]; grow = layout.users < 0 ? 12 : 0;
    us_offset = edit_align((uint64_t)layout.metadata_size + grow, 4);
    metadata_size = us_offset + pool_size; metadata_file = edit_align(file_size, 4);
    metadata_rva = last->va + metadata_file - last->raw;
    virtual_size = metadata_file - last->raw + metadata_size;
    if (last->virtual_size > virtual_size) virtual_size = last->virtual_size;
    raw_size = edit_align(metadata_file - last->raw + metadata_size, layout.file_alignment);
    total = last->raw + raw_size; image_size = edit_align(last->va + virtual_size, layout.section_alignment);
    initialized = edit_u32(file + layout.optional + 8) + ((last->flags & 0x40) ? raw_size - last->raw_size : raw_size);
    if (total > max_output || metadata_size > UINT32_MAX || metadata_rva + metadata_size > UINT32_MAX ||
        (uint64_t)last->va + virtual_size > UINT32_MAX || image_size > UINT32_MAX || initialized > UINT32_MAX)
        return edit_fail(error, "rebuilt PE exceeds bound");
    data = (uint8_t *)xx_mem_calloc(1, (size_t)total);
    if (!data) return edit_fail(error, "cannot allocate rebuilt file");
    xx_rt_memcpy(data, file, file_size);
    xx_rt_memcpy(data + metadata_file, file + layout.metadata, layout.header_end);
    xx_rt_memcpy(data + metadata_file + layout.header_end + grow, file + layout.metadata + layout.header_end, layout.metadata_size - layout.header_end);
    if (grow) {
        for (i = 0; i < layout.stream_count; ++i)
            edit_put32(data + metadata_file + layout.streams[i].header - layout.metadata, layout.streams[i].offset + grow);
        edit_put16(data + metadata_file + 18 + layout.version_size, layout.stream_count + 1);
        edit_put32(data + metadata_file + layout.header_end, (uint32_t)us_offset);
        edit_put32(data + metadata_file + layout.header_end + 4, (uint32_t)pool_size);
        edit_put32(data + metadata_file + layout.header_end + 8, UINT32_C(0x535523));
    } else {
        edit_put32(data + metadata_file + layout.streams[layout.users].header - layout.metadata, (uint32_t)us_offset);
        edit_put32(data + metadata_file + layout.streams[layout.users].header - layout.metadata + 4, (uint32_t)pool_size);
    }
    xx_rt_memcpy(data + metadata_file + us_offset, pool, pool_size);
    edit_put32(data + layout.cli + 8, (uint32_t)metadata_rva); edit_put32(data + layout.cli + 12, (uint32_t)metadata_size);
    edit_put32(data + last->header + 8, (uint32_t)virtual_size); edit_put32(data + last->header + 16, (uint32_t)raw_size);
    edit_put32(data + last->header + 36, (last->flags & UINT32_C(0xfdffffff)) | UINT32_C(0x40000040));
    edit_put32(data + layout.optional + 8, (uint32_t)initialized); edit_put32(data + layout.optional + 56, (uint32_t)image_size);
    edit_put32(data + layout.optional + 64, 0);
    *output = data; *output_size = (size_t)total;
    return true;
}
void xx_dotnet_edit_free(void *buffer)
{
    xx_mem_free(buffer);
}
