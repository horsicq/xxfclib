/* SPDX-License-Identifier: MIT */
#ifndef XX_UE2_DOCUMENTS_H
#define XX_UE2_DOCUMENTS_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_ue2_documents { Abstractformat format; } xx_ue2_documents;
XXFC_API xx_ue2_documents *xx_ue2_documents_create(xx_io_device *, int64_t, xx_file_type_t);
XXFC_API void xx_ue2_documents_free(xx_ue2_documents *);
XXFC_API xx_file_type_t xx_ue2_documents_detect_device(xx_io_device *);
#endif
