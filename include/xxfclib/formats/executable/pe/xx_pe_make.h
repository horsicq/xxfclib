/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
 * Builder for small, valid PE32+ images (fixtures for PE readers/writers)
 * and the standard PE image checksum.
 */
#ifndef XX_PE_MAKE_H
#define XX_PE_MAKE_H

#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** RVA of the .rsrc section of a made image. Resource data entries hold
 * RVAs, so the resource section must be built for this address. */
#define XX_PE_MAKE_RESOURCE_RVA 0x2000U

typedef struct xx_pe_make_options {
    uint16_t machine; /**< IMAGE_FILE_MACHINE_*; 0 selects AMD64 */
    /** Resource section built for XX_PE_MAKE_RESOURCE_RVA; NULL for none. */
    const void *resources;
    size_t resources_size;
    /** Adds a .reloc section after .rsrc (the usual linker layout). */
    bool reloc_after_resources;
    /** Bytes appended after the last section. */
    const void *overlay;
    size_t overlay_size;
    /** WIN_CERTIFICATE data appended after the overlay (8-byte aligned) and
     * referenced by the security data directory. */
    const void *certificate;
    size_t certificate_size;
    /** Stores the image checksum (otherwise 0). */
    bool checksum;
    /** Ends SizeOfHeaders right at the section table (rounded to the file
     * alignment) instead of 0x400, leaving no room for another section
     * header in typical layouts. */
    bool compact_headers;
} xx_pe_make_options;

/** Appends a PE32+ console image: headers (0x400 bytes with room for
 * further section headers, unless compact_headers), a .text section with a
 * single ret, the optional .rsrc and .reloc sections, overlay and
 * certificate. */
XXFC_API bool xx_pe_make_image(xx_buf_t *out, const xx_pe_make_options *options);

/** Standard PE checksum (as computed by CheckSumMappedFile) of image, whose
 * 4-byte CheckSum field at checksum_offset is excluded from the sum. */
XXFC_API uint32_t xx_pe_checksum(const uint8_t *image, size_t size, size_t checksum_offset);

#ifdef __cplusplus
}
#endif

#endif
