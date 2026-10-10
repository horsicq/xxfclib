/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc8489.html */
#include "xxfclib/formats/stun_message/xx_stun_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_protocol_framing.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;uint64_t at=20,n,start;unsigned attrs=0;bool fingerprint=false,integrity=false,ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=20 && !(b.p[0]&192) && xx_data_get_u32(b.p+4, 4, 0, true)==0x2112a442 && xx_data_get_u16(b.p+2, 2, 0, true)==b.n-20 && !((b.n-20)&3));uint16_t type=xx_data_get_u16(b.p, 2, 0, true);BLOB_NEED(type==1||type==0x101||type==0x111||type==0x11);BLOB_NEED(blob_add(f,s,&b,"stun-header",0,20));while(at<b.n){start=at;BLOB_NEED(++attrs<=256&&!fingerprint&&protocol_take(&b,&at,b.n,4));type=xx_data_get_u16(b.p+(size_t)start, 2, 0, true);n=xx_data_get_u16(b.p+(size_t)start+2, 2, 0, true);uint64_t value=at;BLOB_NEED(protocol_take(&b,&at,b.n,n));
 if(type==6||type==0x8022||type==0x14||type==0x15){BLOB_NEED(!integrity&&n&&n<=763&&serialized_utf(&b,value,n));}else if(type==8){BLOB_NEED(!integrity&&n==20);integrity=true;}else if(type==0x8028){BLOB_NEED(n==4&&at==b.n&&serialized_crc(&b,0,start,xx_data_get_u32(b.p+(size_t)value, 4, 0, true)^0x5354554eU,false));fingerprint=true;}else if(type==1||type==0x20||type==0x8023){BLOB_NEED(!integrity&&(n==8||n==20)&&!b.p[(size_t)value]&&b.p[(size_t)value+1]==(n==8?1:2));}else if(type==9){BLOB_NEED(!integrity&&n>=4&&!b.p[(size_t)value]&&!b.p[(size_t)value+1]&&b.p[(size_t)value+2]>=3&&b.p[(size_t)value+2]<=6&&b.p[(size_t)value+3]<=99&&serialized_utf(&b,value+4,n-4));}else if(type==0xa){BLOB_NEED(!integrity&&n&&!(n&1));}else BLOB_NEED(false);
 BLOB_NEED(protocol_take(&b,&at,b.n,(4-(n&3))&3)&&blob_add(f,s,&b,"attribute",start,at-start));}BLOB_NEED(attrs>0);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_stun_message_init(xx_stun_message *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_STUN_MESSAGE,"bin");}}
xx_stun_message *xx_stun_message_create(xx_io_device *d,int64_t b) {xx_stun_message *r=(xx_stun_message *)xx_mem_alloc(sizeof(*r));if(r)xx_stun_message_init(r,d,b);return r;}
void xx_stun_message_destroy(xx_stun_message *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_stun_message_free(xx_stun_message *r) {if(r){xx_stun_message_destroy(r);xx_mem_free(r);}}
bool xx_stun_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_stun_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
