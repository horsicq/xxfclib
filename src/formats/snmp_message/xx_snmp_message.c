/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc3416.html */
#include "xxfclib/formats/snmp_message/xx_snmp_message.h"
#include "../xx_thirteenth_wrappers.h"

static bool scalar(nh_blob *b,cm_tlv *v){uint64_t n=v->end-v->value;const uint8_t *p=b->p+(size_t)v->value;uint64_t u;if(v->tag==2)return n&&n<=4&&!(n>1&&((p[0]==0&&!(p[1]&128))||(p[0]==255&&(p[1]&128))));if(v->tag==4)return n<=65535;if(v->tag==5||v->tag==128||v->tag==129||v->tag==130)return !n;if(v->tag==6)return cm_oid(b,v);if(v->tag==64)return n==4;if(v->tag==65||v->tag==66||v->tag==67||v->tag==70){cm_tlv x=*v;x.tag=2;return th_uint(b,&x,&u)&&n<=(v->tag==70?9U:5U);}return false;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;cm_tlv msg,ver,community,pdu,id,error,index,list,item,oid,value;uint64_t at=0,p,q,k,version,er,ix,tmp;unsigned vars=0;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(cm_take(&b,&at,b.n,48,&msg)&&at==b.n);p=msg.value;NH_NEED(cm_take(&b,&p,msg.end,2,&ver)&&th_uint(&b,&ver,&version)&&version<=1&&cm_take(&b,&p,msg.end,4,&community)&&community.end>community.value&&community.end-community.value<=255&&cm_read(&b,&p,msg.end,&pdu)&&p==msg.end);NH_NEED((pdu.tag>=160&&pdu.tag<=163)||(version==1&&pdu.tag>=165&&pdu.tag<=168));q=pdu.value;NH_NEED(cm_take(&b,&q,pdu.end,2,&id)&&scalar(&b,&id)&&cm_take(&b,&q,pdu.end,2,&error)&&th_uint(&b,&error,&er)&&cm_take(&b,&q,pdu.end,2,&index)&&th_uint(&b,&index,&ix)&&cm_take(&b,&q,pdu.end,48,&list)&&q==pdu.end);NH_NEED(pdu.tag==165?(er<=65535&&ix<=65535):(er<=(version?18U:5U)&&(pdu.tag==162||!er)&& (er||!ix)));NH_NEED(nh_add(f,s,&b,"snmp-envelope",0,pdu.value)&&nh_add(f,s,&b,"pdu-fields",pdu.value,list.start-pdu.value));k=list.value;while(k<list.end){NH_NEED(++vars<=1024&&cm_take(&b,&k,list.end,48,&item));q=item.value;NH_NEED(cm_take(&b,&q,item.end,6,&oid)&&cm_oid(&b,&oid)&&cm_read(&b,&q,item.end,&value)&&scalar(&b,&value)&&q==item.end&&(version|| (value.tag!=70&&value.tag!=128&&value.tag!=129&&value.tag!=130))&&((pdu.tag!=160&&pdu.tag!=161&&pdu.tag!=165)||value.tag==5));NH_NEED(th_tlv_add(f,s,&b,"oid",&oid)&&th_tlv_add(f,s,&b,"value",&value));}tmp=vars;NH_NEED(vars>0&&(pdu.tag==165||ix<=tmp));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_snmp_message_init(xx_snmp_message *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SNMP_MESSAGE,"bin");}}
xx_snmp_message *xx_snmp_message_create(xx_io_device *d,int64_t b) {xx_snmp_message *r=(xx_snmp_message *)xx_mem_alloc(sizeof(*r));if(r)xx_snmp_message_init(r,d,b);return r;}
void xx_snmp_message_destroy(xx_snmp_message *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_snmp_message_free(xx_snmp_message *r) {if(r){xx_snmp_message_destroy(r);xx_mem_free(r);}}
bool xx_snmp_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_snmp_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
