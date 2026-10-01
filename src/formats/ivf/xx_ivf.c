/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent IVF reader, based on the on-disk layout in
 * https://github.com/webmproject/libvpx/blob/main/ivfenc.c and ivfdec.c.
 * Lists and extracts stored coded video packets; it does not decode video.
 */
#include "xxfclib/formats/ivf/xx_ivf.h"
#include "../xx_payload_members.h"

#define IVF_HEADER_SIZE 32U
#define IVF_FRAME_HEADER_SIZE 12U
#define IVF_MAX_PACKET_SIZE (256U * 1024U * 1024U)
#define IVF_MAX_FRAMES 1000000U

static const char *ivf_codec_extension(const uint8_t *fourcc) {
    if (xx_rt_memcmp(fourcc, "VP80", 4U) == 0) return "vp8";
    if (xx_rt_memcmp(fourcc, "VP90", 4U) == 0) return "vp9";
    if (xx_rt_memcmp(fourcc, "AV01", 4U) == 0) return "av1";
    return "bin";
}

static bool pm_parse(Abstractformat *self, pm_stream *stream,
                     xx_pd_struct *pd) {
    uint8_t header[IVF_HEADER_SIZE], frame[IVF_FRAME_HEADER_SIZE];
    int64_t available = pm_available(self), at;
    const char *extension;
    uint32_t count = 0U;
    if (available < (int64_t)IVF_HEADER_SIZE ||
        !pm_read(self, 0, header, sizeof(header)) ||
        xx_rt_memcmp(header, "DKIF", 4U) != 0 ||
        pm_le16(header + 4U) != 0U ||
        pm_le16(header + 6U) != IVF_HEADER_SIZE ||
        pm_le16(header + 12U) == 0U ||
        pm_le16(header + 14U) == 0U ||
        pm_le32(header + 16U) == 0U ||
        pm_le32(header + 20U) == 0U)
        return false;
    extension = ivf_codec_extension(header + 8U);
    at = IVF_HEADER_SIZE;
    while (at < available) {
        uint32_t size;
        uint64_t pts;
        char label[80];
        if ((pd && xx_pd_is_stopped(pd)) ||
            count >= IVF_MAX_FRAMES ||
            available - at < (int64_t)IVF_FRAME_HEADER_SIZE ||
            !pm_read(self, at, frame, sizeof(frame)))
            return false;
        size = pm_le32(frame);
        pts = (uint64_t)pm_le32(frame + 4U) |
              ((uint64_t)pm_le32(frame + 8U) << 32);
        at += IVF_FRAME_HEADER_SIZE;
        if (size > IVF_MAX_PACKET_SIZE || (int64_t)size > available - at)
            return false;
        (void)xx_rt_snprintf(label, sizeof(label),
                             "frame-%06u-pts-%llu.%s", count,
                             (unsigned long long)pts, extension);
        if (!pm_add(self, stream, label, at, size)) return false;
        at += (int64_t)size;
        ++count;
    }
    /* Some producers write an unknown or stale frame count, so the complete
     * frame chain, rather than header[24..27], determines actual membership. */
    stream->size = at;
    return true;
}

void xx_ivf_init(xx_ivf *reader, xx_io_device *device, int64_t base) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base, XX_FILE_TYPE_IVF, "ivf");
    xx_format_set_mime_type(&reader->format, "video/x-ivf");
}

xx_ivf *xx_ivf_create(xx_io_device *device, int64_t base) {
    xx_ivf *reader = (xx_ivf *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_ivf_init(reader, device, base);
    return reader;
}

void xx_ivf_destroy(xx_ivf *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_ivf_free(xx_ivf *reader) {
    if (!reader) return;
    xx_ivf_destroy(reader);
    xx_mem_free(reader);
}

bool xx_ivf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    return pm_valid(self, pd);
}

bool xx_ivf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    return pm_handle(self, pd);
}
