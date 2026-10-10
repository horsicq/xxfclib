/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.tensorflow.org/tutorials/load_data/tfrecord */
#include "xxfclib/formats/tensorflow_tfrecord/xx_tensorflow_tfrecord.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../common/xx_scientific_numbers.h"
#include "xxfclib/global/xx_global.h"

static uint32_t tf_crc(uint32_t crc, const uint8_t *p, size_t n)
{
    return ~xx_crc32c_calc(~crc, p, n);
}
static uint32_t tf_mask(uint32_t c)
{
    c = ~c;
    return ((c >> 15) | (c << 17)) + 0xa282ead8U;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[12], *buffer, tail[4];
    uint64_t at = 0, end;
    int64_t available = pm_available(f);
    unsigned count = 0;
    size_t capacity = xx_get_file_buffer_size();
    bool ok = false;
    if (available < 16 || available > 64 * 1024 * 1024 || binary_stop(pd)) {
        return false;
    }
    end = (uint64_t)available;
    if (capacity > (SIZE_MAX >> 1)) {
        capacity = SIZE_MAX >> 1;
    }
    if (end < capacity) capacity = (size_t)end;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (at < end) {
        uint64_t n, p, left;
        uint32_t crc = UINT32_MAX;
        char label[64];
        if (binary_stop(pd) || ++count > 4096 || !binary_range(at, 12, end) || !pm_read(f, (int64_t)at, h, 12) ||
            xx_data_get_u32(h + 8, 4, 0, false) != tf_mask(tf_crc(UINT32_MAX, h, 8)))
            goto done;
        n = xx_data_get_u64(h, 8, 0, false);
        p = at + 12;
        if (!binary_range(p, n, end) || !binary_range(p + n, 4, end)) goto done;
        left = n;
        while (left) {
            size_t z = left > capacity ? capacity : (size_t)left;
            if (binary_stop(pd) || !pm_read(f, (int64_t)p, buffer, z)) goto done;
            crc = tf_crc(crc, buffer, z);
            left -= z;
            p += z;
        }
        if (!pm_read(f, (int64_t)p, tail, 4) || xx_data_get_u32(tail, 4, 0, false) != tf_mask(crc)) goto done;
        xx_rt_snprintf(label, sizeof(label), "record-%u.bin", count - 1);
        if (!pm_add(f, s, label, (int64_t)(at + 12), (int64_t)n)) goto done;
        at = p + 4;
    }
    s->size = available;
    ok = count != 0;
done:
    xx_mem_free(buffer);
    return ok;
}

void xx_tensorflow_tfrecord_init(xx_tensorflow_tfrecord *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TENSORFLOW_TFRECORD, "tensorflow_tfrecord");
    }
}
xx_tensorflow_tfrecord *xx_tensorflow_tfrecord_create(xx_io_device *d, int64_t b)
{
    xx_tensorflow_tfrecord *r = (xx_tensorflow_tfrecord *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tensorflow_tfrecord_init(r, d, b);
    return r;
}
void xx_tensorflow_tfrecord_destroy(xx_tensorflow_tfrecord *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tensorflow_tfrecord_free(xx_tensorflow_tfrecord *r)
{
    if (r) {
        xx_tensorflow_tfrecord_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tensorflow_tfrecord_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tensorflow_tfrecord_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
