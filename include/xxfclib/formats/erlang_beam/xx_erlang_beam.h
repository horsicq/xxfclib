/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/erlang/otp/master/lib/stdlib/src/beam_lib.erl */
#ifndef XX_ERLANG_BEAM_H
#define XX_ERLANG_BEAM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_erlang_beam { Abstractformat format; } xx_erlang_beam;
XXFC_API void xx_erlang_beam_init(xx_erlang_beam *,xx_io_device *,int64_t);
XXFC_API xx_erlang_beam *xx_erlang_beam_create(xx_io_device *,int64_t);
XXFC_API void xx_erlang_beam_destroy(xx_erlang_beam *);
XXFC_API void xx_erlang_beam_free(xx_erlang_beam *);
XXFC_API bool xx_erlang_beam_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_erlang_beam_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
