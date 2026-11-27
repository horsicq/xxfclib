/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_7ZIP_STREAM_H
#define XX_7ZIP_STREAM_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

/* Private coder adapters. Devices are borrowed and remain open; output is
 * sequential and must be staged by the caller until size/CRC validation.
 * Folder-sized allocations are confined to codecs with memory-only APIs,
 * where address space and allocation failure remain the limiting factors. */
bool xx_7zip_stream_decode(uint64_t method, const uint8_t *properties, size_t properties_size, xx_io_device *source, int64_t source_offset, uint64_t compressed_size,
                           uint64_t expected_size, xx_io_device *destination, const uint8_t *password_utf16le, size_t password_size, xx_pd_struct *pd);
/* Each independent input device/view starts at offset zero. */
bool xx_7zip_stream_bcj2(xx_io_device *const sources[4], const uint64_t sizes[4], const uint8_t *properties, size_t properties_size, uint64_t expected_size,
                         xx_io_device *destination, xx_pd_struct *pd);
bool xx_7zip_stream_bcj2_streaming(xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3], const uint8_t *properties, size_t properties_size,
                                   uint64_t in0_size, uint64_t expected_size, xx_io_device *u_source, int64_t u_source_offset, uint64_t u_compressed_size,
                                   uint64_t u_method, const uint8_t *u_properties, size_t u_properties_size, xx_io_device *destination, const uint8_t *password_utf16le,
                                   size_t password_size, xx_pd_struct *pd);
bool xx_7zip_stream_bcj2_streaming_spans(xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3], const uint8_t *const secondary_ptrs[3],
                                         const uint8_t *properties, size_t properties_size, uint64_t in0_size, uint64_t expected_size, xx_io_device *u_source,
                                         int64_t u_source_offset, uint64_t u_compressed_size, uint64_t u_method, const uint8_t *u_properties, size_t u_properties_size,
                                         xx_io_device *destination, const uint8_t *password_utf16le, size_t password_size, xx_pd_struct *pd);
/* A length-preserving filter (Copy, Delta, Swap, branch converters) whose
 * input is decoded by `u_method` straight into it, with no intermediate. */
bool xx_7zip_stream_filter_streaming(uint64_t method, const uint8_t *properties, size_t properties_size, uint64_t expected_size, xx_io_device *u_source,
                                     int64_t u_source_offset, uint64_t u_compressed_size, uint64_t u_method, const uint8_t *u_properties, size_t u_properties_size,
                                     xx_io_device *destination, const uint8_t *password_utf16le, size_t password_size, xx_pd_struct *pd);
/* BCJ2 whose main stream (`method` over `source`) is decoded on a worker
 * thread while the caller decodes the other streams. The caller must not
 * use `source` or its parent device until xx_7zip_pipe_bcj2_finish or
 * xx_7zip_pipe_abort returns; both end the worker and free the pipe. */
typedef struct xx_7zip_pipe_s xx_7zip_pipe;
xx_7zip_pipe *xx_7zip_pipe_start(uint64_t method, const uint8_t *properties, size_t properties_size, xx_io_device *source, uint64_t compressed_size,
                                 uint64_t expected_size, const uint8_t *password_utf16le, size_t password_size);
bool xx_7zip_pipe_bcj2_finish(xx_7zip_pipe *pipe, xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3], const uint8_t *properties,
                              size_t properties_size, uint64_t expected_size, xx_io_device *destination, xx_pd_struct *pd);
bool xx_7zip_pipe_bcj2_finish_spans(xx_7zip_pipe *pipe, xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3],
                                    const uint8_t *const secondary_ptrs[3], const uint8_t *properties, size_t properties_size, uint64_t expected_size,
                                    xx_io_device *destination, xx_pd_struct *pd);
void xx_7zip_pipe_abort(xx_7zip_pipe *pipe);
/* A coder's output decoded into memory by a worker thread: the device reads
 * (and seeks) it while it is written, waiting for bytes not there yet. The
 * caller must not use `source` or its parent device until the worker ends:
 * xx_7zip_async_finish (true to stop it) or closing the device. finish gives
 * the worker's verdict: false when it failed or was stopped. */
xx_io_device *xx_7zip_async_open(uint64_t method, const uint8_t *properties, size_t properties_size, xx_io_device *source, uint64_t compressed_size,
                                 uint64_t expected_size, const uint8_t *password_utf16le, size_t password_size, xx_pd_struct *pd);
bool xx_7zip_async_is(const xx_io_device *device);
bool xx_7zip_async_finish(xx_io_device *device, bool stop);
/* Whether xx_7zip_stream_filter_streaming takes `method`. */
bool xx_7zip_stream_filter_method(uint64_t method);

#endif /* XX_7ZIP_STREAM_H */
