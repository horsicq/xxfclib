/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc4511.html */
#include "xxfclib/formats/ldap_message/xx_ldap_message.h"
#include "../xx_thirteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;cm_tlv msg,id,op,x,y,z;uint64_t at=0,p,q,k,v,messageid=0,code;unsigned entries=0,attrs=0;bool doneop=false,ok=false;if(!nh_load(f,&b,pd))return false;while(at<b.n){uint64_t start=at;NH_NEED(!doneop&&cm_take(&b,&at,b.n,48,&msg));p=msg.value;NH_NEED(cm_take(&b,&p,msg.end,2,&id)&&th_uint(&b,&id,&v)&&v&&v<=2147483647&&(messageid==0||v==messageid));messageid=v;NH_NEED(cm_read(&b,&p,msg.end,&op)&&p==msg.end&&(op.tag==100||op.tag==101)&&nh_add(f,s,&b,"ldap-envelope",start,op.value-start));q=op.value;
 if(op.tag==100){NH_NEED(++entries<=512&&cm_take(&b,&q,op.end,4,&x)&&ec_utf(&b,x.value,x.end-x.value)&&x.end>x.value&&th_tlv_add(f,s,&b,"distinguished-name",&x)&&cm_take(&b,&q,op.end,48,&x)&&q==op.end);k=x.value;while(k<x.end){NH_NEED(++attrs<=1024&&cm_take(&b,&k,x.end,48,&y));p=y.value;NH_NEED(cm_take(&b,&p,y.end,4,&z)&&z.end>z.value&&ec_utf(&b,z.value,z.end-z.value)&&th_tlv_add(f,s,&b,"attribute-description",&z)&&cm_take(&b,&p,y.end,49,&z)&&p==y.end);uint64_t values=z.value;unsigned count=0;while(values<z.end){NH_NEED(++count<=1024&&cm_take(&b,&values,z.end,4,&id)&&th_tlv_add(f,s,&b,"attribute-value",&id));}NH_NEED(count>0);}}
 else{NH_NEED(entries&&cm_take(&b,&q,op.end,10,&x)&&th_uint(&b,&x,&code)&&code<=80&&cm_take(&b,&q,op.end,4,&y)&&ec_utf(&b,y.value,y.end-y.value)&&cm_take(&b,&q,op.end,4,&z)&&ec_utf(&b,z.value,z.end-z.value)&&q==op.end&&th_tlv_add(f,s,&b,"search-result",&op));doneop=true;}}
 NH_NEED(doneop&&attrs>0);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ldap_message_init(xx_ldap_message *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_LDAP_MESSAGE,"bin");}}
xx_ldap_message *xx_ldap_message_create(xx_io_device *d,int64_t b) {xx_ldap_message *r=(xx_ldap_message *)xx_mem_alloc(sizeof(*r));if(r)xx_ldap_message_init(r,d,b);return r;}
void xx_ldap_message_destroy(xx_ldap_message *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ldap_message_free(xx_ldap_message *r) {if(r){xx_ldap_message_destroy(r);xx_mem_free(r);}}
bool xx_ldap_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_ldap_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
