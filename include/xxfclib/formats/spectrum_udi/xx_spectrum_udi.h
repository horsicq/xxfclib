/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef XXFCLIB_FORMAT_SPECTRUM_UDI_H
#define XXFCLIB_FORMAT_SPECTRUM_UDI_H

#include "xxfclib/formats/xx_format.h"

/* ZX Spectrum UDI ("Ultra Disk Image", Alex Makeev) raw-track floppy image.
 * The sectors found on the raw tracks are exported either as one flat
 * "disk.img" (regular geometry) or as one "trackCC_H.bin" per track. */
typedef struct xx_spectrum_udi {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_sectors;
    uint32_t cylinders;
    uint32_t heads;
    int64_t archive_end;
    bool regular;   /* exported as a single flat disk.img */
    bool crc_valid; /* stored file CRC matches */
} xx_spectrum_udi;

XXFC_API void xx_spectrum_udi_init(xx_spectrum_udi *archive,
                                   xx_io_device *device,
                                   int64_t base_address);
XXFC_API xx_spectrum_udi *xx_spectrum_udi_create(xx_io_device *device,
                                                 int64_t base_address);
XXFC_API void xx_spectrum_udi_destroy(xx_spectrum_udi *archive);
XXFC_API void xx_spectrum_udi_free(xx_spectrum_udi *archive);
XXFC_API bool xx_spectrum_udi_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_spectrum_udi_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_spectrum_udi_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_spectrum_udi_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_spectrum_udi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_spectrum_udi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_spectrum_udi_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_spectrum_udi_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_spectrum_udi_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
