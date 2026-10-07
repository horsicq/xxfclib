/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Parsec RIB reverse-stream decoder. The grammar follows the stream's
 * backward cursors; malformed references and output sizes are rejected.
 */
#include "xxfclib/formats/parsec_rib/xx_parsec_rib.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define RIB_MAX_OUTPUT (512U * 1024U * 1024U)
#define RIB_CANCEL_MASK 0x3fffU

typedef struct rib_decode_state {
    uint8_t *bytes;
    int64_t packed_size, output_size;
    int64_t input, output;
    uint32_t written;
    xx_pd_struct *pd;
} rib_decode_state;

static bool rib_get(rib_decode_state *state, uint8_t *value) {
    if (!state || !value || state->input < 0 ||
        state->input >= state->packed_size) return false;
    *value = state->bytes[state->input--];
    return true;
}

static bool rib_put(rib_decode_state *state, uint8_t value) {
    if (!state || state->output < 0 ||
        state->output >= state->output_size ||
        ((state->written++ & RIB_CANCEL_MASK) == 0U &&
         state->pd && xx_pd_is_stopped(state->pd))) return false;
    state->bytes[state->output--] = value;
    return true;
}

static bool rib_repeat(rib_decode_state *state, uint8_t value,
                       int64_t count) {
    if (count <= 0 || count > state->output + 1) return false;
    while (count-- > 0) if (!rib_put(state, value)) return false;
    return true;
}

static bool rib_literal(rib_decode_state *state, int64_t count) {
    uint8_t value;
    if (count <= 0 || count > state->output + 1 ||
        count > state->input + 1) return false;
    while (count-- > 0) {
        if (!rib_get(state, &value) || !rib_put(state, value)) return false;
    }
    return true;
}

static bool rib_reference(rib_decode_state *state, int64_t distance,
                          int64_t count) {
    int64_t source;
    if (distance <= 0 || count <= 0 || count > state->output + 1 ||
        distance > (state->output_size - 1) - state->output) return false;
    source = state->output + distance;
    while (count-- > 0) {
        if (source <= state->output || source < 0 ||
            source >= state->output_size ||
            !rib_put(state, state->bytes[source])) return false;
        --source;
    }
    return true;
}

static bool rib_decode(uint8_t *bytes, int64_t packed_size,
                       int64_t output_size, xx_pd_struct *pd) {
    rib_decode_state state;
    if (!bytes || packed_size < 0 || output_size < packed_size ||
        output_size > RIB_MAX_OUTPUT) return false;
    state.bytes = bytes;
    state.packed_size = packed_size;
    state.output_size = output_size;
    state.input = packed_size - 1;
    state.output = output_size - 1;
    state.written = 0U;
    state.pd = pd;
    while (state.input < state.output) {
        uint8_t token, high, low, a, b, c, d;
        bool ok = false;
        if ((pd && xx_pd_is_stopped(pd)) || !rib_get(&state, &token))
            return false;
        high = token >> 4U;
        low = token & 15U;
        if (high == 0U) {
            ok = rib_get(&state, &a) && rib_repeat(&state, a, (int64_t)low + 4);
        } else if (high == 1U) {
            ok = rib_get(&state, &a) && rib_get(&state, &b) &&
                 rib_repeat(&state, b, ((int64_t)low << 8U) + a + 20);
        } else if (high == 2U) {
            ok = rib_literal(&state, (int64_t)low + 1);
        } else if (high == 3U) {
            ok = rib_get(&state, &a) &&
                 rib_literal(&state, ((int64_t)low << 8U) + a + 17);
        } else if (token == 0x40U) {
            ok = rib_get(&state, &a) && rib_get(&state, &b) &&
                 rib_literal(&state, ((int64_t)a << 8U) | b);
        } else if (token == 0x41U) {
            ok = rib_get(&state, &a) && rib_get(&state, &b) &&
                 rib_get(&state, &c) && rib_get(&state, &d) &&
                 rib_reference(&state, ((int64_t)a << 8U) | b,
                               ((int64_t)c << 8U) | d);
        } else if (token == 0x42U) {
            ok = rib_get(&state, &a) && rib_get(&state, &b) &&
                 rib_get(&state, &c) &&
                 rib_reference(&state, ((int64_t)a << 8U) | b,
                               (int64_t)c + 17);
        } else if (high == 4U) {
            ok = rib_get(&state, &a) && rib_get(&state, &b) &&
                 rib_reference(&state, ((int64_t)a << 8U) | b,
                               (int64_t)low + 1);
        } else if (high == 5U) {
            ok = rib_get(&state, &a) && rib_get(&state, &b) &&
                 rib_reference(&state, ((int64_t)low << 8U) + a + 2,
                               (int64_t)b + 14);
        } else {
            ok = rib_get(&state, &a) &&
                 rib_reference(&state, ((int64_t)low << 8U) + a + 2,
                               (int64_t)high - 2);
        }
        if (!ok || state.input > state.output) return false;
    }
    return state.input == state.output && !(pd && xx_pd_is_stopped(pd));
}

static const char *rib_payload_extension(const uint8_t *prefix, size_t size) {
    if (size >= 16U && xx_rt_memcmp(prefix, "MTCVTS PSM 2.00", 16U) == 0)
        return "pmm";
    if (size >= 4U && xx_rt_memcmp(prefix, "SM8\0", 4U) == 0) return "sm8";
    if (size >= 4U && xx_rt_memcmp(prefix, "PLX\0", 4U) == 0) return "plx";
    if (size >= 3U && xx_rt_memcmp(prefix, "DTC", 3U) == 0) return "dtc";
    if (size >= 3U && xx_rt_memcmp(prefix, "DMA", 3U) == 0) return "dma";
    if (size >= 3U && xx_rt_memcmp(prefix, "MUS", 3U) == 0) return "mus";
    if (size >= 3U && xx_rt_memcmp(prefix, "SND", 3U) == 0) return "snd";
    return "dat";
}

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    uint8_t header[8], prefix[32];
    uint32_t output_size;
    int64_t total = pm_available(format), packed_size;
    size_t prefix_size;
    char label[32];
    if (total < 8 || !pm_read(format, 0, header, sizeof(header)) ||
        xx_rt_memcmp(header, "RIB\0", 4U) != 0) return false;
    packed_size = total - 8;
    output_size = xx_data_get_u32(header + 4U, 4, 0, false);
    if ((uint64_t)output_size > RIB_MAX_OUTPUT ||
        packed_size > (int64_t)output_size ||
        ((packed_size == 0) != (output_size == 0U)) ||
        (pd && xx_pd_is_stopped(pd))) return false;
    prefix_size = packed_size < (int64_t)sizeof(prefix)
        ? (size_t)packed_size : sizeof(prefix);
    if (prefix_size && !pm_read(format, 8, prefix, prefix_size)) return false;
    (void)xx_rt_snprintf(label, sizeof(label), "payload.%s",
                         rib_payload_extension(prefix, prefix_size));
    if (!pm_add(format, stream, label, 8, packed_size)) return false;
    stream->items[0].size = output_size;
    stream->size = total;
    return true;
}

static bool rib_unpack(Abstractformat *format, xx_archive_record_state *state,
                       xx_pd_struct *pd) {
    pm_stream *stream;
    pm_member *member;
    const xx_var *limit;
    uint8_t *decoded;
    bool result;
    if (!format || !state || state->format != format || !state->has_record ||
        !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream = (pm_stream *)state->internal_state;
    if (stream->count != 1U || stream->index != 0U) return false;
    member = &stream->items[0];
    limit = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && (uint64_t)member->size > xx_var_get_u64(limit)) return false;
    limit = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit && (uint64_t)member->size > xx_var_get_u64(limit)) return false;
    decoded = (uint8_t *)xx_mem_alloc(member->size ? (size_t)member->size : 1U);
    if (!decoded) return false;
    if (member->packed_size > 0 &&
        !pm_read(format, 8, decoded, (size_t)member->packed_size)) {
        xx_mem_free(decoded);
        return false;
    }
    result = rib_decode(decoded, member->packed_size, member->size, pd);
    if (result) {
        member->memory = decoded;
        result = pm_unpack(format, state, pd);
        member->memory = NULL;
    }
    xx_mem_free(decoded);
    return result;
}

void xx_parsec_rib_init(xx_parsec_rib *reader, xx_io_device *device,
                        int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address,
            XX_FILE_TYPE_PARSEC_RIB, "rib");
    xx_format_set_mime_type(&reader->format,
                            "application/x-parsec-rib");
    reader->format.unpack_current_archive_record = rib_unpack;
}

xx_parsec_rib *xx_parsec_rib_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_parsec_rib *reader = (xx_parsec_rib *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_parsec_rib_init(reader, device, base_address);
    return reader;
}

void xx_parsec_rib_destroy(xx_parsec_rib *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_parsec_rib_free(xx_parsec_rib *reader) {
    if (reader) {
        xx_parsec_rib_destroy(reader);
        xx_mem_free(reader);
    }
}
