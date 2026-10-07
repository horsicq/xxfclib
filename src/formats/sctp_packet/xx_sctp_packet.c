/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc9260.html */
#include "xxfclib/formats/sctp_packet/xx_sctp_packet.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_fourteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;bool ok=false;uint64_t at=12;unsigned count=0;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=32&&xx_data_get_u16(b.p, 2, 0, true)&&xx_data_get_u16(b.p+2, 2, 0, true));uint32_t want=xx_data_get_u32(b.p+8, 4, 0, false);xx_mem_zero(b.p+8,4);NH_NEED(ec_crc(&b,0,b.n,want,true));b.p[8]=(uint8_t)want;b.p[9]=(uint8_t)(want>>8);b.p[10]=(uint8_t)(want>>16);b.p[11]=(uint8_t)(want>>24);NH_NEED(nh_add(f,s,&b,"sctp-header",0,12));while(at<b.n){uint64_t start=at;NH_NEED(++count<=1024&&th_take(&b,&at,b.n,4));uint64_t n=xx_data_get_u16(b.p+(size_t)start+2, 2, 0, true);NH_NEED(b.p[(size_t)start]==0&&!(b.p[(size_t)start+1]&248)&&n>=17&&nh_span(&b,start,n));NH_NEED(nh_add(f,s,&b,"data-fields",start,16)&&nh_add(f,s,&b,"encoded-data",start+16,n-16));at=start+n;NH_NEED(ec_pad(&b,&at,4));}NH_NEED(count>0&&at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_sctp_packet_init(xx_sctp_packet *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SCTP_PACKET,"bin");}}
xx_sctp_packet *xx_sctp_packet_create(xx_io_device *d,int64_t b){xx_sctp_packet *r=(xx_sctp_packet *)xx_mem_alloc(sizeof(*r));if(r)xx_sctp_packet_init(r,d,b);return r;}
void xx_sctp_packet_destroy(xx_sctp_packet *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_sctp_packet_free(xx_sctp_packet *r){if(r){xx_sctp_packet_destroy(r);xx_mem_free(r);}}
bool xx_sctp_packet_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_sctp_packet_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
