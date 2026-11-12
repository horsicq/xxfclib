/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * MIME_MESSAGE: the family header of the mime_message type, served by the shared reader
 * declared in document/ue2_documents/xx_ue2_documents.h.
 */
#ifndef XX_MIME_MESSAGE_FAMILY_H
#define XX_MIME_MESSAGE_FAMILY_H
#include "xxfclib/formats/document/ue2_documents/xx_ue2_documents.h"
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mime_message_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mime_message_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mime_message_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mime_message_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mime_message_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XX_MIME_MESSAGE_FAMILY_H */
