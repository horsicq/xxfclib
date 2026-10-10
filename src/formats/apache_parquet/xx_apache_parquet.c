/* SPDX-License-Identifier: MIT
 * Independent framing implementation from apache/parquet-format parquet.thrift
 * and apache/thrift doc/specs/thrift-compact-protocol.md.
 * Flat required/optional primitive schemas. Encoded chunks stay encoded.
 */
#include "xxfclib/formats/apache_parquet/xx_apache_parquet.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../common/xx_binary_cursor.h"
#include "xxfclib/global/xx_global.h"

typedef struct pq_cursor { const uint8_t *p; size_t at,end; unsigned *work; xx_pd_struct *pd;
    Abstractformat *f; uint64_t base; uint8_t *buffer; size_t capacity,begin,count;
} pq_cursor;
typedef struct pq_column { int64_t type,codec,values,raw,packed,data,dictionary; unsigned encodings; } pq_column;
typedef struct pq_info { char names[64][128]; int64_t types[64]; uint64_t values[64]; unsigned columns,chunks,pages,work; uint64_t rows,crc_bytes; } pq_info;
/* The compact page-header budget is semantic; its I/O staging may be one byte. */
static bool pq_copy(pq_cursor *c,void *target,size_t n) {
    uint8_t *out=(uint8_t *)target;
    if(n>c->end-c->at || binary_stop(c->pd)) return false;
    if(c->p) { xx_rt_memcpy(out,c->p+c->at,n); c->at+=n; return true; }
    while(n) { size_t part;
        if(c->at<c->begin || c->at-c->begin>=c->count) {
            c->count=c->end-c->at>c->capacity ? c->capacity:c->end-c->at;
            if(binary_stop(c->pd) || !pm_read(c->f,(int64_t)(c->base+c->at),c->buffer,c->count)) return false;
            c->begin=c->at;
        }
        part=c->count-(c->at-c->begin); if(part>n) part=n;
        xx_rt_memcpy(out,c->buffer+c->at-c->begin,part); out+=part;c->at+=part;n-=part;
    } return true;
}
static bool pq_byte(pq_cursor *c,uint8_t *v) { if(binary_stop(c->pd) || c->at==c->end || ++*c->work>2000000) return false; return pq_copy(c,v,1); }
static bool pq_var(pq_cursor *c,uint64_t *v) {
    unsigned i; uint64_t n=0; uint8_t b;
    for(i=0;i<10;++i) { if(!pq_byte(c,&b) || (i==9 && b>1)) return false; n|=(uint64_t)(b&127)<<(7*i); if(!(b&128)) { *v=n; return true; } } return false;
}
static bool pq_int(pq_cursor *c,unsigned type,unsigned expected,int64_t *v) {
    uint64_t u; if(type!=expected || !pq_var(c,&u)) return false; *v=(int64_t)(u>>1)^-(int64_t)(u&1);
    return (expected!=4 || (*v>=INT16_MIN && *v<=INT16_MAX)) && (expected!=5 || (*v>=INT32_MIN && *v<=INT32_MAX));
}
static bool pq_field(pq_cursor *c,int *previous,int *id,unsigned *type,uint32_t *seen) {
    uint8_t b; int64_t explicit_id;
    if(!pq_byte(c,&b)) { return false; } *type=b&15; if(!b) { *id=0; return true; } if(!*type || *type>12) return false;
    if(b>>4) *id=*previous+(b>>4); else { if(!pq_int(c,4,4,&explicit_id) || explicit_id<=0) return false; *id=(int)explicit_id; }
    if(*id<=0 || *id>32767) { return false; } *previous=*id;
    if(*id<32) { uint32_t bit=1U<<*id; if(*seen&bit) return false; *seen|=bit; } return true;
}
static bool pq_list(pq_cursor *c,unsigned type,unsigned expected,uint64_t *count) {
    uint8_t b; if(type!=9 || !pq_byte(c,&b) || (unsigned)(b&15)!=expected) return false; *count=b>>4;
    return (*count!=15 || pq_var(c,count)) && *count<=65536;
}
static bool pq_binary(pq_cursor *c,unsigned type,char *text,size_t cap) {
    uint64_t n; if(type!=8 || !pq_var(c,&n) || n>c->end-c->at) return false;
    if(text) { if(!n || n>=cap || !pq_copy(c,text,(size_t)n) || xx_rt_memchr(text,0,(size_t)n)) return false; text[n]=0; }
    else { c->at+=(size_t)n; } return true;
}
static bool pq_skip(pq_cursor *c,unsigned type,unsigned depth) {
    uint64_t n,i,u; uint8_t b; int id,last=0; unsigned t; uint32_t seen=0;
    if(depth>32 || binary_stop(c->pd)) return false;
    switch(type) {
    case 1: case 2:return true;
    case 3:return pq_byte(c,&b);
    case 4:case 5:case 6:return pq_var(c,&u);
    case 7:if(c->end-c->at<8) return false; c->at+=8; return true;
    case 8:return pq_binary(c,type,NULL,0);
    case 9:case 10:
        if(!pq_byte(c,&b) || !(t=b&15) || t>12) { return false; } n=b>>4; if(n==15 && !pq_var(c,&n)) return false; if(n>65536) return false;
        for(i=0;i<n;++i) { if(t==1 || t==2) { if(!pq_byte(c,&b) || (b!=1 && b!=2)) return false; } else if(!pq_skip(c,t,depth+1)) return false; } return true;
    case 11:
        if(!pq_var(c,&n) || n>65536) { return false; } if(!n) return true; if(!pq_byte(c,&b) || !(b>>4) || (b>>4)>12 || !(b&15) || (b&15)>12) return false;
        for(i=0;i<n;++i) { unsigned j; for(j=0;j<2;++j) { unsigned v=j ? b&15:b>>4; if(v==1 || v==2) { uint8_t boolean; if(!pq_byte(c,&boolean) || (boolean!=1 && boolean!=2)) return false; } else if(!pq_skip(c,v,depth+1)) return false; } } return true;
    case 12:
        while(pq_field(c,&last,&id,&t,&seen)) { if(!id) return true; if(!pq_skip(c,t,depth+1)) return false; } return false;
    default:return false;
    }
}
static bool pq_encoding(int64_t n) { return n==0 || (n>=2 && n<=10); }
static bool pq_schema(pq_cursor *c,unsigned type,pq_info *info) {
    uint64_t count,i; if(!pq_list(c,type,12,&count) || count<2 || count>65) return false; info->columns=(unsigned)count-1;
    for(i=0;i<count;++i) { int id,last=0; unsigned t; uint32_t seen=0; int64_t physical=-1,children=-1,repetition=-1,width=0; char name[128];
        for(;;) { int64_t v; if(!pq_field(c,&last,&id,&t,&seen)) return false; if(!id) break;
            if(id==1 || id==2 || id==3 || id==5) { if(!pq_int(c,t,5,&v)) return false; if(id==1) physical=v; else if(id==2) width=v; else if(id==3) repetition=v; else children=v; }
            else if(id==4) { if(!pq_binary(c,t,name,sizeof(name))) return false; }
            else if(!pq_skip(c,t,0)) return false;
        }
        if(!(seen&(1U<<4))) return false;
        if(!i) { if(physical!=-1 || children!=(int64_t)info->columns || repetition!=-1) return false; }
        else { unsigned j; if(physical<0 || physical>7 || children!=-1 || repetition<0 || repetition>1 || (physical==7 && width<=0)) return false;
            for(j=0;j<i-1;++j) if(!xx_rt_strcmp(info->names[j],name)) return false;
            xx_rt_memcpy(info->names[i-1],name,xx_rt_strlen(name)+1); info->types[i-1]=physical;
        }
    } return true;
}
static bool pq_column_metadata(pq_cursor *c,unsigned type,pq_column *column,pq_info *info,unsigned ordinal) {
    int id,last=0; unsigned t; uint32_t seen=0; xx_rt_memset(column,0,sizeof(*column)); column->dictionary=-1;
    if(type!=12) return false;
    for(;;) { int64_t v; uint64_t n,i;
        if(!pq_field(c,&last,&id,&t,&seen)) { return false; } if(!id) break;
        if(id==1 || id==4) { if(!pq_int(c,t,5,&v)) return false; if(id==1) column->type=v; else column->codec=v; }
        else if(id==2) { if(!pq_list(c,t,5,&n) || !n || n>11) return false; for(i=0;i<n;++i) { if(!pq_int(c,5,5,&v) || !pq_encoding(v) || (column->encodings&(1U<<(unsigned)v))) return false; column->encodings|=1U<<(unsigned)v; } }
        else if(id==3) { char name[128]; if(!pq_list(c,t,8,&n) || n!=1 || !pq_binary(c,8,name,sizeof(name)) || xx_rt_strcmp(name,info->names[ordinal])) return false; }
        else if(id==5 || id==6 || id==7 || id==9 || id==11) { if(!pq_int(c,t,6,&v) || v<0) return false; if(id==5) column->values=v; else if(id==6) column->raw=v; else if(id==7) column->packed=v; else if(id==9) column->data=v; else column->dictionary=v; }
        else if(id==10 || id==14 || id==15) return false; /* index/bloom components outside this subset */
        else if(!pq_skip(c,t,0)) return false;
    }
    return (seen&0x2feU)==0x2feU && column->type==info->types[ordinal] && column->codec>=0 && column->codec<=7 && column->raw>0 && column->packed>0 && column->data>=4 && (column->dictionary==-1 || (column->dictionary>=4 && column->dictionary<column->data));
}
typedef struct pq_page { int64_t type,raw,packed,values,nulls,rows,def,rep,encoding,crc; unsigned nested; bool checksum,compressed; } pq_page;
static bool pq_page_detail(pq_cursor *c,unsigned type,unsigned kind,pq_page *p,const pq_column *col) {
    int id,last=0; unsigned t; uint32_t seen=0; if(type!=12) return false;
    for(;;) { int64_t v; if(!pq_field(c,&last,&id,&t,&seen)) return false; if(!id) break;
        if(kind==6) { if(!pq_skip(c,t,0)) return false; }
        else if((kind==5 && id<=4) || (kind==7 && id<=2) || (kind==8 && id<=6)) {
            if(!pq_int(c,t,5,&v) || v<0) return false;
            if(id==1) p->values=v;
            else if(kind==8) { if(id==2) p->nulls=v; else if(id==3) p->rows=v; else if(id==4) p->encoding=v; else if(id==5) p->def=v; else p->rep=v; }
            else if(id==2) p->encoding=v;
            else if(v!=3 && v!=4) return false;
        }
        else if((kind==7 && id==3) || (kind==8 && id==7)) { if(t!=1 && t!=2) return false; if(kind==8) p->compressed=t==1; }
        else if(!pq_skip(c,t,0)) return false;
    }
    if(kind==6) return true;
    if((kind==5 && (seen&30)!=30) || (kind==7 && (seen&6)!=6) || (kind==8 && (seen&126)!=126) || !pq_encoding(p->encoding) || !(col->encodings&(1U<<(unsigned)p->encoding))) return false;
    if(kind==7 && p->encoding!=0 && p->encoding!=2) return false;
    return kind!=8 || (p->nulls<=p->values && p->rows==p->values && p->def<=p->packed && p->rep<=p->packed-p->def && p->def<=p->raw && p->rep<=p->raw-p->def);
}
static bool pq_page_header(pq_cursor *c,pq_page *p,const pq_column *col) {
    int id,last=0; unsigned t; uint32_t seen=0; xx_rt_memset(p,0,sizeof(*p)); p->compressed=true;
    for(;;) { int64_t v; if(!pq_field(c,&last,&id,&t,&seen)) return false; if(!id) break;
        if(id<=4) { if(!pq_int(c,t,5,&v) || (id!=4 && v<0)) return false; if(id==1) p->type=v; else if(id==2) p->raw=v; else if(id==3) p->packed=v; else { p->crc=v; p->checksum=true; } }
        else if(id>=5 && id<=8) { if(p->nested || !pq_page_detail(c,t,(unsigned)id,p,col)) return false; p->nested=(unsigned)id; }
        else if(!pq_skip(c,t,0)) return false;
    }
    if((seen&14)!=14 || p->type>3 || p->nested!=(p->type==0 ? 5U:p->type==1 ? 6U:p->type==2 ? 7U:8U)) return false;
    if((col->codec==0 || (p->type==3 && !p->compressed)) && p->packed!=p->raw) return false;
    return p->type!=3 || (p->nulls<=p->values && p->rows==p->values && p->def<=p->packed && p->rep<=p->packed-p->def && p->def<=p->raw && p->rep<=p->raw-p->def);
}
static bool pq_crc(Abstractformat *f,uint64_t at,uint64_t bytes,uint32_t expected,xx_pd_struct *pd) {
    uint8_t *b; uint32_t crc=0U; size_t capacity=xx_get_file_buffer_size(); bool ok=false;
    if(!bytes) return expected==0U;
    if(capacity>(SIZE_MAX>>1)) { capacity=SIZE_MAX>>1; } if(bytes<capacity) capacity=(size_t)bytes;
    b=(uint8_t *)xx_mem_alloc(capacity); if(!b) return false;
    while(bytes) { size_t n=bytes>capacity ? capacity:(size_t)bytes; if(binary_stop(pd) || !pm_read(f,(int64_t)at,b,n)) goto done;
        crc=xx_crc32_calc(crc,b,n); at+=n; bytes-=n;
    } ok=crc==expected;
done: xx_mem_free(b); return ok;
}
static bool pq_pages(Abstractformat *f,const pq_column *col,uint64_t footer,pq_info *info,xx_pd_struct *pd) {
    uint64_t at=(uint64_t)(col->dictionary>=0 ? col->dictionary:col->data),end,values=0,raw=0; bool dictionary=false,data=false,ok=false; uint8_t *buffer; size_t capacity=xx_get_file_buffer_size();
    if(!binary_range(at,(uint64_t)col->packed,footer)) { return false; } end=at+(uint64_t)col->packed;
    if(capacity>65536) { capacity=65536; } buffer=(uint8_t *)xx_mem_alloc(capacity); if(!buffer) return false;
    while(at<end) { pq_cursor c; pq_page p; size_t n=end-at>65536 ? 65536U:(size_t)(end-at); uint64_t header;
        if(++info->pages>65536 || binary_stop(pd)) goto done;
        xx_mem_zero(&c,sizeof(c));c.at=0;c.end=n;c.pd=pd;c.work=&info->work;
        c.f=f;c.base=at;c.buffer=buffer;c.capacity=capacity;
        if(!pq_page_header(&c,&p,col)) { goto done; } header=c.at;
        if(!binary_range(at,header+(uint64_t)p.packed,end) || (uint64_t)p.raw>UINT64_MAX-header-raw) { goto done; } raw+=header+(uint64_t)p.raw;
        if(p.type==2) { if(dictionary || data || col->dictionary<0 || at!=(uint64_t)col->dictionary) goto done; dictionary=true; }
        else if(p.type==0 || p.type==3) { if(!data && at!=(uint64_t)col->data) goto done; data=true; if((uint64_t)p.values>UINT64_MAX-values) goto done; values+=(uint64_t)p.values; }
        if(p.checksum) { if((uint64_t)p.packed>67108864-info->crc_bytes) goto done; info->crc_bytes+=(uint64_t)p.packed;
            if(!pq_crc(f,at+header,(uint64_t)p.packed,(uint32_t)p.crc,pd)) goto done;
        }
        at+=header+(uint64_t)p.packed;
    }
    ok=data && (col->dictionary<0 || dictionary) && values==(uint64_t)col->values && raw==(uint64_t)col->raw;
done: xx_mem_free(buffer); return ok;
}
static bool pq_chunk(pq_cursor *c,unsigned type,Abstractformat *f,pm_stream *s,uint64_t footer,pq_info *info,unsigned ordinal,uint64_t *raw,uint64_t *packed) {
    int id,last=0; unsigned t; uint32_t seen=0; pq_column col; bool metadata=false; if(type!=12 || ++info->chunks>4096) return false;
    for(;;) { int64_t v; if(!pq_field(c,&last,&id,&t,&seen)) return false; if(!id) break;
        if(id==1 || (id>=4 && id<=9)) return false; /* external/indexed/encrypted */
        if(id==2) { if(!pq_int(c,t,6,&v) || v<0 || (uint64_t)v>footer) return false; }
        else if(id==3) { if(!pq_column_metadata(c,t,&col,info,ordinal)) return false; metadata=true; }
        else if(!pq_skip(c,t,0)) return false;
    }
    if((seen&4)!=4 || !metadata || !pq_pages(f,&col,footer,info,c->pd)) { return false; } info->values[ordinal]=(uint64_t)col.values;
    { char label[64]; uint64_t start=(uint64_t)(col.dictionary>=0 ? col.dictionary:col.data); size_t i;
        if((uint64_t)col.raw>UINT64_MAX-*raw || (uint64_t)col.packed>UINT64_MAX-*packed) { return false; } *raw+=(uint64_t)col.raw; *packed+=(uint64_t)col.packed;
        for(i=0;i<s->count;++i) { uint64_t prior=(uint64_t)(s->items[i].offset-f->base_address); if(binary_stop(c->pd) || (start<prior+(uint64_t)s->items[i].size && prior<start+(uint64_t)col.packed)) return false; }
        xx_rt_snprintf(label,sizeof(label),"column-chunk-%u.bin",info->chunks-1); return pm_add(f,s,label,(int64_t)start,col.packed);
    }
}
static bool pq_rowgroups(pq_cursor *c,unsigned type,Abstractformat *f,pm_stream *s,uint64_t footer,pq_info *info) {
    uint64_t count,i; if(!pq_list(c,type,12,&count) || !count || count>1024) return false;
    for(i=0;i<count;++i) { int id,last=0; unsigned t; uint32_t seen=0; uint64_t raw=0,packed=0; int64_t declared_raw=-1,rows=-1,declared_packed=-1;
        for(;;) { int64_t v; if(!pq_field(c,&last,&id,&t,&seen)) return false; if(!id) break;
            if(id==1) { uint64_t n,j; if(!pq_list(c,t,12,&n) || n!=info->columns) return false; for(j=0;j<n;++j) if(!pq_chunk(c,12,f,s,footer,info,(unsigned)j,&raw,&packed)) return false; }
            else if(id==2 || id==3 || id==5 || id==6 || id==7) { if(!pq_int(c,t,id==7 ? 4U:6U,&v) || v<0 || (id==5 && (uint64_t)v>footer)) return false; if(id==2) declared_raw=v; else if(id==3) rows=v; else if(id==6) declared_packed=v; }
            else if(!pq_skip(c,t,0)) return false;
        }
        if((seen&14)!=14 || rows<0 || declared_raw<0 || (uint64_t)declared_raw!=raw || (declared_packed>=0 && (uint64_t)declared_packed!=packed) || (uint64_t)rows>UINT64_MAX-info->rows) return false;
        /* Flat optional columns count null entries among num_values. */
        { unsigned j; for(j=0;j<info->columns;++j) if(info->values[j]!=(uint64_t)rows) return false; }
        info->rows+=(uint64_t)rows;
    } return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t end=(uint64_t)pm_available(f),footer; uint8_t tail[8],*bytes=NULL; uint32_t length,seen=0; pq_info *info=NULL; pq_cursor c,schema,groups; int id,last=0; unsigned type; int64_t rows=-1,version=0; bool ok=false,have_schema=false,have_groups=false;
    if(end<16 || !binary_equal(f,0,"PAR1",4) || !pm_read(f,(int64_t)end-8,tail,8) || xx_rt_memcmp(tail+4,"PAR1",4) || !(length=xx_data_get_u32(tail, 4, 0, false)) || length>8388608 || length>end-12) { return false; } footer=end-8-length;
    bytes=(uint8_t *)xx_mem_alloc(length);info=(pq_info *)xx_mem_alloc(sizeof(*info)); if(!bytes || !info || !pm_read(f,(int64_t)footer,bytes,length)) goto done; xx_rt_memset(info,0,sizeof(*info));
    xx_mem_zero(&c,sizeof(c));c.p=bytes;c.at=0;c.end=length;c.pd=pd;c.work=&info->work;
    for(;;) { if(!pq_field(&c,&last,&id,&type,&seen)) goto done; if(!id) break;
        if(id==1 || id==3) { int64_t v; if(!pq_int(&c,type,id==1 ? 5U:6U,&v)) goto done; if(id==1) version=v; else rows=v; }
        else if(id==2 || id==4) { pq_cursor slice=c; if(type!=9 || !pq_skip(&c,type,0)) goto done; slice.end=c.at; if(id==2) { schema=slice;have_schema=true; } else { groups=slice;have_groups=true; } }
        else if(id==8 || id==9) goto done;
        else if(!pq_skip(&c,type,0)) goto done;
    }
    if(c.at!=c.end || (seen&30)!=30 || (version!=1 && version!=2) || rows<0 || !have_schema || !have_groups || !pq_schema(&schema,9,info) || schema.at!=schema.end || !pq_rowgroups(&groups,9,f,s,footer,info) || groups.at!=groups.end || info->rows!=(uint64_t)rows || !pm_add(f,s,"parquet-footer.thrift",(int64_t)footer,length)) goto done;
    s->size=(int64_t)end; ok=true;
done: if(bytes) xx_mem_free(bytes); if(info) xx_mem_free(info); return ok;
}

void xx_apache_parquet_init(xx_apache_parquet *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_APACHE_PARQUET,"parquet"); } }
xx_apache_parquet *xx_apache_parquet_create(xx_io_device *d,int64_t b) { xx_apache_parquet *r=(xx_apache_parquet *)xx_mem_alloc(sizeof(*r)); if(r) xx_apache_parquet_init(r,d,b); return r; }
void xx_apache_parquet_destroy(xx_apache_parquet *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_apache_parquet_free(xx_apache_parquet *r) { if(r) { xx_apache_parquet_destroy(r); xx_mem_free(r); } }
bool xx_apache_parquet_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_apache_parquet_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
