/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original bounded parser of the published Xen/DiscUtils XVA layout.
 */
#include "xxfclib/formats/xva/xx_xva.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XVA
#define XV_TYPE XX_FILE_TYPE_XVA
#else
#define XV_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define XV_CHUNK UINT32_C(1048576)
#define XV_COPY 65536U
#define XV_XML UINT32_C(16777216)
#define XV_NODES 262144U
#define XV_ENTRIES 100000U
#define XV_DISKS 1024U
#define XV_STEPS UINT32_C(16777216)
#define XV_MEMORY UINT64_C(134217728)
#define XV_MAX_DISK UINT64_C(1125899906842624)

typedef struct xv_chunk_s {
    uint64_t physical, logical, offset;
    uint32_t size;
    uint8_t digest[20];
    unsigned hash;
} xv_chunk;
typedef struct xv_disk_s {
    char id[80], name[96];
    uint64_t size, stored;
    xv_chunk *chunks;
    size_t count, capacity;
} xv_disk;
typedef struct xv_view_s {
    int64_t base;
    uint64_t size, memory, disk_bytes;
    size_t refs, count, capacity;
    xv_disk *disks;
} xv_view;
typedef struct xv_cursor_s { xv_view *view; size_t index; } xv_cursor;
typedef struct xv_tar_s {
    char name[256];
    uint64_t header, offset, size;
    unsigned type;
} xv_tar;
typedef enum xv_tag_e {
    XT_VALUE, XT_STRUCT, XT_MEMBER, XT_NAME, XT_ARRAY, XT_DATA, XT_STRING,
    XT_INT, XT_I4, XT_I8, XT_BOOLEAN, XT_DOUBLE, XT_DATE, XT_BASE64, XT_NIL,
    XT_BAD
} xv_tag;
typedef struct xv_node_s {
    uint32_t child, next, text, length;
    xv_tag tag;
} xv_node;
typedef struct xv_xml_s {
    xv_view *view;
    char *bytes;
    size_t size, pos, count, capacity;
    xv_node *nodes;
    uint32_t steps;
    xx_pd_struct *pd;
} xv_xml;
static bool xv_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool xv_read(xx_io_device *dev, int64_t at, void *buf, size_t n, xx_pd_struct *pd) {
    int64_t saved;
    size_t done=0;
    bool ok=false;
    if(!dev || at<0 || (!buf&&n) || xv_stop(pd)) return false;
    saved=xx_io_tell(dev); if(saved<0) return false;
    if(xx_io_seek64(dev,at,SEEK_SET)==0) {
        while(done<n && !xv_stop(pd)) {
            ssize_t got=xx_io_read(dev,(uint8_t *)buf+done,n-done);
            if(got<=0 || (size_t)got>n-done || xv_stop(pd)) break;
            done+=(size_t)got;
        }
        ok=done==n && !xv_stop(pd);
    }
    if(xx_io_seek64(dev,saved,SEEK_SET)!=0) ok=false;
    return ok;
}
static bool xv_rel(xx_io_device *d,const xv_view *v,uint64_t at,void *buf,size_t n,xx_pd_struct *pd) {
    return at<=v->size && n<=v->size-at && at<=(uint64_t)(INT64_MAX-v->base) &&
        xv_read(d,v->base+(int64_t)at,buf,n,pd);
}
static void *xv_alloc(xv_view *v,size_t size) {
    void *p;
    if(!size || size>XV_MEMORY-v->memory) return NULL;
    p=xx_mem_calloc(1U,size); if(p) v->memory+=size; return p;
}
static void xv_drop(xv_view *v,void *p,size_t size) {
    if(p) { xx_mem_free(p);v->memory-=size; }
}
static bool xv_grow(xv_view *v,void **p,size_t old,size_t size) {
    void *q=xv_alloc(v,size); if(!q) return false;
    if(*p) memcpy(q,*p,old);xv_drop(v,*p,old);*p=q;return true;
}
static void xv_release(xv_view *v) {
    size_t i;
    if(!v || --v->refs) return;
    for(i=0;i<v->count;++i) xx_mem_free(v->disks[i].chunks);
    xx_mem_free(v->disks);xx_mem_free(v);
}
static bool xv_zero(const uint8_t *p,size_t n) { size_t i;for(i=0;i<n;++i)if(p[i])return false;return true; }
static bool xv_number(const char *p,size_t n,unsigned base,uint64_t *value) {
    size_t i=0;uint64_t x=0;bool digit=false;
    while(i<n && p[i]==' ') ++i;
    for(;i<n;++i) {
        unsigned c=(unsigned)(uint8_t)p[i];
        if(c==' '||!c) { while(i<n) {if(p[i]!=' '&&p[i])return false;++i;} break; }
        if(c<'0'||c>='0'+base || x>(UINT64_MAX-(c-'0'))/base) return false;
        x=x*base+(c-'0');digit=true;
    }
    if(!digit) return false;*value=x;return true;
}
static bool xv_field(const uint8_t *p,size_t n,char *out,size_t capacity) {
    size_t k=0;while(k<n&&p[k])++k;
    if(k>=capacity || (k<n&&!xv_zero(p+k,n-k))) return false;
    memcpy(out,p,k);out[k]=0;return true;
}
static bool xv_path(const char *name,bool directory) {
    size_t i,n=strlen(name),component=0;
    if(!n||n>255U||name[0]=='/'||name[0]=='\\') return false;
    for(i=0;i<n;++i) {
        unsigned c=(uint8_t)name[i];
        if(c<32U||c>=127U||c=='\\') return false;
        if(c=='/') {
            size_t len=i-component;
            if(!len||(len==1U&&name[component]=='.')||
               (len==2U&&name[component]=='.'&&name[component+1U]=='.')) return false;
            component=i+1U;
        }
    }
    if(component==n) return directory;
    return !((n-component==1U&&name[component]=='.')||
        (n-component==2U&&name[component]=='.'&&name[component+1U]=='.'));
}
static bool xv_tar_header(const uint8_t h[512],xv_tar *entry) {
    char prefix[156],name[101];uint64_t checksum,size;unsigned sum=0,i;bool ustar;
    if(!xv_number((const char *)h+148U,8U,8U,&checksum)||
       !xv_number((const char *)h+124U,12U,8U,&size)) return false;
    for(i=0;i<512U;++i) sum+=i>=148U&&i<156U ? 32U : h[i];
    if(checksum!=sum || size>INT64_MAX || (h[156]&&h[156]!='0'&&h[156]!='5')) return false;
    ustar=!memcmp(h+257U,"ustar\0",6U)&&!memcmp(h+263U,"00",2U);
    if(!ustar&&!xv_zero(h+257U,255U)) return false;
    if(!xv_zero(h+157U,100U)||!xv_field(h,100U,name,sizeof(name))) return false;
    prefix[0]=0;
    if(ustar&&!xv_field(h+345U,155U,prefix,sizeof(prefix))) return false;
    if(strlen(prefix)+strlen(name)+(prefix[0]?1U:0U)>255U) return false;
    (void)xx_rt_snprintf(entry->name,sizeof(entry->name),"%s%s%s",prefix,prefix[0]?"/":"",name);
    entry->type=h[156]=='5'?5U:0U;entry->size=size;
    return xv_path(entry->name,entry->type==5U) && (!entry->type||!size);
}
static int xv_tar_compare(const void *a,const void *b) {
    return strcmp(((const xv_tar *)a)->name,((const xv_tar *)b)->name);
}
static bool xv_tar_scan(xx_io_device *dev,xv_view *v,xv_tar **items,size_t *count,size_t *capacity,xx_pd_struct *pd) {
    uint64_t at=0;uint8_t header[512];bool ended=false;
    while(at<=v->size && v->size-at>=512U && !xv_stop(pd)) {
        xv_tar entry;uint64_t padded;
        if(!xv_rel(dev,v,at,header,sizeof(header),pd)) return false;
        if(xv_zero(header,sizeof(header))) {
            if(v->size-at<1024U || !xv_rel(dev,v,at+512U,header,sizeof(header),pd)||
               !xv_zero(header,sizeof(header))) return false;
            at+=1024U;ended=true;break;
        }
        xx_mem_zero(&entry,sizeof(entry));
        if(*count==XV_ENTRIES || !xv_tar_header(header,&entry) || entry.size>UINT64_MAX-511U) return false;
        padded=(entry.size+511U)&~UINT64_C(511);
        if(padded>v->size-at-512U) return false;
        if(*count==*capacity) {
            size_t cap=*capacity?*capacity*2U:32U;
            if(cap>XV_ENTRIES) cap=XV_ENTRIES;
            if(!xv_grow(v,(void **)items,*capacity*sizeof(**items),cap*sizeof(**items))) return false;
            *capacity=cap;
        }
        entry.header=at;entry.offset=at+512U;(*items)[(*count)++]=entry;at+=512U+padded;
    }
    if(!ended || xv_stop(pd)) return false;
    if(v->size-at>XV_CHUNK) return false;
    /* A complete container ends with zero TAR padding; reject concatenations. */
    while(at<v->size) {
        size_t part=(size_t)(v->size-at<512U?v->size-at:512U);
        if(!xv_rel(dev,v,at,header,part,pd)||!xv_zero(header,part)) return false;
        at+=part;
    }
    qsort(*items,*count,sizeof(**items),xv_tar_compare);
    {
        size_t i;for(i=1;i<*count;++i) if(!strcmp((*items)[i-1U].name,(*items)[i].name)) return false;
    }
    return true;
}
static bool xv_space(unsigned c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; }
static xv_tag xv_tag_name(const char *p,size_t n) {
    static const char *const names[]={"value","struct","member","name","array","data",
        "string","int","i4","i8","boolean","double","dateTime.iso8601","base64","nil"};
    unsigned i;for(i=0;i<(unsigned)XT_BAD;++i) if(strlen(names[i])==n&&!memcmp(p,names[i],n)) return (xv_tag)i;
    return XT_BAD;
}
static bool xv_xml_misc(xv_xml *x) {
    for(;;) {
        while(x->pos<x->size&&xv_space((uint8_t)x->bytes[x->pos])) ++x->pos;
        if(x->size-x->pos<4U||memcmp(x->bytes+x->pos,"<!--",4U)) return true;
        x->pos+=4U;
        for(;;) {
            if(x->size-x->pos<3U || ++x->steps>XV_STEPS || xv_stop(x->pd)) return false;
            if(!memcmp(x->bytes+x->pos,"-->",3U)) { x->pos+=3U;break; }
            if(!memcmp(x->bytes+x->pos,"--",2U)) return false;
            ++x->pos;
        }
    }
}
static bool xv_codepoint(uint32_t c) {
    return (c>=32U&&c<=UINT32_C(0x10ffff)&&!(c>=0xd800U&&c<=0xdfffU)&&c!=0xfffeU&&c!=0xffffU)||
        c=='\t'||c=='\r'||c=='\n';
}
static bool xv_text(xv_xml *x,size_t start,size_t end,uint32_t *length) {
    size_t in=start,out=start;
    while(in<end) {
        uint32_t c=(uint8_t)x->bytes[in++];unsigned continuation=0;
        if(c=='&') {
            size_t first=in;bool numeric=false;unsigned base=10U;
            while(in<end&&x->bytes[in]!=';'&&in-first<16U) ++in;
            if(in==end||x->bytes[in]!=';') return false;
            if(in-first==3U&&!memcmp(x->bytes+first,"amp",3U)) c='&';
            else if(in-first==2U&&!memcmp(x->bytes+first,"lt",2U)) c='<';
            else if(in-first==2U&&!memcmp(x->bytes+first,"gt",2U)) c='>';
            else if(in-first==4U&&!memcmp(x->bytes+first,"quot",4U)) c='"';
            else if(in-first==4U&&!memcmp(x->bytes+first,"apos",4U)) c='\'';
            else if(first<in&&x->bytes[first]=='#') {
                size_t j=first+1U;c=0;numeric=true;
                if(j<in&&x->bytes[j]=='x') { base=16U;++j; }
                if(j==in) return false;
                for(;j<in;++j) {
                    unsigned digit=(uint8_t)x->bytes[j];
                    if(digit>='0'&&digit<='9') digit-='0';
                    else if(base==16U&&digit>='a'&&digit<='f') digit=digit-'a'+10U;
                    else if(base==16U&&digit>='A'&&digit<='F') digit=digit-'A'+10U;
                    else return false;
                    if(digit>=base||c>(UINT32_C(0x10ffff)-digit)/base) return false;
                    c=c*base+digit;
                }
            } else return false;
            ++in;
            if(!xv_codepoint(c)) return false;
            if(c<128U) x->bytes[out++]=(char)c;
            else if(c<2048U) { x->bytes[out++]=(char)(0xc0U|(c>>6));x->bytes[out++]=(char)(0x80U|(c&63U)); }
            else if(c<65536U) { x->bytes[out++]=(char)(0xe0U|(c>>12));x->bytes[out++]=(char)(0x80U|((c>>6)&63U));x->bytes[out++]=(char)(0x80U|(c&63U)); }
            else { x->bytes[out++]=(char)(0xf0U|(c>>18));x->bytes[out++]=(char)(0x80U|((c>>12)&63U));x->bytes[out++]=(char)(0x80U|((c>>6)&63U));x->bytes[out++]=(char)(0x80U|(c&63U)); }
            (void)numeric;
            continue;
        }
        if(c<128U) { if(!xv_codepoint(c)||c=='<') return false;x->bytes[out++]=(char)c;continue; }
        if(c>=0xc2U&&c<=0xdfU) { c&=31U;continuation=1U; }
        else if(c>=0xe0U&&c<=0xefU) { c&=15U;continuation=2U; }
        else if(c>=0xf0U&&c<=0xf4U) { c&=7U;continuation=3U; }
        else return false;
        {
            unsigned j;size_t first=in-1U;
            if(continuation>end-in) return false;
            for(j=0;j<continuation;++j) {
                uint8_t b=(uint8_t)x->bytes[in++];if((b&0xc0U)!=0x80U) return false;c=(c<<6)|(b&63U);
            }
            if(!xv_codepoint(c)||(continuation==1U&&c<128U)||
               (continuation==2U&&c<2048U)||(continuation==3U&&c<65536U)) return false;
            memmove(x->bytes+out,x->bytes+first,continuation+1U);out+=continuation+1U;
        }
    }
    *length=(uint32_t)(out-start);return true;
}
static bool xv_xml_node(xv_xml *x,unsigned depth,uint32_t *index) {
    size_t first;uint32_t node,last=0U;bool self=false; x->steps++;
    if(depth>64U||x->count==XV_NODES||x->steps>XV_STEPS||xv_stop(x->pd)||
       !xv_xml_misc(x)||x->pos>=x->size||x->bytes[x->pos++]!='<') return false;
    first=x->pos;
    while(x->pos<x->size&&x->bytes[x->pos]!='>'&&!xv_space((uint8_t)x->bytes[x->pos])&&x->bytes[x->pos]!='/') ++x->pos;
    if(x->pos-first>32U) return false;
    if(x->count==x->capacity) {
        size_t cap=x->capacity?x->capacity*2U:64U;
        if(cap>XV_NODES) cap=XV_NODES;
        if(!xv_grow(x->view,(void **)&x->nodes,x->capacity*sizeof(*x->nodes),cap*sizeof(*x->nodes))) return false;
        x->capacity=cap;
    }
    node=(uint32_t)x->count++;x->nodes[node].tag=xv_tag_name(x->bytes+first,x->pos-first);
    if(x->nodes[node].tag==XT_BAD) return false;
    while(x->pos<x->size&&xv_space((uint8_t)x->bytes[x->pos])) ++x->pos;
    if(x->pos<x->size&&x->bytes[x->pos]=='/') { self=true;++x->pos; }
    if(x->pos>=x->size||x->bytes[x->pos++]!='>') return false;
    *index=node+1U;
    if(self) return true;
    first=x->pos;
    while(x->pos<x->size&&x->bytes[x->pos]!='<') ++x->pos;
    x->nodes[node].text=(uint32_t)first;
    if(!xv_text(x,first,x->pos,&x->nodes[node].length)) return false;
    for(;;) {
        uint32_t child;
        if(!xv_xml_misc(x)||x->size-x->pos<2U) return false;
        if(x->bytes[x->pos]=='<'&&x->bytes[x->pos+1U]=='/') break;
        if(!xv_xml_node(x,depth+1U,&child)) return false;
        if(last) x->nodes[last-1U].next=child;else x->nodes[node].child=child;
        last=child;
    }
    x->pos+=2U;first=x->pos;
    while(x->pos<x->size&&x->bytes[x->pos]!='>'&&!xv_space((uint8_t)x->bytes[x->pos])) ++x->pos;
    if(xv_tag_name(x->bytes+first,x->pos-first)!=x->nodes[node].tag) return false;
    while(x->pos<x->size&&xv_space((uint8_t)x->bytes[x->pos])) ++x->pos;
    if(x->pos>=x->size||x->bytes[x->pos++]!='>') return false;
    return true;
}
static bool xv_text_equal(const xv_xml *x,uint32_t node,const char *s) {
    const xv_node *n=&x->nodes[node-1U];size_t len=strlen(s);
    return n->length==len&&!memcmp(x->bytes+n->text,s,len);
}
static uint32_t xv_value(const xv_xml *x,uint32_t node) {
    const xv_node *n;
    if(!node) return 0;n=&x->nodes[node-1U];
    return n->tag==XT_VALUE&&n->child?n->child:node;
}
static uint32_t xv_lookup(const xv_xml *x,uint32_t object,const char *key) {
    uint32_t c;object=xv_value(x,object);
    if(!object||x->nodes[object-1U].tag!=XT_STRUCT) return 0;
    for(c=x->nodes[object-1U].child;c;c=x->nodes[c-1U].next) {
        uint32_t name=x->nodes[c-1U].child;
        if(xv_text_equal(x,name,key)) return x->nodes[name-1U].next;
    }
    return 0;
}
static bool xv_xml_validate(xv_xml *x,uint32_t index) {
    const xv_node *n=&x->nodes[index-1U];uint32_t c;
    if(++x->steps>XV_STEPS||xv_stop(x->pd)) return false;
    if(n->child) {
        size_t i;for(i=0;i<n->length;++i) if(!xv_space((uint8_t)x->bytes[n->text+i])) return false;
    }
    switch(n->tag) {
    case XT_VALUE:
        if(n->child && (x->nodes[n->child-1U].next||x->nodes[n->child-1U].tag==XT_VALUE||
           x->nodes[n->child-1U].tag==XT_MEMBER||x->nodes[n->child-1U].tag==XT_NAME||
           x->nodes[n->child-1U].tag==XT_DATA)) return false;
        break;
    case XT_STRUCT:
        for(c=n->child;c;c=x->nodes[c-1U].next) {
            uint32_t p,name=x->nodes[c-1U].child;
            if(x->nodes[c-1U].tag!=XT_MEMBER||!name) return false;
            for(p=n->child;p!=c;p=x->nodes[p-1U].next) {
                const xv_node *a=&x->nodes[x->nodes[p-1U].child-1U],*b=&x->nodes[name-1U];
                if(++x->steps>XV_STEPS||xv_stop(x->pd)||
                   (a->length==b->length&&!memcmp(x->bytes+a->text,x->bytes+b->text,a->length))) return false;
            }
        }
        break;
    case XT_MEMBER:
        c=n->child;
        if(!c||x->nodes[c-1U].tag!=XT_NAME||!x->nodes[c-1U].length||
           !(c=x->nodes[c-1U].next)||x->nodes[c-1U].tag!=XT_VALUE||x->nodes[c-1U].next) return false;
        break;
    case XT_ARRAY:
        if(!n->child||x->nodes[n->child-1U].tag!=XT_DATA||x->nodes[n->child-1U].next) return false;
        break;
    case XT_DATA:
        for(c=n->child;c;c=x->nodes[c-1U].next) if(x->nodes[c-1U].tag!=XT_VALUE) return false;
        break;
    default: if(n->child) return false;break;
    }
    if(!n->child && (n->tag==XT_STRUCT||n->tag==XT_ARRAY||n->tag==XT_MEMBER)) {
        size_t i;for(i=0;i<n->length;++i)if(!xv_space((uint8_t)x->bytes[n->text+i]))return false;
    }
    for(c=n->child;c;c=x->nodes[c-1U].next) if(!xv_xml_validate(x,c)) return false;
    return true;
}
static bool xv_string(const xv_xml *x,uint32_t index,char *out,size_t capacity) {
    const xv_node *n;index=xv_value(x,index);if(!index) return false;n=&x->nodes[index-1U];
    if((n->tag!=XT_VALUE&&n->tag!=XT_STRING)||n->child||n->length>=capacity) return false;
    memcpy(out,x->bytes+n->text,n->length);out[n->length]=0;return true;
}
static bool xv_decimal(const xv_xml *x,uint32_t index,uint64_t *number) {
    const xv_node *n;size_t i;index=xv_value(x,index);if(!index) return false;n=&x->nodes[index-1U];
    if(n->child || (n->tag!=XT_VALUE&&n->tag!=XT_STRING&&n->tag!=XT_INT&&n->tag!=XT_I4&&n->tag!=XT_I8)||
       !n->length||n->length>20U) return false;
    for(i=0;i<n->length;++i) if(x->bytes[n->text+i]<'0'||x->bytes[n->text+i]>'9') return false;
    return xv_number(x->bytes+n->text,n->length,10U,number);
}
static bool xv_id(const char *id,char out[96]) {
    uint64_t number;size_t n=strlen(id),i;
    if(n<5U||n>24U||memcmp(id,"Ref",3U)||(id[3]!=':'&&id[3]!='_')) return false;
    for(i=4U;i<n;++i) if(id[i]<'0'||id[i]>'9') return false;
    if(!xv_number(id+4U,n-4U,10U,&number)) return false;
    (void)xx_rt_snprintf(out,96U,"Ref_%llu.img",(unsigned long long)number);return true;
}
static int xv_disk_compare(const void *a,const void *b) {
    return strcmp(((const xv_disk *)a)->id,((const xv_disk *)b)->id);
}
static bool xv_manifest(xx_io_device *dev,xv_view *v,const xv_tar *tar,xx_pd_struct *pd) {
    xv_xml x;uint32_t root,objects,c,version;uint64_t vsn;bool ok=false;
    xx_mem_zero(&x,sizeof(x));x.view=v;x.pd=pd;
    if(tar->type||tar->header||!tar->size||tar->size>XV_XML) return false;
    x.size=(size_t)tar->size;x.bytes=(char *)xv_alloc(v,x.size+1U);if(!x.bytes) return false;
    if(!xv_rel(dev,v,tar->offset,x.bytes,x.size,pd)) goto done;
    if(x.size>=3U&&!memcmp(x.bytes,"\xef\xbb\xbf",3U)) x.pos=3U;
    if(x.size-x.pos>=6U&&!memcmp(x.bytes+x.pos,"<?xml ",6U)) {
        size_t first=x.pos;x.pos+=6U;
        while(x.size-x.pos>=2U&&memcmp(x.bytes+x.pos,"?>",2U)&&x.pos-first<256U) ++x.pos;
        if(x.size-x.pos<2U||x.pos-first==256U) goto done;x.pos+=2U;
    }
    if(!xv_xml_node(&x,0U,&root)||!xv_xml_misc(&x)||x.pos!=x.size||
       x.nodes[root-1U].tag!=XT_VALUE||!xv_xml_validate(&x,root)) goto done;
    version=xv_lookup(&x,root,"version");
    if(!xv_decimal(&x,xv_lookup(&x,version,"export_vsn"),&vsn)||!vsn||vsn>UINT32_MAX) goto done;
    objects=xv_value(&x,xv_lookup(&x,root,"objects"));
    if(!objects||x.nodes[objects-1U].tag!=XT_ARRAY) goto done;
    objects=x.nodes[objects-1U].child;
    for(c=x.nodes[objects-1U].child;c;c=x.nodes[c-1U].next) {
        char cls[80],id[80];uint32_t snapshot;uint64_t size;size_t i;
        if(xv_stop(pd)||!xv_string(&x,xv_lookup(&x,c,"class"),cls,sizeof(cls))||
           !xv_string(&x,xv_lookup(&x,c,"id"),id,sizeof(id))) goto done;
        snapshot=xv_value(&x,xv_lookup(&x,c,"snapshot"));
        if(!snapshot||x.nodes[snapshot-1U].tag!=XT_STRUCT) goto done;
        if(strcmp(cls,"VDI")) continue;
        if(!xv_decimal(&x,xv_lookup(&x,snapshot,"virtual_size"),&size)||!size||size>XV_MAX_DISK||v->count==XV_DISKS) goto done;
        if(v->count==v->capacity) {
            size_t cap=v->capacity?v->capacity*2U:8U;
            if(!xv_grow(v,(void **)&v->disks,v->capacity*sizeof(*v->disks),cap*sizeof(*v->disks))) goto done;
            v->capacity=cap;
        }
        if(!xv_id(id,v->disks[v->count].name)) goto done;
        for(i=0;i<v->count;++i) if(!strcmp(v->disks[i].id,id)||!strcmp(v->disks[i].name,v->disks[v->count].name)) goto done;
        memcpy(v->disks[v->count].id,id,strlen(id)+1U);v->disks[v->count].size=size;++v->count;
    }
    qsort(v->disks,v->count,sizeof(*v->disks),xv_disk_compare);ok=!xv_stop(pd);
done:
    xv_drop(v,x.nodes,x.capacity*sizeof(*x.nodes));xv_drop(v,x.bytes,x.size+1U);return ok;
}
static xv_disk *xv_find_disk(xv_view *v,const char *id) {
    size_t lo=0,hi=v->count;
    while(lo<hi) { size_t mid=lo+(hi-lo)/2U;int c=strcmp(v->disks[mid].id,id);
        if(c<0) lo=mid+1U;else if(c>0) hi=mid;else return &v->disks[mid]; }
    return NULL;
}
static const xv_tar *xv_find_tar(const xv_tar *items,size_t count,const char *name) {
    size_t lo=0,hi=count;
    while(lo<hi) { size_t mid=lo+(hi-lo)/2U;int c=strcmp(items[mid].name,name);
        if(c<0) lo=mid+1U;else if(c>0) hi=mid;else return &items[mid]; }
    return NULL;
}
static bool xv_chunk_name(const char *path,char id[80],uint64_t *index,unsigned *hash) {
    const char *slash=strchr(path,'/'),*p;size_t n,len;
    if(!slash||strchr(slash+1U,'/')||(n=(size_t)(slash-path))>=80U) return false;
    memcpy(id,path,n);id[n]=0;p=slash+1U;len=strspn(p,"0123456789");
    if(!len||len>20U||!xv_number(p,len,10U,index)) return false;
    *hash=0U;
    if(!p[len]) return true;
    if(!strcmp(p+len,".checksum")) *hash=1U;
    else if(!strcmp(p+len,".xxhash")) *hash=2U;
    else return false;
    return true;
}
static int xv_hex(unsigned c) {
    if(c>='0'&&c<='9')return (int)(c-'0');if(c>='a'&&c<='f')return (int)(c-'a'+10U);
    if(c>='A'&&c<='F')return (int)(c-'A'+10U);return -1;
}
static int xv_chunk_compare(const void *a,const void *b) {
    uint64_t x=((const xv_chunk *)a)->physical,y=((const xv_chunk *)b)->physical;
    return x<y?-1:x>y?1:0;
}
static bool xv_index(xx_io_device *dev,xv_view *v,const xv_tar *items,size_t count,xx_pd_struct *pd) {
    size_t i,kept=0;
    for(i=0;i<count;++i) {
        const xv_tar *t=&items[i],*cs;char id[80],name[280];uint64_t physical;unsigned hash; xv_disk *disk;
        if(xv_stop(pd)) return false;
        if(!strcmp(t->name,"ova.xml")) continue;
        if(t->type==5U) {
            size_t n=strlen(t->name);if(n&&t->name[n-1U]=='/')--n;
            if(n>=sizeof(id)) return false;memcpy(id,t->name,n);id[n]=0;
            if(!xv_find_disk(v,id)) return false;continue;
        }
        if(!xv_chunk_name(t->name,id,&physical,&hash)||!(disk=xv_find_disk(v,id))) return false;
        if(hash) {
            size_t n=strlen(t->name)-(hash==1U?9U:7U);
            memcpy(name,t->name,n);name[n]=0;
            if(!xv_find_tar(items,count,name)) return false;continue;
        }
        if(t->size>XV_CHUNK||physical>(disk->size+XV_CHUNK-1U)/XV_CHUNK+XV_ENTRIES) return false;
        (void)xx_rt_snprintf(name,sizeof(name),"%s.checksum",t->name);cs=xv_find_tar(items,count,name);hash=1U;
        if(cs) {
            (void)xx_rt_snprintf(name,sizeof(name),"%s.xxhash",t->name);
            if(xv_find_tar(items,count,name)) return false;
        } else { (void)xx_rt_snprintf(name,sizeof(name),"%s.xxhash",t->name);cs=xv_find_tar(items,count,name);hash=2U; }
        if(!cs||cs->type||cs->size!=(hash==1U?40U:16U)||
           cs->header!=t->offset+((t->size+511U)&~UINT64_C(511))) return false;
        if(disk->count==disk->capacity) {
            size_t cap=disk->capacity?disk->capacity*2U:8U;
            if(!xv_grow(v,(void **)&disk->chunks,disk->capacity*sizeof(*disk->chunks),cap*sizeof(*disk->chunks))) return false;
            disk->capacity=cap;
        }
        {
            char digest[40];unsigned j; xv_chunk *ch=&disk->chunks[disk->count];
            if(!xv_rel(dev,v,cs->offset,digest,(size_t)cs->size,pd)) return false;
            for(j=0;j<(unsigned)cs->size/2U;++j) {
                int a=xv_hex((uint8_t)digest[j*2U]),b=xv_hex((uint8_t)digest[j*2U+1U]);
                if(a<0||b<0) return false;ch->digest[j]=(uint8_t)((unsigned)a*16U+(unsigned)b);
            }
            ch->physical=physical;ch->offset=t->offset;ch->size=(uint32_t)t->size;ch->hash=hash;
            ++disk->count;disk->stored+=t->size;
        }
    }
    for(i=0;i<v->count;++i) {
        xv_disk *disk=&v->disks[i];size_t j,skips=0;uint64_t chunks=(disk->size+XV_CHUNK-1U)/XV_CHUNK;
        if(!disk->count) continue; /* ISO/metadata-only references have no disk payload. */
        qsort(disk->chunks,disk->count,sizeof(*disk->chunks),xv_chunk_compare);
        for(j=0;j<disk->count;++j) {
            xv_chunk *ch=&disk->chunks[j];uint64_t expected;
            if(xv_stop(pd)||(j&&(ch->physical==disk->chunks[j-1U].physical||ch->offset<=disk->chunks[j-1U].offset))||ch->physical<skips) return false;
            ch->logical=ch->physical-skips;
            if(ch->logical>=chunks) return false;
            if(!ch->size) { ++skips;continue; }
            expected=ch->logical+1U==chunks?disk->size-ch->logical*XV_CHUNK:XV_CHUNK;
            if(ch->size!=expected&&!(ch->logical+1U==chunks&&ch->size==XV_CHUNK)) return false;
        }
        if(!disk->chunks[disk->count-1U].size||disk->chunks[disk->count-1U].logical+1U!=chunks) return false;
        v->disk_bytes+=disk->size;
        if(kept!=i) { v->disks[kept]=*disk;xx_mem_zero(disk,sizeof(*disk)); }
        ++kept;
    }
    v->count=kept;return !xv_stop(pd);
}
static xv_view *xv_parse(Abstractformat *f,xx_pd_struct *pd) {
    xv_view *v; xv_tar *items=NULL;size_t count=0,capacity=0;const xv_tar *manifest;int64_t total;
    if(!f||!f->device||f->base_address<0||xv_stop(pd)||(total=xx_io_total_size(f->device))<f->base_address||
       total-f->base_address<1024) return NULL;
    v=(xv_view *)xx_mem_calloc(1U,sizeof(*v));if(!v) return NULL;
    v->refs=1U;v->memory=sizeof(*v);v->base=f->base_address;v->size=(uint64_t)(total-v->base);
    if(!xv_tar_scan(f->device,v,&items,&count,&capacity,pd)||
       !(manifest=xv_find_tar(items,count,"ova.xml"))||!xv_manifest(f->device,v,manifest,pd)||
       !xv_index(f->device,v,items,count,pd)) goto bad;
    xv_drop(v,items,capacity*sizeof(*items));return v;
bad:xv_drop(v,items,capacity*sizeof(*items));xv_release(v);return NULL;
}
/* Original streaming seed-zero XXH64, from the published algorithm. */
#define XV_P1 UINT64_C(0x9e3779b185ebca87)
#define XV_P2 UINT64_C(0xc2b2ae3d27d4eb4f)
#define XV_P3 UINT64_C(0x165667b19e3779f9)
#define XV_P4 UINT64_C(0x85ebca77c2b2ae63)
#define XV_P5 UINT64_C(0x27d4eb2f165667c5)
typedef struct xv_xxh_s { uint64_t lanes[4],length;uint8_t tail[32];size_t used; } xv_xxh;
static uint64_t xv_le64(const uint8_t *p) {
    uint64_t x=0;unsigned i;for(i=0;i<8U;++i)x|=(uint64_t)p[i]<<(i*8U);return x;
}
static uint64_t xv_rotate(uint64_t x,unsigned n) { return (x<<n)|(x>>(64U-n)); }
static uint64_t xv_round(uint64_t x,uint64_t lane) { return xv_rotate(x+lane*XV_P2,31U)*XV_P1; }
static void xv_xxh_init(xv_xxh *h) {
    xx_mem_zero(h,sizeof(*h));h->lanes[0]=XV_P1;h->lanes[0]+=XV_P2;
    h->lanes[1]=XV_P2;h->lanes[3]=UINT64_C(0)-XV_P1;
}
static void xv_xxh_stripe(xv_xxh *h,const uint8_t *p) {
    unsigned i;for(i=0;i<4U;++i)h->lanes[i]=xv_round(h->lanes[i],xv_le64(p+i*8U));
}
static void xv_xxh_update(xv_xxh *h,const uint8_t *p,size_t n) {
    h->length+=n;
    if(h->used) {
        size_t take=32U-h->used;if(take>n)take=n;
        memcpy(h->tail+h->used,p,take);h->used+=take;p+=take;n-=take;
        if(h->used!=32U)return;xv_xxh_stripe(h,h->tail);h->used=0;
    }
    while(n>=32U) {xv_xxh_stripe(h,p);p+=32U;n-=32U;}
    if(n)memcpy(h->tail,p,n);h->used=n;
}
static uint64_t xv_xxh_final(const xv_xxh *h) {
    uint64_t x;size_t at=0;unsigned i;
    if(h->length>=32U) {
        x=xv_rotate(h->lanes[0],1U)+xv_rotate(h->lanes[1],7U)+
          xv_rotate(h->lanes[2],12U)+xv_rotate(h->lanes[3],18U);
        for(i=0;i<4U;++i)x=(x^xv_round(0U,h->lanes[i]))*XV_P1+XV_P4;
    } else x=XV_P5;
    x+=h->length;
    while(h->used-at>=8U) {x=xv_rotate(x^xv_round(0U,xv_le64(h->tail+at)),27U)*XV_P1+XV_P4;at+=8U;}
    if(h->used-at>=4U) {
        const uint8_t *p=h->tail+at;uint64_t n=(uint64_t)p[0]|((uint64_t)p[1]<<8)|((uint64_t)p[2]<<16)|((uint64_t)p[3]<<24);
        x=xv_rotate(x^n*XV_P1,23U)*XV_P2+XV_P3;at+=4U;
    }
    while(at<h->used)x=xv_rotate(x^(uint64_t)h->tail[at++]*XV_P5,11U)*XV_P1;
    x^=x>>33;x*=XV_P2;x^=x>>29;x*=XV_P3;x^=x>>32;return x;
}
static void xv_destroy_format(Abstractformat *f) { xx_xva_destroy((xx_xva *)f); }
void xx_xva_init(xx_xva *v,xx_io_device *d,int64_t base) {
    if(!v) return;xx_mem_zero(v,sizeof(*v));xx_format_init(&v->format,d,base);
    v->format.file_type=XV_TYPE;v->format.format_type=XX_TYPE_ARCHIVE;v->format.is_archive=true;
    xx_format_set_mime_type(&v->format,"application/x-xen-xva");xx_format_set_extension(&v->format,"xva");
    v->format.check_is_valid=xx_xva_check_is_valid;v->format.handle_base_info=xx_xva_handle_base_info;
    v->format.get_format_size=xx_xva_get_format_size;v->format.get_number_of_archive_records=xx_xva_get_number_of_archive_records;
    v->format.create_archive_records_reading=xx_xva_create_archive_records_reading;
    v->format.get_current_archive_record=xx_xva_get_current_archive_record;
    v->format.archive_record_move_to_next=xx_xva_archive_record_move_to_next;
    v->format.unpack_current_archive_record=xx_xva_unpack_current_archive_record;
    v->format.free_archive_records_reading=xx_xva_free_archive_records_reading;v->format.destroy=xv_destroy_format;
}
xx_xva *xx_xva_create(xx_io_device *d,int64_t base) {
    xx_xva *v=(xx_xva *)xx_mem_alloc(sizeof(*v));if(v)xx_xva_init(v,d,base);return v;
}
void xx_xva_destroy(xx_xva *v) {
    if(!v)return;xv_release((xv_view *)v->internal);v->internal=NULL;xx_format_cleanup_extra_parameters(&v->format);
}
void xx_xva_free(xx_xva *v) {if(v){xx_xva_destroy(v);xx_mem_free(v);}}
bool xx_xva_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {
    xv_view *v=xv_parse(f,pd);if(!v)return false;xv_release(v);return true;
}
bool xx_xva_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {
    xx_xva *container=(xx_xva *)f;xv_view *v;
    if(!f||xv_stop(pd))return false;
    if(f->base_info_handled&&container->internal)return f->is_valid;
    v=xv_parse(f,pd);if(!v){f->is_valid=false;f->base_info_handled=false;return false;}
    xv_release((xv_view *)container->internal);container->internal=v;container->number_of_records=v->count;
    container->total_disk_size=v->disk_bytes;container->chunk_size=XV_CHUNK;
    f->number_of_archive_records=v->count;f->format_size=(int64_t)v->size;
    f->overlay_offset=-1;f->overlay_size=0;f->is_valid=true;f->base_info_handled=true;return true;
}
int64_t xx_xva_get_format_size(Abstractformat *f,xx_pd_struct *pd) {return f&&xx_xva_handle_base_info(f,pd)?f->format_size:-1;}
uint64_t xx_xva_get_number_of_archive_records(Abstractformat *f,xx_pd_struct *pd) {
    return f&&xx_xva_handle_base_info(f,pd)?((xx_xva *)f)->number_of_records:0U;
}
static bool xv_record(xx_archive_record *r,const xv_view *v,size_t index) {
    const xv_disk *disk=&v->disks[index];
    xx_archive_record_cleanup(r);xx_archive_record_init(r);r->header_offset=v->base;r->header_size=512;
    r->data_offset=-1;r->compressed_size=(int64_t)disk->stored;
    return xx_archive_record_set_original_name(r,disk->name)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,disk->size)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,disk->stored)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,0U)&&
        xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false);
}
static void xv_cursor_free(void *p) {
    xv_cursor *c=(xv_cursor *)p;if(c){xv_release(c->view);xx_mem_free(c);}
}
xx_archive_record_state *xx_xva_create_archive_records_reading(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    xx_archive_record_state *s;xv_cursor *c;xv_view *v;size_t i;
    if(!f||!xx_xva_handle_base_info(f,pd))return NULL;v=(xv_view *)((xx_xva *)f)->internal;
    c=(xv_cursor *)xx_mem_calloc(1U,sizeof(*c));s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if(!c||!s){xx_mem_free(c);xx_mem_free(s);return NULL;}
    ++v->refs;c->view=v;xx_archive_record_state_init(s,f);s->internal_state=c;s->free_internal=xv_cursor_free;
    s->total_records=(int64_t)v->count;
    if(options)for(i=0;i<options->count;++i){
        const xx_meta *m=(const xx_meta *)xx_list_at(options,i);xx_meta copy;
        if(!m)continue;xx_meta_init(&copy,m->meta_id);
        if(!xx_var_copy(&copy.var,&m->var)||!xx_list_append(&s->options,&copy)){
            xx_meta_cleanup(&copy);xx_archive_record_state_free(s);return NULL;
        }
    }
    if(v->count&&!xv_record(&s->current_record,v,0U)){xx_archive_record_state_free(s);return NULL;}
    s->has_record=v->count!=0U;s->current_index=s->has_record?0:-1;return s;
}
const xx_archive_record *xx_xva_get_current_archive_record(Abstractformat *f,xx_archive_record_state *s) {
    return f&&s&&s->format==f&&s->has_record?&s->current_record:NULL;
}
bool xx_xva_archive_record_move_to_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd) {
    xv_cursor *c;
    if(!f||!s||s->format!=f||!s->has_record||!(c=(xv_cursor *)s->internal_state)||xv_stop(pd))return false;
    if(c->index+1U>=c->view->count){xx_archive_record_cleanup(&s->current_record);xx_archive_record_init(&s->current_record);s->has_record=false;return false;}
    if(!xv_record(&s->current_record,c->view,c->index+1U)){s->has_record=false;return false;}
    ++c->index;++s->current_index;return true;
}
static bool xv_limits(Abstractformat *f,const xx_archive_record_state *s,const xv_cursor *c,size_t *copy) {
    uint64_t size=c->view->disks[c->index].size;
    const xx_var *max=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_MEMORY_LIMIT);
    /* Padded final chunks also need transfer workspace when logical size is tiny. */
    *copy=XV_COPY;
    return (!max||size<=xx_var_get_u64(max))&&
        (!mem||c->view->memory+sizeof(*c)+*copy+sizeof(xx_hash_context)+sizeof(xv_xxh)<=xx_var_get_u64(mem));
}
static bool xv_write(xx_io_device *d,const uint8_t *p,size_t n,xx_pd_struct *pd) {
    size_t done=0;while(done<n&&!xv_stop(pd)){
        ssize_t got=xx_io_write(d,p+done,n-done);
        if(got<=0||(size_t)got>n-done||xv_stop(pd))return false;done+=(size_t)got;
    }return done==n&&!xv_stop(pd);
}
bool xx_xva_extract_record_to_device(Abstractformat *f,xx_archive_record_state *s,xx_io_device *dst,xx_pd_struct *pd) {
    xv_cursor *c;const xv_disk *disk;uint8_t *buf;size_t copy,i;uint64_t written=0;bool ok=false;
    if(!f||!f->device||dst==f->device||!s||s->format!=f||!s->has_record||
       !(c=(xv_cursor *)s->internal_state)||c->index>=c->view->count||xv_stop(pd)||!xv_limits(f,s,c,&copy))return false;
    disk=&c->view->disks[c->index];buf=(uint8_t *)xx_mem_alloc(copy);if(!buf)return false;
    for(i=0;i<disk->count;++i){
        const xv_chunk *ch=&disk->chunks[i];uint64_t logical=ch->logical*XV_CHUNK;uint32_t at=0;
        xx_hash_context sha;xv_xxh xxh;uint8_t digest[20];unsigned j;
        if(xv_stop(pd))goto done;
        if(ch->size){
            if(logical<written)goto done;
            if(dst){
                xx_mem_zero(buf,copy);
                while(written<logical){size_t part=(size_t)(logical-written<copy?logical-written:copy);
                    if(!xv_write(dst,buf,part,pd))goto done;written+=part;}
            }else written=logical;
        }
        if(ch->hash==1U){if(!xx_hash_init(&sha,XX_HASH_SHA1))goto done;}else xv_xxh_init(&xxh);
        while(at<ch->size){
            size_t part=ch->size-at<copy?ch->size-at:copy,out=part;
            uint64_t available=disk->size-logical;
            uint64_t remaining=at<available?available-at:0U;
            if(!xv_rel(f->device,c->view,ch->offset+at,buf,part,pd))goto done;
            if(ch->hash==1U)xx_hash_update(&sha,buf,part);else xv_xxh_update(&xxh,buf,part);
            if(remaining<out)out=(size_t)remaining;
            if(out<part&&!xv_zero(buf+out,part-out))goto done;
            if(dst&&out&&!xv_write(dst,buf,out,pd))goto done;
            written+=out;at+=(uint32_t)part;
        }
        if(ch->hash==1U){if(!xx_hash_final(&sha,digest,sizeof(digest)))goto done;}
        else {uint64_t hash=xv_xxh_final(&xxh);for(j=0;j<8U;++j)digest[j]=(uint8_t)(hash>>((7U-j)*8U));}
        if(memcmp(digest,ch->digest,ch->hash==1U?20U:8U)||xv_stop(pd))goto done;
    }
    ok=written==disk->size&&!xv_stop(pd);
done:xx_mem_free(buf);return ok;
}
static char xv_fold(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static bool xv_equal(const char *a,const char *b){while(*a&&xv_fold(*a)==xv_fold(*b)){++a;++b;}return *a==*b;}
static xx_io_device *xv_stage(const char *dest,char **path){
    size_t i,parent=0;unsigned attempt;char *dir=xx_str_dup(dest);*path=NULL;if(!dir)return NULL;
    for(i=0;dir[i];++i)if(dir[i]=='/'||dir[i]=='\\')parent=i+1U;dir[parent]=0;
    for(attempt=0;attempt<128U;++attempt){
        char suffix[40];char *candidate;xx_io_device *d;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),".xx_xva.tmp.%u",attempt);candidate=xx_str_concat(dir,suffix);if(!candidate)break;
        if(xv_equal(candidate,dest)){xx_str_free(candidate);continue;}
        d=xx_io_file_open(candidate,"wbx");if(d){*path=candidate;xx_str_free(dir);return d;}xx_str_free(candidate);
    }xx_str_free(dir);return NULL;
}
bool xx_xva_unpack_current_archive_record(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd){
    xv_cursor *c;const xx_var *option,*ov;const char *base=NULL;char *owned=NULL,*path=NULL,*stage=NULL;
    size_t copy;bool ok=false,overwrite;
    if(!f||!s||s->format!=f||!s->has_record||!(c=(xv_cursor *)s->internal_state)||
       c->index>=c->view->count||xv_stop(pd)||!xv_limits(f,s,c,&copy))return false;
    option=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_UNPACK_PATH);
    ov=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_OVERWRITE);overwrite=ov&&xx_var_get_bool(ov);
    if(!option)return xx_xva_extract_record_to_device(f,s,NULL,pd);
    if(option->type==XX_VAR_TYPE_STRING||option->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(option);
    else if(option->type==XX_VAR_TYPE_WSTRING||option->type==XX_VAR_TYPE_WSTRING_VIEW){owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option));base=owned;}
    if(!base)goto done;
    path=*base&&base[strlen(base)-1U]!='/'&&base[strlen(base)-1U]!='\\'?
        xx_str_concat3(base,"/",c->view->disks[c->index].name):xx_str_concat(base,c->view->disks[c->index].name);
    if(!path||(!overwrite&&xx_io_file_exists_a(path))||!xx_store_create_dirs_a(path,false)||xv_stop(pd))goto done;
    {xx_io_device *out=xv_stage(path,&stage);if(!out)goto done;
        ok=xx_xva_extract_record_to_device(f,s,out,pd);if(xx_io_close(out)!=0)ok=false;}
    if(ok&&!xv_stop(pd))ok=xx_io_file_replace_a(stage,path,overwrite);else ok=false;
done:if(stage){if(!ok)(void)xx_io_file_remove_a(stage);xx_str_free(stage);}xx_str_free(path);xx_str_free(owned);return ok;
}
void xx_xva_free_archive_records_reading(Abstractformat *f,xx_archive_record_state *s){(void)f;xx_archive_record_state_free(s);}
