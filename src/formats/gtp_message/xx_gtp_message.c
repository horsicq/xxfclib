/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.etsi.org/deliver/etsi_ts/129200_129299/129281/18.01.00_60/ts_129281v180100p.pdf */
#include "xxfclib/formats/gtp_message/xx_gtp_message.h"
#include "../xx_fifteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=36&&b.p[0]==48&&b.p[1]==255&&pm_be16(b.p+2)==b.n-8&&pm_be32(b.p+4)&&nh_add(f,s,&b,"gtp-header",0,8)&&f15_ip_add(f,s,&b,8,b.n-8));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_gtp_message_init(xx_gtp_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_GTP_MESSAGE,"bin");}}
xx_gtp_message *xx_gtp_message_create(xx_io_device *d,int64_t b){xx_gtp_message *r=(xx_gtp_message *)xx_mem_alloc(sizeof(*r));if(r)xx_gtp_message_init(r,d,b);return r;}
void xx_gtp_message_destroy(xx_gtp_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gtp_message_free(xx_gtp_message *r){if(r){xx_gtp_message_destroy(r);xx_mem_free(r);}}
bool xx_gtp_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_gtp_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
