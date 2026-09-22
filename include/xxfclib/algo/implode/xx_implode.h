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
 * @file xx_implode.h
 * @brief Decoder for the legacy ZIP Implode compression method (method 6).
 */

#ifndef XX_IMPLODE_H
#define XX_IMPLODE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a raw ZIP Implode stream into an output device.
 *
 * The two booleans correspond to ZIP general-purpose flags 1 and 2: an 8 KiB
 * dictionary and a coded-literal tree, respectively. expected_size is required.
 */
XXFC_API bool xx_implode_unpack_device(xx_io_device *src_dev,
                                       int64_t src_offset,
                                       int64_t comp_size,
                                       xx_io_device *dst_dev,
                                       int64_t expected_size,
                                       bool use_8k_dictionary,
                                       bool use_literal_tree,
                                       xx_pd_struct *pd);

/** @brief Decode a raw ZIP Implode stream directly to a UTF-8 file path. */
XXFC_API bool xx_implode_unpack_device_to_file(xx_io_device *src_dev,
                                               int64_t src_offset,
                                               int64_t comp_size,
                                               const char *dst_file_path,
                                               int64_t expected_size,
                                               bool use_8k_dictionary,
                                               bool use_literal_tree,
                                               xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_IMPLODE_H */
