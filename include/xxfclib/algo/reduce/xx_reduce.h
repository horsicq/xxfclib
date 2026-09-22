/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_reduce.h
 * @brief Decoder for the legacy ZIP Reduce methods (methods 2 through 5).
 */

#ifndef XX_REDUCE_H
#define XX_REDUCE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a raw ZIP Reduce stream into an output device.
 *
 * factor is the Reduce factor 1 through 4 (ZIP methods 2 through 5 use
 * method - 1). expected_size is mandatory because Reduce has no end marker.
 */
XXFC_API bool xx_reduce_unpack_device(xx_io_device *src_dev,
                                      int64_t src_offset,
                                      int64_t comp_size,
                                      xx_io_device *dst_dev,
                                      int64_t expected_size,
                                      int factor,
                                      xx_pd_struct *pd);

/** @brief Decode a raw ZIP Reduce stream directly to a UTF-8 file path. */
XXFC_API bool xx_reduce_unpack_device_to_file(xx_io_device *src_dev,
                                              int64_t src_offset,
                                              int64_t comp_size,
                                              const char *dst_file_path,
                                              int64_t expected_size,
                                              int factor,
                                              xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_REDUCE_H */
