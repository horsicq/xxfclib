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
    uint32_t strong_name, strong_name_size, assembly;
    unsigned blob_width;
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

/* Removing a declared signature must not erase a second live use of its bytes.
 * This deliberately conservative validation is limited to the explicit strip
 * path; ordinary metadata rewriting still does not decode method bodies. */
static bool edit_protect(const edit_layout *layout, uint64_t offset, uint64_t size, const char **error)
{
    if (size && offset < (uint64_t)layout->strong_name + layout->strong_name_size &&
        layout->strong_name < offset + size)
        return edit_fail(error, "strong-name signature overlaps live image data");
    return true;
}
static bool edit_protect_rva(const edit_layout *layout, size_t size, uint32_t rva, uint32_t extent, const char **error)
{
    uint32_t offset;
    if (!extent) return true;
    if (!rva || !edit_rva(layout, size, rva, extent, &offset))
        return edit_fail(error, "live image data is not uniquely raw backed");
    return edit_protect(layout, offset, extent, error);
}
static bool edit_protect_string(const uint8_t *file, size_t size, const edit_layout *layout,
    uint32_t rva, unsigned prefix, uint32_t *work, const char **error)
{
    uint32_t offset, i;
    if (!rva || !edit_rva(layout, size, rva, prefix + 1, &offset))
        return edit_fail(error, "invalid import string extent");
    for (i = 0; i < UINT32_C(0x10000); ++i) {
        if (++*work > UINT32_C(0x100000) || !edit_rva(layout, size, rva, prefix + i + 1, &offset))
            return edit_fail(error, "unterminated or excessive import strings");
        if (!file[offset + prefix + i]) return edit_protect(layout, offset, prefix + i + 1, error);
    }
    return edit_fail(error, "import string exceeds bound");
}
static bool edit_protect_imports(const uint8_t *file, size_t size, const edit_layout *layout,
    uint32_t rva, uint32_t extent, unsigned width, const char **error)
{
    uint32_t offset, at = 0, descriptors = 0, slots = 0, strings = 0;
    if (!extent) return true;
    if (!edit_rva(layout, size, rva, extent, &offset)) return edit_fail(error, "invalid import directory extent");
    while (at <= extent && extent - at >= 20) {
        const uint8_t *descriptor = file + offset + at;
        uint32_t lookup, iat, name, index;
        if (++descriptors > UINT32_C(0x400)) return edit_fail(error, "too many import descriptors");
        if (!(edit_u32(descriptor) | edit_u32(descriptor + 4) | edit_u32(descriptor + 8) |
            edit_u32(descriptor + 12) | edit_u32(descriptor + 16))) return true;
        lookup = edit_u32(descriptor); name = edit_u32(descriptor + 12); iat = edit_u32(descriptor + 16);
        if (!lookup) lookup = iat;
        if (!lookup || !iat || !edit_protect_string(file, size, layout, name, 0, &strings, error)) return false;
        for (index = 0;; ++index) {
            uint32_t source, destination;
            uint64_t value, source_rva = (uint64_t)lookup + (uint64_t)index * width;
            uint64_t destination_rva = (uint64_t)iat + (uint64_t)index * width;
            if (++slots > UINT32_C(0x10000) || source_rva > UINT32_MAX || destination_rva > UINT32_MAX ||
                !edit_rva(layout, size, (uint32_t)source_rva, width, &source) ||
                !edit_rva(layout, size, (uint32_t)destination_rva, width, &destination))
                return edit_fail(error, "invalid or excessive import thunks");
            if (!edit_protect(layout, source, width, error) || !edit_protect(layout, destination, width, error)) return false;
            value = edit_u32(file + source);
            if (width == 8) value |= (uint64_t)edit_u32(file + source + 4) << 32;
            if (!value) break;
            if (value & (width == 8 ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000))) continue;
            if (value > UINT32_MAX || !edit_protect_string(file, size, layout, (uint32_t)value, 2, &strings, error)) return false;
        }
        at += 20;
    }
    return edit_fail(error, "unterminated import descriptors");
}
static bool edit_protect_resources(const uint8_t *file, size_t size, const edit_layout *layout,
    uint32_t root, uint32_t extent, uint32_t relative, uint32_t *stack,
    unsigned depth, uint32_t *work, const char **error)
{
    uint32_t at, count, i;
    unsigned j;
    if (depth == 16 || ++*work > UINT32_C(0x10000) || relative > extent || extent - relative < 16)
        return edit_fail(error, "invalid or excessive PE resource directories");
    for (j = 0; j < depth; ++j) if (stack[j] == relative) return edit_fail(error, "cyclic PE resource directory");
    stack[depth] = relative; at = root + relative;
    count = (uint32_t)edit_u16(file + at + 12) + edit_u16(file + at + 14);
    if (count > (extent - relative - 16) / 8) return edit_fail(error, "truncated PE resource entries");
    for (i = 0; i < count; ++i) {
        uint32_t name = edit_u32(file + at + 16 + i * 8), target = edit_u32(file + at + 20 + i * 8);
        if (++*work > UINT32_C(0x10000)) return edit_fail(error, "excessive PE resource entries");
        if (name & UINT32_C(0x80000000)) {
            uint32_t offset = name & UINT32_C(0x7fffffff);
            if (offset > extent || extent - offset < 2 || edit_u16(file + root + offset) > (extent - offset - 2) / 2)
                return edit_fail(error, "invalid PE resource name extent");
        }
        if (target & UINT32_C(0x80000000)) {
            if (!edit_protect_resources(file, size, layout, root, extent, target & UINT32_C(0x7fffffff), stack, depth + 1, work, error)) return false;
        } else {
            if (target > extent || extent - target < 16) return edit_fail(error, "invalid PE resource data entry");
            if (!edit_protect_rva(layout, size, edit_u32(file + root + target), edit_u32(file + root + target + 4), error)) return false;
        }
    }
    return true;
}
static bool edit_protect_pe(const uint8_t *file, size_t size, const edit_layout *layout, uint32_t directories, const char **error)
{
    uint32_t count = edit_u32(file + directories - 4), i, entry = edit_u32(file + layout->optional + 16);
    unsigned width = edit_u16(file + layout->optional) == 0x20b ? 8 : 4;
    if (count > 16 || count > (edit_u16(file + layout->optional - 4) - (directories - layout->optional)) / 8 ||
        !edit_range(size, directories, (uint64_t)count * 8)) return edit_fail(error, "unsupported PE data directory count");
    if (entry && !edit_protect_rva(layout, size, entry, 6, error)) return false;
    for (i = 0; i < count; ++i) {
        uint32_t rva = edit_u32(file + directories + i * 8), extent = edit_u32(file + directories + i * 8 + 4), offset;
        if (!rva && !extent) continue;
        if (i == 0 || i == 3 || i == 4 || i == 7 || i == 8 || i == 9 || i == 10 || i == 11 || i == 13 || i == 15)
            return edit_fail(error, "unsupported PE runtime directory while stripping strong name");
        if (!rva || !extent || !edit_rva(layout, size, rva, extent, &offset)) return edit_fail(error, "invalid live PE directory");
        if (!edit_protect(layout, offset, extent, error)) return false;
        if (i == 1 && !edit_protect_imports(file, size, layout, rva, extent, width, error)) return false;
        if (i == 2) {
            uint32_t stack[16], work = 0;
            if (!edit_protect_resources(file, size, layout, offset, extent, 0, stack, 0, &work, error)) return false;
        }
        if (i == 5) {
            uint32_t at = 0, work = 0;
            while (at < extent) {
                uint32_t page, block, j;
                if (extent - at < 8) return edit_fail(error, "truncated relocation block");
                page = edit_u32(file + offset + at); block = edit_u32(file + offset + at + 4);
                if (block < 8 || (block & 1) || block > extent - at) return edit_fail(error, "invalid relocation block");
                for (j = 8; j < block; j += 2) {
                    uint32_t value = edit_u16(file + offset + at + j), kind = value >> 12, field_width;
                    uint64_t target = (uint64_t)page + (value & 0xfff);
                    if (++work > UINT32_C(0x100000)) return edit_fail(error, "too many relocation entries while stripping strong name");
                    if (!kind) continue;
                    field_width = kind == 1 || kind == 2 ? 2 : kind == 3 ? 4 : kind == 10 ? 8 : 0;
                    if (!field_width || target > UINT32_MAX || !edit_protect_rva(layout, size, (uint32_t)target, field_width, error))
                        return edit_fail(error, "unsupported or overlapping relocation target");
                }
                at += block;
            }
        }
        if (i == 6) {
            uint32_t at;
            if (extent % 28 || extent / 28 > UINT32_C(0x1000)) return edit_fail(error, "invalid debug directory");
            for (at = 0; at < extent; at += 28) {
                uint32_t n = edit_u32(file + offset + at + 16), va = edit_u32(file + offset + at + 20), raw = edit_u32(file + offset + at + 24);
                if (!n) continue;
                if ((!va && !raw) || (raw && (!edit_range(size, raw, n) || !edit_protect(layout, raw, n, error))) ||
                    (va && !edit_protect_rva(layout, size, va, n, error))) return edit_fail(error, "invalid or overlapping debug data");
            }
        }
    }
    return edit_protect_rva(layout, size, edit_u32(file + layout->cli + 24), edit_u32(file + layout->cli + 28), error);
}

static bool edit_protect_methods(const uint8_t *file, size_t size, const edit_layout *layout,
    xx_dotnet_inspection *inspection, const char **error)
{
    uint32_t i;
    uint64_t total = 0;
    if (inspection->cli.pRows[6] > UINT32_C(0x100000)) return edit_fail(error, "too many managed methods");
    for (i = 1; i <= inspection->cli.pRows[6]; ++i) {
        int64_t row; uint32_t n, rva; xx_dotnet_method_body body;
        if (!xx_dotnet_inspect_table_row(inspection, 6, i, &row, &n) || row < 0 || n < 8 || !edit_range(size, (uint64_t)row, n))
            return edit_fail(error, "invalid managed method row");
        rva = edit_u32(file + row); if (!rva) continue;
        if ((edit_u16(file + row + 4) & 7) || (edit_u16(file + row + 6) & 0x2000) ||
            !xx_dotnet_inspect_method_body_ex(inspection, rva, XX_DOTNET_METHOD_BODY_ALLOW_UNALIGNED_FAT, &body) ||
            body.header_offset < 0 || !edit_protect(layout, (uint64_t)body.header_offset, body.total_size, error))
            return edit_fail(error, "unsupported or overlapping managed method body");
        total += body.total_size;
        if (total > XX_DOTNET_EDIT_MAX_IMAGE) return edit_fail(error, "managed method validation exceeds strip work bound");
    }
    return true;
}

static uint32_t edit_index(const uint8_t *bytes, unsigned width);
static bool edit_protect_fields(const uint8_t *file, size_t size, const edit_layout *layout,
    xx_dotnet_inspection *inspection, const char **error)
{
    uint32_t *classes = NULL, i;
    uint8_t *seen = NULL;
    bool valid = false;
    if (!inspection->cli.pRows[29]) return true;
    if (inspection->cli.pRows[2] > UINT32_C(0x100000) || inspection->cli.pRows[4] > UINT32_C(0x100000) ||
        inspection->cli.pRows[15] > UINT32_C(0x100000) || inspection->cli.pRows[29] > UINT32_C(0x100000))
        return edit_fail(error, "too many initialized field layouts");
    classes = xx_mem_calloc((size_t)inspection->cli.pRows[2] + 1, sizeof(*classes));
    seen = xx_mem_calloc((size_t)inspection->cli.pRows[4] + 1, 1);
    if (!classes || !seen) { edit_fail(error, "cannot allocate initialized field layouts"); goto done; }
    for (i = 1; i <= inspection->cli.pRows[15]; ++i) {
        int64_t row; uint32_t n, owner, extent;
        if (!xx_dotnet_inspect_table_row(inspection, 15, i, &row, &n) || row < 0 ||
            n < 6U + (unsigned)inspection->cli.pIndexSize[2] || !edit_range(size, (uint64_t)row, n)) goto unsupported;
        owner = edit_index(file + row + 6, (unsigned)inspection->cli.pIndexSize[2]); extent = edit_u32(file + row + 2);
        if (!owner || owner > inspection->cli.pRows[2] || classes[owner] || extent > XX_DOTNET_EDIT_MAX_IMAGE) goto unsupported;
        classes[owner] = extent + 1;
    }
    for (i = 1; i <= inspection->cli.pRows[29]; ++i) {
        int64_t row, blob; uint32_t n, rva, rid, index, blob_size, extent = 0;
        const uint8_t *signature; unsigned kind;
        if (!xx_dotnet_inspect_table_row(inspection, 29, i, &row, &n) || row < 0 ||
            n < 4U + (unsigned)inspection->cli.pIndexSize[4] || !edit_range(size, (uint64_t)row, n)) goto unsupported;
        rva = edit_u32(file + row); rid = edit_index(file + row + 4, (unsigned)inspection->cli.pIndexSize[4]);
        if (!rid || rid > inspection->cli.pRows[4] || seen[rid] ||
            !xx_dotnet_inspect_table_row(inspection, 4, rid, &row, &n) || row < 0 ||
            n < 2U + (unsigned)inspection->cli.nStringIndexSize + (unsigned)inspection->cli.nBlobIndexSize || !edit_range(size, (uint64_t)row, n) ||
            (edit_u16(file + row) & 0x110) != 0x110) goto unsupported;
        seen[rid] = 1;
        index = edit_index(file + row + 2 + inspection->cli.nStringIndexSize, (unsigned)inspection->cli.nBlobIndexSize);
        if (!xx_dotnet_inspect_blob(inspection, index, &blob, &blob_size) || blob < 0 || blob_size < 2 ||
            !edit_range(size, (uint64_t)blob, blob_size)) goto unsupported;
        signature = file + blob; if (signature[0] != 6) goto unsupported;
        kind = signature[1];
        if (kind == 2 || kind == 4 || kind == 5) extent = 1;
        else if (kind == 3 || kind == 6 || kind == 7) extent = 2;
        else if (kind == 8 || kind == 9 || kind == 12) extent = 4;
        else if (kind == 10 || kind == 11 || kind == 13) extent = 8;
        else if (kind == 24 || kind == 25) extent = edit_u16(file + layout->optional) == 0x20b ? 8 : 4;
        else if (kind == 0x11) {
            uint32_t owner, bytes;
            if (blob_size < 3) goto unsupported;
            owner = signature[2]; bytes = 1;
            if (owner >= 0xc0 && owner < 0xe0) {
                if (blob_size < 6) goto unsupported;
                owner = (owner & 0x1f) << 24 | (uint32_t)signature[3] << 16 | (uint32_t)signature[4] << 8 | signature[5]; bytes = 4;
            } else if (owner >= 0x80 && owner < 0xc0) {
                if (blob_size < 4) goto unsupported;
                owner = (owner & 0x3f) << 8 | signature[3]; bytes = 2;
            } else if (owner >= 0xe0) goto unsupported;
            if (blob_size != bytes + 2 || (owner & 3) || !(owner >>= 2) ||
                owner > inspection->cli.pRows[2] || !classes[owner]) goto unsupported;
            extent = classes[owner] - 1;
        }
        if (!extent || (kind != 0x11 && blob_size != 2)) goto unsupported;
        if (!edit_protect_rva(layout, size, rva, extent, error)) goto done;
    }
    valid = true; goto done;
unsupported:
    edit_fail(error, "unsupported initialized field extent while stripping strong name");
done:
    xx_mem_free(classes); xx_mem_free(seen); return valid;
}

static bool edit_parse_options(const uint8_t *file, size_t size, edit_layout *layout, bool strip_strong_name, const char **error)
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
    EDIT_REQUIRE((flags & 1) && !(flags & (UINT32_C(0xfffdfffc) & ~(strip_strong_name ? UINT32_C(8) : UINT32_C(0)))), "expected IL-only managed PE");
    if (strip_strong_name) {
        uint32_t signature_rva = edit_u32(file + layout->cli + 32);
        layout->strong_name_size = edit_u32(file + layout->cli + 36);
        EDIT_REQUIRE((!signature_rva && !layout->strong_name_size) ||
            (signature_rva && layout->strong_name_size && edit_rva(layout, size, signature_rva, layout->strong_name_size, &layout->strong_name)), "invalid strong-name signature extent");
        EDIT_REQUIRE(!layout->strong_name_size || (layout->strong_name >= headers &&
            ((uint64_t)layout->strong_name + layout->strong_name_size <= layout->cli || layout->strong_name >= layout->cli + 72)), "strong-name signature overlaps headers");
    } else EDIT_REQUIRE(!edit_u32(file + layout->cli + 32) && !edit_u32(file + layout->cli + 36), "strong-name signed input is unsupported");
    for (i = 40; i < 72; i += 4) EDIT_REQUIRE(!edit_u32(file + layout->cli + i), "native CLR directories are unsupported");
    layout->metadata_size = edit_u32(file + layout->cli + 12);
    EDIT_REQUIRE(layout->metadata_size >= 32 && layout->metadata_size <= XX_DOTNET_US_MAX_HEAP && edit_rva(layout, size, edit_u32(file + layout->cli + 8), layout->metadata_size, &layout->metadata), "invalid CLI metadata extent");
    EDIT_REQUIRE(!layout->strong_name_size || (layout->strong_name >= layout->metadata + layout->metadata_size ||
        (uint64_t)layout->strong_name + layout->strong_name_size <= layout->metadata), "strong-name signature overlaps metadata");
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
        if (valid && strip_strong_name && layout->strong_name_size)
            valid = edit_protect_pe(file, size, layout, directories, error) && edit_protect_methods(file, size, layout, &inspection, error) &&
                edit_protect_fields(file, size, layout, &inspection, error);
        if (valid && strip_strong_name) {
            if (inspection.cli.pRows[32] > 1) valid = false;
            else if (inspection.cli.pRows[32]) {
                int64_t assembly; uint32_t row_size;
                valid = xx_dotnet_inspect_table_row(&inspection, 32, 1, &assembly, &row_size) && assembly >= 0 &&
                    edit_range(size, (uint64_t)assembly, row_size) && row_size >= 16 + (uint32_t)inspection.cli.nBlobIndexSize &&
                    (uint64_t)assembly >= layout->metadata && (uint64_t)assembly + row_size <= (uint64_t)layout->metadata + layout->metadata_size;
                if (valid) { layout->assembly = (uint32_t)assembly; layout->blob_width = (unsigned)inspection.cli.nBlobIndexSize; }
            }
        }
        xx_dotnet_inspect_free(&inspection);
        xx_io_close(device);
        if (!valid) return error && *error ? false : edit_fail(error, "invalid .NET metadata tables");
    }
    return true;
#undef EDIT_REQUIRE
}

static bool edit_parse(const uint8_t *file, size_t size, edit_layout *layout, const char **error)
{
    return edit_parse_options(file, size, layout, false, error);
}

bool xx_dotnet_rebuild_user_strings(const uint8_t *file, size_t file_size,
    const uint8_t *pool, size_t pool_size, size_t max_output,
    uint8_t **output, size_t *output_size, const char **error)
{
    return xx_dotnet_rebuild_user_strings_ex(file, file_size, pool, pool_size, max_output, 0, output, output_size, error);
}

bool xx_dotnet_rebuild_user_strings_ex(const uint8_t *file, size_t file_size,
    const uint8_t *pool, size_t pool_size, size_t max_output, uint32_t flags,
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
    if (!output || !output_size || !pool || !pool_size || pool_size > XX_DOTNET_US_MAX_HEAP || pool[0] || !max_output || max_output > XX_DOTNET_EDIT_MAX_IMAGE || (flags & ~XX_DOTNET_EDIT_STRIP_STRONG_NAME))
        return edit_fail(error, "invalid rebuild arguments or user-string pool");
    if (!edit_parse_options(file, file_size, &layout, (flags & XX_DOTNET_EDIT_STRIP_STRONG_NAME) != 0, error)) return false;
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
    if (flags & XX_DOTNET_EDIT_STRIP_STRONG_NAME) {
        edit_put32(data + layout.cli + 16, edit_u32(data + layout.cli + 16) & ~UINT32_C(8));
        xx_rt_memset(data + layout.cli + 32, 0, 8);
        if (layout.strong_name_size) xx_rt_memset(data + layout.strong_name, 0, layout.strong_name_size);
        if (layout.assembly) {
            uint64_t relocated = metadata_file + layout.assembly - layout.metadata + grow;
            edit_put32(data + layout.assembly + 12, edit_u32(data + layout.assembly + 12) & ~UINT32_C(1));
            xx_rt_memset(data + layout.assembly + 16, 0, layout.blob_width);
            edit_put32(data + relocated + 12, edit_u32(data + relocated + 12) & ~UINT32_C(1));
            xx_rt_memset(data + relocated + 16, 0, layout.blob_width);
        }
    }
    edit_put32(data + layout.cli + 8, (uint32_t)metadata_rva); edit_put32(data + layout.cli + 12, (uint32_t)metadata_size);
    edit_put32(data + last->header + 8, (uint32_t)virtual_size); edit_put32(data + last->header + 16, (uint32_t)raw_size);
    edit_put32(data + last->header + 36, (last->flags & UINT32_C(0xfdffffff)) | UINT32_C(0x40000040));
    edit_put32(data + layout.optional + 8, (uint32_t)initialized); edit_put32(data + layout.optional + 56, (uint32_t)image_size);
    edit_put32(data + layout.optional + 64, 0);
    *output = data; *output_size = (size_t)total;
    return true;
}

static uint32_t edit_index(const uint8_t *bytes, unsigned width)
{
    return width == 2 ? edit_u16(bytes) : edit_u32(bytes);
}
static void edit_index_put(uint8_t *bytes, unsigned width, uint32_t value)
{
    if (width == 2) edit_put16(bytes, value); else edit_put32(bytes, value);
}
static bool edit_heap_string(const uint8_t *file, size_t size, int64_t heap, int64_t extent,
    uint32_t index, const uint8_t **bytes, size_t *length)
{
    size_t n = 0, units;
    if (heap < 0 || extent < 1 || !edit_range(size, (uint64_t)heap, (uint64_t)extent) ||
        index >= (uint64_t)extent || file[heap] != 0) return false;
    while (n < (size_t)extent - index && file[(size_t)heap + index + n]) ++n;
    if (n == (size_t)extent - index || n > 0x10000 ||
        !xx_dotnet_utf8_to_utf16(file + (size_t)heap + index, n, NULL, 0, &units)) return false;
    *bytes = file + (size_t)heap + index; *length = n;
    return true;
}
static bool edit_inspection(const uint8_t *file, size_t size, xx_dotnet_inspection *view,
    xx_io_device **device, xx_pd_struct *pd)
{
    xx_rt_memset(view, 0, sizeof(*view)); *pd = xx_pd_init();
    *device = xx_io_mem_open_ro(file, size);
    return *device && xx_dotnet_inspect_analyze_from_device(view, *device, 0, pd) && view->cli.bValid;
}
static bool edit_entry(xx_dotnet_inspection *view, const uint8_t *file, size_t size,
    uint32_t token)
{
    int64_t offset, blob;
    uint32_t row_size, blob_size;
    xx_dotnet_method_body body;
    uint32_t rid = token & 0xffffff;
    if ((token >> 24) != 6 || !rid || !xx_dotnet_inspect_table_row(view, 6, rid, &offset, &row_size) ||
        offset < 0 || !edit_range(size, (uint64_t)offset, row_size) || row_size < 12 ||
        (edit_u16(file + offset + 4) & 7) ||
        !(edit_u16(file + offset + 6) & 0x10) || (edit_u16(file + offset + 6) & 0x2000) ||
        !xx_dotnet_inspect_method_body(view, edit_u32(file + offset), &body) || !body.code_size) return false;
    if (!xx_dotnet_inspect_blob(view, edit_index(file + offset + 8 + view->cli.nStringIndexSize,
            (unsigned)view->cli.nBlobIndexSize), &blob, &blob_size) || blob < 0 ||
        !edit_range(size, (uint64_t)blob, blob_size) || (blob_size != 3 && blob_size != 5)) return false;
    return file[blob] == 0 && (file[blob + 2] == 1 || file[blob + 2] == 8) &&
        ((blob_size == 3 && file[blob + 1] == 0) ||
         (blob_size == 5 && file[blob + 1] == 1 && file[blob + 3] == 0x1d && file[blob + 4] == 0x0e));
}
/* A promoted netmodule already has the CLR import bootstrap. Both CLR entry
 * names have equal storage length, so its existing IAT/thunk stays in place. */
static void edit_promote_import(uint8_t *file, size_t size, const edit_layout *layout)
{
    uint32_t dirs = layout->optional + (edit_u16(file + layout->optional) == 0x10b ? 96u : 112u);
    uint32_t address = edit_u32(file + dirs + 8), count = edit_u32(file + dirs + 12), at;
    unsigned width = edit_u16(file + layout->optional) == 0x10b ? 4u : 8u;
    uint32_t i;
    if (!address || count < 20 || count > 0x10000 || !edit_rva(layout, size, address, count, &at)) return;
    for (i = 0; i + 20 <= count; i += 20) {
        uint32_t lookup = edit_u32(file + at + i), name = edit_u32(file + at + i + 12), name_at, j;
        if (!name) break;
        if (!lookup) lookup = edit_u32(file + at + i + 16);
        if (!edit_rva(layout, size, name, 12, &name_at)) continue;
        { static const char library[] = "mscoree.dll"; unsigned k;
            for (k = 0; k < sizeof(library); ++k) {
                unsigned c = file[name_at + k]; if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (c != (unsigned char)library[k]) break;
            }
            if (k != sizeof(library)) continue;
        }
        for (j = 0; j < 0x10000; ++j) {
            uint32_t thunk, item, hint;
            if ((uint64_t)lookup + (uint64_t)j * width > UINT32_MAX ||
                !edit_rva(layout, size, lookup + j * width, width, &thunk)) break;
            item = edit_u32(file + thunk);
            if (!item) break;
            if ((width == 4 && (item & 0x80000000)) || (width == 8 && edit_u32(file + thunk + 4))) continue;
            if (edit_rva(layout, size, item, 14, &hint) && !xx_rt_memcmp(file + hint + 2, "_CorDllMain", 12))
                xx_rt_memcpy(file + hint + 2, "_CorExeMain", 12);
        }
    }
}

bool xx_dotnet_promote_module(const uint8_t *file, size_t file_size,
    const uint8_t *donor, size_t donor_size, uint32_t entry_token, size_t max_output,
    uint8_t **output, size_t *output_size, const char **error)
{
    edit_layout layout, donor_layout;
    xx_dotnet_inspection view, donor_view;
    xx_io_device *device = NULL, *donor_device = NULL;
    xx_pd_struct pd, donor_pd;
    uint8_t *data = NULL, *tables = NULL, *metadata = NULL;
    const uint8_t *name = NULL, *culture = NULL;
    size_t name_size = 0, culture_size = 0;
    uint32_t row_size, strings_size, name_index, culture_index, new_tables_size;
    int64_t donor_row;
    int strings = -1, table = -1;
    unsigned i, string_width, blob_width, assembly_size;
    uint32_t table_header, count_insert, row_insert, row_at, table_at, counts, valid_lo, valid_hi;
    uint64_t metadata_size, metadata_file, metadata_rva, virtual_size, raw_size, total, initialized, image_size;
    const edit_section *last;
    bool okay = false;
#define PROMOTE_REQUIRE(test,message) do { if (!(test)) { edit_fail(error,message); goto done; } } while (0)
    xx_rt_memset(&view, 0, sizeof(view)); xx_rt_memset(&donor_view, 0, sizeof(donor_view));
    if (output) *output = NULL;
    if (output_size) *output_size = 0;
    if (error) *error = NULL;
    PROMOTE_REQUIRE(output && output_size && max_output && max_output <= XX_DOTNET_EDIT_MAX_IMAGE,
        "invalid module promotion arguments");
    PROMOTE_REQUIRE(edit_parse(file, file_size, &layout, error), "unsupported module layout");
    PROMOTE_REQUIRE(edit_inspection(file, file_size, &view, &device, &pd), "invalid module metadata");
    PROMOTE_REQUIRE(edit_entry(&view, file, file_size, entry_token), "unsupported managed entry point");
    PROMOTE_REQUIRE(view.cli.pRows[32] <= 1, "ambiguous assembly identity");
    if (view.cli.pRows[32] == 1) {
        PROMOTE_REQUIRE(file_size <= max_output, "assembly exceeds output bound");
        data = (uint8_t *)xx_mem_calloc(1, file_size);
        PROMOTE_REQUIRE(data, "cannot allocate assembly copy");
        xx_rt_memcpy(data, file, file_size); edit_put32(data + layout.cli + 20, entry_token);
        edit_put16(data + layout.optional - 2, edit_u16(data + layout.optional - 2) & ~0x2000u);
        edit_promote_import(data, file_size, &layout);
        edit_put32(data + layout.optional + 64, 0);
        *output = data; *output_size = file_size; data = NULL; okay = true; goto done;
    }
    PROMOTE_REQUIRE(edit_parse(donor, donor_size, &donor_layout, error) &&
        edit_inspection(donor, donor_size, &donor_view, &donor_device, &donor_pd) &&
        donor_view.cli.pRows[32] == 1 &&
        xx_dotnet_inspect_table_row(&donor_view, 32, 1, &donor_row, &row_size) && donor_row >= 0 &&
        edit_range(donor_size, (uint64_t)donor_row, row_size), "invalid donor assembly");
    string_width = (unsigned)view.cli.nStringIndexSize; blob_width = (unsigned)view.cli.nBlobIndexSize;
    { unsigned ds = (unsigned)donor_view.cli.nStringIndexSize, db = (unsigned)donor_view.cli.nBlobIndexSize;
        uint32_t key_index; int64_t key_at; uint32_t key_size;
        PROMOTE_REQUIRE(row_size == 16u + db + ds * 2u && !(edit_u32(donor + donor_row + 12) & 1), "signed donor identity is unsupported");
        key_index = edit_index(donor + donor_row + 16, db);
        PROMOTE_REQUIRE(xx_dotnet_inspect_blob(&donor_view, key_index, &key_at, &key_size) && !key_size,
            "nonempty donor public key is unsupported");
        PROMOTE_REQUIRE(edit_heap_string(donor, donor_size, donor_view.cli.nStringsOffset, donor_view.cli.nStringsSize,
            edit_index(donor + donor_row + 16 + db, ds), &name, &name_size) && name_size &&
            edit_heap_string(donor, donor_size, donor_view.cli.nStringsOffset, donor_view.cli.nStringsSize,
            edit_index(donor + donor_row + 16 + db + ds, ds), &culture, &culture_size), "invalid donor name/culture");
    }
    for (i = 0; i < layout.stream_count; ++i) {
        if (!xx_rt_strcmp(layout.streams[i].name, "#Strings")) strings = (int)i;
        if (!xx_rt_strcmp(layout.streams[i].name, "#~")) table = (int)i;
    }
    PROMOTE_REQUIRE(strings >= 0 && table >= 0 && layout.streams[strings].size &&
        file[layout.metadata + layout.streams[strings].offset] == 0, "missing string/table stream");
    name_index = layout.streams[strings].size; culture_index = culture_size ? name_index + (uint32_t)name_size + 1 : 0;
    PROMOTE_REQUIRE((uint64_t)name_index + name_size + 1 + (culture_size ? culture_size + 1 : 0) <= XX_DOTNET_US_MAX_HEAP,
        "promoted strings exceed bound");
    strings_size = name_index + (uint32_t)name_size + 1 + (culture_size ? (uint32_t)culture_size + 1 : 0);
    PROMOTE_REQUIRE(string_width == 4 || strings_size < 0x10000, "string index width would change");
    table_at = layout.metadata + layout.streams[table].offset;
    PROMOTE_REQUIRE(layout.streams[table].size >= 24, "truncated tables header");
    valid_lo = edit_u32(file + table_at + 8); valid_hi = edit_u32(file + table_at + 12);
    PROMOTE_REQUIRE(!(valid_hi & 1), "assembly table already present");
    counts = 0; count_insert = 24; row_insert = 0;
    for (i = 0; i < 64; ++i) {
        bool present = i < 32 ? ((valid_lo >> i) & 1) != 0 : ((valid_hi >> (i - 32)) & 1) != 0;
        if (present) { ++counts; if (i < 32) count_insert += 4; }
        if (i < 32) row_insert += view.cli.pRows[i] * (uint32_t)view.cli.pElementSize[i];
    }
    /* HeapSizes bit 0x40 adds one data DWORD after all row counts. It is
     * not another row-count slot; preserve it before inserting row data. */
    table_header = 24 + counts * 4 + ((file[table_at + 6] & 0x40) ? 4u : 0u);
    row_insert += table_header;
    assembly_size = 16 + blob_width + string_width * 2;
    PROMOTE_REQUIRE(table_header <= layout.streams[table].size && row_insert <= layout.streams[table].size &&
        (uint64_t)layout.streams[table].size + 4 + assembly_size <= XX_DOTNET_US_MAX_HEAP,
        "invalid assembly table insertion range");
    new_tables_size = layout.streams[table].size + 4 + assembly_size;
    tables = (uint8_t *)xx_mem_calloc(1, new_tables_size);
    PROMOTE_REQUIRE(tables, "cannot allocate promoted tables");
    xx_rt_memcpy(tables, file + table_at, count_insert);
    edit_put32(tables + count_insert, 1);
    xx_rt_memcpy(tables + count_insert + 4, file + table_at + count_insert, row_insert - count_insert);
    row_at = row_insert + 4;
    xx_rt_memcpy(tables + row_at, donor + donor_row, 16);
    edit_index_put(tables + row_at + 16, blob_width, 0);
    edit_index_put(tables + row_at + 16 + blob_width, string_width, name_index);
    edit_index_put(tables + row_at + 16 + blob_width + string_width, string_width, culture_index);
    xx_rt_memcpy(tables + row_at + assembly_size, file + table_at + row_insert, layout.streams[table].size - row_insert);
    edit_put32(tables + 12, valid_hi | 1); edit_put32(tables + 20, edit_u32(tables + 20) & ~UINT32_C(1));
    metadata_size = edit_align(layout.header_end, 4);
    for (i = 0; i < layout.stream_count; ++i)
        metadata_size = edit_align(metadata_size + (i == (unsigned)table ? new_tables_size : i == (unsigned)strings ? strings_size : layout.streams[i].size), 4);
    PROMOTE_REQUIRE(metadata_size <= XX_DOTNET_US_MAX_HEAP, "promoted metadata exceeds bound");
    metadata = (uint8_t *)xx_mem_calloc(1, (size_t)metadata_size);
    PROMOTE_REQUIRE(metadata, "cannot allocate promoted metadata");
    xx_rt_memcpy(metadata, file + layout.metadata, layout.header_end);
    { uint32_t cursor = (uint32_t)edit_align(layout.header_end, 4);
        for (i = 0; i < layout.stream_count; ++i) {
            const edit_stream *stream = &layout.streams[i];
            uint32_t length = i == (unsigned)table ? new_tables_size : i == (unsigned)strings ? strings_size : stream->size;
            edit_put32(metadata + stream->header - layout.metadata, cursor);
            edit_put32(metadata + stream->header - layout.metadata + 4, length);
            if (i == (unsigned)table) xx_rt_memcpy(metadata + cursor, tables, length);
            else {
                xx_rt_memcpy(metadata + cursor, file + layout.metadata + stream->offset, stream->size);
                if (i == (unsigned)strings) {
                    xx_rt_memcpy(metadata + cursor + name_index, name, name_size + 1);
                    if (culture_size) xx_rt_memcpy(metadata + cursor + culture_index, culture, culture_size + 1);
                }
            }
            cursor = (uint32_t)edit_align((uint64_t)cursor + length, 4);
        }
    }
    last = &layout.sections[layout.section_count - 1]; metadata_file = edit_align(file_size, 4);
    metadata_rva = last->va + metadata_file - last->raw; virtual_size = metadata_file - last->raw + metadata_size;
    if (last->virtual_size > virtual_size) virtual_size = last->virtual_size;
    raw_size = edit_align(metadata_file - last->raw + metadata_size, layout.file_alignment);
    total = last->raw + raw_size; image_size = edit_align(last->va + virtual_size, layout.section_alignment);
    initialized = edit_u32(file + layout.optional + 8) + ((last->flags & 0x40) ? raw_size - last->raw_size : raw_size);
    PROMOTE_REQUIRE(total <= max_output && metadata_rva + metadata_size <= UINT32_MAX &&
        (uint64_t)last->va + virtual_size <= UINT32_MAX && image_size <= UINT32_MAX && initialized <= UINT32_MAX,
        "promoted PE exceeds bound");
    data = (uint8_t *)xx_mem_calloc(1, (size_t)total);
    PROMOTE_REQUIRE(data, "cannot allocate promoted PE");
    xx_rt_memcpy(data, file, file_size); xx_rt_memcpy(data + metadata_file, metadata, (size_t)metadata_size);
    edit_put32(data + layout.cli + 8, (uint32_t)metadata_rva); edit_put32(data + layout.cli + 12, (uint32_t)metadata_size);
    edit_put32(data + layout.cli + 20, entry_token);
    edit_put16(data + layout.optional - 2, edit_u16(data + layout.optional - 2) & ~0x2000u);
    edit_promote_import(data, (size_t)total, &layout);
    edit_put32(data + last->header + 8, (uint32_t)virtual_size); edit_put32(data + last->header + 16, (uint32_t)raw_size);
    edit_put32(data + last->header + 36, (last->flags & UINT32_C(0xfdffffff)) | UINT32_C(0x40000040));
    edit_put32(data + layout.optional + 8, (uint32_t)initialized); edit_put32(data + layout.optional + 56, (uint32_t)image_size);
    edit_put32(data + layout.optional + 64, 0);
    *output = data; *output_size = (size_t)total; data = NULL; okay = true;
done:
    if (data) xx_mem_free(data);
    if (tables) xx_mem_free(tables);
    if (metadata) xx_mem_free(metadata);
    xx_dotnet_inspect_free(&view); xx_dotnet_inspect_free(&donor_view);
    if (device) xx_io_close(device);
    if (donor_device) xx_io_close(donor_device);
    return okay;
#undef PROMOTE_REQUIRE
}

void xx_dotnet_edit_free(void *buffer)
{
    xx_mem_free(buffer);
}
