/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/qt/qtbase/5.15/src/gui/image/qpicture.cpp
 * Qt QPicture version9 bounded painter command stream with original CRC16, counted begin/end framing, typed integer/double point/polygon/drawing/save/restore records and
 * bounded geometry. Only explicitly validated drawing opcodes; fonts, images, paths and replay unsupported. Limits64MiB input,4096 components; encoded assets are never
 * executed.
 */
#ifndef XX_QT_QPICTURE_H
#define XX_QT_QPICTURE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_qt_qpicture {
    Abstractformat format;
} xx_qt_qpicture;
XXFC_API void xx_qt_qpicture_init(xx_qt_qpicture *, xx_io_device *, int64_t);
XXFC_API xx_qt_qpicture *xx_qt_qpicture_create(xx_io_device *, int64_t);
XXFC_API void xx_qt_qpicture_destroy(xx_qt_qpicture *);
XXFC_API void xx_qt_qpicture_free(xx_qt_qpicture *);
XXFC_API bool xx_qt_qpicture_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_qt_qpicture_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_qt_qpicture_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_qt_qpicture_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_qt_qpicture_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_qt_qpicture_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_qt_qpicture_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
