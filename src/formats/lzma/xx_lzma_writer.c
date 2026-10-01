/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/lzma/xx_lzma.h"
#include "xxfclib/algo/lzma/xx_lzma.h"

bool xx_lzma_pack_to_device(xx_io_device *source, int64_t source_offset,
                            int64_t uncompressed_size,
                            xx_io_device *destination, int level,
                            xx_pd_struct *pd)
{
    uint8_t header[XX_LZMA_PROPS_SIZE + 8];
    uint8_t actual_properties[XX_LZMA_PROPS_SIZE];
    size_t properties_size = XX_LZMA_PROPS_SIZE;
    size_t done = 0;
    unsigned index;
    int64_t source_size;

    if (!source || !destination || source == destination ||
        source_offset < 0 || uncompressed_size < 0 ||
        (pd && xx_pd_is_stopped(pd)) ||
        !xx_lzma_get_properties(uncompressed_size, level,
                                 header, &properties_size)) return false;

    source_size = xx_io_total_size(source);
    if (source_size < source_offset ||
        uncompressed_size > source_size - source_offset ||
        xx_io_seek64(source, source_offset, SEEK_SET) != 0) return false;
    for (index = 0; index < 8; ++index) {
        /* Use the end marker for completion, including with older liblzma
         * decoders that reject a known-size header followed by a marker. */
        header[XX_LZMA_PROPS_SIZE + index] = 0xFF;
    }
    while (done < sizeof(header)) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(destination, header + done, sizeof(header) - done);
        if (amount <= 0 || (size_t)amount > sizeof(header) - done) return false;
        done += (size_t)amount;
    }
    properties_size = sizeof(actual_properties);
    return xx_lzma_pack_device(source, source_offset, uncompressed_size,
                                destination, level, actual_properties,
                                &properties_size, pd);
}
