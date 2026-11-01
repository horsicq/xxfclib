/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxfclib/extractor/xx_extractor.h"
#include "../io/platforms/xx_io_platform.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/archive/bz2/xx_bz2.h"
#include "xxfclib/formats/archive/cab/xx_cab.h"
#include "xxfclib/formats/archive/gz/xx_gz.h"
#include "xxfclib/formats/archive/lha/xx_lha.h"
#include "xxfclib/formats/archive/rar/xx_rar.h"
#include "xxfclib/formats/archive/tar/xx_tar.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include "xxfclib/formats/archive/zlib/xx_zlib.h"
#include "xxfclib/formats/audio/audio_mpeg_mp3/xx_audio_mpeg_mp3.h"
#include "xxfclib/formats/document/djvu/xx_djvu.h"
#include "xxfclib/formats/document/pdf/xxpdf.h"
#include "xxfclib/formats/executable/amigahunk/xx_amigahunk.h"
#include "xxfclib/formats/executable/dex/xx_dex.h"
#include "xxfclib/formats/executable/elf/xx_elf.h"
#include "xxfclib/formats/executable/java_class/xx_java_class.h"
#include "xxfclib/formats/executable/le/xx_le.h"
#include "xxfclib/formats/executable/macho/xx_macho.h"
#include "xxfclib/formats/executable/machofat/xx_machofat.h"
#include "xxfclib/formats/executable/ne/xx_ne.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/formats/image/bmp/xx_bmp.h"
#include "xxfclib/formats/image/gif/xx_gif.h"
#include "xxfclib/formats/image/ico/xx_ico.h"
#include "xxfclib/formats/image/jpeg/xx_jpeg.h"
#include "xxfclib/formats/image/png/xx_png.h"
#include "xxfclib/formats/image/tiff/xx_tiff.h"
#include "xxfclib/formats/other/riff/xx_riff.h"
#include "xxfclib/formats/video/mp4/xx_mp4.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/xx_format_streams.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define FACTORY(n)                                                                                                                         \
    static Abstractformat* open_##n(xx_io_device* d) { return (Abstractformat*)xx_##n##_create(d, 0); }                                    \
    static void close_##n(Abstractformat* f) { xx_##n##_free((xx_##n*)f); }
// clang-format off
FACTORY(pe) FACTORY(elf) FACTORY(macho) FACTORY(machofat) FACTORY(dex)
FACTORY(ne) FACTORY(le) FACTORY(amigahunk) FACTORY(java_class) FACTORY(pdf)
FACTORY(png) FACTORY(jpeg) FACTORY(tiff) FACTORY(bmp) FACTORY(gif) FACTORY(ico)
FACTORY(zip) FACTORY(rar) FACTORY(gz) FACTORY(zlib) FACTORY(cab)
FACTORY(bz2) FACTORY(lha) FACTORY(tar) FACTORY(mp4) FACTORY(audio_mpeg_mp3) FACTORY(riff) FACTORY(djvu)
// clang-format on
#undef FACTORY
    /* Raw carving only needs the authenticated physical 7z boundary. Reading
     * the whole solid decoder would add every optional codec just to inspect a
     * file. Both CRCs and the 64-bit next-header extent are part of the native
     * grammar. */
    static uint64_t little64(const unsigned char* p)
{
    unsigned i;
    uint64_t v = 0;
    for (i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}
static uint32_t little32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool seven_info(Abstractformat* f, xx_pd_struct* pd)
{
    unsigned char h[32];
    int64_t total;
    uint64_t off, size, crc;
    if (!f || xx_pd_is_stopped(pd) || (total = xx_io_total_size(f->device)) < 32 || !xx_io_read_at(f->device, 0, h, 32)
        || xx_rt_memcmp(h, "7z\xbc\xaf\x27\x1c", 6) || h[6] != 0 || xx_crc32_calc(0, h + 12, 20) != little32(h + 8))
        return false;
    off = little64(h + 12);
    size = little64(h + 20);
    if (off > (uint64_t)total - 32 || size > (uint64_t)total - 32 - off
        || !xx_crc_calculate_device_by_type(f->device, (int64_t)(32 + off), (int64_t)size, XX_CRC_TYPE_CRC32, pd, &crc)
        || (uint32_t)crc != little32(h + 28))
        return false;
    f->format_size = (int64_t)(32 + off + size);
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
static int64_t seven_size(Abstractformat* f, xx_pd_struct* pd)
{
    return f && (f->base_info_handled || seven_info(f, pd)) ? f->format_size : -1;
}
static Abstractformat* open_7zip(xx_io_device* d)
{
    Abstractformat* f = xx_mem_alloc(sizeof(*f));
    if (!f)
        return NULL;
    xx_format_init(f, d, 0);
    f->file_type = XX_FILE_TYPE_7ZIP;
    f->is_archive = true;
    xx_format_set_extension(f, "7z");
    f->check_is_valid = seven_info;
    f->handle_base_info = seven_info;
    f->get_format_size = seven_size;
    return f;
}
static void close_7zip(Abstractformat* f)
{
    if (f)
        xx_format_cleanup_extra_parameters(f);
    xx_mem_free(f);
}
typedef struct reader_desc {
    xx_extractor_type type;
    Abstractformat* (*open)(xx_io_device*);
    void (*close)(Abstractformat*);
} reader_desc;
#define ROW(t, n, label, def) { { XX_FILE_TYPE_##t, label, def }, open_##n, close_##n }
/* The deliberately bounded catalog mirrors XExtractor's offered families.
 * It avoids the global reader factory, which pulls every registered reader
 * into small applications using only the extraction widget. */
static const reader_desc readers[]
    = { ROW(PE, pe, "PE", true), ROW(ELF, elf, "ELF", true), ROW(MACHOFAT, machofat, "Mach-O Fat", true), ROW(MACHO, macho, "Mach-O", true),
          ROW(PDF, pdf, "PDF", true), ROW(PNG, png, "PNG", true), ROW(JPEG, jpeg, "JPEG", true), ROW(TIFF, tiff, "TIFF", false),
          ROW(BMP, bmp, "BMP", true), ROW(GIF, gif, "GIF", true), ROW(ICO, ico, "ICO", false), ROW(DEX, dex, "DEX", true),
          ROW(ZIP, zip, "ZIP", true), ROW(RAR, rar, "RAR", true), ROW(GZ, gz, "GZIP", true), ROW(ZLIB, zlib, "ZLIB", true),
          ROW(7ZIP, 7zip, "7-Zip", true), ROW(CAB, cab, "CAB", true), ROW(AUDIO_MPEG_MP3, audio_mpeg_mp3, "MP3", true),
          ROW(MP4, mp4, "MP4", true), ROW(RIFF, riff, "RIFF", true), ROW(LE, le, "LE", false), ROW(NE, ne, "NE", false),
          ROW(AMIGAHUNK, amigahunk, "Amiga Hunk", false), ROW(JAVA_CLASS, java_class, "Java Class", false), ROW(BZ2, bz2, "BZIP2", false),
          ROW(LHA, lha, "LHA", false), ROW(DJVU, djvu, "DjVu", false), { { XX_FILE_TYPE_BINARY, "Binary", false }, NULL, NULL } };
#undef ROW
typedef struct anchor {
    unsigned char bytes[8];
    unsigned char length, delta, reader;
} anchor;
static const anchor anchors[] = { { { 'M', 'Z' }, 2, 0, 0 }, { { 0x7f, 'E', 'L', 'F' }, 4, 0, 1 }, { { 0xca, 0xfe, 0xba, 0xbe }, 4, 0, 2 },
    { { 0xbe, 0xba, 0xfe, 0xca }, 4, 0, 2 }, { { 0xfe, 0xed, 0xfa, 0xce }, 4, 0, 3 }, { { 0xce, 0xfa, 0xed, 0xfe }, 4, 0, 3 },
    { { 0xfe, 0xed, 0xfa, 0xcf }, 4, 0, 3 }, { { 0xcf, 0xfa, 0xed, 0xfe }, 4, 0, 3 }, { { '%', 'P', 'D', 'F' }, 4, 0, 4 },
    { { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 }, 8, 0, 5 }, { { 0xff, 0xd8, 0xff }, 3, 0, 6 }, { { 'I', 'I', 42, 0 }, 4, 0, 7 },
    { { 'M', 'M', 0, 42 }, 4, 0, 7 }, { { 'B', 'M' }, 2, 0, 8 }, { { 'G', 'I', 'F', '8' }, 4, 0, 9 }, { { 0, 0, 1, 0 }, 4, 0, 10 },
    { { 0, 0, 2, 0 }, 4, 0, 10 }, { { 'd', 'e', 'x', 10 }, 4, 0, 11 }, { { 'P', 'K', 3, 4 }, 4, 0, 12 }, { { 'P', 'K', 5, 6 }, 4, 0, 12 },
    { { 'R', 'a', 'r', '!', 26, 7 }, 6, 0, 13 }, { { 31, 139, 8 }, 3, 0, 14 }, { { 0x78, 0x01 }, 2, 0, 15 }, { { 0x78, 0x5e }, 2, 0, 15 },
    { { 0x78, 0x9c }, 2, 0, 15 }, { { 0x78, 0xda }, 2, 0, 15 }, { { '7', 'z', 0xbc, 0xaf, 0x27, 0x1c }, 6, 0, 16 },
    { { 'M', 'S', 'C', 'F' }, 4, 0, 17 }, { { 'I', 'D', '3' }, 3, 0, 18 }, { { 'f', 't', 'y', 'p' }, 4, 4, 19 },
    { { 'R', 'I', 'F', 'F' }, 4, 0, 20 }, { { 'M', 'Z' }, 2, 0, 21 }, { { 'M', 'Z' }, 2, 0, 22 }, { { 0, 0, 3, 0xf3 }, 4, 0, 23 },
    { { 0, 0, 3, 0xe7 }, 4, 0, 23 }, { { 0xca, 0xfe, 0xba, 0xbe }, 4, 0, 24 }, { { 'B', 'Z', 'h' }, 3, 0, 25 },
    { { '-', 'l', 'h' }, 3, 2, 26 }, { { '-', 'l', 'z' }, 3, 2, 26 }, { { '-', 'p', 'm' }, 3, 2, 26 },
    { { 'A', 'T', '&', 'T', 'F', 'O', 'R', 'M' }, 8, 0, 27 }, { { 'S', 'D', 'J', 'V', 'F', 'O', 'R', 'M' }, 8, 0, 27 } };
static xx_file_type_t family(xx_file_type_t t)
{
    if (t == XX_FILE_TYPE_PE32 || t == XX_FILE_TYPE_PE64)
        return XX_FILE_TYPE_PE;
    if (t == XX_FILE_TYPE_ELF32 || t == XX_FILE_TYPE_ELF64)
        return XX_FILE_TYPE_ELF;
    if (t == XX_FILE_TYPE_MACHO32 || t == XX_FILE_TYPE_MACHO64)
        return XX_FILE_TYPE_MACHO;
    if (t == XX_FILE_TYPE_ZIP64 || t == XX_FILE_TYPE_JAR || t == XX_FILE_TYPE_APK || t == XX_FILE_TYPE_IPA)
        return XX_FILE_TYPE_ZIP;
    return t;
}
size_t xx_extractor_type_count(void) { return COUNT(readers); }
const xx_extractor_type* xx_extractor_type_at(size_t i) { return i < COUNT(readers) ? &readers[i].type : NULL; }
xx_extractor_options xx_extractor_defaults(void)
{
    xx_extractor_options o;
    xx_mem_zero(&o, sizeof(o));
    o.deep_scan = true;
    o.length = UINT64_MAX;
    o.max_member_size = UINT64_C(256) * 1024 * 1024;
    o.chunk_size = 65536;
    return o;
}
const char* xx_extractor_status_name(xx_extractor_status s)
{
    switch (s) {
    case XX_EXTRACTOR_OK:
        return "Success";
    case XX_EXTRACTOR_CANCELLED:
        return "Cancelled";
    case XX_EXTRACTOR_INVALID:
        return "Invalid extraction input";
    case XX_EXTRACTOR_IO:
        return "Extraction I/O error";
    case XX_EXTRACTOR_NO_MEMORY:
        return "Out of memory";
    case XX_EXTRACTOR_UNSUPPORTED:
        return "Extractor mode is unavailable for this format";
    case XX_EXTRACTOR_SINK:
        return "Result consumer failed";
    case XX_EXTRACTOR_LIMIT:
        return "Decoded member exceeds extraction limit";
    default:
        return "Extraction failed";
    }
}
static xx_extractor_status fail(xx_pd_struct* pd, xx_extractor_status s, const char* m)
{
    if (xx_pd_is_stopped(pd))
        return XX_EXTRACTOR_CANCELLED;
    xx_pd_set_error(pd, (int)s, m ? m : xx_extractor_status_name(s));
    return s;
}
bool xx_extractor_format_available(xx_file_type_t t)
{
    t = family(t);
    return t == XX_FILE_TYPE_PE || t == XX_FILE_TYPE_ZIP || t == XX_FILE_TYPE_TAR || t == XX_FILE_TYPE_PDF;
}
static const reader_desc* find_reader(xx_file_type_t type)
{
    size_t i;
    type = family(type);
    for (i = 0; i < COUNT(readers); i++)
        if (readers[i].type.file_type == type)
            return &readers[i];
    return NULL;
}
static bool selected(const xx_extractor_options* o, xx_file_type_t type)
{
    size_t i;
    if (o->all_types)
        return true;
    type = family(type);
    if (o->types) {
        for (i = 0; i < o->type_count; i++)
            if (family(o->types[i]) == type)
                return true;
        return false;
    }
    for (i = 0; i < COUNT(readers); i++)
        if (readers[i].type.file_type == type)
            return readers[i].type.enabled_by_default;
    return false;
}
static bool crc_range(xx_io_device* d, uint64_t off, uint64_t n, xx_pd_struct* pd, uint32_t* crc)
{
    uint64_t result;
    if (off > INT64_MAX || n > INT64_MAX)
        return false;
    if (!xx_crc_calculate_device_by_type(d, (int64_t)off, (int64_t)n, XX_CRC_TYPE_CRC32, pd, &result))
        return false;
    *crc = (uint32_t)result;
    return true;
}
/* A reader sees a zero-based bounded view, so offsets and sizes cannot escape
 * the requested scan range. Failed structural candidates are ordinary misses.
 */
static Abstractformat* probe(
    xx_io_device* d, uint64_t off, uint64_t remaining, const reader_desc* desc, xx_io_device** view, xx_pd_struct* pd)
{
    Abstractformat* f;
    int error = pd ? pd->last_error : 0;
    char message[128];
    if (pd)
        xx_mem_copy(message, pd->error_string, sizeof(message));
    *view = xx_io_sub_open_ro(d, (int64_t)off, (int64_t)remaining);
    if (!*view)
        return NULL;
    f = desc->open(*view);
    if (f && xx_format_is_valid(f, pd) && xx_format_handle_base_info(f, pd))
        return f;
    if (f)
        desc->close(f);
    xx_io_close(*view);
    *view = NULL;
    if (pd) {
        pd->last_error = error;
        xx_mem_copy(pd->error_string, message, sizeof(message));
    }
    return NULL;
}
static xx_file_type_t payload_type(xx_io_device* d, xx_pd_struct* pd, char* ext, size_t extcap)
{
    unsigned char head[16];
    int64_t total = xx_io_total_size(d);
    size_t a;
    if (extcap)
        ext[0] = 0;
    if (total <= 0)
        return XX_FILE_TYPE_BINARY;
    xx_mem_zero(head, sizeof(head));
    if (!xx_io_read_at(d, 0, head, (size_t)(total < 16 ? total : 16)))
        return XX_FILE_TYPE_UNKNOWN;
    for (a = 0; a < COUNT(anchors); a++) {
        const anchor* v = &anchors[a];
        xx_io_device* view;
        Abstractformat* f;
        xx_file_type_t t;
        if ((int64_t)v->delta + v->length > total || xx_rt_memcmp(head + v->delta, v->bytes, v->length))
            continue;
        f = probe(d, 0, (uint64_t)total, &readers[v->reader], &view, pd);
        if (!f)
            continue;
        t = f->file_type;
        if (extcap)
            xx_rt_snprintf(ext, extcap, "%s", xx_format_get_extension(f));
        readers[v->reader].close(f);
        xx_io_close(view);
        return t;
    }
    return XX_FILE_TYPE_BINARY;
}
xx_file_type_t xx_extractor_detect_device(xx_io_device* device, xx_pd_struct* pd)
{
    char ext[32];
    xx_file_type_t type;
    int64_t saved;
    if (!device || xx_pd_is_stopped(pd) || (saved = xx_io_tell(device)) < 0)
        return XX_FILE_TYPE_UNKNOWN;
    type = payload_type(device, pd, ext, sizeof(ext));
    if (type == XX_FILE_TYPE_BINARY) {
        Abstractformat* f = open_tar(device);
        if (f && xx_format_is_valid(f, pd) && xx_format_handle_base_info(f, pd))
            type = XX_FILE_TYPE_TAR;
        if (f)
            close_tar(f);
    }
    if (xx_io_seek64(device, saved, SEEK_SET) != 0 || xx_pd_is_stopped(pd))
        return XX_FILE_TYPE_UNKNOWN;
    return type;
}
static bool publish(const xx_extractor_options* o, xx_extractor_record* r, xx_extractor_sink sink, void* u, xx_extractor_stats* s)
{
    if (!selected(o, r->file_type))
        return true;
    if (sink && !sink(r, u))
        return false;
    ++s->record_count;
    if (o->result_limit && s->record_count >= o->result_limit)
        s->limit_reached = true;
    return true;
}
static xx_extractor_status raw_scan(
    xx_io_device* d, const xx_extractor_options* o, xx_pd_struct* pd, xx_extractor_sink sink, void* u, xx_extractor_stats* s)
{
    uint8_t* buffer;
    uint64_t total = (uint64_t)xx_io_total_size(d), next = 0;
    int level = xx_pd_enter_level(pd, total, "Searching embedded formats");
    int heads[256], links[COUNT(anchors)];
    size_t a;
    xx_extractor_status status = XX_EXTRACTOR_OK;
    buffer = xx_mem_alloc(o->chunk_size + 16);
    if (!buffer) {
        xx_pd_leave_level(pd, level);
        return fail(pd, XX_EXTRACTOR_NO_MEMORY, NULL);
    }
    for (a = 0; a < 256; a++)
        heads[a] = -1;
    for (a = 0; a < COUNT(anchors); a++) {
        unsigned c = anchors[a].bytes[0];
        links[a] = heads[c];
        heads[c] = (int)a;
    }
    while (next < total && !s->limit_reached) {
        size_t n = (size_t)((total - next) < o->chunk_size + 16 ? (total - next) : o->chunk_size + 16), i,
               steps = n > o->chunk_size ? o->chunk_size : n;
        uint64_t skip_to = next;
        if (xx_pd_is_stopped(pd)) {
            status = XX_EXTRACTOR_CANCELLED;
            break;
        }
        if (!xx_io_read_at(d, (int64_t)next, buffer, n)) {
            status = fail(pd, XX_EXTRACTOR_IO, "Cannot read extractor input");
            break;
        }
        for (i = 0; i < steps && !s->limit_reached; i++) {
            int ai;
            uint64_t position = next + i;
            if (position < skip_to)
                continue;
            if ((i & 4095) == 0) {
                xx_pd_set_current(pd, level, position);
                if (xx_pd_is_stopped(pd)) {
                    status = XX_EXTRACTOR_CANCELLED;
                    break;
                }
            }
            for (ai = heads[buffer[i]]; ai >= 0; ai = links[ai]) {
                const anchor* v = &anchors[ai];
                const reader_desc* rd = &readers[v->reader];
                uint64_t begin;
                xx_io_device* view;
                Abstractformat* f;
                int64_t length;
                xx_extractor_record r;
                char info[256], ext[32];
                if (!selected(o, rd->type.file_type) || i + v->length > n || position < v->delta
                    || xx_rt_memcmp(buffer + i, v->bytes, v->length))
                    continue;
                begin = position - v->delta;
                f = probe(d, begin, total - begin, rd, &view, pd);
                if (!f)
                    continue;
                length = xx_format_get_format_size(f, pd);
                if (length <= 0 || (uint64_t)length > total - begin) {
                    rd->close(f);
                    xx_io_close(view);
                    continue;
                }
                xx_mem_zero(&r, sizeof(r));
                r.offset = o->offset + begin;
                r.size = r.unpacked_size = (uint64_t)length;
                r.file_type = f->file_type;
                r.method = XX_EXTRACTOR_STORE;
                xx_rt_snprintf(r.compression, sizeof(r.compression), "Store");
                r.max_member_size = o->max_member_size;
                xx_rt_snprintf(ext, sizeof(ext), "%s", xx_format_get_extension(f));
                r.extension = ext;
                xx_rt_snprintf(
                    info, sizeof(info), "%s%s%s", xx_format_file_type_to_string(f->file_type), f->version[0] ? " " : "", f->version);
                r.info = info;
                r.name = "";
                rd->close(f);
                xx_io_close(view);
                if (!crc_range(d, begin, r.size, pd, &r.crc32)) {
                    status = fail(pd, XX_EXTRACTOR_IO, "Cannot checksum extracted range");
                    break;
                }
                r.has_crc32 = true;
                if (!publish(o, &r, sink, u, s)) {
                    status = fail(pd, XX_EXTRACTOR_SINK, NULL);
                    break;
                }
                /* LHA headers are themselves the archive member chain. */
                if (!o->deep_scan || rd->type.file_type == XX_FILE_TYPE_LHA) {
                    if (begin + r.size > skip_to)
                        skip_to = begin + r.size;
                }
                break;
            }
            if (status != XX_EXTRACTOR_OK)
                break;
        }
        if (status != XX_EXTRACTOR_OK)
            break;
        next += steps;
        if (skip_to > next)
            next = skip_to;
        s->bytes_scanned = next;
    }
    if (status == XX_EXTRACTOR_OK && !s->limit_reached)
        s->bytes_scanned = total;
    xx_pd_set_current(pd, level, s->bytes_scanned);
    xx_pd_leave_level(pd, level);
    xx_mem_free(buffer);
    return status;
}
static bool copy_range(xx_io_device* source, uint64_t offset, uint64_t size, xx_io_device* destination, xx_pd_struct* pd)
{
    unsigned char buffer[65536];
    uint64_t done = 0;
    int level = xx_pd_enter_level(pd, size, "Copying extracted data");
    bool ok = true;
    while (done < size) {
        size_t n = (size_t)(size - done > sizeof(buffer) ? sizeof(buffer) : size - done);
        if (xx_pd_is_stopped(pd) || !xx_io_read_at(source, (int64_t)(offset + done), buffer, n)
            || xx_io_write(destination, buffer, n) != (ssize_t)n) {
            ok = false;
            break;
        }
        done += n;
        xx_pd_set_current(pd, level, done);
    }
    xx_pd_leave_level(pd, level);
    return ok && !xx_pd_is_stopped(pd);
}
static xx_extractor_status pe_scan(
    xx_io_device* d, const xx_extractor_options* o, xx_pd_struct* pd, xx_extractor_sink sink, void* u, xx_extractor_stats* s)
{
    Abstractformat* f = open_pe(d);
    xx_resource_state* state = NULL;
    const xx_resource_record* native;
    xx_extractor_status status = XX_EXTRACTOR_OK;
    int64_t total = xx_io_total_size(d);
    int level = xx_pd_enter_level(pd, 0, "Analyzing resources");
    if (!f) {
        status = fail(pd, XX_EXTRACTOR_NO_MEMORY, NULL);
        goto done;
    }
    if (!xx_format_is_valid(f, pd) || !xx_format_handle_base_info(f, pd)) {
        status = fail(pd, XX_EXTRACTOR_INVALID, "Invalid PE resource container");
        goto done;
    }
    state = xx_format_create_resources_reading(f, pd);
    if (!state) {
        status = fail(pd, XX_EXTRACTOR_INVALID, "Cannot enumerate PE resources");
        goto done;
    }
    while ((native = xx_format_get_current_resource(f, state)) != NULL && !s->limit_reached) {
        xx_extractor_record r;
        xx_io_device* view;
        char name[512], ext[32];
        if (xx_pd_is_stopped(pd)) {
            status = XX_EXTRACTOR_CANCELLED;
            break;
        }
        if (native->offset < 0 || native->size < 0 || native->offset > total || native->size > total - native->offset) {
            status = fail(pd, XX_EXTRACTOR_INVALID, "Invalid resource extent");
            break;
        }
        if (native->size > 0) {
            xx_mem_zero(&r, sizeof(r));
            r.offset = o->offset + (uint64_t)native->offset;
            r.size = r.unpacked_size = (uint64_t)native->size;
            r.container_type = f->file_type;
            xx_rt_snprintf(r.compression, sizeof(r.compression), "Store");
            r.container_offset = o->offset;
            r.container_size = (uint64_t)total;
            r.archive_index = (uint64_t)state->current_index;
            r.max_member_size = o->max_member_size;
            view = xx_io_sub_open_ro(d, native->offset, native->size);
            if (!view) {
                status = fail(pd, XX_EXTRACTOR_NO_MEMORY, NULL);
                break;
            }
            r.file_type = payload_type(view, pd, ext, sizeof(ext));
            xx_io_close(view);
            r.extension = ext;
            xx_rt_snprintf(name, sizeof(name), "%s/%s/%s (%u/%u/%u)", native->type_name ? native->type_name : "Resource",
                native->name ? native->name : "", native->language_name ? native->language_name : "", native->type_id, native->name_id,
                native->language_id);
            r.name = name;
            r.info = xx_format_file_type_to_string(r.file_type);
            if (!crc_range(d, (uint64_t)native->offset, r.size, pd, &r.crc32)) {
                status = fail(pd, XX_EXTRACTOR_IO, "Cannot checksum resource");
                break;
            }
            r.has_crc32 = true;
            if (!publish(o, &r, sink, u, s)) {
                status = fail(pd, XX_EXTRACTOR_SINK, NULL);
                break;
            }
        }
        xx_pd_set_current(pd, level, (uint64_t)state->current_index + 1);
        if (!xx_format_resource_move_to_next(f, state, pd))
            break;
    }
    if (state->failed && status == XX_EXTRACTOR_OK)
        status = fail(pd, XX_EXTRACTOR_INVALID, "Incomplete PE resource table");
done:
    if (state)
        xx_format_free_resources_reading(f, state);
    if (f)
        close_pe(f);
    xx_pd_leave_level(pd, level);
    s->bytes_scanned = (uint64_t)total;
    return status;
}
static bool set_limits(xx_archive_record* limits, uint64_t maximum)
{
    xx_archive_record_init(limits);
    return xx_archive_record_set_meta_u64(limits, XX_META_ID_OPT_MAX_MEMBER_SIZE, maximum)
        && xx_archive_record_set_meta_u64(limits, XX_META_ID_OPT_MEMORY_LIMIT, maximum);
}
/* ZIP records keep data_offset at -1 until the local header is read on demand,
 * so resolve it before the extent checks below. A failed resolve stays -1. */
static const xx_archive_record* current_record(Abstractformat* f, xx_file_type_t type, xx_archive_record_state* state)
{
    const xx_archive_record* native = xx_format_get_current_archive_record(f, state);
    if (native && type == XX_FILE_TYPE_ZIP && native->data_offset < 0)
        (void)xx_zip_resolve_record_data_offset((xx_zip*)f, (xx_archive_record*)native);
    return native;
}
static bool decode_member(Abstractformat* f, xx_file_type_t type, xx_archive_record_state* state, xx_io_device* out, xx_pd_struct* pd)
{
    const xx_archive_record* native = xx_format_get_current_archive_record(f, state);
    if (type == XX_FILE_TYPE_ZIP)
        return xx_zip_unpack_current_archive_record_to_device(f, state, out, pd);
    if (type == XX_FILE_TYPE_PDF)
        return xx_pdf_unpack_current_archive_record_to_device(f, state, out, pd);
    return native && native->data_offset >= 0 && native->compressed_size >= 0
        && copy_range(f->device, (uint64_t)native->data_offset, (uint64_t)native->compressed_size, out, pd);
}
static const char* zip_method_name(uint64_t method)
{
    switch (method) {
    case 0:
        return "Store";
    case 1:
        return "Shrink";
    case 2:
    case 3:
    case 4:
    case 5:
        return "Reduce";
    case 6:
        return "Implode";
    case 8:
        return "Deflate";
    case 9:
        return "Deflate64";
    case 12:
        return "BZIP2";
    case 14:
        return "LZMA";
    case 20:
    case 93:
        return "Zstandard";
    case 95:
        return "XZ";
    case 98:
        return "PPMd";
    default:
        return "Unknown";
    }
}
static xx_extractor_status archive_scan(xx_io_device* d, const xx_extractor_options* o, xx_file_type_t type, xx_pd_struct* pd,
    xx_extractor_sink sink, void* u, xx_extractor_stats* s)
{
    static const reader_desc tar_reader = { { XX_FILE_TYPE_TAR, "TAR", false }, open_tar, close_tar };
    const reader_desc* rd = type == XX_FILE_TYPE_TAR ? &tar_reader : find_reader(type);
    Abstractformat* f = NULL;
    xx_archive_record_state* state = NULL;
    xx_archive_record limits;
    const xx_archive_record* native;
    int64_t total = xx_io_total_size(d);
    uint64_t visited = 0;
    int level = xx_pd_enter_level(pd, 0, "Analyzing container streams");
    xx_extractor_status status = XX_EXTRACTOR_OK;
    bool initialized = set_limits(&limits, o->max_member_size);
    if (!initialized || !(f = rd->open(d))) {
        status = fail(pd, XX_EXTRACTOR_NO_MEMORY, NULL);
        goto done;
    }
    if (!xx_format_is_valid(f, pd) || !xx_format_handle_base_info(f, pd)) {
        status = fail(pd, XX_EXTRACTOR_INVALID, "Invalid extraction container");
        goto done;
    }
    state = xx_format_create_archive_records_reading(f, &limits.list_meta, pd);
    if (!state) {
        status = fail(pd, XX_EXTRACTOR_INVALID, "Cannot enumerate container streams");
        goto done;
    }
    while ((native = current_record(f, type, state)) != NULL && !s->limit_reached) {
        xx_io_device* decoded = NULL;
        xx_extractor_record r;
        uint64_t declared;
        const char* name;
        char* owned_name = NULL;
        char ext[32], info[256];
        int64_t decoded_size;
        if (xx_pd_is_stopped(pd)) {
            status = XX_EXTRACTOR_CANCELLED;
            break;
        }
        ++visited;
        if (native->data_offset < 0 || native->compressed_size < 0 || native->data_offset > total
            || native->compressed_size > total - native->data_offset) {
            status = fail(pd, XX_EXTRACTOR_INVALID, "Invalid container member extent");
            break;
        }
        if (xx_archive_record_get_meta_bool(native, XX_META_ID_IS_FOLDER, false) || native->compressed_size == 0)
            goto advance;
        if (xx_archive_record_get_meta_bool(native, XX_META_ID_IS_ENCRYPTED, false)) {
            status = fail(pd, XX_EXTRACTOR_UNSUPPORTED, "Encrypted members require a password");
            break;
        }
        declared = xx_archive_record_get_meta_u64(native, XX_META_ID_UNCOMPRESSED_SIZE, UINT64_MAX);
        if (declared != UINT64_MAX && declared > o->max_member_size) {
            status = fail(pd, XX_EXTRACTOR_LIMIT, NULL);
            break;
        }
        decoded = xx_io_temp_open();
        if (!decoded) {
            status = fail(pd, XX_EXTRACTOR_NO_MEMORY, "Cannot open temporary decoded member");
            break;
        }
        if (!decode_member(f, type, state, decoded, pd) || xx_io_seek64(decoded, 0, SEEK_SET) != 0) {
            status = fail(pd, XX_EXTRACTOR_INVALID, "Cannot decode container member");
            xx_io_close(decoded);
            break;
        }
        decoded_size = xx_io_total_size(decoded);
        if (decoded_size < 0 || (uint64_t)decoded_size > o->max_member_size
            || (declared != UINT64_MAX && (uint64_t)decoded_size != declared)) {
            status = fail(pd, XX_EXTRACTOR_LIMIT, "Decoded member size does not match its declared extent");
            xx_io_close(decoded);
            break;
        }
        xx_mem_zero(&r, sizeof(r));
        r.offset = o->offset + (uint64_t)native->data_offset;
        r.size = (uint64_t)native->compressed_size;
        r.unpacked_size = (uint64_t)decoded_size;
        r.container_type = f->file_type;
        r.container_offset = o->offset;
        r.container_size = (uint64_t)total;
        r.archive_index = (uint64_t)state->current_index;
        r.max_member_size = o->max_member_size;
        r.method = type == XX_FILE_TYPE_ZIP ? XX_EXTRACTOR_ZIP_MEMBER
            : type == XX_FILE_TYPE_PDF      ? XX_EXTRACTOR_PDF_STREAM
                                            : XX_EXTRACTOR_STORE;
        if (type == XX_FILE_TYPE_ZIP) {
            r.compression_method = xx_archive_record_get_meta_u64(native, XX_META_ID_COMPRESSION_METHOD, 0);
            xx_rt_snprintf(r.compression, sizeof(r.compression), "%s", zip_method_name(r.compression_method));
        } else if (type == XX_FILE_TYPE_PDF) {
            const char* filters = xx_archive_record_get_meta_str(native, XX_META_ID_COMPRESSION_METHOD);
            xx_rt_snprintf(r.compression, sizeof(r.compression), "%s", filters && filters[0] ? filters : "Store");
        } else {
            xx_rt_snprintf(r.compression, sizeof(r.compression), "Store");
        }
        r.file_type = payload_type(decoded, pd, ext, sizeof(ext));
        r.extension = ext;
        if (!crc_range(decoded, 0, r.unpacked_size, pd, &r.crc32)) {
            status = fail(pd, XX_EXTRACTOR_IO, "Cannot checksum container member");
            xx_io_close(decoded);
            break;
        }
        r.has_crc32 = true;
        if (xx_io_close(decoded) != 0) {
            status = fail(pd, XX_EXTRACTOR_IO, "Cannot close decoded member");
            break;
        }
        name = xx_archive_record_get_original_name(native);
        if (!name) {
            const wchar_t* wide = xx_archive_record_get_original_name_w(native);
            if (wide)
                owned_name = xx_str_unicode_to_utf8(wide);
            name = owned_name ? owned_name : "";
        }
        r.name = name;
        xx_rt_snprintf(info, sizeof(info), "%s%s", xx_format_file_type_to_string(r.file_type),
            xx_rt_strcmp(r.compression, "Store") ? " (compressed stream)" : "");
        r.info = info;
        if (!publish(o, &r, sink, u, s))
            status = fail(pd, XX_EXTRACTOR_SINK, NULL);
        xx_str_free(owned_name);
        if (status != XX_EXTRACTOR_OK)
            break;
    advance:
        xx_pd_set_current(pd, level, visited);
        if (!xx_format_archive_record_move_to_next(f, state, pd))
            break;
    }
    if (!s->limit_reached && state->total_records >= 0 && visited != (uint64_t)state->total_records && status == XX_EXTRACTOR_OK)
        status = fail(pd, XX_EXTRACTOR_INVALID, "Incomplete archive record table");
    if (type == XX_FILE_TYPE_ZIP && f && ((xx_zip*)f)->directory_damaged && status == XX_EXTRACTOR_OK)
        status = fail(pd, XX_EXTRACTOR_INVALID, "Damaged ZIP directory");
done:
    if (state)
        xx_format_free_archive_records_reading(f, state);
    if (f)
        rd->close(f);
    xx_archive_record_cleanup(&limits);
    xx_pd_leave_level(pd, level);
    s->bytes_scanned = (uint64_t)total;
    return status;
}
xx_extractor_status xx_extractor_scan_device(
    xx_io_device* device, const xx_extractor_options* input, xx_pd_struct* pd, xx_extractor_sink sink, void* u, xx_extractor_stats* stats)
{
    xx_extractor_options o = input ? *input : xx_extractor_defaults();
    xx_extractor_stats local;
    xx_io_device* view = NULL;
    int64_t saved, total;
    xx_file_type_t outer;
    xx_extractor_status result = XX_EXTRACTOR_OK;
    xx_mem_zero(&local, sizeof(local));
    if (stats)
        *stats = local;
    if (!device || o.mode < XX_EXTRACTOR_HEURISTIC || o.mode > XX_EXTRACTOR_RAW || o.offset > INT64_MAX || o.chunk_size == 0
        || o.chunk_size > 16U * 1024U * 1024U || (!o.types && o.type_count))
        return fail(pd, XX_EXTRACTOR_INVALID, NULL);
    if (xx_pd_is_stopped(pd))
        return XX_EXTRACTOR_CANCELLED;
    if (!o.max_member_size)
        o.max_member_size = UINT64_C(256) * 1024 * 1024;
    saved = xx_io_tell(device);
    total = xx_io_total_size(device);
    if (saved < 0 || total < 0)
        return fail(pd, XX_EXTRACTOR_IO, "Extractor requires a seekable known-size device");
    if (o.offset > (uint64_t)total || (o.length != UINT64_MAX && o.length > (uint64_t)total - o.offset))
        return fail(pd, XX_EXTRACTOR_INVALID, "Extractor range exceeds input");
    if (o.length == UINT64_MAX)
        o.length = (uint64_t)total - o.offset;
    view = xx_io_sub_open_ro(device, (int64_t)o.offset, (int64_t)o.length);
    if (!view)
        return fail(pd, XX_EXTRACTOR_NO_MEMORY, NULL);
    outer = o.file_type;
    if (outer == XX_FILE_TYPE_UNKNOWN)
        outer = xx_extractor_detect_device(view, pd);
    outer = family(outer);
    local.resolved_mode
        = o.mode == XX_EXTRACTOR_HEURISTIC ? (xx_extractor_format_available(outer) ? XX_EXTRACTOR_FORMAT : XX_EXTRACTOR_RAW) : o.mode;
    if (local.resolved_mode == XX_EXTRACTOR_FORMAT && !xx_extractor_format_available(outer))
        result = fail(pd, XX_EXTRACTOR_UNSUPPORTED, NULL);
    else if (local.resolved_mode == XX_EXTRACTOR_RAW)
        result = raw_scan(view, &o, pd, sink, u, &local);
    else if (outer == XX_FILE_TYPE_PE)
        result = pe_scan(view, &o, pd, sink, u, &local);
    else
        result = archive_scan(view, &o, outer, pd, sink, u, &local);
    xx_io_close(view);
    if (xx_io_seek64(device, saved, SEEK_SET) != 0)
        result = fail(pd, XX_EXTRACTOR_IO, "Cannot restore extractor input position");
    if (xx_pd_is_stopped(pd))
        result = XX_EXTRACTOR_CANCELLED;
    if (stats)
        *stats = local;
    return result;
}
xx_extractor_status xx_extractor_extract_device(
    xx_io_device* source, const xx_extractor_record* r, xx_io_device* destination, xx_pd_struct* pd)
{
    int64_t saved, total;
    xx_io_device* view = NULL;
    Abstractformat* f = NULL;
    xx_archive_record_state* state = NULL;
    xx_archive_record limits;
    bool limits_init = false;
    xx_extractor_status status = XX_EXTRACTOR_OK;
    uint64_t crc;
    const reader_desc* rd = NULL;
    xx_io_device* verified = NULL;
    if (!source || !r || !destination || source == destination || r->offset > INT64_MAX || r->size > INT64_MAX
        || r->unpacked_size > INT64_MAX)
        return fail(pd, XX_EXTRACTOR_INVALID, NULL);
    if (xx_pd_is_stopped(pd))
        return XX_EXTRACTOR_CANCELLED;
    saved = xx_io_tell(source);
    total = xx_io_total_size(source);
    if (saved < 0 || total < 0)
        return fail(pd, XX_EXTRACTOR_IO, NULL);
    if (r->offset > (uint64_t)total || r->size > (uint64_t)total - r->offset)
        return fail(pd, XX_EXTRACTOR_INVALID, "Extraction record exceeds source");
    if (r->method == XX_EXTRACTOR_STORE) {
        if (r->unpacked_size != r->size) {
            status = fail(pd, XX_EXTRACTOR_INVALID, "Stored extraction sizes differ");
            goto done;
        }
        if (r->has_crc32
            && (!xx_crc_calculate_device_by_type(source, (int64_t)r->offset, (int64_t)r->size, XX_CRC_TYPE_CRC32, pd, &crc)
                || (uint32_t)crc != r->crc32)) {
            status = fail(pd, XX_EXTRACTOR_INVALID, "Source data changed since analysis");
            goto done;
        }
        if (!copy_range(source, r->offset, r->size, destination, pd))
            status = fail(pd, XX_EXTRACTOR_IO, "Cannot copy extraction record");
        goto done;
    }
    if (r->method != XX_EXTRACTOR_ZIP_MEMBER && r->method != XX_EXTRACTOR_PDF_STREAM) {
        status = fail(pd, XX_EXTRACTOR_UNSUPPORTED, NULL);
        goto done;
    }
    if (r->unpacked_size > r->max_member_size || r->container_offset > (uint64_t)total
        || r->container_size > (uint64_t)total - r->container_offset) {
        status = fail(pd, XX_EXTRACTOR_LIMIT, NULL);
        goto done;
    }
    rd = find_reader(r->method == XX_EXTRACTOR_ZIP_MEMBER ? XX_FILE_TYPE_ZIP : XX_FILE_TYPE_PDF);
    view = xx_io_sub_open_ro(source, (int64_t)r->container_offset, (int64_t)r->container_size);
    if (!view || !(f = rd->open(view))) {
        status = fail(pd, XX_EXTRACTOR_NO_MEMORY, NULL);
        goto done;
    }
    limits_init = true;
    if (!set_limits(&limits, r->max_member_size) || !xx_format_is_valid(f, pd) || !xx_format_handle_base_info(f, pd)
        || !(state = xx_format_create_archive_records_reading(f, &limits.list_meta, pd))) {
        status = fail(pd, XX_EXTRACTOR_INVALID, "Cannot reopen extraction container");
        goto done;
    }
    while (state->has_record && (uint64_t)state->current_index < r->archive_index) {
        if (xx_pd_is_stopped(pd) || !xx_format_archive_record_move_to_next(f, state, pd))
            break;
    }
    {
        const xx_archive_record* n = current_record(f, rd->type.file_type, state);
        if (!n || (uint64_t)state->current_index != r->archive_index || n->data_offset < 0 || n->compressed_size < 0
            || r->container_offset + (uint64_t)n->data_offset != r->offset || (uint64_t)n->compressed_size != r->size) {
            status = fail(pd, XX_EXTRACTOR_INVALID, "Container changed since analysis");
            goto done;
        }
    }
    verified = xx_io_temp_open();
    if (!verified) {
        status = fail(pd, XX_EXTRACTOR_NO_MEMORY, NULL);
        goto done;
    }
    if (!decode_member(f, rd->type.file_type, state, verified, pd) || xx_io_seek64(verified, 0, SEEK_SET) != 0
        || xx_io_total_size(verified) != (int64_t)r->unpacked_size) {
        status = fail(pd, XX_EXTRACTOR_INVALID, "Cannot verify decoded extraction record");
        goto done;
    }
    if (r->has_crc32
        && (!xx_crc_calculate_device_by_type(verified, 0, (int64_t)r->unpacked_size, XX_CRC_TYPE_CRC32, pd, &crc)
            || (uint32_t)crc != r->crc32)) {
        status = fail(pd, XX_EXTRACTOR_INVALID, "Decoded data changed since analysis");
        goto done;
    }
    if (!copy_range(verified, 0, r->unpacked_size, destination, pd))
        status = fail(pd, XX_EXTRACTOR_IO, "Cannot write decoded extraction record");
done:
    if (verified && xx_io_close(verified) != 0 && status == XX_EXTRACTOR_OK)
        status = fail(pd, XX_EXTRACTOR_IO, NULL);
    if (state)
        xx_format_free_archive_records_reading(f, state);
    if (f)
        rd->close(f);
    if (view)
        xx_io_close(view);
    if (limits_init)
        xx_archive_record_cleanup(&limits);
    if (xx_io_seek64(source, saved, SEEK_SET) != 0)
        status = fail(pd, XX_EXTRACTOR_IO, "Cannot restore extraction source position");
    return xx_pd_is_stopped(pd) ? XX_EXTRACTOR_CANCELLED : status;
}
xx_extractor_status xx_extractor_extract_file(
    xx_io_device* source, const xx_extractor_record* record, const char* path, bool overwrite, xx_pd_struct* pd)
{
    static const char hex[] = "0123456789abcdef";
    unsigned attempt;
    xx_io_device* out = NULL;
    char* stage = NULL;
    xx_extractor_status status;
    if (!path || !path[0] || !source || !record)
        return fail(pd, XX_EXTRACTOR_INVALID, NULL);
    if (xx_pd_is_stopped(pd))
        return XX_EXTRACTOR_CANCELLED;
    if (!overwrite && xx_io_file_exists_a(path))
        return fail(pd, XX_EXTRACTOR_IO, "Extraction destination already exists");
    if (!xx_io_create_dirs_a(path, false))
        return fail(pd, XX_EXTRACTOR_IO, "Cannot create extraction destination directory");
    for (attempt = 0; attempt < 8; attempt++) {
        uint8_t random[16];
        char suffix[48] = ".xxextract.tmp.";
        size_t i;
        if (!xx_io_platform_secure_random(random, sizeof(random)))
            break;
        for (i = 0; i < 16; i++) {
            suffix[15 + 2 * i] = hex[random[i] >> 4];
            suffix[16 + 2 * i] = hex[random[i] & 15];
        }
        suffix[47] = 0;
        stage = xx_str_concat(path, suffix);
        if (!stage)
            break;
        out = xx_io_file_open(stage, "wbx");
        if (out)
            break;
        xx_str_free(stage);
        stage = NULL;
    }
    if (!out) {
        xx_str_free(stage);
        return fail(pd, XX_EXTRACTOR_IO, "Cannot create atomic extraction staging file");
    }
    status = xx_extractor_extract_device(source, record, out, pd);
    if (xx_io_close(out) != 0 && status == XX_EXTRACTOR_OK)
        status = fail(pd, XX_EXTRACTOR_IO, "Cannot close extraction staging file");
    if (xx_pd_is_stopped(pd))
        status = XX_EXTRACTOR_CANCELLED;
    if (status == XX_EXTRACTOR_OK && !xx_io_file_replace_a(stage, path, overwrite))
        status = fail(pd, XX_EXTRACTOR_IO, "Cannot publish extraction output");
    if (status != XX_EXTRACTOR_OK)
        (void)xx_io_file_remove_a(stage);
    xx_str_free(stage);
    return status;
}
bool xx_extractor_record_filename(const xx_extractor_record* r, char* out, size_t capacity)
{
    char base[96], ext[20];
    size_t i, n = 0, e = 0;
    const char* name;
    int needed;
    if (!r || !out || !capacity)
        return false;
    name = r->name ? r->name : "";
    for (i = 0; name[i] && n < 70; i++) {
        unsigned char c = (unsigned char)name[i];
        base[n++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ? (char)c : '_';
    }
    if (!n) {
        xx_rt_memcpy(base, "extract", 7);
        n = 7;
    }
    base[n] = 0;
    name = r->extension ? r->extension : "";
    for (i = 0; name[i] && e < 12; i++) {
        unsigned char c = (unsigned char)name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            ext[e++] = (char)c;
    }
    ext[e] = 0;
    needed = xx_rt_snprintf(out, capacity, "%s_%016llx_%08x%s%s", base, (unsigned long long)r->offset, r->crc32, e ? "." : "", ext);
    return needed >= 0 && (size_t)needed < capacity;
}
