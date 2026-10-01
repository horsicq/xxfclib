/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_SSH_TRANSPORT_H
#define XX_SSH_TRANSPORT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ssh_transport {Abstractformat format;} xx_ssh_transport;
XXFC_API void xx_ssh_transport_init(xx_ssh_transport *,xx_io_device *,int64_t);
XXFC_API xx_ssh_transport *xx_ssh_transport_create(xx_io_device *,int64_t);
XXFC_API void xx_ssh_transport_destroy(xx_ssh_transport *);
XXFC_API void xx_ssh_transport_free(xx_ssh_transport *);
XXFC_API bool xx_ssh_transport_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ssh_transport_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
