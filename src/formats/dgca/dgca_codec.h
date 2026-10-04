/* Independent DGCA payload codecs. SPDX-License-Identifier: MIT. */
#ifndef DGCA_CODEC_H
#define DGCA_CODEC_H
#include "dgca_native.h"
dg_status dg_codec_output_size(const dg_callbacks *,const unsigned char *,size_t,size_t *);
dg_status dg_codec_decode(const dg_callbacks *,const unsigned char *,size_t,
                          unsigned char *,size_t);
#endif
