/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/parsec_pmm/xx_parsec_pmm.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define PMM_MDH_OFFSET 0x38U
#define PMM_SAMPLE_COUNT_OFFSET 0x40U
#define PMM_SAMPLE_OFFSETS 0x14U
#define PMM_SAMPLE_HEADER 10U

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    uint8_t header[PMM_SAMPLE_COUNT_OFFSET + 1U], sample_header[PMM_SAMPLE_HEADER];
    uint32_t offsets[8], instrument_offset, track_offset;
    uint32_t count, index;
    int64_t total = pm_available(format);
    if (total < (int64_t)sizeof(header) + 4 + PMM_SAMPLE_HEADER ||
        !pm_read(format, 0, header, sizeof(header)) ||
        xx_rt_memcmp(header, "MTCVTS PSM 2.00", 16U) != 0 ||
        xx_rt_memcmp(header + PMM_MDH_OFFSET, "MDH\0", 4U) != 0 ||
        xx_data_get_u32(header + 0x34U, 4, 0, false) != 0U) return false;
    count = header[PMM_SAMPLE_COUNT_OFFSET];
    if (count < 1U || count > 8U ||
        total < (int64_t)sizeof(header) + 4 + (int64_t)count * PMM_SAMPLE_HEADER)
        return false;
    instrument_offset = xx_data_get_u32(header + 0x10U, 4, 0, false);
    track_offset = PMM_MDH_OFFSET + xx_data_get_u16(header + 0x3eU, 2, 0, false);
    for (index = 0U; index < 8U; ++index) {
        offsets[index] = xx_data_get_u32(header + PMM_SAMPLE_OFFSETS + index * 4U, 4, 0, false);
        if (index >= count && offsets[index] != 0U) return false;
    }
    if (instrument_offset < sizeof(header) || track_offset < sizeof(header) ||
        track_offset >= instrument_offset ||
        (uint64_t)instrument_offset + 4U > (uint64_t)total ||
        (uint64_t)offsets[0] < (uint64_t)instrument_offset + 4U)
        return false;
    for (index = 0U; index < count; ++index) {
        uint32_t offset = offsets[index];
        int64_t next = index + 1U < count ? offsets[index + 1U] : total;
        uint16_t pcm_size;
        if ((pd && xx_pd_is_stopped(pd)) ||
            (uint64_t)offset + PMM_SAMPLE_HEADER > (uint64_t)total ||
            (index && offset <= offsets[index - 1U]) ||
            !pm_read(format, offset, sample_header, sizeof(sample_header)) ||
            xx_rt_memcmp(sample_header, "SM8\0\0\1", 6U) != 0)
            return false;
        pcm_size = xx_data_get_u16(sample_header + 6U, 2, 0, false);
        if (next - offset != (int64_t)PMM_SAMPLE_HEADER + pcm_size) return false;
    }
    if (!pm_read(format, instrument_offset, sample_header, 4U) ||
        xx_rt_memcmp(sample_header, "PLX\0", 4U) != 0)
        return false;
    if (!pm_add(format, stream, "metadata.mdh", PMM_MDH_OFFSET,
                (int64_t)instrument_offset - PMM_MDH_OFFSET) ||
        !pm_add(format, stream, "instruments.pma", instrument_offset,
                (int64_t)offsets[0] - instrument_offset)) return false;
    for (index = 0U; index < count; ++index) {
        int64_t next = index + 1U < count ? offsets[index + 1U] : total;
        char label[32];
        (void)xx_rt_snprintf(label, sizeof(label), "sample_%02u.sm8",
                             (unsigned)(index + 1U));
        if (!pm_add(format, stream, label, offsets[index],
                    next - offsets[index])) return false;
    }
    stream->size = total;
    return true;
}

void xx_parsec_pmm_init(xx_parsec_pmm *reader, xx_io_device *device,
                        int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address,
            XX_FILE_TYPE_PARSEC_PMM, "pmm");
    xx_format_set_mime_type(&reader->format, "audio/x-parsec-pmm");
}

xx_parsec_pmm *xx_parsec_pmm_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_parsec_pmm *reader = (xx_parsec_pmm *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_parsec_pmm_init(reader, device, base_address);
    return reader;
}

void xx_parsec_pmm_destroy(xx_parsec_pmm *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_parsec_pmm_free(xx_parsec_pmm *reader) {
    if (reader) {
        xx_parsec_pmm_destroy(reader);
        xx_mem_free(reader);
    }
}
