/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc5905.html */
#include "xxfclib/formats/ntp_message/xx_ntp_message.h"
#include "../xx_fourteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n==48&&(b.p[0]>>3&7)==4&&((b.p[0]&7)==3||(b.p[0]&7)==4)&&b.p[1]<=16);NH_NEED(nh_add(f,s,&b,"ntp-header",0,16));for(uint64_t at=16;at<48;at+=8)NH_NEED(nh_add(f,s,&b,"ntp-timestamp",at,8));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ntp_message_init(xx_ntp_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_NTP_MESSAGE,"bin");}}
xx_ntp_message *xx_ntp_message_create(xx_io_device *d,int64_t b){xx_ntp_message *r=(xx_ntp_message *)xx_mem_alloc(sizeof(*r));if(r)xx_ntp_message_init(r,d,b);return r;}
void xx_ntp_message_destroy(xx_ntp_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ntp_message_free(xx_ntp_message *r){if(r){xx_ntp_message_destroy(r);xx_mem_free(r);}}
bool xx_ntp_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_ntp_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
