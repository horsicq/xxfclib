/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.cisco.com/c/en/us/td/docs/net_mgmt/netflow_collection_engine/3-6/user/guide/format.html */
#include "xxfclib/formats/netflow_datagram/xx_netflow_datagram.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_fields.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=72&&xx_data_get_u16(b.p, 2, 0, true)==5);unsigned count=xx_data_get_u16(b.p+2, 2, 0, true);uint32_t uptime=xx_data_get_u32(b.p+4, 4, 0, true);BLOB_NEED(count&&count<=30&&b.n==24+(uint64_t)count*48&&xx_data_get_u32(b.p+12, 4, 0, true)<1000000000U&&(xx_data_get_u16(b.p+22, 2, 0, true)>>14)<=2&&blob_add(f,s,&b,"netflow-header",0,24));for(uint64_t p=24;p<b.n;p+=48){const uint8_t *q=b.p+(size_t)p;BLOB_NEED(network_ip4(q,false)&&network_ip4(q+4,false)&&(!xx_data_get_u32(q+8, 4, 0, true)||network_ip4(q+8,false))&&!q[36]&&!xx_data_get_u16(q+46, 2, 0, true)&&q[44]<=32&&q[45]<=32&&q[38]>0&&xx_data_get_u32(q+16, 4, 0, true)>0&&xx_data_get_u32(q+20, 4, 0, true)>0&&xx_data_get_u32(q+24, 4, 0, true)<=xx_data_get_u32(q+28, 4, 0, true)&&xx_data_get_u32(q+28, 4, 0, true)<=uptime&&blob_add(f,s,&b,"flow-record",p,48));}s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_netflow_datagram_init(xx_netflow_datagram *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_NETFLOW_DATAGRAM,"bin");}}
xx_netflow_datagram *xx_netflow_datagram_create(xx_io_device *d,int64_t b){xx_netflow_datagram *r=(xx_netflow_datagram *)xx_mem_alloc(sizeof(*r));if(r)xx_netflow_datagram_init(r,d,b);return r;}
void xx_netflow_datagram_destroy(xx_netflow_datagram *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_netflow_datagram_free(xx_netflow_datagram *r){if(r){xx_netflow_datagram_destroy(r);xx_mem_free(r);}}
bool xx_netflow_datagram_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_netflow_datagram_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
