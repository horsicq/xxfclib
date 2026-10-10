/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rpce/ */
#include "xxfclib/formats/dcerpc_pdu/xx_dcerpc_pdu.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_fields.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=24&&b.n<=65535&&b.p[0]==5&&!b.p[1]&&b.p[3]==3&&(b.p[4]==0||b.p[4]==16)&&blob_zero(&b,5,3));bool le=b.p[4]==16;BLOB_NEED(xx_data_get_u16(b.p+8, 2, 0, !le)==b.n&&!xx_data_get_u16(b.p+10, 2, 0, !le)&&blob_add(f,s,&b,"dcerpc-common-header",0,16));unsigned type=b.p[2];if(type==11){uint64_t at=28;BLOB_NEED(b.n>=28&&xx_data_get_u16(b.p+16, 2, 0, !le)>=16&&xx_data_get_u16(b.p+18, 2, 0, !le)>=16&&b.p[24]>0&&b.p[24]<=64&&blob_zero(&b,25,3)&&blob_add(f,s,&b,"bind-fields",16,12));uint16_t ids[64];unsigned count=b.p[24];for(unsigned i=0;i<count;++i){uint64_t start=at;BLOB_NEED(protocol_take(&b,&at,b.n,24));unsigned transfers=b.p[(size_t)start+2];uint16_t id=xx_data_get_u16(b.p+(size_t)start, 2, 0, !le);for(unsigned j=0;j<i;++j)BLOB_NEED(ids[j]!=id);ids[i]=id;BLOB_NEED(transfers&&transfers<=16&&!b.p[(size_t)start+3]&&!blob_zero(&b,start+4,16)&&blob_add(f,s,&b,"presentation-context",start,24));for(unsigned j=0;j<transfers;++j){uint64_t p=at;BLOB_NEED(protocol_take(&b,&at,b.n,20)&&!blob_zero(&b,p,16)&&blob_add(f,s,&b,"transfer-syntax",p,20));}}BLOB_NEED(at==b.n);}else if(type==0||type==2){BLOB_NEED(b.n>24);if(type==2)BLOB_NEED(!b.p[23]);BLOB_NEED(blob_add(f,s,&b,type==0?"request-fields":"response-fields",16,8)&&blob_add(f,s,&b,"encoded-ndr-stub",24,b.n-24));}else goto done;s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_dcerpc_pdu_init(xx_dcerpc_pdu *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_DCERPC_PDU,"bin");}}
xx_dcerpc_pdu *xx_dcerpc_pdu_create(xx_io_device *d,int64_t b){xx_dcerpc_pdu *r=(xx_dcerpc_pdu *)xx_mem_alloc(sizeof(*r));if(r)xx_dcerpc_pdu_init(r,d,b);return r;}
void xx_dcerpc_pdu_destroy(xx_dcerpc_pdu *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_dcerpc_pdu_free(xx_dcerpc_pdu *r){if(r){xx_dcerpc_pdu_destroy(r);xx_mem_free(r);}}
bool xx_dcerpc_pdu_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_dcerpc_pdu_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
