/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/ros/ros_comm/blob/noetic-devel/tools/rosbag/src/rosbag/bag.py */
#include "xxfclib/formats/rosbag1/xx_rosbag1.h"
#include "../xx_ninth_data.h"

typedef struct rb_field {const uint8_t *name,*value;uint32_t name_size,size;} rb_field;
typedef struct rb_record {rb_field fields[16];unsigned count;uint8_t op;uint64_t data,size,next;} rb_record;
static const rb_field *rb_find(const rb_record *r,const char *name,unsigned size) {unsigned i;size_t n=xx_rt_strlen(name);for(i=0;i<r->count;++i) if(r->fields[i].name_size==n && !xx_rt_memcmp(r->fields[i].name,name,n) && r->fields[i].size==size) return &r->fields[i];return NULL;}
static bool rb_header(nh_blob *b,uint64_t at,uint64_t end,rb_record *r) {
    r->count=0;while(at<end) {uint32_t n,i,key=0;const uint8_t *p;if(!eh_span(at,4,end) || r->count==16) return false;n=pm_le32(b->p+(size_t)at);at+=4;if(!n || !eh_span(at,n,end)) return false;p=b->p+(size_t)at;
        while(key<n && p[key]!='=') { ++key; } if(!key || key==n || key>63 || !nh_ascii(p,key,false)) return false;
        for(i=0;i<r->count;++i) if(r->fields[i].name_size==key && !xx_rt_memcmp(p,r->fields[i].name,key)) return false;
        r->fields[r->count].name=p;r->fields[r->count].name_size=key;r->fields[r->count].value=p+key+1;r->fields[r->count++].size=n-key-1;at+=n;
    }return at==end;
}
static bool rb_read(nh_blob *b,uint64_t at,uint64_t end,rb_record *r) {
    uint32_t header;const rb_field *op;if(!eh_span(at,4,end)) return false;header=pm_le32(b->p+(size_t)at);at+=4;if(header>65536 || !eh_span(at,(uint64_t)header+4,end) || !rb_header(b,at,at+header,r)) return false;at+=header;
    r->size=pm_le32(b->p+(size_t)at);r->data=at+4;r->next=r->data+r->size;if(r->next>end) return false;op=rb_find(r,"op",1);if(!op) return false;r->op=*op->value;return !fd_stop(b->pd);
}
static bool rb_number(const rb_record *r,const char *key,unsigned width,uint64_t *value) {const rb_field *v=rb_find(r,key,width);if(!v) return false;*value=width==8 ? fd_le64(v->value):pm_le32(v->value);return true;}
static bool rb_time(const rb_record *r,const char *key,uint64_t *value) {const rb_field *v=rb_find(r,key,8);uint32_t ns;if(!v) return false;ns=pm_le32(v->value+4);if(ns>=1000000000U) return false;*value=(uint64_t)pm_le32(v->value)*1000000000U+ns;return true;}
static bool rb_connection(nh_blob *b,const rb_record *r,uint64_t *id) {
    rb_record data;const rb_field *topic,*type,*md5;unsigned i;if(!rb_number(r,"conn",4,id) || *id>65535 || !rb_header(b,r->data,r->next,&data)) return false;
    topic=rb_find(r,"topic",0);for(i=0;i<r->count;++i) if(r->fields[i].name_size==5 && !xx_rt_memcmp(r->fields[i].name,"topic",5)) topic=&r->fields[i];if(!topic || !topic->size || !fourth_utf8(topic->value,topic->size,b->pd)) return false;
    type=NULL;for(i=0;i<data.count;++i) if(data.fields[i].name_size==4 && !xx_rt_memcmp(data.fields[i].name,"type",4)) type=&data.fields[i];md5=rb_find(&data,"md5sum",32);
    if(!type || !type->size || !md5 || !fourth_utf8(type->value,type->size,b->pd)) { return false; } for(i=0;i<32;++i) if(!((md5->value[i]>='0' && md5->value[i]<='9') || (md5->value[i]>='a' && md5->value[i]<='f'))) return false;return true;
}
typedef struct rb_msg {uint32_t conn,offset;uint64_t time;bool indexed;} rb_msg;
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[13];nh_blob b={0};bool ok=false;rb_record r;uint64_t at=13,index,conn_count,chunk_count,chunk_pos,chunk_data,chunk_end,declared,min_time=UINT64_MAX,max_time=0;uint32_t ids[128],counts[128]={0};uint64_t metadata[128],metadata_size[128];bool terminal[128]={0},indexed[128]={0};rb_msg messages[4090];unsigned connections=0,message_count=0,i,j,info_count=0;
    if(!pm_read(f,0,h,13) || xx_rt_memcmp(h,"#ROSBAG V2.0\n",13)) return false;
    NH_NEED(nh_load(f,&b,pd) && rb_read(&b,at,b.n,&r) && r.op==3 && rb_number(&r,"index_pos",8,&index) && rb_number(&r,"conn_count",4,&conn_count) && rb_number(&r,"chunk_count",4,&chunk_count) && conn_count && conn_count<=128 && chunk_count==1 && index<b.n);
    for(i=0;i<r.size;++i) { NH_NEED(b.p[(size_t)r.data+i]==' '); } at=r.next;NH_NEED(nh_add(f,s,&b,"file-header",0,at));chunk_pos=at;
    NH_NEED(rb_read(&b,at,index,&r) && r.op==5 && rb_number(&r,"size",4,&declared) && declared==r.size);{const rb_field *v=rb_find(&r,"compression",4);NH_NEED(v && !xx_rt_memcmp(v->value,"none",4));}
    chunk_data=r.data;chunk_end=r.next;at=chunk_data;NH_NEED(nh_add(f,s,&b,"chunk-header",chunk_pos,chunk_data-chunk_pos));
    while(at<chunk_end) {uint64_t id,time,start=at;NH_NEED(rb_read(&b,at,chunk_end,&r));
        if(r.op==7) {NH_NEED(connections<128 && rb_connection(&b,&r,&id));for(i=0;i<connections;++i) NH_NEED(ids[i]!=id);ids[connections]=(uint32_t)id;metadata[connections]=r.data;metadata_size[connections]=r.size;++connections;NH_NEED(nh_add(f,s,&b,"connection",start,r.next-start));}
        else if(r.op==2) {NH_NEED(message_count<4090 && rb_number(&r,"conn",4,&id) && rb_time(&r,"time",&time));for(i=0;i<connections && ids[i]!=id;++i) {}NH_NEED(i<connections && nh_add(f,s,&b,"message",r.data,r.size));messages[message_count].conn=(uint32_t)id;messages[message_count].offset=(uint32_t)(start-chunk_data);messages[message_count].time=time;messages[message_count++].indexed=false;++counts[i];if(time<min_time) min_time=time;if(time>max_time) max_time=time;}
        else { NH_NEED(false); } at=r.next;
    }NH_NEED(connections==conn_count && message_count);at=chunk_end;
    while(at<index) {uint64_t id,count,ver;NH_NEED(rb_read(&b,at,index,&r) && r.op==4 && rb_number(&r,"ver",4,&ver) && ver==1 && rb_number(&r,"conn",4,&id) && rb_number(&r,"count",4,&count) && count && count*12==r.size);
        for(i=0;i<connections && ids[i]!=id;++i) {}NH_NEED(i<connections && !indexed[i] && counts[i]==count);indexed[i]=true;
        for(j=0;j<count;++j) {const uint8_t *p=b.p+(size_t)r.data+j*12;uint32_t offset=pm_le32(p+8);uint64_t time;unsigned k;NH_NEED(pm_le32(p+4)<1000000000U);time=(uint64_t)pm_le32(p)*1000000000U+pm_le32(p+4);for(k=0;k<message_count && messages[k].offset!=offset;++k) {}NH_NEED(k<message_count && !messages[k].indexed && messages[k].conn==id && messages[k].time==time);messages[k].indexed=true;}
        NH_NEED(nh_add(f,s,&b,"message-index",at,r.next-at));at=r.next;
    }NH_NEED(at==index);for(i=0;i<message_count;++i) NH_NEED(messages[i].indexed);
    for(j=0;j<connections;++j) {uint64_t id;NH_NEED(rb_read(&b,at,b.n,&r) && r.op==7 && rb_connection(&b,&r,&id));for(i=0;i<connections && ids[i]!=id;++i) {}NH_NEED(i<connections && !terminal[i] && r.size==metadata_size[i] && !xx_rt_memcmp(b.p+(size_t)r.data,b.p+(size_t)metadata[i],(size_t)r.size));terminal[i]=true;NH_NEED(nh_add(f,s,&b,"terminal-connection",at,r.next-at));at=r.next;}
    {uint64_t ver,pos,count,start,end;NH_NEED(rb_read(&b,at,b.n,&r) && r.op==6 && rb_number(&r,"ver",4,&ver) && ver==1 && rb_number(&r,"chunk_pos",8,&pos) && pos==chunk_pos && rb_number(&r,"count",4,&count) && count<=connections && r.size==count*8 && rb_time(&r,"start_time",&start) && start==min_time && rb_time(&r,"end_time",&end) && end==max_time);
        for(j=0;j<count;++j) {const uint8_t *p=b.p+(size_t)r.data+j*8;uint32_t id=pm_le32(p),n=pm_le32(p+4);for(i=0;i<connections && ids[i]!=id;++i) {}NH_NEED(i<connections && n==counts[i] && n && indexed[i]);indexed[i]=false;++info_count;}
        NH_NEED(nh_add(f,s,&b,"chunk-info",at,r.next-at));at=r.next;
    }NH_NEED(info_count && at==b.n);for(i=0;i<connections;++i) NH_NEED(!indexed[i]);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_rosbag1_init(xx_rosbag1 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ROSBAG1,"rosbag1"); } }
xx_rosbag1 *xx_rosbag1_create(xx_io_device *d,int64_t b) { xx_rosbag1 *r=(xx_rosbag1 *)xx_mem_alloc(sizeof(*r)); if(r) xx_rosbag1_init(r,d,b); return r; }
void xx_rosbag1_destroy(xx_rosbag1 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_rosbag1_free(xx_rosbag1 *r) { if(r) { xx_rosbag1_destroy(r); xx_mem_free(r); } }
bool xx_rosbag1_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_rosbag1_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
