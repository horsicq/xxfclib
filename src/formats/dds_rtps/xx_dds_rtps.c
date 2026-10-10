/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.omg.org/spec/DDSI-RTPS/2.3/PDF */
#include "xxfclib/formats/dds_rtps/xx_dds_rtps.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_fields.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;uint64_t at=20;unsigned count=0;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=24&&protocol_eq(&b,0,4,"RTPS")&&b.p[4]==2&&b.p[5]>=1&&b.p[5]<=5&&blob_add(f,s,&b,"rtps-header",0,20));while(at<b.n){uint64_t start=at;BLOB_NEED(++count<=1024&&protocol_take(&b,&at,b.n,4));unsigned type=b.p[(size_t)start],flags=b.p[(size_t)start+1];bool le=(flags&1)!=0;uint64_t n=xx_data_get_u16(b.p+(size_t)start+2, 2, 0, !le);if(!n&&type!=9)n=b.n-at;BLOB_NEED((n&3)==0&&protocol_take(&b,&at,b.n,n));uint64_t p=start+4;BLOB_NEED(blob_add(f,s,&b,"submessage-header",start,4));if(type==9){BLOB_NEED(!(flags&~3U)&&n==((flags&2)?0U:8U));if(n)BLOB_NEED(blob_add(f,s,&b,"timestamp",p,n));}else if(type==14){BLOB_NEED(!(flags&~1U)&&n==12&&blob_add(f,s,&b,"destination-guid",p,n));}else if(type==7){BLOB_NEED(!(flags&~7U)&&n==28);uint64_t first=network_seq(b.p+(size_t)p+8,le),last=network_seq(b.p+(size_t)p+16,le);BLOB_NEED(first&&first<=INT64_MAX&&last<=INT64_MAX&&last+1>=first&&blob_add(f,s,&b,"heartbeat-fields",p,n));}else if(type==21){BLOB_NEED((flags==4||flags==5)&&n>=28&&!xx_data_get_u16(b.p+(size_t)p, 2, 0, !le)&&xx_data_get_u16(b.p+(size_t)p+2, 2, 0, !le)==16);uint64_t seq=network_seq(b.p+(size_t)p+12,le);BLOB_NEED(seq&&seq<=INT64_MAX&&xx_data_get_u16(b.p+(size_t)p+20, 2, 0, true)<=1&&!xx_data_get_u16(b.p+(size_t)p+22, 2, 0, true)&&blob_add(f,s,&b,"data-fields",p,20)&&blob_add(f,s,&b,"cdr-encapsulation",p+20,4)&&blob_add(f,s,&b,"encoded-cdr-payload",p+24,n-24));}else goto done;}BLOB_NEED(count);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_dds_rtps_init(xx_dds_rtps *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_DDS_RTPS,"bin");}}
xx_dds_rtps *xx_dds_rtps_create(xx_io_device *d,int64_t b){xx_dds_rtps *r=(xx_dds_rtps *)xx_mem_alloc(sizeof(*r));if(r)xx_dds_rtps_init(r,d,b);return r;}
void xx_dds_rtps_destroy(xx_dds_rtps *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_dds_rtps_free(xx_dds_rtps *r){if(r){xx_dds_rtps_destroy(r);xx_mem_free(r);}}
bool xx_dds_rtps_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_dds_rtps_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
