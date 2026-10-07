/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT AND BSD-3-Clause
 * Native Qlie PACK reader. Algorithm/layout evidence and MIT adaptations:
 * GARbro ArcFormats/Qlie/ArcQLIE.cs and Encryption.cs, morkt2015-2017.
 * https://github.com/morkt/GARbro/tree/b09ee4570ccb1daf6ac56710ee8934dc0b8baeb0/ArcFormats/Qlie
 * Modified64-word generator evidence: QlieMersenneTwister.cs; original
 * Mersenne Twister by Makoto Matsumoto and Takuji Nishimura1997-2002.
 * No executable scans, game key database, external runtime or restored DIE code.
 *
 * Copyright (C)2015-2017 morkt. Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the
 * following conditions: The above copyright notice and this permission notice
 * shall be included in all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Generator notice: Copyright (C)1997-2002 Makoto Matsumoto and Takuji
 * Nishimura. All rights reserved. Redistribution and use in source and binary
 * forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *1. Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *2. Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *3. The names of its contributors may not be used to endorse or promote
 *   products derived from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qlie_pack/xx_qlie_pack.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"
#ifdef QLIE_PACK
#define QP_FILE_TYPE XX_FILE_TYPE_QLIE_PACK
#else
#define QP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define QP_MAX_COUNT 0xFFFFFU
#define QP_MAX_INDEX (64U * 1024U * 1024U)
#define QP_MAX_MEMBER (256U * 1024U * 1024U)
#define QP_MAX_KEY (1024U * 1024U)
#define QP_NO_KEY 2U
#define QP_CODEC_WORK 8192U

typedef struct qp_member {
    int64_t offset, header_offset;
    uint32_t size, unpacked_size, header_size, encryption, hash;
    uint16_t raw_size;
    uint8_t key_index;
    char *name;
    uint8_t *raw_name;
    bool packed, duplicate;
} qp_member;
typedef struct qp_layout {
    xx_io_device *device;
    qp_member *members;
    uint32_t count, archive_key, legacy, key_size[2];
    uint8_t major, minor, game_key[256];
    uint8_t *key_file[2];
    bool has_game_key, has_key[2];
    int64_t format_size, base;
    uint64_t retained_memory;
    size_t index;
} qp_layout;
typedef struct qp_name_key { const char *name; uint32_t index; } qp_name_key;
static bool qp_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool qp_read(xx_io_device *device, int64_t at, void *buffer, size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || at < 0 || qp_stopped(pd) || xx_io_seek64(device, at, XX_RT_SEEK_SET)) return false;
    while (done < size) {
        ssize_t got; size_t take = size - done;
        if (qp_stopped(pd)) { return false; } if (take > 65536U) take = 65536U;
        got = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (got <= 0 || (size_t)got > take) { return false; } done += (size_t)got;
    }
    return !qp_stopped(pd);
}
static void qp_layout_free(void *pointer) {
    qp_layout *layout = (qp_layout *)pointer; uint32_t i;
    if (!layout) return;
    if (layout->members) {
        for (i = 0U; i < layout->count; ++i) { xx_mem_free(layout->members[i].name); xx_mem_free(layout->members[i].raw_name); }
        xx_mem_free(layout->members);
    }
    for (i = 0U; i < 2U; ++i) if (layout->key_file[i]) { xx_mem_zero(layout->key_file[i], layout->key_size[i]); xx_mem_free(layout->key_file[i]); }
    xx_mem_zero(layout, sizeof(*layout)); xx_mem_free(layout);
}
static size_t qp_escape(char *out, uint8_t c) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '%'; out[1] = hex[c >> 4U]; out[2] = hex[c & 15U];
    return 3U;
}
static bool qp_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}
static bool qp_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}
static char *qp_name(const uint8_t *raw, size_t size) {
    char *result = (char *)xx_mem_alloc(size * 3U + 14U);
    size_t i = 0U, at = 0U;
    if (!result) return NULL;
    while (i < size) {
        uint8_t c = raw[i++];
        if (qp_lead(c) && i < size && qp_trail(raw[i])) {
            at += qp_escape(result + at, c);
            at += qp_escape(result + at, raw[i++]);
        } else if (c >= 0x80U || c < 0x20U || c == 0x7fU || c == '%') {
            at += qp_escape(result + at, c);
        } else {
            result[at++] = c == '\\' ? '/' : (char)c;
        }
    }
    result[at] = 0;
    return result;
}
static int qp_fold_compare(const char *a, const char *b) {
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}
static int qp_compare_keys(const void *a, const void *b) {
    const qp_name_key *x = (const qp_name_key *)a;
    const qp_name_key *y = (const qp_name_key *)b;
    int order = qp_fold_compare(x->name, y->name);
    if (order) return order;
    return x->index < y->index ? -1 : x->index > y->index ? 1 : 0;
}
static void qp_suffix(char *name, uint32_t index) {
    char suffix[14];
    size_t length = xx_str_len(name), component = 0U, dot = length, i;
    int amount = xx_rt_snprintf(suffix, sizeof(suffix), "%%_%u", index);
    for (i = 0U; i < length; ++i) if (name[i] == '/') component = i + 1U;
    for (i = length; i > component + 1U; --i)
        if (name[i - 1U] == '.') { dot = i - 1U; break; }
    if (amount <= 0 || (size_t)amount >= sizeof(suffix)) return;
    xx_rt_memmove(name + dot + (size_t)amount, name + dot, length - dot + 1U);
    xx_rt_memcpy(name + dot, suffix, (size_t)amount);
}


static int qp_prefix_compare(const char *name, const char *prefix, size_t length) {
    size_t i;
    for (i = 0U; i < length; ++i) {
        unsigned char a = (unsigned char)name[i], b = (unsigned char)prefix[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 'a' - 'A');
        if (a != b) return a < b ? -1 : 1;
        if (!a) return 0;
    }
    return name[length] ? 1 : 0;
}
/* A leaf named root cannot coexist with the implicit directory root/child.
 * Alias every such leaf before any output; raw '%' is escaped, so generated
 * aliases cannot collide with original archive names. */
static bool qp_mark_prefixes(qp_layout *layout, const qp_name_key *keys, xx_pd_struct *pd) {
    uint32_t i;
    for (i = 0U; i < layout->count; ++i) {
        const char *name = layout->members[i].name;
        size_t n;
        for (n = 0U; name[n]; ++n) if (name[n] == '/' && n) {
            uint32_t first = 0U, end = layout->count;
            if (qp_stopped(pd)) return false;
            while (first < end) {
                uint32_t middle = first + (end - first) / 2U;
                if (qp_prefix_compare(keys[middle].name, name, n) < 0) first = middle + 1U;
                else end = middle;
            }
            while (first < layout->count && !qp_prefix_compare(keys[first].name, name, n))
                layout->members[keys[first++].index].duplicate = true;
        }
    }
    return true;
}

static char *qp_unicode_name(const uint8_t *raw, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    char *out = (char *)xx_mem_alloc(size * 3U + 14U);
    size_t i, at = 0U;
    if (!out) return NULL;
    for (i = 0U; i < size; i += 2U) {
        uint16_t c = xx_data_get_u16(raw + i, 2, 0, false);
        if (c >= 32U && c < 127U && c != '%') out[at++] = c == '\\' ? '/' : (char)c;
        else if (c == '%') at += qp_escape(out + at, '%');
        else {
            out[at++] = '%'; out[at++] = 'u';
            out[at++] = hex[c >> 12]; out[at++] = hex[(c >> 8) & 15U];
            out[at++] = hex[(c >> 4) & 15U]; out[at++] = hex[c & 15U];
        }
    }
    out[at] = 0; return out;
}
static bool qp_key_name(const qp_member *member, bool unicode) {
    static const char marker[] = "pack_keyfile";
    size_t i, k, units = unicode ? member->raw_size / 2U : member->raw_size;
    for (i = 0U; i + sizeof(marker) - 1U <= units; ++i) {
        for (k = 0U; k < sizeof(marker) - 1U; ++k) {
            uint16_t c = unicode ? xx_data_get_u16(member->raw_name + (i + k) * 2U, 2, 0, false) : member->raw_name[i + k];
            if (c != (uint8_t)marker[k]) break;
        }
        if (k == sizeof(marker) - 1U) return true;
    }
    return false;
}
/* MMX lane operations, expressed without host alignment/endian assumptions. */
static uint64_t qp_add(uint64_t a, uint64_t b, unsigned width) {
    uint64_t out = 0U, mask = ((uint64_t)1U << width) - 1U;
    unsigned i;
    for (i = 0U; i < 64U; i += width) out |= (((a >> i) + (b >> i)) & mask) << i;
    return out;
}
static uint64_t qp_shift(uint64_t a, unsigned n) {
    return (uint32_t)((uint32_t)a << n) | (uint64_t)((uint32_t)(a >> 32) << n) << 32;
}
static uint64_t qp_rotate(uint64_t a) {
    uint32_t lo = (uint32_t)a, hi = (uint32_t)(a >> 32);
    return (uint32_t)((lo << 3) | (lo >> 29)) | (uint64_t)((hi << 3) | (hi >> 29)) << 32;
}
static bool qp_hash(const uint8_t *bytes, size_t size, bool v31, uint32_t *out, xx_pd_struct *pd) {
    uint64_t hash = 0U, key = 0U;
    size_t i;
    for (i = 0U; i + 8U <= size; i += 8U) {
        if (!(i & 65535U) && qp_stopped(pd)) return false;
        hash = qp_add(hash, v31 ? UINT64_C(0xA35793A7A35793A7) : UINT64_C(0x0307030703070307), 16U);
        key = qp_add(key, xx_data_get_u64(bytes + i, 8, 0, false) ^ hash, 16U);
        if (v31) key = qp_rotate(key);
    }
    if (v31) {
        int64_t a = (int16_t)key, b = (int16_t)(key >> 16), c = (int16_t)(key >> 32), d = (int16_t)(key >> 48);
        *out = (uint32_t)(a * c + b * d);
    } else *out = (uint32_t)(key ^ (key >> 32));
    return !qp_stopped(pd);
}
typedef struct qp_mt { uint32_t state[64]; unsigned next; } qp_mt;
static void qp_mt_init(qp_mt *mt, uint32_t seed) {
    unsigned i; mt->state[0] = seed; mt->next = 64U;
    for (i = 1U; i < 64U; ++i) mt->state[i] = 0x6611BC19U * (mt->state[i - 1U] ^ (mt->state[i - 1U] >> 30)) + i;
}
static void qp_mt_mix(qp_mt *mt, const uint8_t *bytes, size_t size) {
    size_t i, count = size / 4U; if (count > 64U) count = 64U;
    for (i = 0U; i < count; ++i) mt->state[i] ^= xx_data_get_u32(bytes + i * 4U, 4, 0, false);
}
static uint32_t qp_mt_rand(qp_mt *mt) {
    uint32_t y; unsigned i;
    if (mt->next == 64U) {
        for (i = 0U; i < 63U; ++i) {
            y = (mt->state[i] & 0x80000000U) | ((mt->state[i + 1U] & 0x7FFFFFFFU) >> 1);
            mt->state[i] = mt->state[(i + 39U) & 63U] ^ y ^ ((mt->state[i + 1U] & 1U) ? 0x9908B0DFU : 0U);
        }
        y = (mt->state[63] & 0x80000000U) | ((mt->state[0] & 0x7FFFFFFFU) >> 1);
        mt->state[63] = mt->state[38] ^ y ^ ((mt->state[62] & 1U) ? 0x9908B0DFU : 0U);
        mt->next = 0U;
    }
    y = mt->state[mt->next++]; y ^= y >> 11; y ^= (y << 7) & 0x9C4F88E3U;
    y ^= (y << 15) & 0xE7F70000U; return y ^ (y >> 18);
}
static uint64_t qp_mt_rand64(qp_mt *mt) {
    uint64_t lo = qp_mt_rand(mt); return lo | (uint64_t)qp_mt_rand(mt) << 32;
}
static void qp_decode_name(qp_layout *layout, uint8_t *raw, size_t size) {
    size_t i;
    if (layout->minor == 1U && layout->major == 3U) {
        uint32_t units = (uint32_t)(size / 2U), hash = ((units * units) ^ units ^ 0x3E13U ^
            (layout->archive_key >> 16) ^ layout->archive_key) & 0xFFFFU, key = hash;
        for (i = 0U; i < size / 2U; ++i) {
            key = hash + (uint32_t)i + 8U * key;
            raw[i * 2U] ^= (uint8_t)key; raw[i * 2U + 1U] ^= (uint8_t)(key >> 8);
        }
    } else {
        uint32_t key = (layout->major == 3U ? layout->archive_key : 0xC4U) ^ 0x3EU;
        if (layout->major != 1U || layout->legacy != XX_QLIE_PACK_LEGACY_V1) key += (uint32_t)size;
        for (i = 0U; i < size; ++i) raw[i] ^= (uint8_t)((((uint32_t)i + 1U) ^ key) + (uint32_t)i + 1U);
    }
}
static bool qp_decrypt(qp_layout *layout, const qp_member *member, uint8_t *bytes, xx_pd_struct *pd) {
    const uint8_t *key = member->key_index < 2U && layout->has_key[member->key_index] ? layout->key_file[member->key_index] : NULL;
    size_t key_size = member->key_index < 2U ? layout->key_size[member->key_index] : 0U, i;
    uint64_t hash64, table64[16]; uint32_t hash, seed, t, table[32];
    uint8_t key_data[1024]; bool v31 = layout->major == 3U && layout->minor == 1U;
    if (!member->encryption || member->size < 8U) return !qp_stopped(pd);
    if (!v31 && (layout->major < 3U || member->key_index == QP_NO_KEY)) {
        uint32_t x = ((layout->major == 1U && layout->legacy == XX_QLIE_PACK_LEGACY_V1 ? 0U : member->size) + layout->archive_key) ^ 0xFEC9753EU;
        uint64_t feedback = x | (uint64_t)x << 32;
        hash64 = UINT64_C(0xA73C5F9DA73C5F9D);
        for (i = 0U; i + 8U <= member->size; i += 8U) {
            if (!(i & 65535U) && qp_stopped(pd)) return false;
            hash64 = qp_add(hash64, UINT64_C(0xCE24F523CE24F523), 32U) ^ feedback;
            feedback = xx_data_get_u64(bytes + i, 8, 0, false) ^ hash64; xx_data_set_u64(bytes + i, 8, 0, feedback, false);
        }
        return !qp_stopped(pd);
    }
    hash = v31 && member->encryption == 2U ? 0x86F7E2U : 0x85F532U;
    seed = v31 && member->encryption == 2U ? 0x4437F1U : 0x33F641U;
    for (i = 0U; i < (v31 ? member->raw_size / 2U : member->raw_size); ++i) {
        hash += v31 ? (uint32_t)xx_data_get_u16(member->raw_name + i * 2U, 2, 0, false) << (i & 7U) : ((uint32_t)i & 255U) * member->raw_name[i];
        seed ^= hash;
    }
    t = v31 && member->encryption == 2U ? 13U : 7U;
    seed += layout->archive_key ^ (t * (member->size & 0xFFFFFFU) + member->size + hash +
        (hash ^ member->size ^ (t == 13U ? 0x56E213U : 0x8F32DCU)));
    seed = (t == 13U ? 13U : 9U) * (seed & 0xFFFFFFU);
    if (!v31) {
        qp_mt mt;
        if (layout->has_game_key) seed ^= 0x453AU;
        qp_mt_init(&mt, seed); qp_mt_mix(&mt, key, key_size);
        if (layout->has_game_key) qp_mt_mix(&mt, layout->game_key, 256U);
        for (i = 0U; i < 16U; ++i) table64[i] = qp_mt_rand64(&mt);
        for (i = 0U; i < 9U; ++i) (void)qp_mt_rand(&mt);
        hash64 = qp_mt_rand64(&mt); t = qp_mt_rand(&mt) & 15U;
        xx_mem_zero(&mt, sizeof(mt));
    } else {
        uint32_t multiplier = member->encryption == 2U ? 0x8A77F473U : 0x8DF21431U;
        for (i = 0U; i < 32U; ++i) {
            uint64_t product = (uint64_t)multiplier * (seed ^ multiplier);
            seed = (uint32_t)((product >> 32) + product); table[i] = seed;
        }
        hash64 = table[6] | (uint64_t)table[7] << 32;
        t = member->encryption == 2U ? (8U * (table[8] & 13U)) & 127U : 2U * (table[13] & 15U);
        if (member->encryption == 2U) {
            for (i = 0U; i < 256U; ++i) {
                int32_t value = (int32_t)((i + 7U) * (i + 3U));
                if (i % 3U) { value = -value; } xx_data_set_u32(key_data + i * 4U, 4, 0, (uint32_t)value, false);
            }
            if (key_size >= 128U) {
                size_t k = key[49] % 73U + 128U, step = key[79] % 7U + 7U;
                for (i = 0U; i < sizeof(key_data); ++i) { k = (k + step) % key_size; key_data[i] ^= key[k]; }
            }
        }
    }
    for (i = 0U; i + 8U <= member->size; i += 8U) {
        uint64_t cell, plain;
        if (!(i & 65535U) && qp_stopped(pd)) return false;
        if (!v31) cell = table64[t];
        else {
            uint32_t index = member->encryption == 2U ? 2U * (t & 15U) : t;
            cell = table[index] | (uint64_t)table[index + 1U] << 32;
            if (member->encryption == 2U) cell ^= xx_data_get_u64(key_data + 8U * t, 8, 0, false);
        }
        hash64 = qp_add(hash64 ^ cell, cell, 32U); plain = xx_data_get_u64(bytes + i, 8, 0, false) ^ hash64;
        xx_data_set_u64(bytes + i, 8, 0, plain, false); hash64 = qp_add(hash64, plain, 8U) ^ plain;
        hash64 = qp_add(qp_shift(hash64, 1U), plain, 16U);
        t = v31 ? (member->encryption == 2U ? (t + 1U) & 127U : (t + 2U) & 31U) : (t + 1U) & 15U;
    }
    xx_mem_zero(table, sizeof(table)); xx_mem_zero(table64, sizeof(table64)); xx_mem_zero(key_data, sizeof(key_data));
    return !qp_stopped(pd);
}
/* Validate the entire dictionary as a DAG. A repeated child is permitted;
 * a gray child is a cycle. Expansion lengths saturate at the output bound+1. */
static bool qp_bpe_lengths(const uint8_t left[256], const uint8_t right[256],
                           uint64_t lengths[256], uint64_t cap, xx_pd_struct *pd) {
    uint8_t color[256], node[257], phase[257]; unsigned root;
    xx_mem_zero(color, sizeof(color));
    for (root = 0U; root < 256U; ++root) {
        unsigned depth = 0U;
        if (color[root] == 2U) continue;
        node[0] = (uint8_t)root; phase[0] = 0U;
        for (;;) {
            uint8_t n = node[depth];
            if (qp_stopped(pd)) return false;
            if (left[n] == n) { color[n] = 2U; lengths[n] = 1U; }
            else if (phase[depth] < 2U) {
                uint8_t child = phase[depth] ? right[n] : left[n];
                color[n] = 1U; ++phase[depth];
                if (color[child] == 1U) return false;
                if (!color[child]) {
                    if (depth == 256U) return false;
                    ++depth; node[depth] = child; phase[depth] = 0U; continue;
                }
                continue;
            } else {
                uint64_t length = lengths[left[n]] + lengths[right[n]];
                lengths[n] = length > cap ? cap : length; color[n] = 2U;
            }
            if (!depth) { break; } --depth;
        }
    }
    return true;
}
static bool qp_bpe(const uint8_t *input, size_t size, uint8_t *output,
                   size_t wanted, xx_pd_struct *pd) {
    uint8_t left[256], right[256], stack[257]; uint64_t lengths[256];
    size_t src = 12U, dst = 0U; bool short_count;
    if (size < 12U || xx_data_get_u32(input, 4, 0, false) != 0xFF435031U || xx_data_get_u32(input + 8U, 4, 0, false) != wanted) return false;
    short_count = (input[4] & 1U) != 0U;
    while (src < size) {
        unsigned i; uint32_t count, k;
        if (qp_stopped(pd)) return false;
        for (i = 0U; i < 256U; ++i) { left[i] = (uint8_t)i; right[i] = 0U; }
        i = 0U;
        while (i < 256U) {
            unsigned run;
            if (src == size) return false;
            run = input[src++];
            if (run > 127U) { i += run - 127U; run = 0U; }
            if (i >= 256U) break;
            ++run; if (run > 256U - i) return false;
            while (run--) {
                if (src == size) return false;
                left[i] = input[src++];
                if (left[i] != i) { if (src == size) return false; right[i] = input[src++]; }
                ++i;
            }
        }
        if (size - src < (short_count ? 2U : 4U)) return false;
        count = short_count ? xx_data_get_u16(input + src, 2, 0, false) : xx_data_get_u32(input + src, 4, 0, false); src += short_count ? 2U : 4U;
        if ((uint64_t)count > size - src || !qp_bpe_lengths(left, right, lengths, (uint64_t)wanted + 1U, pd)) return false;
        for (k = 0U; k < count; ++k) {
            unsigned pending = 1U;
            uint8_t token = input[src++];
            if (lengths[token] > wanted - dst) return false;
            stack[0] = token;
            while (pending) {
                uint8_t n = stack[--pending];
                if (!(dst & 4095U) && qp_stopped(pd)) return false;
                if (left[n] == n) output[dst++] = n;
                else {
                    if (pending + 2U > sizeof(stack)) return false;
                    stack[pending++] = right[n]; stack[pending++] = left[n];
                }
            }
        }
    }
    return dst == wanted && !qp_stopped(pd);
}
static const xx_var *qp_option(Abstractformat *format, const xx_list_s *options, uint32_t id) {
    return xx_format_resolve_extra_parameter(format, options, id);
}
static bool qp_limits(Abstractformat *format, const xx_list_s *options, const qp_layout *layout,
                       const qp_member *member, xx_pd_struct *pd) {
    const xx_var *limit;
    uint64_t need = layout->retained_memory + member->size + (member->packed ? member->unpacked_size : 0U) + QP_CODEC_WORK;
    if (qp_stopped(pd) || member->size > QP_MAX_MEMBER || member->unpacked_size > QP_MAX_MEMBER) return false;
    limit = qp_option(format, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && member->unpacked_size > xx_var_get_u64(limit)) return false;
    limit = qp_option(format, options, XX_META_ID_OPT_MEMORY_LIMIT);
    return !limit || need <= xx_var_get_u64(limit);
}
/* Full decode precedes writes, so parser/cipher/codec errors cannot publish a
 * partially validated member even through the direct-device API. */
static bool qp_decode(Abstractformat *format, const xx_list_s *options, qp_layout *layout,
                       const qp_member *member, uint8_t **bytes, xx_pd_struct *pd) {
    uint8_t *packed = NULL, *plain = NULL; uint32_t hash; bool ok = false;
    *bytes = NULL;
    if (!qp_limits(format, options, layout, member, pd)) return false;
    packed = (uint8_t *)xx_mem_alloc(member->size ? member->size : 1U);
    if (!packed || !qp_read(layout->device, member->offset, packed, member->size, pd)) goto done;
    if (layout->major == 3U && (!qp_hash(packed, member->size, layout->minor == 1U, &hash, pd) || hash != member->hash)) goto done;
    if (!qp_decrypt(layout, member, packed, pd)) goto done;
    if (member->packed) {
        plain = (uint8_t *)xx_mem_alloc(member->unpacked_size ? member->unpacked_size : 1U);
        if (!plain || !qp_bpe(packed, member->size, plain, member->unpacked_size, pd)) goto done;
        *bytes = plain; plain = NULL;
    } else { *bytes = packed; packed = NULL; }
    ok = !qp_stopped(pd);
done:
    if (plain) { xx_mem_zero(plain, member->unpacked_size); xx_mem_free(plain); }
    if (packed) { xx_mem_zero(packed, member->size); xx_mem_free(packed); }
    if (!ok && *bytes) { xx_mem_zero(*bytes, member->unpacked_size); xx_mem_free(*bytes); *bytes = NULL; }
    return ok;
}
static int qp_hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool qp_snapshot_key(xx_qlie_pack *archive, const xx_list_s *options, qp_layout *layout) {
    const xx_var *value = qp_option(&archive->format, options, XX_META_ID_OPT_PASSWORD);
    const uint8_t *source = archive->key_file; size_t size = archive->key_file_size, i;
    const char *hex = NULL;
    layout->has_key[0] = archive->has_key_file;
    if (value) {
        layout->has_key[0] = true;
        if (value->type == XX_VAR_TYPE_BYTES || value->type == XX_VAR_TYPE_BYTES_VIEW) source = (const uint8_t *)xx_var_get_bytes(value, &size);
        else if (value->type == XX_VAR_TYPE_STRING || value->type == XX_VAR_TYPE_STRING_VIEW) {
            hex = xx_var_get_str(value); if (!hex) return false;
            size = xx_str_len(hex); if (size & 1U) return false; size /= 2U;
        } else return false;
    }
    if (size > QP_MAX_KEY || (size && !source && !hex)) return false;
    if (layout->has_key[0]) {
        layout->key_file[0] = (uint8_t *)xx_mem_alloc(size ? size : 1U);
        if (!layout->key_file[0]) return false;
        layout->key_size[0] = (uint32_t)size;
        if (hex) for (i = 0U; i < size; ++i) {
            int hi = qp_hex((unsigned char)hex[i * 2U]), lo = qp_hex((unsigned char)hex[i * 2U + 1U]);
            if (hi < 0 || lo < 0) return false;
            layout->key_file[0][i] = (uint8_t)(hi * 16 + lo);
        } else if (size) xx_rt_memcpy(layout->key_file[0], source, size);
        layout->retained_memory += size ? size : 1U;
    }
    layout->has_game_key = archive->has_game_key;
    if (layout->has_game_key) xx_rt_memcpy(layout->game_key, archive->game_key, 256U);
    return true;
}
static bool qp_footer(Abstractformat *format, uint8_t footer[28], int64_t *size, xx_pd_struct *pd) {
    xx_qlie_pack *archive = (xx_qlie_pack *)format; int64_t total;
    if (!format || !format->device || format->base_address < 0 || qp_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    *size = total - format->base_address;
    if (archive->archive_size) {
        if (archive->archive_size > (uint64_t)*size) return false;
        *size = (int64_t)archive->archive_size;
    }
    if (*size < 28 || !qp_read(format->device, format->base_address + *size - 28, footer, 28U, pd) ||
        xx_rt_memcmp(footer, "FilePackVer", 11U) || footer[12] != '.' || footer[14] || footer[15]) return false;
    return (footer[11] == '1' || footer[11] == '2') ? footer[13] == '0' : footer[11] == '3' && (footer[13] == '0' || footer[13] == '1');
}
static qp_layout *qp_parse_inner(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    qp_layout *layout = NULL; qp_name_key *keys = NULL; uint8_t footer[28], key_data[256], raw[512], fields[28], length_bytes[2];
    uint64_t index, position, bound, name_memory = 0U; int64_t size; uint32_t i, field_size; bool ok = false;
    const xx_var *profile, *limit;
    if (!qp_footer(format, footer, &size, pd)) return NULL;
    layout = (qp_layout *)xx_mem_alloc(sizeof(*layout)); if (!layout) return NULL;
    xx_mem_zero(layout, sizeof(*layout)); layout->device = format->device; layout->base = format->base_address;
    layout->format_size = size; layout->major = footer[11] - '0'; layout->minor = footer[13] - '0';
    layout->count = xx_data_get_u32(footer + 16U, 4, 0, false); index = xx_data_get_u64(footer + 20U, 8, 0, false); position = index; bound = (uint64_t)size - 28U;
    profile = qp_option(format, options, XX_QLIE_PACK_OPT_LEGACY_PROFILE);
    layout->legacy = profile ? (uint32_t)xx_var_get_u64(profile) : ((xx_qlie_pack *)format)->legacy_profile;
    if (layout->count > QP_MAX_COUNT || index > bound || (layout->major == 1U && (!layout->legacy || layout->legacy > 3U))) goto done;
    field_size = layout->major == 1U && layout->legacy != XX_QLIE_PACK_LEGACY_V2_WITH_HASH ? 24U : 28U;
    if (layout->major == 3U) {
        if (size < 0x440 || index > (uint64_t)size - 0x440U ||
            !qp_read(format->device, layout->base + size - 0x41C, key_data, sizeof(key_data), pd) ||
            !qp_hash(key_data, sizeof(key_data), layout->minor == 1U, &layout->archive_key, pd)) goto done;
        layout->archive_key &= 0x0FFFFFFFU; bound = (uint64_t)size - 0x440U;
    }
    if ((uint64_t)layout->count * (field_size + 2U) > bound - index) goto done;
    layout->retained_memory = sizeof(*layout) + (uint64_t)layout->count * sizeof(qp_member);
    limit = qp_option(format, options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit && layout->retained_memory > xx_var_get_u64(limit)) goto done;
    if (!qp_snapshot_key((xx_qlie_pack *)format, options, layout) ||
        (layout->major == 3U && !layout->minor && layout->has_game_key && !layout->has_key[0])) goto done;
    if (layout->count) {
        layout->members = (qp_member *)xx_mem_alloc((size_t)layout->count * sizeof(qp_member));
        keys = (qp_name_key *)xx_mem_alloc((size_t)layout->count * sizeof(qp_name_key));
        if (!layout->members || !keys) goto done;
        xx_mem_zero(layout->members, (size_t)layout->count * sizeof(qp_member));
    }
    for (i = 0U; i < layout->count; ++i) {
        qp_member *member = &layout->members[i]; uint32_t units; uint64_t offset;
        bool unicode = layout->major == 3U && layout->minor == 1U;
        if (qp_stopped(pd) || bound - position < 2U || position - index > QP_MAX_INDEX ||
            !qp_read(format->device, layout->base + (int64_t)position, length_bytes, 2U, pd)) goto done;
        units = xx_data_get_u16(length_bytes, 2, 0, false); member->header_offset = layout->base + (int64_t)position; position += 2U;
        if (!units || units > 256U) goto done;
        member->raw_size = (uint16_t)(units * (unicode ? 2U : 1U));
        if (bound - position < (uint64_t)member->raw_size + field_size ||
            !qp_read(format->device, layout->base + (int64_t)position, raw, member->raw_size, pd)) goto done;
        qp_decode_name(layout, raw, member->raw_size);
        member->raw_name = (uint8_t *)xx_mem_alloc(member->raw_size);
        member->name = unicode ? qp_unicode_name(raw, member->raw_size) : qp_name(raw, member->raw_size);
        if (!member->raw_name || !member->name) goto done;
        xx_rt_memcpy(member->raw_name, raw, member->raw_size); position += member->raw_size;
        name_memory += (uint64_t)member->raw_size * 4U + 14U;
        layout->retained_memory += (uint64_t)member->raw_size * 4U + 14U;
        if (name_memory > QP_MAX_INDEX || position - index + field_size > QP_MAX_INDEX ||
            !qp_read(format->device, layout->base + (int64_t)position, fields, field_size, pd)) goto done;
        offset = xx_data_get_u64(fields, 8, 0, false); member->size = xx_data_get_u32(fields + 8U, 4, 0, false); member->unpacked_size = xx_data_get_u32(fields + 12U, 4, 0, false);
        member->packed = xx_data_get_u32(fields + 16U, 4, 0, false) != 0U; member->encryption = xx_data_get_u32(fields + 20U, 4, 0, false);
        member->hash = field_size == 28U ? xx_data_get_u32(fields + 24U, 4, 0, false) : 0U;
        if (offset > index || member->size > index - offset || member->size > QP_MAX_MEMBER ||
            member->unpacked_size > QP_MAX_MEMBER || (!member->packed && member->size != member->unpacked_size) ||
            (unicode && member->encryption > 2U)) goto done;
        member->offset = layout->base + (int64_t)offset; position += field_size;
        member->header_size = 2U + member->raw_size + field_size;
        member->key_index = layout->has_key[0] ? 0U : QP_NO_KEY;
        keys[i].name = member->name; keys[i].index = i;
    }
    if (limit && layout->retained_memory > xx_var_get_u64(limit)) goto done;
    if (layout->count) {
        xx_rt_qsort(keys, layout->count, sizeof(*keys), qp_compare_keys);
        for (i = 1U; i < layout->count; ++i) if (!qp_fold_compare(keys[i - 1U].name, keys[i].name)) {
            layout->members[keys[i - 1U].index].duplicate = true; layout->members[keys[i].index].duplicate = true;
        }
        if (!qp_mark_prefixes(layout, keys, pd)) goto done;
        for (i = 0U; i < layout->count; ++i) if (layout->members[i].duplicate) qp_suffix(layout->members[i].name, i);
    }
    if (layout->major == 3U && (layout->minor == 1U || layout->has_key[0])) {
        for (i = 0U; i < layout->count; ++i) if (qp_key_name(&layout->members[i], layout->minor == 1U)) {
            uint8_t *embedded = NULL; uint32_t j;
            if (layout->members[i].unpacked_size > QP_MAX_KEY ||
                !qp_decode(format, options, layout, &layout->members[i], &embedded, pd)) goto done;
            layout->key_file[1] = embedded; layout->key_size[1] = layout->members[i].unpacked_size; layout->has_key[1] = true;
            layout->retained_memory += layout->key_size[1] ? layout->key_size[1] : 1U;
            for (j = i + 1U; j < layout->count; ++j) layout->members[j].key_index = 1U;
            break;
        }
    }
    if (limit && layout->retained_memory > xx_var_get_u64(limit)) goto done;
    ok = !qp_stopped(pd);
done:
    xx_mem_zero(key_data, sizeof(key_data)); xx_mem_zero(raw, sizeof(raw)); if (keys) xx_mem_free(keys);
    if (!ok) { qp_layout_free(layout); layout = NULL; } return layout;
}
static qp_layout *qp_parse(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    qp_layout *layout;
    if (cursor < 0) return NULL;
    layout = qp_parse_inner(format, options, pd);
    if (xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) { qp_layout_free(layout); return NULL; }
    return layout;
}
static void qp_destroy_format(Abstractformat *format) { xx_qlie_pack_destroy((xx_qlie_pack *)format); }
void xx_qlie_pack_init(xx_qlie_pack *archive, xx_io_device *device, int64_t base) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive)); xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE; archive->format.file_type = QP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE; archive->format.is_archive = true;
    xx_format_set_extension(&archive->format, "pack");
    xx_format_set_mime_type(&archive->format, "application/x-qlie-pack");
    archive->format.destroy = qp_destroy_format;
    archive->format.check_is_valid = xx_qlie_pack_check_is_valid;
    archive->format.handle_base_info = xx_qlie_pack_handle_base_info;
    archive->format.get_format_size = xx_qlie_pack_get_format_size;
    archive->format.get_number_of_archive_records = xx_qlie_pack_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_qlie_pack_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_qlie_pack_get_current_archive_record;
    archive->format.archive_record_move_to_next = xx_qlie_pack_archive_record_move_to_next;
    archive->format.unpack_current_archive_record = xx_qlie_pack_unpack_current_archive_record;
    archive->format.free_archive_records_reading = xx_qlie_pack_free_archive_records_reading;
}
xx_qlie_pack *xx_qlie_pack_create(xx_io_device *device, int64_t base) {
    xx_qlie_pack *archive = (xx_qlie_pack *)xx_mem_alloc(sizeof(*archive));
    if (archive) { xx_qlie_pack_init(archive, device, base); } return archive;
}
void xx_qlie_pack_clear_key_file(xx_qlie_pack *archive) {
    if (!archive) return;
    if (archive->key_file) { xx_mem_zero(archive->key_file, archive->key_file_size); xx_mem_free(archive->key_file); }
    archive->key_file = NULL; archive->key_file_size = 0U; archive->has_key_file = false;
    archive->format.is_valid = false; archive->format.base_info_handled = false;
}
void xx_qlie_pack_clear_game_key(xx_qlie_pack *archive) {
    if (!archive) return;
    xx_mem_zero(archive->game_key, sizeof(archive->game_key)); archive->has_game_key = false;
    archive->format.is_valid = false; archive->format.base_info_handled = false;
}
void xx_qlie_pack_destroy(xx_qlie_pack *archive) {
    if (!archive) return;
    xx_qlie_pack_clear_key_file(archive); xx_qlie_pack_clear_game_key(archive);
    xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_qlie_pack_free(xx_qlie_pack *archive) {
    if (!archive) { return; } xx_qlie_pack_destroy(archive); xx_mem_free(archive);
}
bool xx_qlie_pack_set_legacy_profile(xx_qlie_pack *archive, xx_qlie_pack_legacy_profile profile) {
    if (!archive || profile > XX_QLIE_PACK_LEGACY_V2_WITH_HASH) return false;
    archive->legacy_profile = profile; archive->format.is_valid = false;
    archive->format.base_info_handled = false; return true;
}
bool xx_qlie_pack_set_archive_size(xx_qlie_pack *archive, uint64_t size) {
    if (!archive || size > INT64_MAX) return false;
    archive->archive_size = size; archive->format.is_valid = false;
    archive->format.base_info_handled = false; return true;
}
bool xx_qlie_pack_set_key_file(xx_qlie_pack *archive, const uint8_t *bytes, size_t size) {
    uint8_t *copy;
    if (!archive || (size && !bytes) || size > QP_MAX_KEY) return false;
    copy = (uint8_t *)xx_mem_alloc(size ? size : 1U); if (!copy) return false;
    if (size) xx_rt_memcpy(copy, bytes, size);
    xx_qlie_pack_clear_key_file(archive); archive->key_file = copy;
    archive->key_file_size = size; archive->has_key_file = true; return true;
}
bool xx_qlie_pack_set_game_key(xx_qlie_pack *archive, const uint8_t bytes[256]) {
    if (!archive || !bytes) return false;
    xx_rt_memcpy(archive->game_key, bytes, 256U); archive->has_game_key = true;
    archive->format.is_valid = false; archive->format.base_info_handled = false; return true;
}
bool xx_qlie_pack_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    uint8_t footer[28]; int64_t size, cursor = format && format->device ? xx_io_tell(format->device) : -1;
    bool valid;
    if (cursor < 0) return false;
    valid = qp_footer(format, footer, &size, pd);
    if (xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) return false;
    if (valid && (footer[11] != '1' || ((xx_qlie_pack *)format)->legacy_profile ||
        xx_format_find_extra_parameter(format, XX_QLIE_PACK_OPT_LEGACY_PROFILE))) {
        qp_layout *layout = qp_parse(format, NULL, pd); valid = layout != NULL; qp_layout_free(layout);
    }
    return valid;
}
bool xx_qlie_pack_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    qp_layout *layout; xx_qlie_pack *archive;
    if (!format || qp_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    layout = qp_parse(format, NULL, pd); if (!layout) return false;
    archive = (xx_qlie_pack *)format; archive->number_of_records = layout->count;
    archive->archive_key = layout->archive_key; archive->major_version = layout->major;
    archive->minor_version = layout->minor;
    format->number_of_archive_records = layout->count; format->format_size = layout->format_size;
    format->base_info_handled = true; format->is_valid = true;
    qp_layout_free(layout); return true;
}
int64_t xx_qlie_pack_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return xx_qlie_pack_handle_base_info(format, pd) ? format->format_size : -1;
}
uint64_t xx_qlie_pack_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd) {
    return xx_qlie_pack_handle_base_info(format, pd) ? format->number_of_archive_records : 0U;
}
static bool qp_set_record(Abstractformat *format, xx_archive_record_state *state) {
    qp_layout *layout = (qp_layout *)state->internal_state;
    const qp_member *member = &layout->members[layout->index]; xx_archive_record *record = &state->current_record;
    (void)format;
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = member->header_offset; record->header_size = member->header_size;
    record->data_offset = member->offset; record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->unpacked_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->packed ? 1U : 0U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, member->encryption != 0U);
}
xx_archive_record_state *xx_qlie_pack_create_archive_records_reading(Abstractformat *format,
    const xx_list_s *options, xx_pd_struct *pd) {
    qp_layout *layout = qp_parse(format, options, pd); xx_archive_record_state *state; size_t i;
    if (!layout) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { qp_layout_free(layout); return NULL; }
    xx_archive_record_state_init(state, format); state->internal_state = layout;
    state->free_internal = qp_layout_free; state->total_records = layout->count;
    state->current_index = 0;
    if (options) for (i = 0U; i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i); xx_meta copy;
        if (qp_stopped(pd) || !meta) goto fail;
        xx_meta_init(&copy, meta->meta_id);
        if (!xx_var_copy(&copy.var, &meta->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); goto fail;
        }
    }
    state->has_record = layout->count ? qp_set_record(format, state) : false;
    if (!layout->count || state->has_record) return state;
fail:
    xx_archive_record_state_free(state); return NULL;
}
const xx_archive_record *xx_qlie_pack_get_current_archive_record(Abstractformat *format,
    xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}
bool xx_qlie_pack_archive_record_move_to_next(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    qp_layout *layout;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (qp_layout *)state->internal_state)) return false;
    if (qp_stopped(pd) || layout->index + 1U >= layout->count) {
        state->has_record = false; return false;
    }
    ++layout->index; state->current_index = (int64_t)layout->index;
    state->has_record = qp_set_record(format, state); return state->has_record;
}
bool xx_qlie_pack_unpack_current_archive_record_to_device(Abstractformat *format,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    qp_layout *layout; const qp_member *member; uint8_t *plain = NULL;
    size_t done = 0U; int64_t cursor; bool ok = false;
    if (!format || !format->device || !state || state->format != format || !state->has_record ||
        !(layout = (qp_layout *)state->internal_state) || layout->index >= layout->count ||
        destination == format->device || qp_stopped(pd)) return false;
    member = &layout->members[layout->index]; cursor = xx_io_tell(format->device);
    if (cursor < 0 || !qp_decode(format, &state->options, layout, member, &plain, pd)) goto done;
    while (destination && done < member->unpacked_size) {
        size_t take = member->unpacked_size - done; ssize_t wrote;
        if (qp_stopped(pd)) goto done;
        if (take > 65536U) take = 65536U;
        wrote = xx_io_write(destination, plain + done, take);
        if (wrote <= 0 || (size_t)wrote > take) goto done;
        done += (size_t)wrote;
    }
    ok = !qp_stopped(pd);
done:
    if (plain) { xx_mem_zero(plain, member->unpacked_size); xx_mem_free(plain); }
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) ok = false;
    return ok;
}


static bool qp_reserved(const char *component, size_t length) {
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"};
    char stem[9];
    size_t n = 0U, i;
    while (n < length && component[n] != '.') ++n;
    while (n && component[n - 1U] == ' ') --n;
    if (n >= sizeof(stem)) return false;
    for (i = 0U; i < n; ++i) {
        char c = component[i]; stem[i] = c >= 'a' && c <= 'z' ? (char)(c + 'A' - 'a') : c;
    }
    stem[n] = 0;
    if (n == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (!xx_rt_memcmp(stem, "COM", 3U) || !xx_rt_memcmp(stem, "LPT", 3U))) return true;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!xx_str_cmp(stem, names[i])) return true;
    return false;
}
static bool qp_safe_name(const char *name) {
    const char *component = name, *at;
    if (!name || !*name || *name == '/') return false;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 127U || (c && c < 32U)) return false;
        if (c == '/' || !c) {
            size_t n = (size_t)(at - component);
            if (!n || component[0] == ' ' || component[n - 1U] == '.' ||
                component[n - 1U] == ' ' || qp_reserved(component, n)) return false;
            if (!c) return true;
            component = at + 1;
        }
    }
}
bool xx_qlie_pack_unpack_current_archive_record(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    qp_layout *layout;
    const xx_var *path_option, *overwrite_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL, *stage_path = NULL;
    xx_io_device *stage = NULL;
    bool ok = false, overwrite = false;
    unsigned attempt;
    size_t prefix = 0U, n;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (qp_layout *)state->internal_state) ||
        layout->index >= layout->count || qp_stopped(pd)) return false;
    if (!qp_limits(format, &state->options, layout, &layout->members[layout->index], pd)) return false;
    path_option = qp_option(format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xx_qlie_pack_unpack_current_archive_record_to_device(format, state, NULL, pd);
    if (!qp_safe_name(layout->members[layout->index].name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option)); base = owned_base;
    }
    if (!base) goto done;
    overwrite_option = qp_option(format, &state->options, XX_META_ID_OPT_OVERWRITE);
    if (overwrite_option) overwrite = xx_var_get_bool(overwrite_option);
    path = !*base || base[xx_str_len(base) - 1U] == '/' || base[xx_str_len(base) - 1U] == '\\'
        ? xx_str_concat(base, layout->members[layout->index].name)
        : xx_str_concat3(base, "/", layout->members[layout->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    for (n = 0U; path[n]; ++n) if (path[n] == '/' || path[n] == '\\') prefix = n + 1U;
    stage_path = (char *)xx_mem_alloc(prefix + 50U);
    if (!stage_path) goto done;
    xx_rt_memcpy(stage_path, path, prefix);
    for (attempt = 0U; attempt < 128U && !qp_stopped(pd); ++attempt) {
        int wrote = xx_rt_snprintf(stage_path + prefix, 50U,
            ".xxfc-qlie-%u-%u.tmp", (unsigned)layout->index, attempt);
        if (wrote <= 0) goto done;
        /* An archive member may legally use the staging component itself. */
        if (!qp_fold_compare(stage_path, path)) continue;
        stage = xx_io_file_open(stage_path, "wbx");
        if (stage) break;
    }
    if (!stage) goto done;
    ok = xx_qlie_pack_unpack_current_archive_record_to_device(format, state, stage, pd);
    if (xx_io_close(stage)) ok = false;
    stage = NULL;
    if (ok && !qp_stopped(pd)) ok = xx_io_file_replace_a(stage_path, path, overwrite);
    else ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage_path);
done:
    if (stage) { (void)xx_io_close(stage); (void)xx_io_file_remove_a(stage_path); }
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}
void xx_qlie_pack_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state) {
    (void)format; xx_archive_record_state_free(state);
}
