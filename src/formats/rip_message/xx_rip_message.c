/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc2453.html */
#include "xxfclib/formats/rip_message/xx_rip_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_fields.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=24&&b.n<=504&&(b.n-4)%20==0&&b.p[0]==2&&b.p[1]==2&&!xx_data_get_u16(b.p+2, 2, 0, true)&&blob_add(f,s,&b,"rip-header",0,4));for(uint64_t p=4;p<b.n;p+=20){uint32_t address=xx_data_get_u32(b.p+(size_t)p+4, 4, 0, true),mask=xx_data_get_u32(b.p+(size_t)p+8, 4, 0, true),next=xx_data_get_u32(b.p+(size_t)p+12, 4, 0, true),metric=xx_data_get_u32(b.p+(size_t)p+16, 4, 0, true);BLOB_NEED(xx_data_get_u16(b.p+(size_t)p, 2, 0, true)==2&&network_mask(mask)&&!(address&~mask)&&(address==0||network_ip4(b.p+(size_t)p+4,false))&&(!next||network_ip4(b.p+(size_t)p+12,false))&&metric>=1&&metric<=16&&blob_add(f,s,&b,"route-entry",p,20));}s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_rip_message_init(xx_rip_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_RIP_MESSAGE,"bin");}}
xx_rip_message *xx_rip_message_create(xx_io_device *d,int64_t b){xx_rip_message *r=(xx_rip_message *)xx_mem_alloc(sizeof(*r));if(r)xx_rip_message_init(r,d,b);return r;}
void xx_rip_message_destroy(xx_rip_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_rip_message_free(xx_rip_message *r){if(r){xx_rip_message_destroy(r);xx_mem_free(r);}}
bool xx_rip_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_rip_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
