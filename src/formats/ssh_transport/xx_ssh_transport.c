/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc4253.html */
#include "xxfclib/formats/ssh_transport/xx_ssh_transport.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_fourteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;bool ok=false;uint64_t at=0,start,n;unsigned count=0;if(!nh_load(f,&b,pd))return false;NH_NEED(th_line(&b,&at,&start,&n,true)&&n>=9&&n<=253&&!xx_rt_memcmp(b.p,"SSH-2.0-",8)&&nh_ascii(b.p,(size_t)n,false)&&nh_add(f,s,&b,"ssh-identification",0,at));while(at<b.n){start=at;NH_NEED(++count<=3&&th_take(&b,&at,b.n,5));n=xx_data_get_u32(b.p+(size_t)start, 4, 0, true);unsigned pad=b.p[(size_t)start+4];NH_NEED(n>=12&&n<=35000&&(n+4)%8==0&&pad>=4&&pad<n-1&&nh_span(&b,start,n+4));uint64_t end=start+4+n-pad,p=at;unsigned type=b.p[(size_t)p++];if(count==1){NH_NEED(type==20&&th_take(&b,&p,end,16)&&nh_add(f,s,&b,"kexinit-cookie",at,17));for(unsigned i=0;i<10;++i){uint64_t v,z;NH_NEED(fw_string(&b,&p,end,&v,&z,false)&&fw_namelist(&b,v,z,i>=8)&&nh_add(f,s,&b,"algorithm-list",v,z));}NH_NEED(th_take(&b,&p,end,5)&&b.p[(size_t)p-5]<=1&&nh_zero(&b,p-4,4)&&p==end);}else if(count==2){uint64_t v,z;NH_NEED(type==30&&fw_string(&b,&p,end,&v,&z,false)&&z>0&&(z!=1||b.p[(size_t)v])&&!(b.p[(size_t)v]&128)&&(z==1||b.p[(size_t)v]||(b.p[(size_t)v+1]&128))&&p==end&&nh_add(f,s,&b,"dh-public-mpint",v,z));}else NH_NEED(type==21&&p==end&&nh_add(f,s,&b,"newkeys",at,1));NH_NEED(nh_add(f,s,&b,"packet-padding",end,pad));at=start+n+4;}NH_NEED(count==3&&at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ssh_transport_init(xx_ssh_transport *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SSH_TRANSPORT,"bin");}}
xx_ssh_transport *xx_ssh_transport_create(xx_io_device *d,int64_t b){xx_ssh_transport *r=(xx_ssh_transport *)xx_mem_alloc(sizeof(*r));if(r)xx_ssh_transport_init(r,d,b);return r;}
void xx_ssh_transport_destroy(xx_ssh_transport *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ssh_transport_free(xx_ssh_transport *r){if(r){xx_ssh_transport_destroy(r);xx_mem_free(r);}}
bool xx_ssh_transport_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_ssh_transport_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
