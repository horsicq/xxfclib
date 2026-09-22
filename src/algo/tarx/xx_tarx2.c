/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TARX2 is a QNX transport with a fixed Blowfish key.  Its clear header is
 * followed by ECB-encrypted Gzip data; the Gzip payload is an ordinary TAR.
 *
 * The Blowfish S-boxes are the public hexadecimal digits of pi.  They are
 * generated locally with Machin's formula instead of importing an external
 * crypto implementation or embedding a copied third-party table.
 */

#include "xxfclib/algo/tarx/xx_tarx2.h"

#include "xxfclib/formats/gz/xx_gz.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

/* Only the MSVC arm of the atomics fork below needs Win32; MinGW and
 * clang take the __atomic path and must not pull this in. */
#if defined(_MSC_VER)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define XX_TARX2_P_WORDS 18U
#define XX_TARX2_SBOX_COUNT 4U
#define XX_TARX2_SBOX_WORDS 256U
#define XX_TARX2_PI_PREFIX_WORDS XX_TARX2_P_WORDS
#define XX_TARX2_PI_OUTPUT_WORDS \
    (XX_TARX2_PI_PREFIX_WORDS + XX_TARX2_SBOX_COUNT * XX_TARX2_SBOX_WORDS)
/* Extra base-2^32 words make the integer arctangent truncation irrelevant
 * to every required S-box digit. */
#define XX_TARX2_PI_GUARD_WORDS 20U
#define XX_TARX2_PI_LIMBS \
    (XX_TARX2_PI_OUTPUT_WORDS + XX_TARX2_PI_GUARD_WORDS + 1U)
#define XX_TARX2_IO_CHUNK_SIZE 32768U
#define XX_TARX2_MAX_INPUT_SIZE \
    (INT64_C(1024) * INT64_C(1024) * INT64_C(1024))

typedef struct xx_tarx2_cipher_s {
    uint32_t p[XX_TARX2_P_WORDS];
    uint32_t s[XX_TARX2_SBOX_COUNT][XX_TARX2_SBOX_WORDS];
} xx_tarx2_cipher;

typedef struct xx_tarx2_filter_s {
    xx_io_device device;
    xx_io_device *source;
    const xx_tarx2_cipher *cipher;
    int64_t data_offset;
    int64_t size;
    int64_t position;
} xx_tarx2_filter;

typedef struct xx_tarx2_count_sink_s {
    xx_io_device device;
    xx_io_device *target;
    int64_t written;
    bool failed;
} xx_tarx2_count_sink;

/* Fixed P words are the interoperable TARX2 cipher state.  The S boxes are
 * regenerated below from the standard Blowfish pi sequence. */
static const uint32_t xx_tarx2_fixed_p[XX_TARX2_P_WORDS] = {
    UINT32_C(0xe2ecd94d), UINT32_C(0x18ec84a8),
    UINT32_C(0xa919d08d), UINT32_C(0x1813ebb9),
    UINT32_C(0x553fabe1), UINT32_C(0x9e1d2471),
    UINT32_C(0x00436f8d), UINT32_C(0x2db9c225),
    UINT32_C(0x9e8544e2), UINT32_C(0x8b94ff80),
    UINT32_C(0xac045aaf), UINT32_C(0x7552e074),
    UINT32_C(0xd451a2b5), UINT32_C(0x3a67c818),
    UINT32_C(0x83883c85), UINT32_C(0x99f7318f),
    UINT32_C(0x471e2d14), UINT32_C(0xf009e778)
};

static void xx_tarx2_big_zero(uint32_t *value, size_t words) {
    if (value) xx_mem_zero(value, words * sizeof(*value));
}

static void xx_tarx2_big_copy(uint32_t *destination, const uint32_t *source,
                               size_t words) {
    if (destination && source) {
        xx_mem_copy(destination, source, words * sizeof(*destination));
    }
}

static bool xx_tarx2_big_is_zero(const uint32_t *value, size_t words) {
    size_t index;
    if (!value) return true;
    for (index = 0U; index < words; ++index) {
        if (value[index] != 0U) return false;
    }
    return true;
}

static int xx_tarx2_big_compare(const uint32_t *left, const uint32_t *right,
                                size_t words) {
    size_t index;
    if (!left || !right) return 0;
    for (index = words; index != 0U; --index) {
        uint32_t a = left[index - 1U];
        uint32_t b = right[index - 1U];
        if (a < b) return -1;
        if (a > b) return 1;
    }
    return 0;
}

static bool xx_tarx2_big_add(uint32_t *left, const uint32_t *right,
                             size_t words) {
    uint64_t carry = 0U;
    size_t index;
    if (!left || !right) return false;
    for (index = 0U; index < words; ++index) {
        uint64_t sum = (uint64_t)left[index] + right[index] + carry;
        left[index] = (uint32_t)sum;
        carry = sum >> 32U;
    }
    return carry == 0U;
}

static bool xx_tarx2_big_subtract(uint32_t *left, const uint32_t *right,
                                  size_t words) {
    uint64_t borrow = 0U;
    size_t index;
    if (!left || !right || xx_tarx2_big_compare(left, right, words) < 0) {
        return false;
    }
    for (index = 0U; index < words; ++index) {
        uint64_t subtrahend = (uint64_t)right[index] + borrow;
        if ((uint64_t)left[index] < subtrahend) {
            left[index] = (uint32_t)((UINT64_C(1) << 32U) +
                                     (uint64_t)left[index] - subtrahend);
            borrow = 1U;
        } else {
            left[index] = (uint32_t)((uint64_t)left[index] - subtrahend);
            borrow = 0U;
        }
    }
    return borrow == 0U;
}

static bool xx_tarx2_big_multiply_word(uint32_t *value, size_t words,
                                       uint32_t factor) {
    uint64_t carry = 0U;
    size_t index;
    if (!value) return false;
    for (index = 0U; index < words; ++index) {
        uint64_t product = (uint64_t)value[index] * factor + carry;
        value[index] = (uint32_t)product;
        carry = product >> 32U;
    }
    return carry == 0U;
}

static bool xx_tarx2_big_divide_word(uint32_t *value, size_t words,
                                     uint32_t divisor) {
    uint64_t remainder = 0U;
    size_t index;
    if (!value || divisor == 0U) return false;
    for (index = words; index != 0U; --index) {
        uint64_t composed = (remainder << 32U) | value[index - 1U];
        value[index - 1U] = (uint32_t)(composed / divisor);
        remainder = composed % divisor;
    }
    return true;
}

/* Calculate floor(scale * atan(1/divisor)) using its alternating series.
 * Both the scale and the arithmetic are base 2^32, so this remains exact up
 * to the deliberate per-term truncation absorbed by the guard words. */
static bool xx_tarx2_pi_arctangent(uint32_t *result, size_t words,
                                   uint32_t divisor) {
    uint32_t power[XX_TARX2_PI_LIMBS];
    uint32_t term[XX_TARX2_PI_LIMBS];
    uint32_t denominator = 1U;
    uint32_t divisor_squared;
    bool subtract = false;
    if (!result || words != XX_TARX2_PI_LIMBS || divisor < 2U ||
        divisor > UINT32_MAX / divisor) {
        return false;
    }
    divisor_squared = divisor * divisor;
    xx_tarx2_big_zero(result, words);
    xx_tarx2_big_zero(power, words);
    power[words - 1U] = 1U;
    if (!xx_tarx2_big_divide_word(power, words, divisor)) return false;

    while (!xx_tarx2_big_is_zero(power, words)) {
        xx_tarx2_big_copy(term, power, words);
        if (!xx_tarx2_big_divide_word(term, words, denominator)) return false;
        if (xx_tarx2_big_is_zero(term, words)) break;
        if (subtract) {
            if (!xx_tarx2_big_subtract(result, term, words)) return false;
        } else if (!xx_tarx2_big_add(result, term, words)) {
            return false;
        }
        if (!xx_tarx2_big_divide_word(power, words, divisor_squared) ||
            denominator > UINT32_MAX - 2U) {
            return false;
        }
        denominator += 2U;
        subtract = !subtract;
    }
    return true;
}

static bool xx_tarx2_make_standard_sboxes(
    uint32_t boxes[XX_TARX2_SBOX_COUNT][XX_TARX2_SBOX_WORDS]) {
    uint32_t atan_five[XX_TARX2_PI_LIMBS];
    uint32_t atan_two_thirty_nine[XX_TARX2_PI_LIMBS];
    size_t index;
    if (!boxes ||
        !xx_tarx2_pi_arctangent(atan_five, XX_TARX2_PI_LIMBS, 5U) ||
        !xx_tarx2_pi_arctangent(atan_two_thirty_nine, XX_TARX2_PI_LIMBS,
                                239U) ||
        !xx_tarx2_big_multiply_word(atan_five, XX_TARX2_PI_LIMBS, 16U) ||
        !xx_tarx2_big_multiply_word(atan_two_thirty_nine,
                                    XX_TARX2_PI_LIMBS, 4U) ||
        !xx_tarx2_big_subtract(atan_five, atan_two_thirty_nine,
                                XX_TARX2_PI_LIMBS) ||
        atan_five[XX_TARX2_PI_LIMBS - 1U] != 3U) {
        return false;
    }
    atan_five[XX_TARX2_PI_LIMBS - 1U] = 0U;
    for (index = 0U;
         index < XX_TARX2_SBOX_COUNT * XX_TARX2_SBOX_WORDS; ++index) {
        size_t pi_word = XX_TARX2_PI_PREFIX_WORDS + index;
        size_t limb = XX_TARX2_PI_LIMBS - 2U - pi_word;
        boxes[index / XX_TARX2_SBOX_WORDS][index % XX_TARX2_SBOX_WORDS] =
            atan_five[limb];
    }
    /* Detect a platform/arithmetic regression before a cipher state is used. */
    return boxes[0][0] == UINT32_C(0xd1310ba6) &&
           boxes[1][0] == UINT32_C(0x4b7a70e9) &&
           boxes[2][0] == UINT32_C(0xe93d5a68) &&
           boxes[3][0] == UINT32_C(0x3a39ce37) &&
           boxes[3][255] == UINT32_C(0x3ac372e6);
}

static uint32_t xx_tarx2_blowfish_f(const xx_tarx2_cipher *cipher,
                                    uint32_t value) {
    return ((cipher->s[0][(value >> 24U) & 255U] +
             cipher->s[1][(value >> 16U) & 255U]) ^
            cipher->s[2][(value >> 8U) & 255U]) +
           cipher->s[3][value & 255U];
}

static void xx_tarx2_blowfish_encrypt_block(const xx_tarx2_cipher *cipher,
                                            uint32_t *left,
                                            uint32_t *right) {
    uint32_t low = *left;
    uint32_t high = *right;
    unsigned round;
    for (round = 0U; round < 16U; ++round) {
        uint32_t swap;
        low ^= cipher->p[round];
        high ^= xx_tarx2_blowfish_f(cipher, low);
        swap = low;
        low = high;
        high = swap;
    }
    *left = high ^ cipher->p[17U];
    *right = low ^ cipher->p[16U];
}

static void xx_tarx2_blowfish_decrypt_block(const xx_tarx2_cipher *cipher,
                                            uint32_t *left,
                                            uint32_t *right) {
    uint32_t low = *left;
    uint32_t high = *right;
    int round;
    for (round = 17; round > 1; --round) {
        uint32_t swap;
        low ^= cipher->p[(unsigned)round];
        high ^= xx_tarx2_blowfish_f(cipher, low);
        swap = low;
        low = high;
        high = swap;
    }
    *left = high ^ cipher->p[0U];
    *right = low ^ cipher->p[1U];
}

static bool xx_tarx2_cipher_build(xx_tarx2_cipher *cipher) {
    uint32_t left;
    uint32_t right;
    unsigned box;
    unsigned pair;
    if (!cipher || !xx_tarx2_make_standard_sboxes(cipher->s)) return false;
    xx_mem_copy(cipher->p, xx_tarx2_fixed_p, sizeof(cipher->p));
    left = cipher->p[16U];
    right = cipher->p[17U];
    for (box = 0U; box < XX_TARX2_SBOX_COUNT; ++box) {
        for (pair = 0U; pair < XX_TARX2_SBOX_WORDS / 2U; ++pair) {
            xx_tarx2_blowfish_encrypt_block(cipher, &left, &right);
            cipher->s[box][pair * 2U] = left;
            cipher->s[box][pair * 2U + 1U] = right;
        }
    }
    return true;
}

/* The pi expansion is deterministic but deliberately computed rather than
 * copied.  Cache the resulting 4 KiB cipher state after the first caller;
 * acquire/release ordering prevents another thread from observing it midway
 * through S-box regeneration. */
static xx_tarx2_cipher xx_tarx2_cached_cipher;

/* Three toolchain arms, not three operating systems: MSVC has no __atomic
 * builtin, GCC and clang do, and anything else falls back to plain loads.
 * Keying the first arm on _MSC_VER rather than _WIN32 keeps MinGW on the
 * builtin path, where it belongs. */
#if defined(_MSC_VER)
static volatile LONG xx_tarx2_cached_cipher_state = 0;

static int xx_tarx2_cached_state_load(void) {
    return (int)InterlockedCompareExchange(&xx_tarx2_cached_cipher_state, 0, 0);
}

static bool xx_tarx2_cached_state_claim(void) {
    return InterlockedCompareExchange(&xx_tarx2_cached_cipher_state, 1, 0) ==
           0;
}

static void xx_tarx2_cached_state_finish(bool success) {
    MemoryBarrier();
    (void)InterlockedExchange(&xx_tarx2_cached_cipher_state,
                              success ? 2 : 3);
}
#elif defined(__GNUC__) || defined(__clang__)
static int xx_tarx2_cached_cipher_state = 0;

static int xx_tarx2_cached_state_load(void) {
    return __atomic_load_n(&xx_tarx2_cached_cipher_state, __ATOMIC_ACQUIRE);
}

static bool xx_tarx2_cached_state_claim(void) {
    int expected = 0;
    return __atomic_compare_exchange_n(&xx_tarx2_cached_cipher_state,
                                       &expected, 1, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static void xx_tarx2_cached_state_finish(bool success) {
    __atomic_store_n(&xx_tarx2_cached_cipher_state, success ? 2 : 3,
                     __ATOMIC_RELEASE);
}
#else
/* Compilers without an atomic primitive still build correctly; their caller
 * must serialize first-use just as it must for the underlying I/O device. */
static int xx_tarx2_cached_cipher_state = 0;

static int xx_tarx2_cached_state_load(void) {
    return xx_tarx2_cached_cipher_state;
}

static bool xx_tarx2_cached_state_claim(void) {
    if (xx_tarx2_cached_cipher_state != 0) return false;
    xx_tarx2_cached_cipher_state = 1;
    return true;
}

static void xx_tarx2_cached_state_finish(bool success) {
    xx_tarx2_cached_cipher_state = success ? 2 : 3;
}
#endif

static bool xx_tarx2_cipher_init(xx_tarx2_cipher *cipher) {
    int state;
    if (!cipher) return false;
    for (;;) {
        state = xx_tarx2_cached_state_load();
        if (state == 2) {
            xx_mem_copy(cipher, &xx_tarx2_cached_cipher, sizeof(*cipher));
            return true;
        }
        if (state == 3) return false;
        if (state == 0 && xx_tarx2_cached_state_claim()) {
            bool built = xx_tarx2_cipher_build(&xx_tarx2_cached_cipher);
            xx_tarx2_cached_state_finish(built);
            if (!built) return false;
            xx_mem_copy(cipher, &xx_tarx2_cached_cipher, sizeof(*cipher));
            return true;
        }
    }
}

static uint32_t xx_tarx2_read_u32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint32_t xx_tarx2_read_u32be(const uint8_t *data) {
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

static void xx_tarx2_write_u32le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void xx_tarx2_write_u32be(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static bool xx_tarx2_decrypt_blocks(const xx_tarx2_cipher *cipher,
                                    const uint8_t *source,
                                    uint8_t *destination, size_t size) {
    size_t offset;
    if (!cipher || (!source && size != 0U) ||
        (!destination && size != 0U) || (size % XX_TARX2_BLOCK_SIZE) != 0U) {
        return false;
    }
    for (offset = 0U; offset < size; offset += XX_TARX2_BLOCK_SIZE) {
        uint32_t left = xx_tarx2_read_u32le(source + offset);
        uint32_t right = xx_tarx2_read_u32le(source + offset + 4U);
        xx_tarx2_blowfish_decrypt_block(cipher, &left, &right);
        xx_tarx2_write_u32be(destination + offset, left);
        xx_tarx2_write_u32be(destination + offset + 4U, right);
    }
    return true;
}

static bool xx_tarx2_read_exact_at(xx_io_device *device, int64_t offset,
                                   void *data, size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static ssize_t xx_tarx2_filter_read(xx_io_device *device, void *data,
                                    size_t size) {
    xx_tarx2_filter *filter = device ? (xx_tarx2_filter *)device->priv : NULL;
    uint8_t *target = (uint8_t *)data;
    size_t remaining;
    size_t completed = 0U;
    if (!filter || !target || size == 0U) return size == 0U ? 0 : -1;
    if (filter->position < 0 || filter->position > filter->size) return -1;
    if (filter->position == filter->size) return 0;
    if (size > (size_t)PTRDIFF_MAX) size = (size_t)PTRDIFF_MAX;
    remaining = (int64_t)size > filter->size - filter->position
                    ? (size_t)(filter->size - filter->position)
                    : size;

    while (remaining != 0U) {
        size_t block_offset = (size_t)(filter->position & 7);
        if (block_offset != 0U || remaining < XX_TARX2_BLOCK_SIZE) {
            uint8_t encrypted[XX_TARX2_BLOCK_SIZE];
            uint8_t plain[XX_TARX2_BLOCK_SIZE];
            int64_t block_position = filter->position - (int64_t)block_offset;
            size_t copy_size = XX_TARX2_BLOCK_SIZE - block_offset;
            if (copy_size > remaining) copy_size = remaining;
            if (!xx_tarx2_read_exact_at(filter->source,
                                        filter->data_offset + block_position,
                                        encrypted, sizeof(encrypted)) ||
                !xx_tarx2_decrypt_blocks(filter->cipher, encrypted, plain,
                                         sizeof(plain))) {
                return completed != 0U ? (ssize_t)completed : -1;
            }
            xx_mem_copy(target + completed, plain + block_offset, copy_size);
            filter->position += (int64_t)copy_size;
            completed += copy_size;
            remaining -= copy_size;
        } else {
            uint8_t encrypted[XX_TARX2_IO_CHUNK_SIZE];
            size_t block_size = remaining & ~(size_t)7U;
            if (block_size > sizeof(encrypted)) block_size = sizeof(encrypted);
            if (!xx_tarx2_read_exact_at(filter->source,
                                        filter->data_offset + filter->position,
                                        encrypted, block_size) ||
                !xx_tarx2_decrypt_blocks(filter->cipher, encrypted,
                                         target + completed, block_size)) {
                return completed != 0U ? (ssize_t)completed : -1;
            }
            filter->position += (int64_t)block_size;
            completed += block_size;
            remaining -= block_size;
        }
    }
    return (ssize_t)completed;
}

static int xx_tarx2_filter_seek64(xx_io_device *device, int64_t offset,
                                  int whence) {
    xx_tarx2_filter *filter = device ? (xx_tarx2_filter *)device->priv : NULL;
    int64_t base;
    if (!filter) return -1;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = filter->position;
            break;
        case SEEK_END:
            base = filter->size;
            break;
        default:
            return -1;
    }
    if (offset < -base || offset > filter->size - base) return -1;
    filter->position = base + offset;
    return 0;
}

static int xx_tarx2_filter_seek(xx_io_device *device, long offset,
                                int whence) {
    return xx_tarx2_filter_seek64(device, (int64_t)offset, whence);
}

static int64_t xx_tarx2_filter_tell(xx_io_device *device) {
    xx_tarx2_filter *filter = device ? (xx_tarx2_filter *)device->priv : NULL;
    return filter ? filter->position : -1;
}

static int64_t xx_tarx2_filter_size(xx_io_device *device) {
    xx_tarx2_filter *filter = device ? (xx_tarx2_filter *)device->priv : NULL;
    return filter ? filter->size : -1;
}

static bool xx_tarx2_write_all(xx_io_device *device, const void *data,
                               size_t size) {
    const uint8_t *cursor = (const uint8_t *)data;
    while (size != 0U) {
        ssize_t amount = xx_io_write(device, cursor, size);
        if (amount <= 0 || (size_t)amount > size) return false;
        cursor += (size_t)amount;
        size -= (size_t)amount;
    }
    return true;
}

static ssize_t xx_tarx2_count_sink_write(xx_io_device *device,
                                          const void *data, size_t size) {
    xx_tarx2_count_sink *sink =
        device ? (xx_tarx2_count_sink *)device->priv : NULL;
    if (!sink || (!data && size != 0U) ||
        size > (uint64_t)(INT64_MAX - sink->written) ||
        !xx_tarx2_write_all(sink->target, data, size)) {
        if (sink) sink->failed = true;
        return -1;
    }
    sink->written += (int64_t)size;
    return (ssize_t)size;
}

static int64_t xx_tarx2_count_sink_size(xx_io_device *device) {
    const xx_tarx2_count_sink *sink =
        device ? (const xx_tarx2_count_sink *)device->priv : NULL;
    return sink ? sink->written : -1;
}

static void xx_tarx2_filter_init(xx_tarx2_filter *filter,
                                 xx_io_device *source,
                                 const xx_tarx2_cipher *cipher,
                                 int64_t data_offset, int64_t size) {
    xx_mem_zero(filter, sizeof(*filter));
    filter->source = source;
    filter->cipher = cipher;
    filter->data_offset = data_offset;
    filter->size = size;
    filter->device.read = xx_tarx2_filter_read;
    filter->device.seek = xx_tarx2_filter_seek;
    filter->device.seek64 = xx_tarx2_filter_seek64;
    filter->device.tell = xx_tarx2_filter_tell;
    filter->device.total_size = xx_tarx2_filter_size;
    filter->device.get_total_size = xx_tarx2_filter_size;
    filter->device.size = xx_tarx2_filter_size;
    filter->device.priv = filter;
}

static void xx_tarx2_count_sink_init(xx_tarx2_count_sink *sink,
                                     xx_io_device *target) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    sink->device.write = xx_tarx2_count_sink_write;
    sink->device.total_size = xx_tarx2_count_sink_size;
    sink->device.get_total_size = xx_tarx2_count_sink_size;
    sink->device.size = xx_tarx2_count_sink_size;
    sink->device.priv = sink;
}

bool xx_tarx2_has_header(const uint8_t *data, size_t size) {
    return data && size >= XX_TARX2_HEADER_SIZE &&
           data[0] == 0x78U && data[1] == 0x76U && data[2] == 0x95U &&
           data[3] == 0x7dU && data[8] == 0x9cU && data[9] == 0x3bU &&
           data[10] == 0xadU && data[11] == 0x45U;
}

bool xx_tarx2_encrypt_blocks(const uint8_t *source, uint8_t *destination,
                             size_t size) {
    xx_tarx2_cipher cipher;
    size_t offset;
    if ((!source && size != 0U) || (!destination && size != 0U) ||
        (size % XX_TARX2_BLOCK_SIZE) != 0U ||
        !xx_tarx2_cipher_init(&cipher)) {
        return false;
    }
    for (offset = 0U; offset < size; offset += XX_TARX2_BLOCK_SIZE) {
        uint32_t left = xx_tarx2_read_u32be(source + offset);
        uint32_t right = xx_tarx2_read_u32be(source + offset + 4U);
        xx_tarx2_blowfish_encrypt_block(&cipher, &left, &right);
        xx_tarx2_write_u32le(destination + offset, left);
        xx_tarx2_write_u32le(destination + offset + 4U, right);
    }
    return true;
}

bool xx_tarx2_decode_device(xx_io_device *source, int64_t source_offset,
                            int64_t source_size, xx_io_device *destination,
                            int64_t *output_size, xx_pd_struct *pd) {
    uint8_t header[XX_TARX2_HEADER_SIZE];
    xx_tarx2_cipher cipher;
    xx_tarx2_filter filter;
    xx_tarx2_count_sink sink;
    xx_gz gzip;
    int64_t total_size;
    int64_t encrypted_size;
    bool result;
    if (output_size) *output_size = -1;
    if (!source || !destination || source_offset < 0 ||
        source_size < (int64_t)(XX_TARX2_HEADER_SIZE + XX_TARX2_BLOCK_SIZE) ||
        source_size > XX_TARX2_MAX_INPUT_SIZE ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(source);
    if (total_size < source_offset || source_size > total_size - source_offset ||
        !xx_tarx2_read_exact_at(source, source_offset, header,
                                sizeof(header)) ||
        !xx_tarx2_has_header(header, sizeof(header))) {
        return false;
    }
    encrypted_size = (source_size - (int64_t)XX_TARX2_HEADER_SIZE) &
                     ~(int64_t)(XX_TARX2_BLOCK_SIZE - 1U);
    if (encrypted_size < (int64_t)XX_TARX2_BLOCK_SIZE ||
        !xx_tarx2_cipher_init(&cipher)) {
        return false;
    }
    xx_tarx2_filter_init(&filter, source, &cipher,
                         source_offset + (int64_t)XX_TARX2_HEADER_SIZE,
                         encrypted_size);
    xx_tarx2_count_sink_init(&sink, destination);
    xx_gz_init(&gzip, &filter.device, 0);
    result = xx_gz_handle_base_info(&gzip.format, pd) &&
             xx_gz_unpack_to_device(&gzip, &sink.device, pd) && !sink.failed;
    xx_gz_destroy(&gzip);
    if (!result || sink.written < 0) return false;
    if (output_size) *output_size = sink.written;
    return true;
}
