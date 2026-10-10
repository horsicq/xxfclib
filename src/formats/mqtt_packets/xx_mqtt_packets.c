/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html */
#include "xxfclib/formats/mqtt_packets/xx_mqtt_packets.h"
#include "../common/xx_container_wire_helpers.h"

static bool mq_string(Abstractformat *f,pm_stream *s,memory_blob *b,uint64_t *at,uint64_t end,const char *label,bool utf,bool topic,bool empty) {uint64_t n;if(!record_span(*at,2,end)) return false;n=xx_data_get_u16(b->p+(size_t)*at, 2, 0, true);*at+=2;if(!record_span(*at,n,end) || (!empty && !n) || (utf && !serialized_utf(b,*at,n))) return false;for(uint64_t i=0;utf && i<n;++i) {uint8_t c=b->p[(size_t)(*at+i)];if(!c || (topic && (c=='+' || c=='#'))) return false;}if(!blob_add(f,s,b,label,*at,n)) return false;*at+=n;return true;}
static bool mq_filter(memory_blob *b,uint64_t at,uint64_t end) {uint64_t n;if(!record_span(at,2,end)) return false;n=xx_data_get_u16(b->p+(size_t)at, 2, 0, true);at+=2;if(!n || !record_span(at,n,end)) return false;for(uint64_t i=0;i<n;++i) {uint8_t c=b->p[(size_t)(at+i)];if(c==43 && ((i && b->p[(size_t)(at+i-1)]!=47) || (i+1<n && b->p[(size_t)(at+i+1)]!=47))) return false;if(c==35 && ((i && b->p[(size_t)(at+i-1)]!=47) || i+1!=n)) return false;}return true;}
static bool mq_id(memory_blob *b,uint64_t *at,uint64_t end) {if(!record_span(*at,2,end) || !xx_data_get_u16(b->p+(size_t)*at, 2, 0, true)) return false;*at+=2;return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {memory_blob b;uint64_t at=0,start,end,n;unsigned packets=0,publishes=0;uint8_t last=0;bool ok=false;if(!blob_load(f,&b,pd)) return false;BLOB_NEED(b.n>=20);while(at<b.n) {uint8_t h,t,c;unsigned digits=0;uint64_t scale=1;start=at;h=b.p[(size_t)at++];t=h>>4;BLOB_NEED(t>=1 && t<=14 && (!packets ? t==1:t!=1) && t!=15);if(t!=3) BLOB_NEED((unsigned)(h&15)==(t==6 || t==8 || t==10 ? 2U:0U));else BLOB_NEED(((h>>1)&3)!=3 && (((h>>1)&3) || !(h&8)));n=0;do {BLOB_NEED(at<b.n && ++digits<=4);c=b.p[(size_t)at++];n+=(c&127)*scale;scale*=128;}while(c&128);BLOB_NEED((digits==1 || c) && record_span(at,n,b.n) && blob_add(f,s,&b,"packet-prefix",start,at-start));end=at+n;
 if(t==1) {uint8_t flags;BLOB_NEED(n>=10 && xx_data_get_u16(b.p+(size_t)at, 2, 0, true)==4 && !xx_rt_memcmp(b.p+(size_t)at+2,"MQTT",4) && b.p[(size_t)at+6]==4);flags=b.p[(size_t)at+7];BLOB_NEED(!(flags&1) && ((flags&4) ? ((flags>>3)&3)!=3:!(flags&56)) && (!(flags&64) || (flags&128)) && blob_add(f,s,&b,"connect-metadata",at,10));at+=10;BLOB_NEED(mq_string(f,s,&b,&at,end,"client-id",true,false,(flags&2)!=0));if(flags&4) BLOB_NEED(mq_string(f,s,&b,&at,end,"will-topic",true,true,false) && mq_string(f,s,&b,&at,end,"will-payload",false,false,true));if(flags&128) BLOB_NEED(mq_string(f,s,&b,&at,end,"username",true,false,true));if(flags&64) BLOB_NEED(mq_string(f,s,&b,&at,end,"password",false,false,true));}
 else if(t==2) {BLOB_NEED(n==2 && b.p[(size_t)at]<=1 && b.p[(size_t)at+1]<=5 && (!b.p[(size_t)at+1] || !b.p[(size_t)at]));BLOB_NEED(blob_add(f,s,&b,"connack-fields",at,2));at+=2;}
 else if(t==3) {BLOB_NEED(mq_string(f,s,&b,&at,end,"topic",true,true,false));if((h>>1)&3) {start=at;BLOB_NEED(mq_id(&b,&at,end) && blob_add(f,s,&b,"packet-id",start,2));}BLOB_NEED(blob_add(f,s,&b,"payload",at,end-at));at=end;++publishes;}
 else if(t==8 || t==10) {start=at;BLOB_NEED(mq_id(&b,&at,end) && blob_add(f,s,&b,"packet-id",start,2));unsigned filters=0;while(at<end) {BLOB_NEED(++filters<=256 && mq_filter(&b,at,end) && mq_string(f,s,&b,&at,end,"topic-filter",true,false,false));if(t==8) {BLOB_NEED(at<end && b.p[(size_t)at]<=2 && blob_add(f,s,&b,"requested-qos",at,1));++at;}}BLOB_NEED(filters);}
 else if(t==9) {start=at;BLOB_NEED(n>=3 && mq_id(&b,&at,end));while(at<end) {BLOB_NEED(b.p[(size_t)at]<=2 || b.p[(size_t)at]==128);++at;}BLOB_NEED(blob_add(f,s,&b,"suback-fields",start,n));}
 else if(t>=4 && t<=11) {start=at;BLOB_NEED(n==2 && mq_id(&b,&at,end) && blob_add(f,s,&b,"ack-fields",start,2));}else BLOB_NEED(!n);
 BLOB_NEED(at==end && ++packets<=512 && (t!=14 || at==b.n));last=t;}
 BLOB_NEED(packets>=3 && publishes && last==14);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_mqtt_packets_init(xx_mqtt_packets *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MQTT_PACKETS,"mqtt"); } }
xx_mqtt_packets *xx_mqtt_packets_create(xx_io_device *d,int64_t b) { xx_mqtt_packets *r=(xx_mqtt_packets *)xx_mem_alloc(sizeof(*r)); if(r) xx_mqtt_packets_init(r,d,b); return r; }
void xx_mqtt_packets_destroy(xx_mqtt_packets *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mqtt_packets_free(xx_mqtt_packets *r) { if(r) { xx_mqtt_packets_destroy(r); xx_mem_free(r); } }
bool xx_mqtt_packets_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mqtt_packets_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
