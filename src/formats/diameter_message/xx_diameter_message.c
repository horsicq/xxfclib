/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc6733.html */
#include "xxfclib/formats/diameter_message/xx_diameter_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;uint64_t at=20;unsigned mask=0,count=0;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=20&&b.n<=16777215&&b.p[0]==1&&xx_data_get_u24(b.p+1, 3, 0, true)==b.n&&b.p[4]==128&&xx_data_get_u24(b.p+5, 3, 0, true)==257&&!xx_data_get_u32(b.p+8, 4, 0, true)&&blob_add(f,s,&b,"diameter-header",0,20));while(at<b.n){uint64_t start=at;BLOB_NEED(++count<=5&&protocol_take(&b,&at,b.n,8));uint32_t code=xx_data_get_u32(b.p+(size_t)start, 4, 0, true);uint64_t n=xx_data_get_u24(b.p+(size_t)start+5, 3, 0, true);unsigned flag=b.p[(size_t)start+4],bit=0;BLOB_NEED(n>=8&&n<=65544&&blob_span(&b,start,n)&&(flag==64||flag==0));uint64_t p=start+8,z=n-8;if(code==264||code==296){BLOB_NEED(flag==64);bit=code==264?1U:2U;BLOB_NEED(packet_ascii(&b,p,z,false));}else if(code==257){BLOB_NEED(flag==64);bit=4;BLOB_NEED((z==6&&xx_data_get_u16(b.p+(size_t)p, 2, 0, true)==1)||(z==18&&xx_data_get_u16(b.p+(size_t)p, 2, 0, true)==2));}else if(code==266){BLOB_NEED(flag==64);bit=8;BLOB_NEED(z==4);}else if(code==269){bit=16;BLOB_NEED(flag==0&&packet_utf(&b,p,z));}else goto done;BLOB_NEED(!(mask&bit));mask|=bit;BLOB_NEED(blob_add(f,s,&b,"avp-fields",start,8)&&blob_add(f,s,&b,"avp-value",p,z));at=start+n;uint64_t pad=(4-(n&3))&3;BLOB_NEED(protocol_take(&b,&at,b.n,pad)&&blob_zero(&b,at-pad,pad));}BLOB_NEED(mask==31);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_diameter_message_init(xx_diameter_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_DIAMETER_MESSAGE,"bin");}}
xx_diameter_message *xx_diameter_message_create(xx_io_device *d,int64_t b){xx_diameter_message *r=(xx_diameter_message *)xx_mem_alloc(sizeof(*r));if(r)xx_diameter_message_init(r,d,b);return r;}
void xx_diameter_message_destroy(xx_diameter_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_diameter_message_free(xx_diameter_message *r){if(r){xx_diameter_message_destroy(r);xx_mem_free(r);}}
bool xx_diameter_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_diameter_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
