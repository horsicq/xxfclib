/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM SaveRam / SaveRam2 "FLS" adaptive phrase codec.
 *
 * Ported from the reference decoder XArchive/Algos/xflsdecoder.cpp.  The
 * algorithm is reproduced verbatim: same dictionary layout, same replacement
 * cursors, same aging and scoring arithmetic, same order of operations.  The
 * only structural change is that this port reads from and writes to caller
 * buffers instead of QIODevices, and that the decoded-size ceiling comes from
 * the output capacity rather than from a container-supplied field.
 *
 * The codec emits whole dictionary phrases and never back-references the
 * produced output, so "measure" mode is simply "decode with the stores turned
 * off" -- one core routine, and the measure and the decode cannot disagree.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/flslz/xx_flslz.h"

#define FLS_ENTRY_COUNT 0x163f
#define FLS_END_CODE 0x163f
#define FLS_CODE_COUNT 0x1640
#define FLS_LITERAL_COUNT 0x100
#define FLS_CLASS_COUNT 6
#define FLS_MAX_PHRASE_LENGTH 250
#define FLS_MAX_EXTENSION_LENGTH 30
#define FLS_STREAM_TAG 0x53
/* Same bound as the reference: entries * 64 sweeps of the replacement ring. */
#define FLS_REPLACEMENT_SCAN_LIMIT (FLS_ENTRY_COUNT * 64)

static const uint16_t FLS_CLASS_BASE[FLS_CLASS_COUNT] = {
    0x000, 0x080, 0x0c0, 0x140, 0x240, 0x640
};
static const uint16_t FLS_CLASS_END[FLS_CLASS_COUNT] = {
    0x080, 0x0c0, 0x140, 0x240, 0x640, 0x1640
};
static const uint8_t FLS_CLASS_BITS[FLS_CLASS_COUNT] = { 7, 6, 7, 8, 10, 12 };

typedef struct flslz_state {
    const uint8_t *input;
    size_t input_size;
    size_t input_pos;

    uint8_t *output;
    size_t output_limit;   /* hard ceiling on the decoded size */
    size_t produced;
    bool store_output;     /* false in measure mode */

    uint32_t current_byte;
    int bits_left;

    uint16_t last_slot;
    uint16_t previous_entry;
    uint16_t previous_length;
    uint16_t token_counter;
    bool have_previous;

    uint8_t phrases[FLS_ENTRY_COUNT][FLS_MAX_PHRASE_LENGTH];
    uint8_t phrase_lengths[FLS_ENTRY_COUNT];
    uint16_t parents[FLS_ENTRY_COUNT];
    uint16_t reference_counts[FLS_ENTRY_COUNT];
    uint8_t ages[FLS_ENTRY_COUNT];
    uint16_t code_to_entry[FLS_ENTRY_COUNT];
    uint16_t entry_to_code[FLS_ENTRY_COUNT];
    uint8_t code_classes[FLS_CODE_COUNT];
    uint8_t code_scores[FLS_CODE_COUNT];
    uint16_t scan_cursors[FLS_CLASS_COUNT];
    uint8_t class_order[FLS_CLASS_COUNT];
} flslz_state;

static void flslz_initialize(flslz_state *s) {
    int i;
    int cls;
    int code;

    for (i = 0; i < FLS_ENTRY_COUNT; ++i) {
        s->parents[i] = 0xffff;
        s->code_to_entry[i] = (uint16_t)i;
        s->entry_to_code[i] = (uint16_t)i;
        if (i < FLS_LITERAL_COUNT) {
            s->phrases[i][0] = (uint8_t)i;
            s->phrase_lengths[i] = 1;
        }
    }

    for (cls = 0; cls < FLS_CLASS_COUNT; ++cls) {
        for (code = (int)FLS_CLASS_BASE[cls]; code < (int)FLS_CLASS_END[cls];
             ++code) {
            s->code_classes[code] = (uint8_t)cls;
        }
    }

    /* Each replacement cursor begins immediately before the class it scans.
     * Cursor one deliberately wraps from 0xffff to code zero on first use --
     * this is load-bearing, not an uninitialised value.  Kept from the
     * reference. */
    s->scan_cursors[0] = 0;
    s->scan_cursors[1] = 0xffff;
    s->scan_cursors[2] = 0x007f;
    s->scan_cursors[3] = 0x00bf;
    s->scan_cursors[4] = 0x013f;
    s->scan_cursors[5] = 0x023f;

    s->last_slot = 0xff;
    s->token_counter = 0x1f3;
}

static bool flslz_read_byte(flslz_state *s, uint8_t *value) {
    if (s->input_pos >= s->input_size) return false;
    *value = s->input[s->input_pos++];
    return true;
}

static bool flslz_read_bits(flslz_state *s, int count, uint32_t *value) {
    uint32_t result = 0;
    int take;
    int shift;
    uint32_t mask;
    uint8_t byte;

    if (count < 0 || count > 16) return false;

    while (count != 0) {
        if (s->bits_left == 0) {
            byte = 0;
            if (!flslz_read_byte(s, &byte)) return false;
            s->current_byte = byte;
            s->bits_left = 8;
        }
        take = (count < s->bits_left) ? count : s->bits_left;
        shift = s->bits_left - take;
        mask = ((uint32_t)1 << take) - 1;
        result = (result << take) | ((s->current_byte >> shift) & mask);
        s->bits_left -= take;
        count -= take;
    }

    *value = result;
    return true;
}

static bool flslz_read_class_order(flslz_state *s) {
    uint32_t first = 0;
    uint32_t second = 0;
    int output;
    int i;

    if (!flslz_read_bits(s, 3, &first) || !flslz_read_bits(s, 3, &second) ||
        first >= FLS_CLASS_COUNT || second >= FLS_CLASS_COUNT ||
        first == second) {
        return false;
    }

    s->class_order[0] = (uint8_t)first;
    s->class_order[1] = (uint8_t)second;
    output = 2;
    for (i = 0; i < FLS_CLASS_COUNT; ++i) {
        if (i != (int)first && i != (int)second) {
            s->class_order[output++] = (uint8_t)i;
        }
    }
    return output == FLS_CLASS_COUNT;
}

static bool flslz_read_code(flslz_state *s, uint16_t *code) {
    uint32_t selector = 0;
    uint32_t extra = 0;
    uint32_t value = 0;
    int cls;

    if (!flslz_read_bits(s, 2, &selector)) return false;
    if (selector >= 2) {
        if (!flslz_read_bits(s, 1, &extra)) return false;
        selector = selector * 2 - 2 + extra;
    }
    if (selector >= FLS_CLASS_COUNT) return false;

    cls = (int)s->class_order[selector];
    if (cls < 0 || cls >= FLS_CLASS_COUNT) return false;
    if (!flslz_read_bits(s, (int)FLS_CLASS_BITS[cls], &value)) return false;
    value += FLS_CLASS_BASE[cls];
    if (value >= FLS_CODE_COUNT) return false;
    *code = (uint16_t)value;
    return true;
}

/* Advances the replacement cursor for group `group`, which scans the code range
 * of class `group - 1`.  The off-by-one between cursor index and class index is
 * deliberate and matches the reference; do not "fix" it. */
static uint16_t flslz_advance_scan(flslz_state *s, int group) {
    uint16_t value = (uint16_t)(s->scan_cursors[group] + 1);
    if (value == FLS_CLASS_END[group - 1]) value = FLS_CLASS_BASE[group - 1];
    s->scan_cursors[group] = value;
    return value;
}

static bool flslz_swap_codes(flslz_state *s, uint16_t left, uint16_t right) {
    uint16_t left_entry;
    uint16_t right_entry;

    if (left >= FLS_ENTRY_COUNT || right >= FLS_ENTRY_COUNT) return false;
    left_entry = s->code_to_entry[left];
    right_entry = s->code_to_entry[right];
    if (left_entry >= FLS_ENTRY_COUNT || right_entry >= FLS_ENTRY_COUNT) {
        return false;
    }
    s->code_to_entry[left] = right_entry;
    s->code_to_entry[right] = left_entry;
    s->entry_to_code[right_entry] = left;
    s->entry_to_code[left_entry] = right;
    return true;
}

static bool flslz_insert_phrase(flslz_state *s, uint16_t current_entry,
                                const uint8_t *current, int current_length) {
    uint16_t candidate;
    uint16_t old_parent;
    uint16_t current_code;
    uint16_t replacement_code;
    int iterations = 0;
    int extension_length;
    int new_length;
    int remaining;

    if (current_entry >= FLS_ENTRY_COUNT || current_length <= 0 ||
        current_length > FLS_MAX_PHRASE_LENGTH ||
        s->previous_entry >= FLS_ENTRY_COUNT ||
        s->previous_length >= FLS_MAX_PHRASE_LENGTH ||
        s->phrase_lengths[s->previous_entry] != s->previous_length ||
        s->reference_counts[s->previous_entry] == 0xffff) {
        return false;
    }
    ++s->reference_counts[s->previous_entry];

    /* Ring scan for a victim slot.  Literal codes (< 0x100) are never
     * replaced.  Every slot passed over ages by one; the first unreferenced,
     * fully aged slot wins.  The previous entry cannot be chosen because its
     * reference count was just incremented above, and the current entry is
     * excluded explicitly, so the copy below never overlaps its sources. */
    candidate = s->last_slot;
    for (;;) {
        ++candidate;
        if (candidate == FLS_ENTRY_COUNT) candidate = FLS_LITERAL_COUNT;

        if (s->ages[candidate] != 0) {
            --s->ages[candidate];
        } else if (s->reference_counts[candidate] == 0 &&
                   candidate != current_entry) {
            break;
        }

        if (++iterations > FLS_REPLACEMENT_SCAN_LIMIT) return false;
    }

    old_parent = s->parents[candidate];
    if (old_parent != 0xffff) {
        if (old_parent >= FLS_ENTRY_COUNT ||
            s->reference_counts[old_parent] == 0) {
            return false;
        }
        --s->reference_counts[old_parent];
    }

    extension_length = (current_length < FLS_MAX_EXTENSION_LENGTH)
                           ? current_length
                           : FLS_MAX_EXTENSION_LENGTH;
    remaining = FLS_MAX_PHRASE_LENGTH - (int)s->previous_length;
    if (remaining < extension_length) extension_length = remaining;
    new_length = (int)s->previous_length + extension_length;
    if (extension_length <= 0 || new_length <= 0 ||
        new_length > FLS_MAX_PHRASE_LENGTH) {
        return false;
    }

    xx_rt_memcpy(s->phrases[candidate], s->phrases[s->previous_entry],
                 (size_t)s->previous_length);
    xx_rt_memcpy(s->phrases[candidate] + s->previous_length, current,
                 (size_t)extension_length);
    s->parents[candidate] = s->previous_entry;
    s->phrase_lengths[candidate] = (uint8_t)new_length;
    s->last_slot = candidate;

    current_code = s->entry_to_code[candidate];
    if (current_code >= FLS_CLASS_BASE[5]) {
        replacement_code = flslz_advance_scan(s, 1);
        if (!flslz_swap_codes(s, replacement_code, current_code)) return false;
    }
    return true;
}

static bool flslz_adapt_code(flslz_state *s, uint16_t entry,
                             int phrase_length) {
    uint16_t current_code;
    uint16_t replacement_code;
    int cls;
    int target_class;
    int score;
    int iterations = 0;

    if (entry >= FLS_ENTRY_COUNT || phrase_length <= 0 ||
        phrase_length > FLS_MAX_PHRASE_LENGTH) {
        return false;
    }
    current_code = s->entry_to_code[entry];
    if (current_code >= FLS_ENTRY_COUNT) return false;

    cls = (int)s->code_classes[current_code];
    if (cls < 0 || cls >= FLS_CLASS_COUNT) return false;
    if (cls == 1) {
        score = (int)s->code_scores[current_code] + 2;
        if (phrase_length == 1) score += 2;
        s->code_scores[current_code] = (uint8_t)((score < 20) ? score : 20);
        return true;
    }

    /* Class 0 promotes through the class-5 cursor; every other class uses its
     * own.  Deliberate: the cursor index is one above the class it scans. */
    target_class = (cls == 0) ? 5 : cls;
    if (target_class <= 0 || target_class >= FLS_CLASS_COUNT) return false;
    if (target_class < 5) {
        s->code_scores[current_code] = (uint8_t)(target_class * 2 - 2);
    }

    for (;;) {
        replacement_code = flslz_advance_scan(s, target_class);
        if (replacement_code >= FLS_ENTRY_COUNT) return false;
        if (s->code_scores[replacement_code] == 0) break;
        --s->code_scores[replacement_code];
        if (++iterations > FLS_CODE_COUNT * 21) return false;
    }

    return flslz_swap_codes(s, replacement_code, current_code);
}

static bool flslz_emit(flslz_state *s, const uint8_t *data, int size) {
    if (size <= 0 || (size_t)size > (s->output_limit - s->produced)) {
        return false;
    }
    if (s->store_output) {
        xx_rt_memcpy(s->output + s->produced, data, (size_t)size);
    }
    s->produced += (size_t)size;
    return true;
}

/* The one core routine.  `require_exact_input` mirrors the reference decoder's
 * container-level check that the member's packed extent is fully consumed; the
 * measuring entry point turns it off and reports the extent instead. */
static bool flslz_run(const uint8_t *input, size_t input_size, uint8_t *output,
                      size_t output_limit, bool store_output,
                      bool require_exact_input, size_t *consumed,
                      size_t *produced) {
    flslz_state *s;
    uint8_t tag = 0;
    uint16_t code;
    uint16_t entry;
    int length;
    bool end_seen = false;
    bool ok = true;

    if (consumed) *consumed = 0;
    if (produced) *produced = 0;

    if (input == NULL || input_size == 0) return false;
    if (store_output && output == NULL && output_limit != 0) return false;

    s = (flslz_state *)xx_mem_alloc(sizeof(flslz_state));
    if (s == NULL) return false;
    xx_rt_memset(s, 0, sizeof(flslz_state));

    s->input = input;
    s->input_size = input_size;
    s->output = output;
    s->output_limit = output_limit;
    s->store_output = store_output;
    flslz_initialize(s);

    if (!flslz_read_byte(s, &tag) || tag != FLS_STREAM_TAG) ok = false;

    while (ok) {
        ++s->token_counter;
        if (s->token_counter == 0x1f4) {
            /* A fresh class permutation every 500 tokens, the first one before
             * the very first code (the counter starts at 0x1f3). */
            if (!flslz_read_class_order(s)) {
                ok = false;
                break;
            }
            s->token_counter = 0;
        }

        code = 0;
        if (!flslz_read_code(s, &code)) {
            ok = false;
            break;
        }
        if (code == FLS_END_CODE) {
            end_seen = true;
            break;
        }
        if (code >= FLS_ENTRY_COUNT || s->produced >= s->output_limit) {
            ok = false;
            break;
        }

        entry = s->code_to_entry[code];
        if (entry >= FLS_ENTRY_COUNT) {
            ok = false;
            break;
        }
        length = (int)s->phrase_lengths[entry];
        if (length <= 0 || length > FLS_MAX_PHRASE_LENGTH ||
            (size_t)length > (s->output_limit - s->produced) ||
            !flslz_emit(s, s->phrases[entry], length)) {
            ok = false;
            break;
        }

        if (s->ages[entry] < 0x32) s->ages[entry] = (uint8_t)(s->ages[entry] + 5);
        if (s->have_previous && s->previous_length < FLS_MAX_PHRASE_LENGTH &&
            !flslz_insert_phrase(s, entry, s->phrases[entry], length)) {
            ok = false;
            break;
        }

        s->previous_entry = entry;
        s->previous_length = (uint16_t)length;
        s->have_previous = true;
        if (!flslz_adapt_code(s, entry, length)) {
            ok = false;
            break;
        }
    }

    /* The end code may leave up to seven padding bits in its final byte, but
     * no additional whole byte is valid inside a member's declared extent. */
    if (ok && end_seen && require_exact_input && s->input_pos != s->input_size) {
        ok = false;
    }
    ok = ok && end_seen;

    /* Reported on every path; the public entry points below zero them again
     * when the decode failed, so a caller never sees a partial count. */
    if (consumed) *consumed = s->input_pos;
    if (produced) *produced = s->produced;

    xx_mem_free(s);
    return ok;
}

XXFC_API bool xx_flslz_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written) {
    size_t produced = 0;
    bool ok;

    if (written) *written = 0;
    ok = flslz_run(input, input_size, output, output_size, true, true, NULL,
                   &produced);
    if (!ok) return false;
    if (written) *written = produced;
    return true;
}

XXFC_API bool xx_flslz_scan_memory(const uint8_t *input, size_t input_size,
                                   size_t max_output, size_t *consumed,
                                   size_t *produced) {
    bool ok;

    if (consumed) *consumed = 0;
    if (produced) *produced = 0;
    ok = flslz_run(input, input_size, NULL, max_output, false, false, consumed,
                   produced);
    if (!ok) {
        if (consumed) *consumed = 0;
        if (produced) *produced = 0;
    }
    return ok;
}
