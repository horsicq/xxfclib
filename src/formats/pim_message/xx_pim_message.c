/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc7761.html */
#include "xxfclib/formats/pim_message/xx_pim_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_fields.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;uint64_t at=4;unsigned mask=0,count=0;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=10&&b.p[0]==32&&!b.p[1]&&packet_checksum(&b,0,b.n,0)&&blob_add(f,s,&b,"pim-header",0,4));while(at<b.n){uint64_t start=at;BLOB_NEED(++count<=4&&protocol_take(&b,&at,b.n,4));unsigned type=xx_data_get_u16(b.p+(size_t)start, 2, 0, true),n=xx_data_get_u16(b.p+(size_t)start+2, 2, 0, true),bit=type==1?1:type==2?2:type==19?4:type==20?8:0;BLOB_NEED(bit&&!(mask&bit)&&n==(type==1?2U:4U)&&protocol_take(&b,&at,b.n,n));mask|=bit;BLOB_NEED(blob_add(f,s,&b,"hello-option-fields",start,4)&&blob_add(f,s,&b,"hello-option-value",start+4,n));}BLOB_NEED(mask&1);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_pim_message_init(xx_pim_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_PIM_MESSAGE,"bin");}}
xx_pim_message *xx_pim_message_create(xx_io_device *d,int64_t b){xx_pim_message *r=(xx_pim_message *)xx_mem_alloc(sizeof(*r));if(r)xx_pim_message_init(r,d,b);return r;}
void xx_pim_message_destroy(xx_pim_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_pim_message_free(xx_pim_message *r){if(r){xx_pim_message_destroy(r);xx_mem_free(r);}}
bool xx_pim_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_pim_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
