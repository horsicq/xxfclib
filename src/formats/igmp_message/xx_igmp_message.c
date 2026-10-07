/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc3376.html */
#include "xxfclib/formats/igmp_message/xx_igmp_message.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_sixteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=8;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=16&&b.p[0]==34&&!b.p[1]&&!xx_data_get_u16(b.p+4, 2, 0, true)&&f15_checksum(&b,0,b.n,0)&&nh_add(f,s,&b,"igmp-report-header",0,8));unsigned records=xx_data_get_u16(b.p+6, 2, 0, true);NH_NEED(records&&records<=512);for(unsigned i=0;i<records;++i){uint64_t start=at;NH_NEED(th_take(&b,&at,b.n,8));unsigned type=b.p[(size_t)start],aux=b.p[(size_t)start+1],sources=xx_data_get_u16(b.p+(size_t)start+2, 2, 0, true);NH_NEED(type>=1&&type<=6&&sources<=512&&f16_ip4(b.p+(size_t)start+4,true)&&nh_add(f,s,&b,"group-record-fields",start,8));for(unsigned j=0;j<sources;++j){uint64_t p=at;NH_NEED(th_take(&b,&at,b.n,4)&&f16_ip4(b.p+(size_t)p,false)&&nh_add(f,s,&b,"source-ipv4-address",p,4));}if(aux){uint64_t p=at,n=(uint64_t)aux*4;NH_NEED(th_take(&b,&at,b.n,n)&&nh_add(f,s,&b,"encoded-auxiliary-data",p,n));}}NH_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_igmp_message_init(xx_igmp_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_IGMP_MESSAGE,"bin");}}
xx_igmp_message *xx_igmp_message_create(xx_io_device *d,int64_t b){xx_igmp_message *r=(xx_igmp_message *)xx_mem_alloc(sizeof(*r));if(r)xx_igmp_message_init(r,d,b);return r;}
void xx_igmp_message_destroy(xx_igmp_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_igmp_message_free(xx_igmp_message *r){if(r){xx_igmp_message_destroy(r);xx_mem_free(r);}}
bool xx_igmp_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_igmp_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
