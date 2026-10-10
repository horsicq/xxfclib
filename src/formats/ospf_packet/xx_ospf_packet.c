/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc2328.html */
#include "xxfclib/formats/ospf_packet/xx_ospf_packet.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_security_framing.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;bool ok=false;uint32_t sum=0;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=44&&b.n<=65535&&(b.n-44)%4==0&&b.p[0]==2&&b.p[1]==1&&xx_data_get_u16(b.p+2, 2, 0, true)==b.n&&blob_zero(&b,14,10));for(uint64_t i=0;i<b.n;i+=2){if(!blob_span(&b,i,2))goto done;if(i<16||i>=24)sum+=xx_data_get_u16(b.p+(size_t)i, 2, 0, true);}while(sum>>16)sum=(sum&65535)+(sum>>16);BLOB_NEED(sum==65535&&xx_data_get_u16(b.p+28, 2, 0, true)>0&&xx_data_get_u32(b.p+32, 4, 0, true)>0&&!(b.p[30]&16));BLOB_NEED(blob_add(f,s,&b,"ospf-header",0,24)&&blob_add(f,s,&b,"hello-fields",24,20));for(uint64_t at=44;at<b.n;at+=4)BLOB_NEED(blob_add(f,s,&b,"neighbor",at,4));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ospf_packet_init(xx_ospf_packet *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_OSPF_PACKET,"bin");}}
xx_ospf_packet *xx_ospf_packet_create(xx_io_device *d,int64_t b){xx_ospf_packet *r=(xx_ospf_packet *)xx_mem_alloc(sizeof(*r));if(r)xx_ospf_packet_init(r,d,b);return r;}
void xx_ospf_packet_destroy(xx_ospf_packet *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ospf_packet_free(xx_ospf_packet *r){if(r){xx_ospf_packet_destroy(r);xx_mem_free(r);}}
bool xx_ospf_packet_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_ospf_packet_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
