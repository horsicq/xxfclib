/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_HXC_STREAM_H
#define XX_HXC_STREAM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hxc_stream {Abstractformat format;} xx_hxc_stream;
XXFC_API void xx_hxc_stream_init(xx_hxc_stream *,xx_io_device *,int64_t);
XXFC_API xx_hxc_stream *xx_hxc_stream_create(xx_io_device *,int64_t);
XXFC_API void xx_hxc_stream_destroy(xx_hxc_stream *);
XXFC_API void xx_hxc_stream_free(xx_hxc_stream *);
XXFC_API bool xx_hxc_stream_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_hxc_stream_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
