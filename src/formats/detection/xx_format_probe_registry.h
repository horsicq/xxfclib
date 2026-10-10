/* SPDX-License-Identifier: MIT
 * Private ordered reader probes, included after the native reader headers.
 */
#ifndef XX_FORMAT_PROBE_REGISTRY_H
#define XX_FORMAT_PROBE_REGISTRY_H

typedef struct xx_format_probe_context {
    xx_io_device *device;
    int64_t total_size;
    int64_t original_position;
    bool is_mz;
    const uint8_t *magic;
    size_t magic_size;
} xx_format_probe_context;

/* Each reader has a separate stack frame. A broad detector must not reserve
 * the combined storage of hundreds of readers on the Windows thread stack.
 * Primary and fallback manifests own the unique readers; carrier probes reuse
 * those same functions while retaining their own preconditions and priority. */
#define XX_FORMAT_PROBE_READ(capacity)
#define XX_FORMAT_PROBE_READ_EXACT(capacity)
#define XX_FORMAT_PROBE_MAGIC()
#define XX_FORMAT_READER_PROBE(name, file_type, predicate)                                    \
    static XX_FORMAT_NOINLINE bool xx_format_validate_registered_##name(xx_io_device *device) \
    {                                                                                         \
        xx_##name reader;                                                                     \
        bool valid;                                                                           \
        xx_##name##_init(&reader, device, 0);                                                 \
        valid = xx_##name##_check_is_valid(&reader.format, NULL);                             \
        xx_##name##_destroy(&reader);                                                         \
        return valid;                                                                         \
    }
#include "xx_format_probe_primary.inc"
#include "xx_format_probe_fallback.inc"
#undef XX_FORMAT_READER_PROBE
#undef XX_FORMAT_PROBE_MAGIC
#undef XX_FORMAT_PROBE_READ_EXACT
#undef XX_FORMAT_PROBE_READ

/* Preserve the original header boundaries and read policy. Most predicates
 * require one complete read; the dedicated exact-read boundary accepts short
 * positive reads until its bounded header has been filled. Reset the
 * buffer at every boundary, including after earlier failed/partial reads. */
static size_t xx_format_probe_load_header(const xx_format_probe_context *context, uint8_t *buffer, size_t capacity, bool read_exact)
{
    size_t size = context->total_size < (int64_t)capacity ? (size_t)context->total_size : capacity;
    xx_mem_zero(buffer, capacity);
    if (xx_io_seek64(context->device, 0, SEEK_SET) != 0) {
        size = 0U;
    } else if (read_exact) {
        if (!xx_format_read_probe_exact(context->device, buffer, size)) size = 0U;
    } else if (xx_io_read(context->device, buffer, size) != (ssize_t)size) {
        size = 0U;
    }
    (void)xx_io_seek64(context->device, context->original_position, SEEK_SET);
    return size;
}

/* These aliases are local to each pass. Predicates retain their original
 * expressions and short-circuit evaluation, rather than a second translated
 * signature language. The single bounded buffer is reused at each boundary. */
#define XX_FORMAT_PROBE_PASS_LOCALS(context)         \
    xx_io_device *dev = (context)->device;           \
    int64_t total_size = (context)->total_size;      \
    int64_t orig_pos = (context)->original_position; \
    bool is_mz = (context)->is_mz;                   \
    const uint8_t *magic = (context)->magic;         \
    size_t magic_size = (context)->magic_size;       \
    uint8_t header_buffer[4100];                     \
    const uint8_t *header = header_buffer;           \
    size_t header_size = 0U;                         \
    (void)total_size;                                \
    (void)is_mz;                                     \
    (void)magic;                                     \
    (void)magic_size
#define XX_FORMAT_PROBE_READ(capacity) \
    header = header_buffer;            \
    header_size = xx_format_probe_load_header(context, header_buffer, capacity, false);
#define XX_FORMAT_PROBE_READ_EXACT(capacity) \
    header = header_buffer;                  \
    header_size = xx_format_probe_load_header(context, header_buffer, capacity, true);
#define XX_FORMAT_PROBE_MAGIC() \
    header = magic;             \
    header_size = magic_size;
#define XX_FORMAT_READER_PROBE(name, file_type, predicate)      \
    if (predicate) {                                            \
        bool valid = xx_format_validate_registered_##name(dev); \
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);            \
        if (valid) return file_type;                            \
    }

static XX_FORMAT_NOINLINE xx_file_type_t xx_format_probe_registered_primary(const xx_format_probe_context *context)
{
    XX_FORMAT_PROBE_PASS_LOCALS(context);
#include "xx_format_probe_primary.inc"
    return XX_FILE_TYPE_UNKNOWN;
}

static XX_FORMAT_NOINLINE xx_file_type_t xx_format_probe_registered_carriers(const xx_format_probe_context *context)
{
    XX_FORMAT_PROBE_PASS_LOCALS(context);
#include "xx_format_probe_carriers.inc"
    return XX_FILE_TYPE_UNKNOWN;
}

static XX_FORMAT_NOINLINE xx_file_type_t xx_format_probe_registered_fallback(const xx_format_probe_context *context)
{
    XX_FORMAT_PROBE_PASS_LOCALS(context);
#include "xx_format_probe_fallback.inc"
    return XX_FILE_TYPE_UNKNOWN;
}

#undef XX_FORMAT_READER_PROBE
#undef XX_FORMAT_PROBE_MAGIC
#undef XX_FORMAT_PROBE_READ_EXACT
#undef XX_FORMAT_PROBE_READ
#undef XX_FORMAT_PROBE_PASS_LOCALS

#endif
