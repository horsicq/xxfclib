/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AnalogX installers (EmuCore "FFS" payload).  xx_sfx_analogx_emucore_ffs.h
 * carries the layout.
 *
 * Everything here was written from the structure of the files and from a
 * static reading of the installer stub's own routines (EmuCore ffs.c,
 * ffs-cmp1.c, ffs-cmp2.c as named by its assertion strings); no stub code is
 * executed.  The stub finds the image through the u32 at the end of the
 * file, or at offset 0 of a flat file, checks "FFS!" and count == ~count,
 * then binary-searches the hash table.  This reader does the same and also
 * accepts an image carved out of its executable.
 *
 * ffs-cmp2 is the LZHUF scheme of Haruki Okumura and Haruyasu Yoshizaki
 * (1988): LZSS whose literals and match lengths share one adaptive Huffman
 * tree, a match position coded as a prefix plus six raw bits.  The model
 * below is written from that published description with the stub's
 * parameters (THRESHOLD 3, 313 symbols, 4096-byte ring preset to 4036
 * spaces).  It streams: only the ring and a small staging buffer are held.
 *
 * ffs-cmp1 is EmuCore's own opcode format.  Its copy opcodes go through the
 * Watcom compiler's inline memcpy, which moves whole dwords once the
 * destination is dword aligned; for a distance below four that reads bytes
 * the copy has not written yet, which in the stub's zero-filled, dword
 * aligned buffer are zero.  The decoder reproduces that, so the output is
 * what the stub would write.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_analogx_emucore_ffs/xx_sfx_analogx_emucore_ffs.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* xxfc_defs.h is shared and is not edited from here, so the file-type
 * constant is resolved through the alias macro that the enumerator defines. */
#ifdef SFX_ANALOGX_EMUCORE_FFS
#define XX_SFX_ANALOGX_EMUCORE_FFS_FILE_TYPE XX_FILE_TYPE_SFX_ANALOGX_EMUCORE_FFS
#else
#define XX_SFX_ANALOGX_EMUCORE_FFS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- layout and limits ----------------------------------------------------- */

#define FFS_HEADER 16
#define FFS_ENTRY 8
#define FFS_RECORD_HEADER 8
#define FFS_PAYLOAD_HEADER 8U
#define FFS_TRAILER 4
/* An installer holds a handful of files; the cap only keeps a crafted table
 * (read whole by the probe) at 512 KiB. */
#define FFS_MAX_COUNT 0x10000U
/* The smallest MZ image that can carry a trailer-located image. */
#define FFS_MIN_EXE 0x40

/* The installer script, CRC-32 of "INSTALL.DAT". */
#define FFS_SCRIPT_HASH 0x69BD3630U
#define FFS_SCRIPT_MAX 0x100000U
/* Stub bytes searched for file-name strings. */
#define FFS_STUB_SCAN_MAX INT64_C(0x1000000)
#define FFS_NAME_MAX 255U
#define FFS_NAME_MIN 3U
#define FFS_NAME_DEPTH 32U

/* ffs-cmp2 (LZHUF, THRESHOLD 3). */
#define LZH_N 4096U
#define LZH_F 60U
#define LZH_THRESHOLD 3U
#define LZH_NCHAR (256U - LZH_THRESHOLD + LZH_F) /* 313 */
#define LZH_T (LZH_NCHAR * 2U - 1U)            /* 625 */
#define LZH_R (LZH_T - 1U)
#define LZH_MAX_FREQ 0x8000U
/* The bit buffer may look up to two bytes past the stream, as the stub's
 * does; a stream that needs more is broken. */
#define LZH_OVERRUN 2U
/* Densest possible coding: a 60-byte match in 1 + 9 bits. */
#define LZH_MAX_RATIO 48U

/* ffs-cmp1. */
#define CMP1_MAX_CHANNELS 8U

/* Output staging: the longest cmp1 distance is 0xFFFF and the longest
 * single operation writes 0xFFFF bytes. */
#define FFS_WINDOW 0x40000U
#define FFS_KEEP 0x10010U
#define FFS_INPUT_BUFFER 0x10000U

#define FFS_METHOD_BROKEN 0xFFU

static const uint8_t ffs_magic[4] = {'F', 'F', 'S', '!'};
static const uint8_t ffs_tag_cmp1[4] = {'F', 'F', 'C', '!'};
static const uint8_t ffs_tag_cmp2[4] = {'F', 'F', 'C', '@'};

/* Names the installer stub itself uses for its script and uninstaller;
 * they let a carved image (no stub strings) still name those two. */
static const char *const ffs_builtin_names[] = {"install.dat", "uninst.exe"};

/* LZHUF position prefix tables (the published d_code/d_len, which the stub
 * carries unchanged), stored as run lengths: code 0 for 32 bytes, 1..3 for
 * 16 each, 4..11 for 8, 12..23 for 4, 24..47 for 2, 48..63 for 1; lengths
 * 3, 4, 5, 6, 7, 8 for 32, 48, 64, 48, 48, 16 bytes. */
static uint32_t lzh_d_code(uint32_t byte) {
    if (byte < 32U) return 0U;
    if (byte < 80U) return 1U + (byte - 32U) / 16U;
    if (byte < 144U) return 4U + (byte - 80U) / 8U;
    if (byte < 192U) return 12U + (byte - 144U) / 4U;
    if (byte < 240U) return 24U + (byte - 192U) / 2U;
    return 48U + (byte - 240U);
}

static uint32_t lzh_d_len(uint32_t byte) {
    if (byte < 32U) return 3U;
    if (byte < 80U) return 4U;
    if (byte < 144U) return 5U;
    if (byte < 192U) return 6U;
    if (byte < 240U) return 7U;
    return 8U;
}

/* --- small helpers --------------------------------------------------------- */

static uint32_t ffs_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool ffs_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool ffs_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}

static char ffs_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* --- image location -------------------------------------------------------- */

typedef struct ffs_info_s {
    int64_t size;    /**< Bytes from the base to the end of the device. */
    int64_t ffs;     /**< "FFS!", relative to the base. */
    int64_t origin;  /**< Table offset value that designates "FFS!". */
    int64_t limit;   /**< Records end at or before this (base relative). */
    int64_t end;     /**< Format size. */
    uint32_t count;
    uint32_t flags;
    bool in_executable;
} ffs_info;

typedef struct ffs_entry_s {
    uint32_t hash;
    uint32_t offset;
} ffs_entry;

/* Header at @p at: magic, a count that is its own complement's complement,
 * and room for the table before @p limit. */
static bool ffs_header_at(xx_io_device *device, int64_t base, int64_t at,
                          int64_t limit, ffs_info *info) {
    uint8_t header[FFS_HEADER];
    uint32_t count;
    if (at < 0 || limit - at < FFS_HEADER ||
        !ffs_read_at(device, base + at, header, sizeof(header)) ||
        xx_rt_memcmp(header, ffs_magic, sizeof(ffs_magic)) != 0)
        return false;
    count = ffs_le32(header + 4);
    if (count == 0U || count > FFS_MAX_COUNT ||
        count != (uint32_t)~ffs_le32(header + 8))
        return false;
    /* Every file needs a table entry and at least a record header. */
    if ((int64_t)count * (FFS_ENTRY + FFS_RECORD_HEADER) >
        limit - at - FFS_HEADER)
        return false;
    info->ffs = at;
    info->count = count;
    info->flags = ffs_le32(header + 12);
    return true;
}

/* Finds the image, reads its table into @p table (count entries, owned by
 * the caller) and checks every record header.  @p table may be NULL, in
 * which case a temporary copy is used. */
static bool ffs_scan(Abstractformat *format, ffs_info *info,
                     ffs_entry **table_out, xx_pd_struct *pd) {
    uint8_t head[4], trailer[4], record[FFS_RECORD_HEADER];
    uint8_t *raw = NULL;
    ffs_entry *table = NULL;
    int64_t total, table_end, max_end = 0;
    uint32_t index, min_offset = UINT32_MAX;
    bool result = false;
    if (table_out) *table_out = NULL;
    if (!format || !format->device || !info || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(info, sizeof(*info));
    info->size = total - format->base_address;
    if (info->size < FFS_HEADER + FFS_ENTRY + FFS_RECORD_HEADER ||
        !ffs_read_at(format->device, format->base_address, head, sizeof(head)))
        return false;
    if (xx_rt_memcmp(head, ffs_magic, sizeof(ffs_magic)) == 0) {
        /* A flat or carved image. */
        info->limit = info->size;
        if (!ffs_header_at(format->device, format->base_address, 0,
                           info->limit, info))
            return false;
    } else if (head[0] == 'M' && head[1] == 'Z') {
        /* The stub's way: the last dword points at the image. */
        int64_t pointer;
        if (info->size < FFS_MIN_EXE ||
            !ffs_read_at(format->device,
                         format->base_address + info->size - FFS_TRAILER,
                         trailer, sizeof(trailer)))
            return false;
        pointer = (int64_t)ffs_le32(trailer);
        info->limit = info->size - FFS_TRAILER;
        if (pointer < 2 ||
            !ffs_header_at(format->device, format->base_address, pointer,
                           info->limit, info))
            return false;
        info->in_executable = true;
    } else {
        return false;
    }
    table_end = info->ffs + FFS_HEADER + (int64_t)info->count * FFS_ENTRY;
    raw = (uint8_t *)xx_mem_alloc((size_t)info->count * FFS_ENTRY);
    table = (ffs_entry *)xx_mem_alloc((size_t)info->count * sizeof(*table));
    if (!raw || !table ||
        !ffs_read_at(format->device,
                     format->base_address + info->ffs + FFS_HEADER, raw,
                     (size_t)info->count * FFS_ENTRY))
        goto done;
    for (index = 0U; index < info->count; ++index) {
        table[index].hash = ffs_le32(raw + (size_t)index * FFS_ENTRY);
        table[index].offset = ffs_le32(raw + (size_t)index * FFS_ENTRY + 4U);
        /* The stub binary-searches the hashes. */
        if (index != 0U && table[index].hash <= table[index - 1U].hash)
            goto done;
        if (table[index].offset < min_offset)
            min_offset = table[index].offset;
    }
    /* The first record follows the table directly. */
    info->origin = (int64_t)min_offset - (table_end - info->ffs);
    if (info->origin < 0) goto done;
    /* In an executable the offsets count from the file start, which is
     * exactly what the trailer says. */
    if (info->in_executable && info->origin != info->ffs) goto done;
    for (index = 0U; index < info->count; ++index) {
        int64_t at = info->ffs + ((int64_t)table[index].offset - info->origin);
        int64_t end;
        if ((index & 0xFFFU) == 0xFFFU && ffs_stopped(pd)) goto done;
        if (at < table_end || info->limit - at < FFS_RECORD_HEADER ||
            !ffs_read_at(format->device, format->base_address + at, record,
                         sizeof(record)) ||
            ffs_le32(record) != table[index].hash)
            goto done;
        end = at + FFS_RECORD_HEADER + (int64_t)ffs_le32(record + 4);
        if (end > info->limit) goto done;
        if (end > max_end) max_end = end;
    }
    if (info->in_executable) {
        info->end = info->size;
    } else {
        /* A carved image may still carry the stub's trailer. */
        info->end = max_end;
        if (info->origin != 0 && info->size - max_end >= FFS_TRAILER &&
            ffs_read_at(format->device, format->base_address + max_end,
                        trailer, sizeof(trailer)) &&
            (int64_t)ffs_le32(trailer) == info->origin)
            info->end = max_end + FFS_TRAILER;
    }
    result = true;
done:
    if (raw) xx_mem_free(raw);
    if (result && table_out) {
        *table_out = table;
    } else if (table) {
        xx_mem_free(table);
    }
    return result;
}

/* --- input and output ------------------------------------------------------ */

typedef struct ffs_input_s {
    xx_io_device *device;
    const uint8_t *memory;
    int64_t offset;      /**< Device offset of the next refill. */
    uint64_t remaining;  /**< Payload bytes not yet buffered. */
    uint32_t overrun;    /**< Reads past the end of the payload. */
    bool failed;
    size_t length;
    size_t position;
    uint8_t buffer[FFS_INPUT_BUFFER];
} ffs_input;

/* The next payload byte, or -1 past the end (a device error sets failed). */
static int32_t ffs_in_get(ffs_input *in) {
    if (in->position == in->length) {
        size_t amount;
        if (in->remaining == 0U) {
            ++in->overrun;
            return -1;
        }
        amount = in->remaining < (uint64_t)FFS_INPUT_BUFFER
                     ? (size_t)in->remaining : FFS_INPUT_BUFFER;
        if (in->memory) {
            xx_rt_memcpy(in->buffer, in->memory, amount);
            in->memory += amount;
        } else if (!ffs_read_at(in->device, in->offset, in->buffer, amount)) {
            in->failed = true;
            in->remaining = 0U;
            return -1;
        }
        in->offset += (int64_t)amount;
        in->remaining -= amount;
        in->length = amount;
        in->position = 0U;
    }
    return in->buffer[in->position++];
}

typedef struct ffs_output_s {
    xx_io_device *device; /**< NULL with memory NULL: verify only. */
    uint8_t *memory;      /**< Memory sink of exactly expected bytes. */
    uint64_t expected;
    uint64_t total;       /**< Bytes produced. */
    uint64_t written;     /**< Bytes delivered. */
    uint8_t *window;
    size_t length;        /**< Bytes staged in window. */
    size_t delivered;     /**< Leading window bytes already delivered. */
} ffs_output;

static bool ffs_deliver(ffs_output *out) {
    size_t amount = out->length - out->delivered;
    const uint8_t *data = out->window + out->delivered;
    if (amount == 0U) return true;
    if ((uint64_t)amount > out->expected - out->written) return false;
    if (out->memory) {
        xx_rt_memcpy(out->memory + out->written, data, amount);
    } else if (out->device) {
        size_t done = 0U;
        while (done < amount) {
            ssize_t wrote = xx_io_write(out->device, data + done,
                                        amount - done);
            if (wrote <= 0 || (size_t)wrote > amount - done) return false;
            done += (size_t)wrote;
        }
    }
    out->written += amount;
    out->delivered = out->length;
    return true;
}

/* Deliver what is staged and keep the last FFS_KEEP bytes as history. */
static bool ffs_make_room(ffs_output *out, size_t needed) {
    if (out->length + needed <= FFS_WINDOW) return true;
    if (!ffs_deliver(out)) return false;
    if (out->length > FFS_KEEP) {
        xx_rt_memmove(out->window, out->window + out->length - FFS_KEEP,
                      FFS_KEEP);
        out->length = FFS_KEEP;
        out->delivered = FFS_KEEP;
    }
    return out->length + needed <= FFS_WINDOW;
}

static bool ffs_finish(ffs_output *out) {
    return out->total == out->expected && ffs_deliver(out) &&
           out->written == out->expected;
}

/* --- ffs-cmp2: LZHUF with THRESHOLD 3 --------------------------------------- */

typedef struct lzh_model_s {
    uint16_t freq[LZH_T + 1U];
    uint16_t son[LZH_T];
    uint16_t prnt[LZH_T + LZH_NCHAR];
    uint8_t text[LZH_N];
    uint32_t bits;   /**< 16-bit window, MSB first. */
    uint32_t count;  /**< Valid bits in the window. */
    bool bad;
} lzh_model;

static void lzh_start(lzh_model *m) {
    uint32_t i, j;
    for (i = 0U; i < LZH_NCHAR; ++i) {
        m->freq[i] = 1U;
        m->son[i] = (uint16_t)(i + LZH_T);
        m->prnt[i + LZH_T] = (uint16_t)i;
    }
    for (i = 0U, j = LZH_NCHAR; j <= LZH_R; i += 2U, ++j) {
        m->freq[j] = (uint16_t)(m->freq[i] + m->freq[i + 1U]);
        m->son[j] = (uint16_t)i;
        m->prnt[i] = m->prnt[i + 1U] = (uint16_t)j;
    }
    m->freq[LZH_T] = 0xFFFFU;
    m->prnt[LZH_R] = 0U;
}

/* Halve the leaf weights and rebuild the tree in weight order. */
static void lzh_reconstruct(lzh_model *m) {
    uint32_t i, j, k, f;
    for (i = 0U, j = 0U; i < LZH_T; ++i) {
        if (m->son[i] >= LZH_T) {
            if (j >= LZH_NCHAR) {
                m->bad = true;
                return;
            }
            m->freq[j] = (uint16_t)((m->freq[i] + 1U) >> 1U);
            m->son[j] = m->son[i];
            ++j;
        }
    }
    if (j != LZH_NCHAR) {
        m->bad = true;
        return;
    }
    for (i = 0U, j = LZH_NCHAR; j < LZH_T; i += 2U, ++j) {
        f = (uint16_t)(m->freq[i] + m->freq[i + 1U]);
        m->freq[j] = (uint16_t)f;
        k = j - 1U;
        while (f < m->freq[k]) {
            if (k == 0U) {
                m->bad = true;
                return;
            }
            --k;
        }
        ++k;
        xx_rt_memmove(&m->freq[k + 1U], &m->freq[k], (j - k) * sizeof(uint16_t));
        m->freq[k] = (uint16_t)f;
        xx_rt_memmove(&m->son[k + 1U], &m->son[k], (j - k) * sizeof(uint16_t));
        m->son[k] = (uint16_t)i;
    }
    for (i = 0U; i < LZH_T; ++i) {
        k = m->son[i];
        if (k >= LZH_T) {
            if (k >= LZH_T + LZH_NCHAR) {
                m->bad = true;
                return;
            }
            m->prnt[k] = (uint16_t)i;
        } else {
            m->prnt[k] = m->prnt[k + 1U] = (uint16_t)i;
        }
    }
}

/* Count one occurrence of symbol @p c and keep the sibling order. */
static void lzh_update(lzh_model *m, uint32_t c) {
    uint32_t i, j, k, l, steps = 0U;
    if (m->freq[LZH_R] == LZH_MAX_FREQ) {
        lzh_reconstruct(m);
        if (m->bad) return;
    }
    c = m->prnt[c + LZH_T];
    do {
        if (c >= LZH_T || ++steps > LZH_T) {
            m->bad = true;
            return;
        }
        k = ++m->freq[c];
        l = c + 1U;
        if (k > m->freq[l]) {
            /* freq[T] is a 0xFFFF sentinel, above any weight. */
            while (k > m->freq[++l]) {
            }
            --l;
            m->freq[c] = m->freq[l];
            m->freq[l] = (uint16_t)k;
            i = m->son[c];
            m->prnt[i] = (uint16_t)l;
            if (i < LZH_T) m->prnt[i + 1U] = (uint16_t)l;
            j = m->son[l];
            m->son[l] = (uint16_t)i;
            m->prnt[j] = (uint16_t)c;
            if (j < LZH_T) m->prnt[j + 1U] = (uint16_t)c;
            m->son[c] = (uint16_t)j;
            c = l;
        }
    } while ((c = m->prnt[c]) != 0U);
}

/* Past the end the stub reads zero bits here but ORs the -1 of getc() into
 * the window in lzh_byte(); both are reproduced. */
static uint32_t lzh_bit(lzh_model *m, ffs_input *in) {
    uint32_t bit;
    while (m->count <= 8U) {
        int32_t c = ffs_in_get(in);
        if (c < 0) c = 0;
        m->bits |= ((uint32_t)c << (8U - m->count)) & 0xFFFFU;
        m->count += 8U;
    }
    bit = (m->bits >> 15U) & 1U;
    m->bits = (m->bits << 1U) & 0xFFFFU;
    --m->count;
    return bit;
}

static uint32_t lzh_byte(lzh_model *m, ffs_input *in) {
    uint32_t byte;
    while (m->count <= 8U) {
        int32_t c = ffs_in_get(in);
        m->bits |= ((uint32_t)c << (8U - m->count)) & 0xFFFFU;
        m->count += 8U;
    }
    byte = (m->bits >> 8U) & 0xFFU;
    m->bits = (m->bits << 8U) & 0xFFFFU;
    m->count -= 8U;
    return byte;
}

static uint32_t lzh_symbol(lzh_model *m, ffs_input *in) {
    uint32_t c = m->son[LZH_R], depth = 0U;
    while (c < LZH_T) {
        if (++depth > LZH_T) {
            m->bad = true;
            return 0U;
        }
        c += lzh_bit(m, in);
        c = m->son[c];
    }
    c -= LZH_T;
    if (c >= LZH_NCHAR) {
        m->bad = true;
        return 0U;
    }
    lzh_update(m, c);
    return c;
}

static uint32_t lzh_position(lzh_model *m, ffs_input *in) {
    uint32_t i = lzh_byte(m, in), high = lzh_d_code(i) << 6U,
             extra = lzh_d_len(i) - 2U;
    while (extra--) i = (i << 1U) + lzh_bit(m, in);
    return high | (i & 0x3FU);
}

static bool lzh_put(ffs_output *out, uint8_t byte) {
    /* A match may run past the size; the stub drops those bytes. */
    if (out->total >= out->expected) return true;
    if (!ffs_make_room(out, 1U)) return false;
    out->window[out->length++] = byte;
    ++out->total;
    return true;
}

static bool ffs_decode_cmp2(ffs_input *in, ffs_output *out, xx_pd_struct *pd) {
    lzh_model *m = (lzh_model *)xx_mem_calloc(1U, sizeof(lzh_model));
    uint32_t r = LZH_N - LZH_F, symbols = 0U;
    bool result = false;
    if (!m) return false;
    lzh_start(m);
    xx_rt_memset(m->text, 0x20, LZH_N - LZH_F);
    while (out->total < out->expected) {
        uint32_t c;
        if ((++symbols & 0xFFFU) == 0U && ffs_stopped(pd)) goto done;
        c = lzh_symbol(m, in);
        if (m->bad || in->failed || in->overrun > LZH_OVERRUN) goto done;
        if (c < 256U) {
            if (!lzh_put(out, (uint8_t)c)) goto done;
            m->text[r] = (uint8_t)c;
            r = (r + 1U) & (LZH_N - 1U);
        } else {
            uint32_t source = (r - lzh_position(m, in) - 1U) & (LZH_N - 1U);
            uint32_t length = c - (255U - LZH_THRESHOLD), k;
            if (in->failed || in->overrun > LZH_OVERRUN) goto done;
            for (k = 0U; k < length; ++k) {
                uint8_t byte = m->text[(source + k) & (LZH_N - 1U)];
                if (!lzh_put(out, byte)) goto done;
                m->text[r] = byte;
                r = (r + 1U) & (LZH_N - 1U);
            }
        }
    }
    result = ffs_finish(out);
done:
    xx_mem_free(m);
    return result;
}

/* --- ffs-cmp1: EmuCore opcodes ---------------------------------------------- */

/* The length field of an opcode: the high nibble, else the next byte + 15,
 * else a u16 in the two bytes after that. */
static bool cmp1_length(ffs_input *in, uint32_t opcode, uint32_t *length) {
    int32_t b1, b2, b3;
    if ((opcode >> 4U) != 0U) {
        *length = opcode >> 4U;
        return true;
    }
    if ((b1 = ffs_in_get(in)) < 0) return false;
    if (b1 != 0) {
        *length = (uint32_t)b1 + 15U;
        return true;
    }
    if ((b2 = ffs_in_get(in)) < 0 || (b3 = ffs_in_get(in)) < 0) return false;
    *length = (uint32_t)b2 | ((uint32_t)b3 << 8U);
    return true;
}

static uint8_t cmp1_back(const ffs_output *out, uint32_t distance) {
    return out->window[out->length - distance];
}

/* Copy @p length bytes from @p distance back, moving them as the stub's
 * inline memcpy does: bytes up to the next dword boundary of the output
 * (the buffer itself is dword aligned), then dwords, then the rest. */
static void cmp1_copy(ffs_output *out, uint32_t distance, uint32_t length) {
    uint8_t *w = out->window;
    size_t at = out->length;
    uint32_t align = (uint32_t)((0U - (uint32_t)out->total) & 3U), k, j;
    if (length <= align) align = length;
    for (k = 0U; k < align; ++k, ++at) w[at] = w[at - distance];
    for (; length - k >= 4U; k += 4U, at += 4U) {
        uint8_t dword[4];
        /* A source byte at or past the destination is not written yet. */
        for (j = 0U; j < 4U; ++j)
            dword[j] = j < distance ? w[at - distance + j] : 0U;
        for (j = 0U; j < 4U; ++j) w[at + j] = dword[j];
    }
    for (; k < length; ++k, ++at) w[at] = w[at - distance];
    out->length += length;
    out->total += length;
}

static bool ffs_decode_cmp1(ffs_input *in, ffs_output *out, xx_pd_struct *pd) {
    uint32_t operations = 0U;
    bool result = false;
    for (;;) {
        int32_t opcode;
        uint32_t length = 0U, k, n;
        if ((++operations & 0xFFFU) == 0U && ffs_stopped(pd)) goto done;
        if ((opcode = ffs_in_get(in)) < 0) goto done;
        switch (opcode & 0x0F) {
        case 0: /* literal run */
            if (!cmp1_length(in, (uint32_t)opcode, &length) ||
                length > out->expected - out->total ||
                !ffs_make_room(out, length))
                goto done;
            for (k = 0U; k < length; ++k) {
                int32_t c = ffs_in_get(in);
                if (c < 0) goto done;
                out->window[out->length++] = (uint8_t)c;
            }
            out->total += length;
            break;
        case 1: { /* fill; a zero length ends the stream */
            int32_t c;
            if (!cmp1_length(in, (uint32_t)opcode, &length)) goto done;
            if (length == 0U) {
                result = ffs_finish(out);
                goto done;
            }
            if ((c = ffs_in_get(in)) < 0 ||
                length > out->expected - out->total ||
                !ffs_make_room(out, length))
                goto done;
            xx_rt_memset(out->window + out->length, c, length);
            out->length += length;
            out->total += length;
            break;
        }
        case 2:   /* copy, 8-bit distance */
        case 3: { /* copy, 16-bit distance */
            int32_t lo, hi = 0;
            uint32_t distance;
            if (!cmp1_length(in, (uint32_t)opcode, &length) ||
                (lo = ffs_in_get(in)) < 0 ||
                ((opcode & 0x0F) == 3 && (hi = ffs_in_get(in)) < 0))
                goto done;
            distance = (uint32_t)lo | ((uint32_t)hi << 8U);
            if (distance == 0U || distance > out->total ||
                length > out->expected - out->total ||
                !ffs_make_room(out, length))
                goto done;
            cmp1_copy(out, distance, length);
            break;
        }
        case 4: { /* each byte = previous + d */
            int32_t d;
            uint8_t value;
            if (!cmp1_length(in, (uint32_t)opcode, &length) ||
                (d = ffs_in_get(in)) < 0 || out->total < 1U ||
                length > out->expected - out->total ||
                !ffs_make_room(out, length))
                goto done;
            value = cmp1_back(out, 1U);
            for (k = 0U; k < length; ++k) {
                value = (uint8_t)(value + (uint8_t)d);
                out->window[out->length++] = value;
            }
            out->total += length;
            break;
        }
        case 5: { /* m interleaved channels, each with its own delta */
            uint8_t last[CMP1_MAX_CHANNELS], delta[CMP1_MAX_CHANNELS];
            int32_t channels, d;
            uint32_t ch = 0U;
            if (!cmp1_length(in, (uint32_t)opcode, &length) ||
                (channels = ffs_in_get(in)) < 0)
                goto done;
            /* The stub keeps eight channels; more would overrun its
             * tables, zero would reuse stale ones. */
            if (channels == 0 || (uint32_t)channels > CMP1_MAX_CHANNELS ||
                (uint32_t)channels > out->total ||
                length > out->expected - out->total ||
                !ffs_make_room(out, length))
                goto done;
            for (k = 0U; k < (uint32_t)channels; ++k) {
                if ((d = ffs_in_get(in)) < 0) goto done;
                delta[k] = (uint8_t)d;
                last[k] = cmp1_back(out, (uint32_t)channels - k);
            }
            for (k = 0U; k < length; ++k) {
                last[ch] = (uint8_t)(last[ch] + delta[ch]);
                out->window[out->length++] = last[ch];
                if (++ch >= (uint32_t)channels) ch = 0U;
            }
            out->total += length;
            break;
        }
        case 6:   /* little-endian word + n */
        case 7:   /* big-endian word + n */
        case 8:   /* little-endian word - n */
        case 9: { /* big-endian word - n */
            uint32_t word, op = (uint32_t)opcode & 0x0FU;
            n = (uint32_t)opcode >> 4U;
            if (out->total < 2U || out->expected - out->total < 2U ||
                !ffs_make_room(out, 2U))
                goto done;
            if (op == 6U || op == 8U)
                word = (uint32_t)cmp1_back(out, 2U) |
                       ((uint32_t)cmp1_back(out, 1U) << 8U);
            else
                word = ((uint32_t)cmp1_back(out, 2U) << 8U) |
                       (uint32_t)cmp1_back(out, 1U);
            word = (op <= 7U ? word + n : word - n) & 0xFFFFU;
            if (op == 6U || op == 8U) {
                out->window[out->length++] = (uint8_t)word;
                out->window[out->length++] = (uint8_t)(word >> 8U);
            } else {
                out->window[out->length++] = (uint8_t)(word >> 8U);
                out->window[out->length++] = (uint8_t)word;
            }
            out->total += 2U;
            break;
        }
        case 10: /* one byte from n + 1 back */
            n = ((uint32_t)opcode >> 4U) + 1U;
            if (n > out->total || out->expected == out->total ||
                !ffs_make_room(out, 1U))
                goto done;
            out->window[out->length] = cmp1_back(out, n);
            ++out->length;
            ++out->total;
            break;
        default: /* the stub reports "Unknown Opcode" */
            goto done;
        }
    }
done:
    return result;
}

/* --- payload decoding ------------------------------------------------------ */

typedef struct ffs_member_s {
    uint32_t hash;
    int64_t record;    /**< Record header, relative to the base. */
    uint32_t length;   /**< Payload bytes. */
    uint32_t method;   /**< XX_..._METHOD_*, or FFS_METHOD_BROKEN. */
    uint32_t unpacked;
    char *name;
} ffs_member;

/* Method and unpacked size from the first payload bytes. */
static void ffs_classify(const uint8_t *head, uint32_t length,
                         uint32_t *method, uint32_t *unpacked) {
    *method = XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_STORED;
    *unpacked = length;
    if (length < 4U) return;
    if (xx_rt_memcmp(head, ffs_tag_cmp2, 4U) == 0) {
        uint32_t size;
        *method = FFS_METHOD_BROKEN;
        if (length < FFS_PAYLOAD_HEADER) return;
        size = ~ffs_le32(head + 4U);
        /* The stub refuses a size that is not positive as an int. */
        if (size == 0U || size > (uint32_t)INT32_MAX ||
            (uint64_t)size > ((uint64_t)length - FFS_PAYLOAD_HEADER +
                              LZH_OVERRUN) * LZH_MAX_RATIO + LZH_F)
            return;
        *method = XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_CMP2;
        *unpacked = size;
    } else if (xx_rt_memcmp(head, ffs_tag_cmp1, 4U) == 0) {
        uint32_t size;
        *method = FFS_METHOD_BROKEN;
        if (length < FFS_PAYLOAD_HEADER) return;
        size = ffs_le32(head + 4U);
        if (size == 0U || size > (uint32_t)INT32_MAX) return;
        *method = XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_CMP1;
        *unpacked = size;
    }
}

/* Decodes a payload of @p length bytes read from @p in (positioned at its
 * start) into @p out (expected already set). */
static bool ffs_decode_payload(ffs_input *in, uint32_t length, uint32_t method,
                               ffs_output *out, xx_pd_struct *pd) {
    bool result = false;
    uint32_t k;
    out->window = (uint8_t *)xx_mem_alloc(FFS_WINDOW);
    if (!out->window) return false;
    if (method == XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_STORED) {
        if ((uint64_t)length != out->expected) goto done;
        for (k = 0U; k < length; ++k) {
            int32_t c;
            if ((k & 0xFFFFFU) == 0xFFFFFU && ffs_stopped(pd)) goto done;
            if ((c = ffs_in_get(in)) < 0 || !ffs_make_room(out, 1U)) goto done;
            out->window[out->length++] = (uint8_t)c;
            ++out->total;
        }
        result = ffs_finish(out);
    } else if (method == XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_CMP1 ||
               method == XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_CMP2) {
        for (k = 0U; k < FFS_PAYLOAD_HEADER; ++k)
            if (ffs_in_get(in) < 0) goto done;
        result = method == XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_CMP2
                     ? ffs_decode_cmp2(in, out, pd)
                     : ffs_decode_cmp1(in, out, pd);
    }
done:
    xx_mem_free(out->window);
    out->window = NULL;
    return result && !in->failed;
}

bool xx_sfx_analogx_emucore_ffs_decode_memory(const uint8_t *payload,
                                              size_t payload_size,
                                              uint8_t *output,
                                              size_t output_size) {
    ffs_input *in;
    ffs_output out;
    uint32_t method, unpacked;
    bool result;
    if (!payload || (!output && output_size != 0U) ||
        payload_size > (size_t)UINT32_MAX)
        return false;
    ffs_classify(payload, (uint32_t)payload_size, &method, &unpacked);
    if (method == FFS_METHOD_BROKEN || (size_t)unpacked != output_size)
        return false;
    in = (ffs_input *)xx_mem_calloc(1U, sizeof(*in));
    if (!in) return false;
    in->memory = payload;
    in->remaining = (uint64_t)payload_size;
    xx_mem_zero(&out, sizeof(out));
    out.expected = (uint64_t)output_size;
    out.memory = output;
    result = ffs_decode_payload(in, (uint32_t)payload_size, method, &out, NULL);
    xx_mem_free(in);
    return result;
}

static bool ffs_decode_member(Abstractformat *format, const ffs_member *member,
                              xx_io_device *destination, uint8_t *memory,
                              xx_pd_struct *pd) {
    ffs_input *in;
    ffs_output out;
    bool result;
    if (member->method == FFS_METHOD_BROKEN) return false;
    in = (ffs_input *)xx_mem_calloc(1U, sizeof(*in));
    if (!in) return false;
    in->device = format->device;
    in->offset = format->base_address + member->record + FFS_RECORD_HEADER;
    in->remaining = member->length;
    xx_mem_zero(&out, sizeof(out));
    out.expected = member->unpacked;
    out.device = destination;
    out.memory = memory;
    result = ffs_decode_payload(in, member->length, member->method, &out, pd);
    xx_mem_free(in);
    return result;
}

/* --- member names ---------------------------------------------------------- */

static bool ffs_is_device_stem(const char *name, size_t stem) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t k, i;
    for (k = 0U; k < sizeof(devices) / sizeof(devices[0]); ++k) {
        const char *word = devices[k];
        for (i = 0U; i < stem && word[i]; ++i)
            if (ffs_upper(name[i]) != word[i]) break;
        if (i == stem && word[i] == 0) return true;
    }
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((ffs_upper(name[0]) == 'C' && ffs_upper(name[1]) == 'O' &&
             ffs_upper(name[2]) == 'M') ||
            (ffs_upper(name[0]) == 'L' && ffs_upper(name[1]) == 'P' &&
             ffs_upper(name[2]) == 'T'));
}

/* A '/'-separated ASCII name that may become a path below the output
 * directory: no empty, "." or ".." component (so nothing absolute and
 * nothing that climbs out), no drive colon or other character Windows
 * refuses, no control byte, no component Windows would trim (trailing dot
 * or space) and no device name in any component. */
static bool ffs_safe_output_name(const char *name) {
    size_t start = 0U, index = 0U, depth = 0U;
    if (!name || !name[0]) return false;
    for (;;) {
        unsigned char c = (unsigned char)name[index];
        if (c == '/' || c == 0U) {
            size_t length = index - start, stem = 0U;
            if (length == 0U || ++depth > FFS_NAME_DEPTH) return false;
            if (name[index - 1U] == '.' || name[index - 1U] == ' ')
                return false;
            while (stem < length && name[start + stem] != '.') ++stem;
            while (stem > 0U && name[start + stem - 1U] == ' ') --stem;
            if (ffs_is_device_stem(name + start, stem)) return false;
            if (c == 0U) break;
            start = index + 1U;
        } else if (c < 0x20U || c > 0x7EU || c == '\\' || c == ':' ||
                   c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                   c == '|') {
            return false;
        }
        ++index;
    }
    return index <= FFS_NAME_MAX;
}

static bool ffs_is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

/* "XXXXXXXX.bin" in any case: the shape of an unresolved member's name,
 * which a recovered name may not take. */
static bool ffs_is_fallback_shape(const char *name, size_t length) {
    size_t k;
    if (length != 12U || name[8] != '.' || ffs_upper(name[9]) != 'B' ||
        ffs_upper(name[10]) != 'I' || ffs_upper(name[11]) != 'N')
        return false;
    for (k = 0U; k < 8U; ++k)
        if (!ffs_is_hex(name[k])) return false;
    return true;
}

static char *ffs_fallback_name(uint32_t hash) {
    static const char digits[] = "0123456789ABCDEF";
    char *name = (char *)xx_mem_alloc(13U);
    uint32_t k;
    if (!name) return NULL;
    for (k = 0U; k < 8U; ++k) name[k] = digits[(hash >> (28U - 4U * k)) & 15U];
    xx_rt_memcpy(name + 8, ".bin", 5U);
    return name;
}

/* Hashes one candidate string and names the member it identifies, if that
 * member has no name yet and the string is a usable path.  Members are
 * sorted by hash. */
static bool ffs_try_name(ffs_member *items, size_t count, const char *text,
                         size_t length) {
    char upper[FFS_NAME_MAX + 1U];
    uint32_t hash;
    size_t low = 0U, high = count, k;
    char *name;
    if (length < FFS_NAME_MIN || length > FFS_NAME_MAX) return true;
    for (k = 0U; k < length; ++k) {
        if ((unsigned char)text[k] < 0x20U || (unsigned char)text[k] > 0x7EU)
            return true;
        upper[k] = ffs_upper(text[k]);
    }
    hash = xx_crc32_calc(0U, upper, length);
    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        if (items[mid].hash < hash)
            low = mid + 1U;
        else
            high = mid;
    }
    if (low >= count || items[low].hash != hash || items[low].name) return true;
    if (ffs_is_fallback_shape(text, length)) return true;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return false;
    for (k = 0U; k < length; ++k) name[k] = text[k] == '\\' ? '/' : text[k];
    name[length] = 0;
    if (!ffs_safe_output_name(name)) {
        xx_mem_free(name);
        return true;
    }
    items[low].name = name;
    return true;
}

static bool ffs_is_script_separator(uint8_t c) {
    return c == 0x16U || c == '\r' || c == '\n' || c == ':' || c == ',' ||
           c == 0U;
}

/* Tokens of the installer script: fields are separated by 0x16, lines by
 * CR LF, keywords end in ':' and lists use ','. */
static bool ffs_names_from_script(Abstractformat *format, ffs_member *items,
                                  size_t count, xx_pd_struct *pd) {
    ffs_member *script = NULL;
    uint8_t *text;
    size_t k, start = 0U, low = 0U, high = count;
    bool result = true;
    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        if (items[mid].hash < FFS_SCRIPT_HASH)
            low = mid + 1U;
        else
            high = mid;
    }
    if (low < count && items[low].hash == FFS_SCRIPT_HASH) script = &items[low];
    if (!script || script->method == FFS_METHOD_BROKEN ||
        script->unpacked == 0U || script->unpacked > FFS_SCRIPT_MAX)
        return true;
    text = (uint8_t *)xx_mem_alloc(script->unpacked);
    if (!text) return false;
    if (!ffs_decode_member(format, script, NULL, text, pd)) {
        xx_mem_free(text);
        return true; /* a broken script only costs the names */
    }
    for (k = 0U; k <= script->unpacked && result; ++k) {
        if (k == script->unpacked || ffs_is_script_separator(text[k])) {
            size_t first = start, last = k;
            while (first < last && text[first] == ' ') ++first;
            while (last > first && text[last - 1U] == ' ') --last;
            result = ffs_try_name(items, count, (const char *)text + first,
                                  last - first);
            start = k + 1U;
        }
    }
    xx_mem_free(text);
    return result;
}

/* NUL-terminated printable strings of the stub. */
static bool ffs_names_from_stub(Abstractformat *format, int64_t stub_size,
                                ffs_member *items, size_t count,
                                xx_pd_struct *pd) {
    uint8_t *chunk;
    char run[FFS_NAME_MAX + 1U];
    size_t used = 0U;
    bool overflow = false, result = true;
    int64_t at = 0;
    if (stub_size > FFS_STUB_SCAN_MAX) stub_size = FFS_STUB_SCAN_MAX;
    if (stub_size <= 0) return true;
    chunk = (uint8_t *)xx_mem_alloc(FFS_INPUT_BUFFER);
    if (!chunk) return false;
    while (at < stub_size && result) {
        size_t amount = stub_size - at < (int64_t)FFS_INPUT_BUFFER
                            ? (size_t)(stub_size - at) : FFS_INPUT_BUFFER;
        size_t k;
        if (ffs_stopped(pd) ||
            !ffs_read_at(format->device, format->base_address + at, chunk,
                         amount)) {
            result = false;
            break;
        }
        for (k = 0U; k < amount && result; ++k) {
            uint8_t c = chunk[k];
            if (c >= 0x20U && c <= 0x7EU) {
                if (used < FFS_NAME_MAX)
                    run[used++] = (char)c;
                else
                    overflow = true;
            } else {
                if (c == 0U && !overflow && used != 0U)
                    result = ffs_try_name(items, count, run, used);
                used = 0U;
                overflow = false;
            }
        }
        at += (int64_t)amount;
    }
    xx_mem_free(chunk);
    return result;
}

static bool ffs_assign_names(Abstractformat *format, const ffs_info *info,
                             ffs_member *items, size_t count,
                             xx_pd_struct *pd) {
    size_t k;
    if (!ffs_names_from_script(format, items, count, pd)) return false;
    if (info->in_executable &&
        !ffs_names_from_stub(format, info->ffs, items, count, pd))
        return false;
    for (k = 0U; k < sizeof(ffs_builtin_names) / sizeof(ffs_builtin_names[0]);
         ++k)
        if (!ffs_try_name(items, count, ffs_builtin_names[k],
                          xx_str_len(ffs_builtin_names[k])))
            return false;
    for (k = 0U; k < count; ++k)
        if (!items[k].name && !(items[k].name = ffs_fallback_name(items[k].hash)))
            return false;
    return true;
}

/* --- member table ---------------------------------------------------------- */

typedef struct ffs_stream_s {
    ffs_member *items;
    size_t count;
    size_t index;
} ffs_stream;

static void ffs_members_free(ffs_member *items, size_t count) {
    size_t index;
    if (!items) return;
    for (index = 0U; index < count; ++index)
        if (items[index].name) xx_mem_free(items[index].name);
    xx_mem_free(items);
}

static void ffs_stream_free(void *opaque) {
    ffs_stream *stream = (ffs_stream *)opaque;
    if (!stream) return;
    ffs_members_free(stream->items, stream->count);
    xx_mem_free(stream);
}

static bool ffs_build_members(Abstractformat *format, const ffs_info *info,
                              const ffs_entry *table, ffs_member *items,
                              xx_pd_struct *pd) {
    uint32_t index;
    for (index = 0U; index < info->count; ++index) {
        uint8_t head[FFS_RECORD_HEADER + FFS_PAYLOAD_HEADER];
        ffs_member *member = &items[index];
        uint32_t length;
        size_t want;
        if ((index & 0xFFFU) == 0xFFFU && ffs_stopped(pd)) return false;
        member->hash = table[index].hash;
        member->record =
            info->ffs + ((int64_t)table[index].offset - info->origin);
        xx_mem_zero(head, sizeof(head));
        if (!ffs_read_at(format->device, format->base_address + member->record,
                         head, FFS_RECORD_HEADER))
            return false;
        length = ffs_le32(head + 4U);
        member->length = length;
        want = length < FFS_PAYLOAD_HEADER ? (size_t)length : FFS_PAYLOAD_HEADER;
        if (want != 0U &&
            !ffs_read_at(format->device,
                         format->base_address + member->record +
                             FFS_RECORD_HEADER,
                         head + FFS_RECORD_HEADER, want))
            return false;
        ffs_classify(head + FFS_RECORD_HEADER, length, &member->method,
                     &member->unpacked);
    }
    return true;
}

/* --- records --------------------------------------------------------------- */

static bool ffs_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ffs_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ffs_set_record(Abstractformat *format, xx_archive_record *record,
                           const ffs_member *member) {
    uint32_t method = member->method == FFS_METHOD_BROKEN
                          ? XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_STORED
                          : member->method;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->record;
    record->header_size = FFS_RECORD_HEADER;
    record->data_offset = format->base_address + member->record +
                          FFS_RECORD_HEADER;
    record->compressed_size = member->length;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* --- lifecycle ------------------------------------------------------------- */

void xx_sfx_analogx_emucore_ffs_init(xx_sfx_analogx_emucore_ffs *archive,
                                     xx_io_device *device,
                                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_ANALOGX_EMUCORE_FFS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdos-program");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_analogx_emucore_ffs_check_is_valid;
    archive->format.handle_base_info =
        xx_sfx_analogx_emucore_ffs_handle_base_info;
    archive->format.get_format_size =
        xx_sfx_analogx_emucore_ffs_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_analogx_emucore_ffs_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_analogx_emucore_ffs_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_analogx_emucore_ffs_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_analogx_emucore_ffs_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_analogx_emucore_ffs_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_analogx_emucore_ffs_free_archive_records_reading;
    archive->ffs_offset = -1;
    archive->offset_origin = -1;
}

xx_sfx_analogx_emucore_ffs *
xx_sfx_analogx_emucore_ffs_create(xx_io_device *device, int64_t base_address) {
    xx_sfx_analogx_emucore_ffs *archive =
        (xx_sfx_analogx_emucore_ffs *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_analogx_emucore_ffs_init(archive, device, base_address);
    return archive;
}

void xx_sfx_analogx_emucore_ffs_destroy(xx_sfx_analogx_emucore_ffs *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_analogx_emucore_ffs_free(xx_sfx_analogx_emucore_ffs *archive) {
    if (!archive) return;
    xx_sfx_analogx_emucore_ffs_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_analogx_emucore_ffs_check_is_valid(Abstractformat *format,
                                               xx_pd_struct *pd) {
    ffs_info info;
    return ffs_scan(format, &info, NULL, pd);
}

bool xx_sfx_analogx_emucore_ffs_handle_base_info(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    ffs_info info;
    xx_sfx_analogx_emucore_ffs *archive;
    if (!format || !ffs_scan(format, &info, NULL, pd)) return false;
    archive = (xx_sfx_analogx_emucore_ffs *)format;
    archive->number_of_records = info.count;
    archive->ffs_offset = info.ffs;
    archive->offset_origin = info.origin;
    archive->flags = info.flags;
    archive->in_executable = info.in_executable;
    if (!info.in_executable) {
        xx_format_set_mime_type(format, "application/octet-stream");
        xx_format_set_extension(format, "ffs");
    }
    format->number_of_archive_records = info.count;
    format->format_size = info.end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_analogx_emucore_ffs_get_format_size(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_analogx_emucore_ffs_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sfx_analogx_emucore_ffs_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_analogx_emucore_ffs_handle_base_info(format, pd))
               ? ((xx_sfx_analogx_emucore_ffs *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_sfx_analogx_emucore_ffs_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ffs_info info;
    ffs_entry *table = NULL;
    ffs_stream *stream;
    xx_archive_record_state *state;
    if (!ffs_scan(format, &info, &table, pd)) return NULL;
    stream = (ffs_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        xx_mem_free(table);
        return NULL;
    }
    stream->items = (ffs_member *)xx_mem_calloc(info.count,
                                                sizeof(*stream->items));
    stream->count = info.count;
    if (!stream->items ||
        !ffs_build_members(format, &info, table, stream->items, pd) ||
        !ffs_assign_names(format, &info, stream->items, stream->count, pd)) {
        xx_mem_free(table);
        ffs_stream_free(stream);
        return NULL;
    }
    xx_mem_free(table);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ffs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ffs_stream_free;
    state->total_records = stream->count;
    if (!ffs_copy_options(&state->options, options) ||
        !ffs_set_record(format, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfx_analogx_emucore_ffs_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sfx_analogx_emucore_ffs_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ffs_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ffs_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    if (!ffs_set_record(format, &state->current_record,
                        &stream->items[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_sfx_analogx_emucore_ffs_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ffs_stream *stream;
    const ffs_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ffs_stream *)state->internal_state) ||
        stream->index >= stream->count || ffs_stopped(pd))
        return false;
    member = &stream->items[stream->index];
    if (member->method == FFS_METHOD_BROKEN) return false;
    path_option = ffs_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return ffs_decode_member(format, member, NULL, NULL, pd);
    if (!ffs_safe_output_name(member->name)) return false;
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
        result = ffs_decode_member(format, member, destination, NULL, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_analogx_emucore_ffs_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
