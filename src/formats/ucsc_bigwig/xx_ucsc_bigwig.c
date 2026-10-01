/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/dpryan79/libBigWig */
#include "xxfclib/formats/ucsc_bigwig/xx_ucsc_bigwig.h"
#include "../xx_tenth_data.h"
#define TB_BIGBED 0
#include "../xx_tenth_bbi.h"

void xx_ucsc_bigwig_init(xx_ucsc_bigwig *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_UCSC_BIGWIG,"ucsc_bigwig"); } }
xx_ucsc_bigwig *xx_ucsc_bigwig_create(xx_io_device *d,int64_t b) { xx_ucsc_bigwig *r=(xx_ucsc_bigwig *)xx_mem_alloc(sizeof(*r)); if(r) xx_ucsc_bigwig_init(r,d,b); return r; }
void xx_ucsc_bigwig_destroy(xx_ucsc_bigwig *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ucsc_bigwig_free(xx_ucsc_bigwig *r) { if(r) { xx_ucsc_bigwig_destroy(r); xx_mem_free(r); } }
bool xx_ucsc_bigwig_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ucsc_bigwig_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
