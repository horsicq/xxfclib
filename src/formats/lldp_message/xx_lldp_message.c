/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://scapy.readthedocs.io/en/stable/api/scapy.contrib.lldp.html */
#include "xxfclib/formats/lldp_message/xx_lldp_message.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_sixteenth_wrappers.h"

static bool identifier(nh_blob *b,uint64_t p,uint64_t n,bool port){if(n<2||n>256)return false;unsigned type=b->p[(size_t)p];if(type<1||type>7)return false;if(type==(port?3U:4U))return n==7;if(type==(port?4U:5U)){if(n<3)return false;unsigned family=b->p[(size_t)p+1];return (family==1&&n==6)||(family==2&&n==18);}return f15_utf(b,p+1,n-1);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=0;unsigned count=0,mask=0;bool ended=false,ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=14&&b.n<=4096);while(at<b.n){uint64_t start=at;NH_NEED(++count<=12&&th_take(&b,&at,b.n,2));unsigned head=xx_data_get_u16(b.p+(size_t)start, 2, 0, true),type=head>>9,n=head&511;uint64_t p=at;NH_NEED(th_take(&b,&at,b.n,n));if(!type){NH_NEED(!n&&at==b.n&&count>=4);ended=true;NH_NEED(nh_add(f,s,&b,"lldp-end-tlv",start,2));break;}NH_NEED(type<=8&&!(mask&(1U<<type)));mask|=1U<<type;if(count<=3)NH_NEED(type==count);else NH_NEED(type>3);if(type==1||type==2)NH_NEED(identifier(&b,p,n,type==2));else if(type==3)NH_NEED(n==2);else if(type>=4&&type<=6)NH_NEED(n>=1&&n<=255&&f15_utf(&b,p,n));else if(type==7)NH_NEED(n==4&&(xx_data_get_u16(b.p+(size_t)p, 2, 0, true)&~0x07ffU)==0&&(xx_data_get_u16(b.p+(size_t)p+2, 2, 0, true)&~xx_data_get_u16(b.p+(size_t)p, 2, 0, true))==0);else{NH_NEED(n>=9);unsigned addr=b.p[(size_t)p];NH_NEED(addr>=2&&addr<=31&&n>=1+addr+6);unsigned family=b.p[(size_t)p+1];NH_NEED((family==1&&addr==5)||(family==2&&addr==17)||(family==6&&addr==7));unsigned numbering=b.p[(size_t)p+1+addr],oid=b.p[(size_t)p+6+addr];NH_NEED(numbering>=1&&numbering<=3&&oid<=128&&n==7+addr+oid);}NH_NEED(nh_add(f,s,&b,"lldp-tlv-fields",start,2)&&nh_add(f,s,&b,"lldp-tlv-value",p,n));}NH_NEED(ended&&(mask&14)==14);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_lldp_message_init(xx_lldp_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_LLDP_MESSAGE,"bin");}}
xx_lldp_message *xx_lldp_message_create(xx_io_device *d,int64_t b){xx_lldp_message *r=(xx_lldp_message *)xx_mem_alloc(sizeof(*r));if(r)xx_lldp_message_init(r,d,b);return r;}
void xx_lldp_message_destroy(xx_lldp_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_lldp_message_free(xx_lldp_message *r){if(r){xx_lldp_message_destroy(r);xx_mem_free(r);}}
bool xx_lldp_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_lldp_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
