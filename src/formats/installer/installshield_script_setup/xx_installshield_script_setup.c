/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * InstallShield InstallScript "setup.exe" launcher (InstallShield 2012 ..
 * 2023, the "Setup Launcher" and "setupPreReqW" images). Layout, measured on
 * the DYMO 2025, Sonos 2024 and Samsung 2012 launchers and cross-checked with
 * the MIT extractor ISx (github.com/lifenjoiner/ISx):
 *
 *   overlay (the end of the furthest section's raw data; a CodeView NB10 or
 *   RSDS record may precede the tag, so it is searched in the first 1 KiB)
 *     "InstallShield\0"          14 bytes
 *     u16 number of files
 *     u32 type (0 here; 4 is the ISSetupStream shape with UTF-16 names,
 *               read by installer/installshield_issetupstream)
 *     26 bytes reserved
 *   then per file a 312-byte record followed by its bytes:
 *     char name[260]  NUL-terminated
 *     u32 flags       6 seen
 *     u32
 *     u32 size        stored bytes
 *     u8[8]
 *     u16 compressed  1: the decoded bytes are a zlib stream
 *     u8[30]
 *
 * The flags choose the obfuscation, as ISx reads them: key[i] = name[i] ^
 * {13 35 86 07}[i mod 4]; with bit 4 set (6 on every launcher since 2012)
 * byte p decodes as ~(key[(p mod 1024) mod keylen] ^ rotl4(b)), the key
 * restarting every 1024 bytes; without bit 4 the bytes are stored as they
 * are (DevStudio 9, InstallShield 2009), except that bit 2 alone marks the
 * 10.5/12 launchers whose files are decoded with the key running over the
 * whole file: those are told apart by their first bytes (a zlib header for
 * a compressed file, a known file signature or text otherwise), since ISx
 * itself leaves that choice to its -2trans switch. The record chain ends
 * at the end of the file or at the Authenticode table. Nothing is executed.
 */
#include "xxfclib/formats/installer/installshield_script_setup/xx_installshield_script_setup.h"
#include "../../common/xx_executable_carrier.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/global/xx_global.h"

#define ISS_TAG_SIZE 14U
#define ISS_HEADER_SIZE 46U
#define ISS_RECORD_SIZE 312U
#define ISS_NAME_SIZE 260U
#define ISS_TAG_WINDOW 1024
#define ISS_KEY_CYCLE 1024U
#define ISS_MAX_FILES 4096U
#define ISS_CHUNK 65536U
#define ISS_SNIFF 64U

#define ISS_MODE_STORED 0U /* flags without bit 4: the bytes as they are */
#define ISS_MODE_CYCLE 1U  /* flags with bit 4: key cycle of 1024 bytes */
#define ISS_MODE_WHOLE 2U  /* flags with bit 2 only, encoded: key over the whole file */

static const uint8_t iss_tag[ISS_TAG_SIZE] = {'I', 'n', 's', 't', 'a', 'l', 'l', 'S', 'h', 'i', 'e', 'l', 'd', 0};
static const uint8_t iss_magic[4] = {0x13, 0x35, 0x86, 0x07};

typedef struct iss_member {
    uint8_t key[ISS_NAME_SIZE];
    size_t key_size;
    bool compressed;
    unsigned mode;
} iss_member;

static void iss_decode(uint8_t *buffer, size_t count, uint64_t position, const iss_member *ctx)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        uint64_t p = position + (uint64_t)i;
        uint8_t k = ctx->key[(size_t)((ctx->mode == ISS_MODE_CYCLE ? p % ISS_KEY_CYCLE : p) % ctx->key_size)];
        uint8_t b = buffer[i];
        buffer[i] = (uint8_t)~(k ^ (uint8_t)((b << 4) | (b >> 4)));
    }
}

static bool iss_write_all(xx_io_device *device, const uint8_t *buffer, size_t count)
{
    size_t done = 0;
    while (done < count) {
        ssize_t w = xx_io_write(device, buffer + done, count - done);
        if (w <= 0 || (size_t)w > count - done) return false;
        done += (size_t)w;
    }
    return true;
}

/* The decoded (and inflated) member into the destination; a NULL destination
 * validates by decoding into a temporary device. */
static bool iss_read_all(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    iss_member *ctx = (iss_member *)m->context;
    xx_io_device *decoded = NULL, *sink = device;
    uint8_t *buffer;
    uint64_t done = 0, packed;
    bool ok = true;
    if (!ctx || !ctx->key_size || m->packed_size < 0 || m->offset < f->base_address) return false;
    packed = (uint64_t)m->packed_size;
    if (ctx->compressed || !sink) {
        decoded = xx_io_temp_open();
        if (!decoded) return false;
        if (!ctx->compressed) sink = decoded; /* validation of a stored member */
    }
    buffer = (uint8_t *)xx_mem_alloc(ISS_CHUNK);
    if (!buffer) ok = false;
    while (ok && done < packed) {
        size_t n = packed - done > ISS_CHUNK ? ISS_CHUNK : (size_t)(packed - done);
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, m->offset - f->base_address + (int64_t)done, buffer, n)) {
            ok = false;
            break;
        }
        if (ctx->mode != ISS_MODE_STORED) iss_decode(buffer, n, done, ctx);
        if (!iss_write_all(ctx->compressed ? decoded : sink, buffer, n)) ok = false;
        done += n;
    }
    if (ok && ctx->compressed) {
        /* A zlib stream: two header bytes, then raw Deflate, then Adler-32. */
        xx_io_device *out = device ? device : xx_io_temp_open();
        ok = packed > 6U && out != NULL && xx_deflate_unpack_device(decoded, 2, (int64_t)packed - 2, out, false, pd);
        if (out && !device) xx_io_close(out);
    }
    if (buffer) xx_mem_free(buffer);
    if (decoded) xx_io_close(decoded);
    return ok && !(pd && xx_pd_is_stopped(pd));
}

/* Do these first bytes look like a file: a zlib header for a compressed
 * member, else a known signature, text or UTF-16 text? */
static bool iss_plausible(const uint8_t *b, size_t n, bool compressed)
{
    static const char *const signatures[] = {"MZ", "\xD0\xCF\x11\xE0", "PK\x03\x04", "MSCF", "7z\xBC\xAF", "\x89PNG", "RIFF", "BM", "{\\rtf", "<?xml", "\xFF\xFE", "\xEF\xBB\xBF"};
    size_t i;
    if (n < 2) return false;
    if (compressed) return b[0] == 0x78U && ((unsigned)b[0] * 256U + b[1]) % 31U == 0U;
    for (i = 0; i < sizeof(signatures) / sizeof(signatures[0]); ++i) {
        size_t length = xx_rt_strlen(signatures[i]);
        if (n >= length && xx_rt_memcmp(b, signatures[i], length) == 0) return true;
    }
    for (i = 0; i < n; ++i) {
        if (b[i] != 0x09U && b[i] != 0x0AU && b[i] != 0x0DU && (b[i] < 0x20U || b[i] > 0x7EU)) return false;
    }
    return true;
}

/* The decode mode of a member from its flags, the bit-2-only case decided
 * by the look of its first bytes stored and decoded. */
static unsigned iss_mode(Abstractformat *f, int64_t at, uint32_t size, uint32_t flags, const iss_member *ctx)
{
    uint8_t head[ISS_SNIFF];
    size_t n = size < ISS_SNIFF ? (size_t)size : ISS_SNIFF;
    iss_member whole = *ctx;
    if (flags & 4U) return ISS_MODE_CYCLE;
    if (!(flags & 2U) || n < 2 || !pm_read(f, at, head, n) || iss_plausible(head, n, ctx->compressed)) return ISS_MODE_STORED;
    whole.mode = ISS_MODE_WHOLE;
    iss_decode(head, n, 0, &whole);
    return iss_plausible(head, n, ctx->compressed) ? ISS_MODE_WHOLE : ISS_MODE_STORED;
}

static bool iss_name_ok(const uint8_t *record, size_t *length)
{
    size_t n = 0;
    while (n < ISS_NAME_SIZE && record[n]) {
        if (record[n] < 0x20U || record[n] == 0x7FU) return false;
        ++n;
    }
    *length = n;
    return n > 0 && n < ISS_NAME_SIZE;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t window[ISS_TAG_WINDOW + ISS_HEADER_SIZE], record[ISS_RECORD_SIZE];
    int64_t overlay, cabinet, cabinet_end, available = pm_available(f), at;
    size_t window_size, k, found = (size_t)-1;
    uint32_t count, type, i;
    if (!carrier_pe(f, &overlay, &cabinet, &cabinet_end, pd) || overlay <= 0 || available - overlay < (int64_t)(ISS_HEADER_SIZE + ISS_RECORD_SIZE)) return false;
    window_size = available - overlay < (int64_t)sizeof(window) ? (size_t)(available - overlay) : sizeof(window);
    if (!pm_read(f, overlay, window, window_size)) return false;
    for (k = 0; k + ISS_HEADER_SIZE <= window_size && k <= ISS_TAG_WINDOW; ++k) {
        if (window[k] == 'I' && xx_rt_memcmp(window + k, iss_tag, ISS_TAG_SIZE) == 0) {
            found = k;
            break;
        }
    }
    if (found == (size_t)-1) return false;
    at = overlay + (int64_t)found;
    count = xx_data_get_u16(window + found + 14U, 2, 0, false);
    type = xx_data_get_u32(window + found + 16U, 4, 0, false);
    if (!count || count > ISS_MAX_FILES || type != 0U) return false;
    at += ISS_HEADER_SIZE;
    for (i = 0; i < count; ++i) {
        size_t name_length;
        uint32_t size, flags, compressed;
        pm_member *m;
        iss_member *ctx;
        if ((pd && xx_pd_is_stopped(pd)) || available - at < (int64_t)ISS_RECORD_SIZE || !pm_read(f, at, record, ISS_RECORD_SIZE)) return false;
        if (!iss_name_ok(record, &name_length)) return false;
        flags = xx_data_get_u32(record + 260U, 4, 0, false);
        size = xx_data_get_u32(record + 268U, 4, 0, false);
        compressed = xx_data_get_u16(record + 280U, 2, 0, false);
        if ((flags & ~0xFFU) != 0U || compressed > 1U || (int64_t)size > available - at - (int64_t)ISS_RECORD_SIZE) return false;
        if (!pm_add(f, s, (const char *)record, at + (int64_t)ISS_RECORD_SIZE, size)) return false;
        m = &s->items[s->count - 1U];
        ctx = (iss_member *)xx_mem_calloc(1U, sizeof(*ctx));
        if (!ctx) return false;
        for (k = 0; k < name_length; ++k) ctx->key[k] = record[k] ^ iss_magic[k & 3U];
        ctx->key_size = name_length;
        ctx->compressed = compressed != 0U;
        ctx->mode = iss_mode(f, at + (int64_t)ISS_RECORD_SIZE, size, flags, ctx);
        m->context = ctx;
        m->free_context = xx_mem_free;
        m->read_all = iss_read_all;
        if (ctx->compressed) {
            m->size = -1; /* known only after inflating */
            m->compression_method = 8U;
        }
        at += (int64_t)ISS_RECORD_SIZE + size;
    }
    s->size = at;
    return s->count > 0 && !(pd && xx_pd_is_stopped(pd));
}

void xx_installshield_script_setup_init(xx_installshield_script_setup *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_INSTALLSHIELD_SCRIPT_SETUP, "exe");
    }
}
xx_installshield_script_setup *xx_installshield_script_setup_create(xx_io_device *d, int64_t b)
{
    xx_installshield_script_setup *r = (xx_installshield_script_setup *)xx_mem_alloc(sizeof(*r));
    if (r) xx_installshield_script_setup_init(r, d, b);
    return r;
}
void xx_installshield_script_setup_destroy(xx_installshield_script_setup *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_installshield_script_setup_free(xx_installshield_script_setup *r)
{
    if (r) {
        xx_installshield_script_setup_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_installshield_script_setup_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_installshield_script_setup_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
