/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/ipl.c
 * Scanalytics IPLab100f image stacks: complete endian marker/version/data/fini framing, counted channel/Z/T planes and checked integer/finite floating samples. Original descriptor and typed planes exported; optional following tags/unknown sample types declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/scanalytics_iplab/xx_scanalytics_iplab.h"
#include "../common/xx_component_binary.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[4];
    return n >= 53 && pm_read(f, 0, b, 4) && (component_tag(b, "iiii", 4) || component_tag(b, "mmmm", 4));
}
static uint32_t image_document_ipl_u(const uint8_t *p, bool le) {
    return le ? xx_data_get_u32(p, 4, 0, false) : xx_data_get_u32(p, 4, 0, true);
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    bool le;
    uint32_t w, h, channels, z, time, type, size, bytes, planes, i;
    uint64_t plane, p;
    char label[48];
    if (n < 53 || (!component_tag(b, "iiii", 4) && !component_tag(b, "mmmm", 4)))
        return false;
    le = b[0] == 'i';
    if (image_document_ipl_u(b + 4, le) != 4 || !component_tag(b + 8, "100fdata", 8))
        return false;
    size = image_document_ipl_u(b + 16, le);
    w = image_document_ipl_u(b + 20, le);
    h = image_document_ipl_u(b + 24, le);
    channels = image_document_ipl_u(b + 28, le);
    z = image_document_ipl_u(b + 32, le);
    time = image_document_ipl_u(b + 36, le);
    type = image_document_ipl_u(b + 40, le);
    bytes = type == 0 || type == 5                ? 1
            : type == 1 || type == 2 || type == 6 ? 2
            : type == 3 || type == 4              ? 4
            : type == 10                          ? 8
                                                  : 0;
    if (!bytes || !w || !h || w > 8192 || h > 8192 || (uint64_t)w * h > 8388608 || (channels != 1 && channels != 3) ||
        !z || !time || z > 4094 || time > 4094 || (uint64_t)channels * z * time > 4094)
        return false;
    planes = channels * z * time;
    plane = (uint64_t)w * h * bytes;
    if (plane > 33554432 || size != 28 + plane * planes || n != 24 + (uint64_t)size ||
        !component_tag(b + n - 8, "fini", 4) || image_document_ipl_u(b + n - 4, le) != 0 ||
        !component_emit(f, s, "descriptor.ipl", 0, 44, n))
        return false;
    if (type == 4 || type == 10)
        for (p = 44; p < n - 8; p += bytes) {
            uint32_t u = image_document_ipl_u(b + p + (type == 10 && le ? 4 : 0), le);
            if ((((p - 44) & 4095) == 0 && xx_component_parser_stopped(pd)) ||
                (type == 4 ? !component_is_finite32(u) : (u & 0x7ff00000U) == 0x7ff00000U))
                return false;
        }
    for (i = 0; i < planes; ++i) {
        if (xx_component_parser_stopped(pd))
            return false;
        xx_rt_snprintf(label, sizeof(label), "sample-plane-%u.bin", i);
        if (!component_emit(f, s, label, 44 + (uint64_t)i * plane, plane, n))
            return false;
    }
    return component_emit(f, s, "terminator.ipl", n - 8, 8, n);
}

void xx_scanalytics_iplab_init(xx_scanalytics_iplab *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_SCANALYTICS_IPLAB, "ipl");
    }
}
xx_scanalytics_iplab *xx_scanalytics_iplab_create(xx_io_device *d, int64_t at) {
    xx_scanalytics_iplab *r = (xx_scanalytics_iplab *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_scanalytics_iplab_init(r, d, at);
    return r;
}
void xx_scanalytics_iplab_destroy(xx_scanalytics_iplab *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_scanalytics_iplab_free(xx_scanalytics_iplab *r) {
    if (r) {
        xx_scanalytics_iplab_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_scanalytics_iplab_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_scanalytics_iplab_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
