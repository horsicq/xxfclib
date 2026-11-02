/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxfclib/strings/xx_strings_scan.h"
#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/global/xx_global.h"

/* SSE2 is architectural on x86-64. Other targets retain the same scalar path;
 * no AVX/OS-state assumptions or unconditional target-specific instructions. */
#if !defined(_M_ARM64EC) && (defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define XX_STRINGS_HAVE_SSE2 1
#include <emmintrin.h>
#endif
#ifdef _MSC_VER
#include <intrin.h>
#endif

#define STRINGS_CHUNK_DEFAULT 65536U
#define STRINGS_CHUNK_MAX (16U * 1024U * 1024U)
#define STRINGS_TEXT_DEFAULT (1024U * 1024U)

typedef struct strings_scan strings_scan;
typedef struct strings_decoder {
    strings_scan *scan;
    xx_buf_t text;
    uint64_t start, end, characters, unit_start;
    uint32_t scalar, minimum, high;
    unsigned encoding, phase, first, pending, streak, ascii;
    bool non_ascii, bom, capped;
} strings_decoder;
struct strings_scan {
    xx_strings_scan_options options;
    xx_strings_scan_sink sink;
    void *user;
    xx_strings_scan_stats stats;
    xx_strings_scan_status status;
    bool text;
};

/* The chunk is the general buffer size (xx_get_buffer_size(), Detect It
 * Easy's Feature/ReadBufferSize), at most STRINGS_CHUNK_MAX. */
xx_strings_scan_options xx_strings_scan_defaults(void)
{
    xx_strings_scan_options o = {4, XX_STRINGS_ALL, 0, UINT64_MAX, STRINGS_CHUNK_DEFAULT, STRINGS_TEXT_DEFAULT, 0};
    size_t chunk = xx_get_buffer_size();
    if (chunk) o.chunk_size = chunk < STRINGS_CHUNK_MAX ? chunk : STRINGS_CHUNK_MAX;
    return o;
}
const char *xx_strings_scan_status_name(xx_strings_scan_status status)
{
    switch (status) {
        case XX_STRINGS_SCAN_OK: return "Success";
        case XX_STRINGS_SCAN_CANCELLED: return "Strings extraction cancelled";
        case XX_STRINGS_SCAN_INVALID: return "Invalid strings scan options or range";
        case XX_STRINGS_SCAN_IO: return "Cannot read strings input";
        case XX_STRINGS_SCAN_NO_MEMORY: return "Cannot allocate strings scan buffer";
        case XX_STRINGS_SCAN_SINK: return "Strings result callback stopped";
        default: return "Unknown strings scan error";
    }
}
const char *xx_strings_scan_backend(unsigned flags)
{
#ifdef XX_STRINGS_HAVE_SSE2
    if (!(flags & XX_STRINGS_SCALAR)) return "SSE2";
#else
    (void)flags;
#endif
    return "scalar";
}
static bool printable(uint32_t cp)
{
    return (cp >= 32 && cp < 127) || (cp >= 160 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff) && !(cp >= 0xfdd0 && cp <= 0xfdef) && (cp & 0xffff) < 0xfffe &&
                                      cp != 0xfeff && !(cp >= 0x200b && cp <= 0x200f) && !(cp >= 0x2028 && cp <= 0x202e) && !(cp >= 0x2060 && cp <= 0x206f));
}
static bool printable_with_flags(uint32_t cp, unsigned flags)
{
    if (flags & XX_STRINGS_DIE_ALPHABET) return (cp >= 32 && cp <= 255) || (cp >= 0x400 && cp <= 0x4ff);
    return printable(cp);
}
static bool finish_run(strings_decoder *d)
{
    strings_scan *s = d->scan;
    bool accepted = d->characters >= s->options.minimum_length;
    if ((d->encoding & (XX_STRINGS_UTF16LE | XX_STRINGS_UTF16BE)) && s->options.encodings == XX_STRINGS_ALL && d->ascii < s->options.minimum_length && !d->bom)
        accepted = false;
    if (accepted) {
        xx_strings_scan_record r;
        r.offset = d->start;
        r.byte_size = d->end - d->start;
        r.character_length = d->characters;
        r.encoding = d->encoding == XX_STRINGS_UTF8 && !d->non_ascii ? XX_STRINGS_ASCII : d->encoding;
        r.flags = d->capped ? XX_STRINGS_TEXT_TRUNCATED : 0;
        r.text = s->text ? (d->text.data ? d->text.data : "") : NULL;
        r.text_size = s->text ? d->text.size : 0;
        if (s->sink && !s->sink(&r, s->user)) {
            s->status = XX_STRINGS_SCAN_SINK;
            return false;
        }
        ++s->stats.record_count;
    }
    xx_buf_clear(&d->text);
    d->characters = 0;
    d->streak = d->ascii = 0;
    d->non_ascii = d->bom = d->capped = false;
    return true;
}
static bool append_text(strings_decoder *d, const char *text, size_t bytes, bool ascii)
{
    size_t available;
    if (!d->scan->text || d->capped) return true;
    available = d->scan->options.max_text_bytes - d->text.size;
    if (bytes > available) {
        d->capped = true;
        if (!ascii) return true; /* Never retain an incomplete UTF-8 codepoint. */
        bytes = available;
    }
    if (bytes && !xx_buf_append(&d->text, text, bytes)) {
        d->scan->status = XX_STRINGS_SCAN_NO_MEMORY;
        return false;
    }
    return true;
}
static bool feed_ascii(strings_decoder *d, const unsigned char *text, size_t bytes, uint64_t offset)
{
    uint64_t streak;
    if (!bytes) return true;
    if (!d->characters) d->start = offset;
    d->end = offset + bytes;
    d->characters += bytes;
    streak = (uint64_t)d->streak + bytes;
    d->streak = streak < d->scan->options.minimum_length ? (unsigned)streak : d->scan->options.minimum_length;
    if (d->streak > d->ascii) d->ascii = d->streak;
    return append_text(d, (const char *)text, bytes, true);
}
static bool feed_scalar(strings_decoder *d, uint32_t cp, uint64_t offset, unsigned bytes)
{
    char encoded[4];
    size_t length;
    if (!printable_with_flags(cp, d->scan->options.flags)) {
        if (!finish_run(d)) return false;
        d->bom = cp == 0xfeff;
        return true;
    }
    if (cp < 128) {
        unsigned char byte = (unsigned char)cp;
        if (!d->characters) d->start = offset;
        d->end = offset + bytes;
        ++d->characters;
        if (d->streak < d->scan->options.minimum_length) ++d->streak;
        if (d->streak > d->ascii) d->ascii = d->streak;
        return append_text(d, (const char *)&byte, 1, true);
    }
    if (!d->characters) d->start = offset;
    d->end = offset + bytes;
    ++d->characters;
    d->non_ascii = true;
    d->streak = 0;
    if (cp < 0x800) {
        encoded[0] = (char)(0xc0 | (cp >> 6));
        encoded[1] = (char)(0x80 | (cp & 63));
        length = 2;
    } else if (cp < 0x10000) {
        encoded[0] = (char)(0xe0 | (cp >> 12));
        encoded[1] = (char)(0x80 | ((cp >> 6) & 63));
        encoded[2] = (char)(0x80 | (cp & 63));
        length = 3;
    } else {
        encoded[0] = (char)(0xf0 | (cp >> 18));
        encoded[1] = (char)(0x80 | ((cp >> 12) & 63));
        encoded[2] = (char)(0x80 | ((cp >> 6) & 63));
        encoded[3] = (char)(0x80 | (cp & 63));
        length = 4;
    }
    return append_text(d, encoded, length, false);
}
static bool feed_utf8(strings_decoder *d, unsigned byte, uint64_t offset)
{
    if (d->pending) {
        if ((byte & 0xc0) != 0x80) {
            d->pending = 0;
            if (!finish_run(d)) return false;
            return feed_utf8(d, byte, offset);
        }
        d->scalar = (d->scalar << 6) | (byte & 63);
        if (--d->pending) return true;
        if (d->scalar < d->minimum || d->scalar > 0x10ffff || (d->scalar >= 0xd800 && d->scalar <= 0xdfff)) return feed_scalar(d, 0, offset, 1);
        return feed_scalar(d, d->scalar, d->unit_start, (unsigned)(offset - d->unit_start + 1));
    }
    if (byte < 128) return feed_scalar(d, byte, offset, 1);
    d->unit_start = offset;
    if (byte >= 0xc2 && byte <= 0xdf) {
        d->scalar = byte & 31;
        d->pending = 1;
        d->minimum = 0x80;
    } else if (byte >= 0xe0 && byte <= 0xef) {
        d->scalar = byte & 15;
        d->pending = 2;
        d->minimum = 0x800;
    } else if (byte >= 0xf0 && byte <= 0xf4) {
        d->scalar = byte & 7;
        d->pending = 3;
        d->minimum = 0x10000;
    } else return feed_scalar(d, 0, offset, 1);
    return true;
}
static bool feed_utf16(strings_decoder *d, unsigned byte, uint64_t offset)
{
    uint32_t unit;
    if ((offset & 1) == d->phase) {
        d->first = byte;
        d->pending = 1;
        return true;
    }
    if (!d->pending) return true;
    d->pending = 0;
    unit = d->encoding == XX_STRINGS_UTF16LE ? d->first | (byte << 8) : (d->first << 8) | byte;
    if (d->high) {
        uint32_t high = d->high;
        d->high = 0;
        if (unit >= 0xdc00 && unit <= 0xdfff) return feed_scalar(d, 0x10000 + ((high - 0xd800) << 10) + unit - 0xdc00, offset - 3, 4);
        if (!finish_run(d)) return false;
    }
    if (unit >= 0xd800 && unit <= 0xdbff) {
        d->high = unit;
        return true;
    }
    return feed_scalar(d, unit >= 0xdc00 && unit <= 0xdfff ? 0 : unit, offset - 1, 2);
}
static unsigned ascii_mask(const unsigned char *data, unsigned flags)
{
#ifdef XX_STRINGS_HAVE_SSE2
    if (!(flags & XX_STRINGS_SCALAR)) {
        __m128i v = _mm_loadu_si128((const __m128i *)(const void *)data);
        __m128i bad = _mm_cmplt_epi8(v, _mm_set1_epi8(32));
        if (!(flags & XX_STRINGS_DIE_ALPHABET)) bad = _mm_or_si128(bad, _mm_cmpgt_epi8(v, _mm_set1_epi8(126)));
        return (~(unsigned)_mm_movemask_epi8(bad)) & 0xffffU;
    }
#else
    (void)flags;
#endif
    {
        unsigned i, mask = 0;
        for (i = 0; i < 16; ++i)
            if (data[i] >= 32 && data[i] < ((flags & XX_STRINGS_DIE_ALPHABET) ? 128 : 127)) mask |= 1U << i;
        return mask;
    }
}
static unsigned first_bit(unsigned value)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    unsigned long bit;
    _BitScanForward(&bit, value);
    return (unsigned)bit;
#elif defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctz(value);
#else
    unsigned bit = 0;
    while (!(value & 1)) {
        value >>= 1;
        ++bit;
    }
    return bit;
#endif
}
static bool scan_narrow(strings_decoder *d, const unsigned char *data, size_t size, uint64_t offset, xx_pd_struct *pd)
{
    size_t at = 0, next_poll = 0;
    while (at < size) {
        if (at >= next_poll) {
            if (xx_pd_is_stopped(pd)) {
                d->scan->status = XX_STRINGS_SCAN_CANCELLED;
                return false;
            }
            next_poll = at + 4096;
        }
        if (!d->pending && size - at >= 16) {
            unsigned mask = ascii_mask(data + at, d->scan->options.flags), consumed = 0;
            while (consumed < 16) {
                unsigned available = 16 - consumed;
                unsigned n = first_bit(((mask & 1) ? ~mask : mask) | (1U << available));
                if (mask & 1) {
                    if (!feed_ascii(d, data + at, n, offset + at)) return false;
                } else if (d->encoding == XX_STRINGS_ASCII) {
                    if (!finish_run(d)) return false;
                } else break; /* Unicode/nonprintable bytes need the stateful decoder. */
                at += n;
                consumed += n;
                mask >>= n;
            }
            if (consumed == 16) continue;
        }
        if (d->encoding == XX_STRINGS_ASCII) {
            if (!feed_scalar(d, data[at] < 128 ? data[at] : 0, offset + at, 1)) return false;
        } else if (!feed_utf8(d, data[at], offset + at)) return false;
        ++at;
    }
    return true;
}
xx_strings_scan_status xx_strings_scan_device(xx_io_device *device, const xx_strings_scan_options *options, xx_pd_struct *pd, xx_strings_scan_sink sink, void *user,
                                              xx_strings_scan_stats *stats)
{
    strings_scan s;
    strings_decoder decoders[5];
    unsigned char *buffer = NULL;
    uint64_t total = 0, done = 0;
    int64_t size;
    unsigned count = 0, i, encoding, phase;
    int level = -1;
    xx_rt_memset(&s, 0, sizeof(s));
    xx_rt_memset(decoders, 0, sizeof(decoders));
    s.options = options ? *options : xx_strings_scan_defaults();
    s.sink = sink;
    s.user = user;
    s.text = sink && !(s.options.flags & XX_STRINGS_NO_TEXT);
    if (!s.options.minimum_length || !s.options.encodings || (s.options.encodings & ~15U) || (s.options.encodings & 3) == 3 ||
        (s.options.flags & ~(XX_STRINGS_NO_TEXT | XX_STRINGS_SCALAR | XX_STRINGS_DIE_ALPHABET)) || !s.options.chunk_size || s.options.chunk_size > STRINGS_CHUNK_MAX ||
        (s.text && !s.options.max_text_bytes) || s.options.max_text_bytes == SIZE_MAX) {
        s.status = XX_STRINGS_SCAN_INVALID;
        goto finish;
    }
    if (xx_pd_is_stopped(pd)) {
        s.status = XX_STRINGS_SCAN_CANCELLED;
        goto finish;
    }
    if (!device || (size = xx_io_total_size(device)) < 0) {
        s.status = XX_STRINGS_SCAN_IO;
        goto finish;
    }
    if (s.options.offset > (uint64_t)size) {
        s.status = XX_STRINGS_SCAN_INVALID;
        goto finish;
    }
    total = (uint64_t)size - s.options.offset;
    if (s.options.length != UINT64_MAX) {
        if (s.options.length > total) {
            s.status = XX_STRINGS_SCAN_INVALID;
            goto finish;
        }
        total = s.options.length;
    }
    buffer = xx_rt_malloc(total && total < s.options.chunk_size ? (size_t)total : s.options.chunk_size);
    if (!buffer) {
        s.status = XX_STRINGS_SCAN_NO_MEMORY;
        goto finish;
    }
    if (s.options.encodings & 3) decoders[count++].encoding = s.options.encodings & 3;
    for (encoding = XX_STRINGS_UTF16LE; encoding <= XX_STRINGS_UTF16BE; encoding *= 2)
        if (s.options.encodings & encoding)
            for (phase = 0; phase < 2; ++phase) {
                decoders[count].encoding = encoding;
                decoders[count++].phase = phase;
            }
    for (i = 0; i < count; ++i) decoders[i].scan = &s;
    level = xx_pd_enter_level(pd, total, "Extracting strings");
    while (done < total) {
        size_t chunk = total - done < s.options.chunk_size ? (size_t)(total - done) : s.options.chunk_size;
        uint64_t offset = s.options.offset + done;
        if (xx_pd_is_stopped(pd)) {
            s.status = XX_STRINGS_SCAN_CANCELLED;
            goto finish;
        }
        if (!xx_io_read_at(device, (int64_t)offset, buffer, chunk)) {
            s.status = XX_STRINGS_SCAN_IO;
            goto finish;
        }
        for (i = 0; i < count; ++i) {
            if (decoders[i].encoding & 3) {
                if (!scan_narrow(decoders + i, buffer, chunk, offset, pd)) goto finish;
            } else {
                size_t at;
                for (at = 0; at < chunk; ++at) {
                    if (!(at & 4095) && xx_pd_is_stopped(pd)) {
                        s.status = XX_STRINGS_SCAN_CANCELLED;
                        goto finish;
                    }
                    if (!feed_utf16(decoders + i, buffer[at], offset + at)) goto finish;
                }
            }
        }
        done += chunk;
        s.stats.bytes_scanned = done;
        xx_pd_set_current(pd, level, done);
    }
    if (xx_pd_is_stopped(pd)) {
        s.status = XX_STRINGS_SCAN_CANCELLED;
        goto finish;
    }
    for (i = 0; i < count; ++i)
        if (!finish_run(decoders + i)) goto finish;
finish:
    xx_pd_leave_level(pd, level);
    if (xx_pd_is_stopped(pd)) s.status = XX_STRINGS_SCAN_CANCELLED;
    for (i = 0; i < count; ++i) xx_buf_free(&decoders[i].text);
    xx_rt_free(buffer);
    if (stats) *stats = s.stats;
    if (s.status != XX_STRINGS_SCAN_OK) xx_pd_set_error(pd, s.status, xx_strings_scan_status_name(s.status));
    return s.status;
}

xx_strings_scan_status xx_strings_decode_device(xx_io_device *device, unsigned encoding, uint64_t offset, uint64_t byte_size, xx_pd_struct *pd, xx_strings_text_sink sink,
                                                void *user)
{
    return xx_strings_decode_device_ex(device, encoding, offset, byte_size, 0, pd, sink, user);
}
xx_strings_scan_status xx_strings_decode_device_ex(xx_io_device *device, unsigned encoding, uint64_t offset, uint64_t byte_size, unsigned flags,
                                                   xx_pd_struct *pd, xx_strings_text_sink sink, void *user)
{
    unsigned char *input = NULL;
    char *output = NULL;
    uint64_t done = 0;
    uint32_t scalar = 0, minimum = 0, high = 0;
    unsigned pending = 0, first = 0;
    int64_t size;
    xx_strings_scan_status status = XX_STRINGS_SCAN_OK;
    if ((flags & ~XX_STRINGS_DIE_ALPHABET) ||
        (encoding != XX_STRINGS_ASCII && encoding != XX_STRINGS_UTF8 && encoding != XX_STRINGS_UTF16LE && encoding != XX_STRINGS_UTF16BE)) return XX_STRINGS_SCAN_INVALID;
    size = xx_io_total_size(device);
    if (size < 0) return XX_STRINGS_SCAN_IO;
    if (offset > (uint64_t)size || byte_size > (uint64_t)size - offset) return XX_STRINGS_SCAN_INVALID;
    if ((encoding & 12) && (byte_size & 1)) return XX_STRINGS_SCAN_INVALID;
    input = xx_rt_malloc(STRINGS_CHUNK_DEFAULT);
    if (encoding & 12) output = xx_rt_malloc(STRINGS_CHUNK_DEFAULT * 2);
    if (!input || ((encoding & 12) && !output)) {
        status = XX_STRINGS_SCAN_NO_MEMORY;
        goto finish;
    }
    while (done < byte_size) {
        size_t i, used = 0, chunk = byte_size - done < STRINGS_CHUNK_DEFAULT ? (size_t)(byte_size - done) : STRINGS_CHUNK_DEFAULT;
        if (xx_pd_is_stopped(pd)) {
            xx_pd_stop(pd);
            status = XX_STRINGS_SCAN_CANCELLED;
            goto finish;
        }
        if (!xx_io_read_at(device, (int64_t)(offset + done), input, chunk)) {
            status = XX_STRINGS_SCAN_IO;
            goto finish;
        }
        for (i = 0; i < chunk; ++i) {
            unsigned b = input[i];
            uint32_t cp;
            if (encoding == XX_STRINGS_ASCII) {
                if (b < 32 || b >= ((flags & XX_STRINGS_DIE_ALPHABET) ? 128U : 127U)) {
                    status = XX_STRINGS_SCAN_INVALID;
                    goto finish;
                }
                continue;
            }
            if (encoding == XX_STRINGS_UTF8) {
                if (pending) {
                    if ((b & 0xc0) != 0x80) {
                        status = XX_STRINGS_SCAN_INVALID;
                        goto finish;
                    }
                    scalar = (scalar << 6) | (b & 63);
                    if (--pending) continue;
                    if (scalar < minimum || !printable_with_flags(scalar, flags)) {
                        status = XX_STRINGS_SCAN_INVALID;
                        goto finish;
                    }
                } else if (b < 128) {
                    if (!printable_with_flags(b, flags)) {
                        status = XX_STRINGS_SCAN_INVALID;
                        goto finish;
                    }
                } else if (b >= 0xc2 && b <= 0xdf) {
                    scalar = b & 31;
                    pending = 1;
                    minimum = 0x80;
                } else if (b >= 0xe0 && b <= 0xef) {
                    scalar = b & 15;
                    pending = 2;
                    minimum = 0x800;
                } else if (b >= 0xf0 && b <= 0xf4) {
                    scalar = b & 7;
                    pending = 3;
                    minimum = 0x10000;
                } else {
                    status = XX_STRINGS_SCAN_INVALID;
                    goto finish;
                }
                continue;
            }
            if (!(i & 1)) {
                first = b;
                continue;
            }
            cp = encoding == XX_STRINGS_UTF16LE ? first | (b << 8) : (first << 8) | b;
            if (high) {
                if (cp < 0xdc00 || cp > 0xdfff) {
                    status = XX_STRINGS_SCAN_INVALID;
                    goto finish;
                }
                cp = 0x10000 + ((high - 0xd800) << 10) + cp - 0xdc00;
                high = 0;
            } else if (cp >= 0xd800 && cp <= 0xdbff) {
                high = cp;
                continue;
            }
            if (!printable_with_flags(cp, flags)) {
                status = XX_STRINGS_SCAN_INVALID;
                goto finish;
            }
            if (cp < 0x80) output[used++] = (char)cp;
            else if (cp < 0x800) {
                output[used++] = (char)(0xc0 | (cp >> 6));
                output[used++] = (char)(0x80 | (cp & 63));
            } else if (cp < 0x10000) {
                output[used++] = (char)(0xe0 | (cp >> 12));
                output[used++] = (char)(0x80 | ((cp >> 6) & 63));
                output[used++] = (char)(0x80 | (cp & 63));
            } else {
                output[used++] = (char)(0xf0 | (cp >> 18));
                output[used++] = (char)(0x80 | ((cp >> 12) & 63));
                output[used++] = (char)(0x80 | ((cp >> 6) & 63));
                output[used++] = (char)(0x80 | (cp & 63));
            }
        }
        if (sink && !sink((encoding & 12) ? output : (const char *)input, (encoding & 12) ? used : chunk, user)) {
            status = XX_STRINGS_SCAN_SINK;
            goto finish;
        }
        done += chunk;
    }
    if (pending || high) status = XX_STRINGS_SCAN_INVALID;
finish:
    xx_rt_free(input);
    xx_rt_free(output);
    return status;
}
