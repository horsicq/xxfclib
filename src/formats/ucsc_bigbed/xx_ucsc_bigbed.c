/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/biopython/biopython/blob/master/Bio/Align/bigbed.py */
#include "xxfclib/formats/ucsc_bigbed/xx_ucsc_bigbed.h"
#include "../xx_tenth_data.h"
#define TB_BIGBED 1
#include "../xx_tenth_bbi.h"

void xx_ucsc_bigbed_init(xx_ucsc_bigbed *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_UCSC_BIGBED,"ucsc_bigbed"); } }
xx_ucsc_bigbed *xx_ucsc_bigbed_create(xx_io_device *d,int64_t b) { xx_ucsc_bigbed *r=(xx_ucsc_bigbed *)xx_mem_alloc(sizeof(*r)); if(r) xx_ucsc_bigbed_init(r,d,b); return r; }
void xx_ucsc_bigbed_destroy(xx_ucsc_bigbed *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ucsc_bigbed_free(xx_ucsc_bigbed *r) { if(r) { xx_ucsc_bigbed_destroy(r); xx_mem_free(r); } }
bool xx_ucsc_bigbed_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ucsc_bigbed_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
