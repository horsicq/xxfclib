/* SPDX-License-Identifier: MIT
 * Wire specification: https://arrow.apache.org/docs/format/Columnar.html */
#include "xxfclib/formats/apache_arrow_file/xx_apache_arrow_file.h"
#include "../xx_fifth_data.h"
#include "xxfclib/global/xx_global.h"

typedef struct af_buf { Abstractformat *f; uint64_t begin,end; xx_pd_struct *pd; } af_buf;
typedef struct af_col { char name[256]; uint8_t width,nullable,is_signed; } af_col;
typedef struct af_schema_info { uint32_t count; uint16_t endian; af_col fields[64]; } af_schema_info;
typedef struct af_table { uint64_t at,vt; uint16_t size,vsize; } af_table;
static bool af_read(af_buf *b,uint64_t at,void *p,size_t n) { return !fd_stop(b->pd) && at>=b->begin && fd_range(at,n,b->end) && pm_read(b->f,(int64_t)at,p,n); }
static bool af_u32(af_buf *b,uint64_t at,uint32_t *v) { uint8_t h[4]; if(!af_read(b,at,h,4)) return false; *v=pm_le32(h); return true; }
static bool af_table_at(af_buf *b,uint64_t at,af_table *t) { uint8_t h[4]; int32_t displacement; uint64_t vt;
    if(!af_read(b,at,h,4)) { return false; } displacement=(int32_t)pm_le32(h);
    if(displacement>=0) { if((uint64_t)displacement>at-b->begin) return false; vt=at-(uint32_t)displacement; }
    else { uint64_t n=(uint64_t)(-(int64_t)displacement); if(at>b->end || n>b->end-at) return false; vt=at+n; }
    if(!af_read(b,vt,h,4)) { return false; } t->at=at; t->vt=vt; t->vsize=pm_le16(h); t->size=pm_le16(h+2);
    return t->vsize>=4 && !(t->vsize&1) && t->size>=4 && fd_range(vt,t->vsize,b->end) && fd_range(at,t->size,b->end);
}
static bool af_root(af_buf *b,af_table *t) { uint32_t n; return af_u32(b,b->begin,&n) && n>=4 && n<=b->end-b->begin && af_table_at(b,b->begin+n,t); }
static bool af_field(af_buf *b,af_table *t,unsigned field,unsigned width,uint64_t *at) { uint8_t h[2]; uint16_t off;
    if(4+field*2>=t->vsize) { *at=0; return true; }
    if(!af_read(b,t->vt+4+field*2,h,2)) { return false; } off=pm_le16(h); if(!off) { *at=0; return true; }
    if(off<4 || off>t->size || width>(unsigned)(t->size-off)) { return false; } *at=t->at+off; return true;
}
static bool af_value(af_buf *b,af_table *t,unsigned field,unsigned width,uint64_t *v) { uint8_t h[8]; uint64_t at; if(!af_field(b,t,field,width,&at)) return false;
    if(!at) { *v=0; return true; } if(!af_read(b,at,h,width)) return false; *v=width==1 ? h[0]:width==2 ? pm_le16(h):width==4 ? pm_le32(h):fd_le64(h); return true;
}
static bool af_ref(af_buf *b,af_table *t,unsigned field,uint64_t *at) { uint64_t field_at; uint32_t off;
    if(!af_field(b,t,field,4,&field_at)) { return false; } if(!field_at) { *at=0; return true; }
    if(!af_u32(b,field_at,&off) || off<4 || !fd_range(field_at,off,b->end)) { return false; } *at=field_at+off; return true;
}
static bool af_vector(af_buf *b,af_table *t,unsigned field,unsigned width,uint64_t *at,uint32_t *n) { uint64_t ref; if(!af_ref(b,t,field,&ref)) return false;
    if(!ref) { *at=0; *n=0; return true; }
    if(!af_u32(b,ref,n) || !fd_range(ref+4,(uint64_t)*n*width,b->end)) { return false; } *at=ref+4; return true;
}
static bool af_schema(af_buf *b,af_table *t,af_schema_info *info) {
    uint64_t endian,vec,extra; uint32_t i,nextra; xx_mem_zero(info,sizeof(*info));
    if(!af_value(b,t,0,2,&endian) || endian>1 || !af_vector(b,t,1,4,&vec,&info->count) || !info->count || info->count>64 || !af_vector(b,t,2,4,&extra,&nextra) || nextra || !af_vector(b,t,3,8,&extra,&nextra) || nextra) return false;
    info->endian=(uint16_t)endian;
    for(i=0;i<info->count;++i) { uint32_t off,n,children; uint64_t name,kind,nullable,type,dict,childvec,width,signedness; af_table field,integer;
        if(!af_u32(b,vec+i*4,&off) || off<4 || !fd_range(vec+i*4,off,b->end) || !af_table_at(b,vec+i*4+off,&field) || !af_ref(b,&field,0,&name) || !name || !af_u32(b,name,&n) || !n || n>255 || !fd_range(name+4,n+1,b->end)) return false;
        { uint8_t text[256]; uint32_t j; if(!af_read(b,name+4,text,n+1) || text[n] || !fourth_utf8(text,n,b->pd)) return false;
          for(j=0;j<n;++j) { if(!text[j]) return false; } xx_rt_memcpy(info->fields[i].name,text,n); }
        if(!af_value(b,&field,1,1,&nullable) || nullable>1 || !af_value(b,&field,2,1,&kind) || kind!=2 || !af_ref(b,&field,3,&type) || !type || !af_table_at(b,type,&integer) || !af_value(b,&integer,0,4,&width) || (width!=8 && width!=16 && width!=32 && width!=64) || !af_value(b,&integer,1,1,&signedness) || signedness>1 || !af_ref(b,&field,4,&dict) || dict || !af_vector(b,&field,5,4,&childvec,&children) || children || !af_vector(b,&field,6,4,&extra,&nextra) || nextra) return false;
        info->fields[i].width=(uint8_t)(width/8); info->fields[i].is_signed=(uint8_t)signedness; info->fields[i].nullable=(uint8_t)nullable;
    } return true;
}
static bool af_bitmap(Abstractformat *f,uint64_t at,uint64_t rows,uint64_t wanted,uint64_t *work,xx_pd_struct *pd) {
    uint64_t bytes=(rows+7)/8,left=rows,nulls=0; uint8_t *buffer; size_t capacity=xx_get_file_buffer_size(); bool ok=false;
    if(bytes>67108864 || *work>67108864-bytes) { return false; } *work+=bytes;
    if(!left) return nulls==wanted;
    if(capacity>(SIZE_MAX>>1)) capacity=SIZE_MAX>>1;
    if(bytes<capacity) capacity=(size_t)bytes;
    buffer=(uint8_t *)xx_mem_alloc(capacity); if(!buffer) return false;
    while(left) { size_t n=bytes>capacity ? capacity:(size_t)bytes,i;
        if(fd_stop(pd) || !pm_read(f,(int64_t)at,buffer,n)) goto done;
        for(i=0;i<n;++i) { unsigned bits=left>8 ? 8:(unsigned)left,j; for(j=0;j<bits;++j) if(!(buffer[i]&(1U<<j))) ++nulls; left-=bits; }
        at+=n; bytes-=n;
    } ok=nulls==wanted;
done: xx_mem_free(buffer); return ok;
}
static bool af_message(Abstractformat *f,uint64_t at,uint64_t meta,uint64_t body,uint64_t end,unsigned kind,const af_schema_info *info,uint64_t expected_version,uint64_t *bitmap_work,xx_pd_struct *pd) {
    uint8_t h[8]; uint32_t n,prefix; af_buf b; af_table message,payload; uint64_t version,typ,p,bodylen,custom; uint32_t fields=info->count,ncustom;
    if((body&7) || meta<8 || !fd_range(at,meta+body,end) || !pm_read(f,(int64_t)at,h,8)) return false;
    prefix=pm_le32(h)==UINT32_MAX ? 8:4; n=pm_le32(h+prefix-4); if(n!=meta-prefix || (meta&7)) return false;
    b.f=f; b.begin=at+prefix; b.end=at+meta; b.pd=pd;
    if(!af_root(&b,&message) || !af_vector(&b,&message,4,4,&custom,&ncustom) || ncustom || !af_value(&b,&message,0,2,&version) || version!=expected_version || !af_value(&b,&message,1,1,&typ) || typ!=kind || !af_value(&b,&message,3,8,&bodylen) || bodylen!=body || !af_ref(&b,&message,2,&p) || !p || !af_table_at(&b,p,&payload)) return false;
    if(kind==1) { af_schema_info other; return !body && af_schema(&b,&payload,&other) && !xx_rt_memcmp(info,&other,sizeof(other)); }
    else { uint64_t rows,nodes,buffers,compression,variadic,starts[128],sizes[128],nullcounts[64]; uint32_t nnode,nbuf,nvar,i;
        if(!af_value(&b,&payload,0,8,&rows) || rows>INT64_MAX || !af_vector(&b,&payload,1,16,&nodes,&nnode) || nnode!=fields || !af_vector(&b,&payload,2,16,&buffers,&nbuf) || nbuf!=fields*2 || !af_ref(&b,&payload,3,&compression) || compression || !af_vector(&b,&payload,4,8,&variadic,&nvar) || nvar) return false;
        for(i=0;i<fields;++i) { uint64_t len,nulls; if(!af_read(&b,nodes+i*16,h,8) || (len=fd_le64(h))!=rows || !af_read(&b,nodes+i*16+8,h,8) || (nulls=fd_le64(h))>rows || (nulls && !info->fields[i].nullable)) return false; nullcounts[i]=nulls; }
        for(i=0;i<nbuf;++i) { uint64_t off,size,required; unsigned j;
            if(!af_read(&b,buffers+i*16,h,8)) { return false; } off=fd_le64(h); if(!af_read(&b,buffers+i*16+8,h,8)) return false; size=fd_le64(h);
            if(off&7 || !fd_range(off,size,body)) return false;
            if(i&1) { if(!fd_mul(rows,info->fields[i/2].width,&required) || size!=required) return false; }
            else { required=(rows+7)/8; if((size && size!=required) || (nullcounts[i/2] && !size)) return false; }
            for(j=0;j<i;++j) if(size && sizes[j] && off<starts[j]+sizes[j] && starts[j]<off+size) return false;
            if(!(i&1) && size && !af_bitmap(f,at+meta+off,rows,nullcounts[i/2],bitmap_work,pd)) return false;
            starts[i]=off; sizes[i]=size;
        } return true;
    }
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t end=(uint64_t)pm_available(f),footer,version,schema_at,dictionaries,batches,last=8,custom,bitmap_work=0; uint32_t footerlen,ndict,nbatch,i,ncustom; uint8_t h[24]; af_schema_info info; af_buf b; af_table ft,schema;
    if(end<32 || !fd_equal(f,0,"ARROW1\0\0",8) || !fd_equal(f,(int64_t)end-6,"ARROW1",6) || !pm_read(f,(int64_t)end-10,h,4) || !(footerlen=pm_le32(h)) || footerlen>16777216 || footerlen>end-18) return false;
    footer=end-10-footerlen; b.f=f; b.begin=footer; b.end=end-10; b.pd=pd;
    if(!af_root(&b,&ft) || !af_vector(&b,&ft,4,4,&custom,&ncustom) || ncustom || !af_value(&b,&ft,0,2,&version) || version<3 || version>4 || !af_ref(&b,&ft,1,&schema_at) || !schema_at || !af_table_at(&b,schema_at,&schema) || !af_schema(&b,&schema,&info) || !af_vector(&b,&ft,2,24,&dictionaries,&ndict) || ndict || !af_vector(&b,&ft,3,24,&batches,&nbatch) || !nbatch || nbatch>1024) return false;
    if(!pm_read(f,8,h,8)) return false;
    { uint32_t prefix=pm_le32(h)==UINT32_MAX ? 8:4,meta=pm_le32(h+prefix-4);
      if(meta>16777216 || !af_message(f,8,(uint64_t)prefix+meta,0,footer,1,&info,version,&bitmap_work,pd)) { return false; } last=8+prefix+meta;
    }
    for(i=0;i<nbatch;++i) { uint64_t at,body; uint32_t meta; char name[64];
        if(fd_stop(pd) || !af_read(&b,batches+i*24,h,24)) { return false; } at=fd_le64(h); meta=pm_le32(h+8); body=fd_le64(h+16);
        if(at!=last || at&7 || meta>16777216 || meta<8 || body>INT64_MAX || !fd_range(at,(uint64_t)meta+body,footer) || !af_message(f,at,meta,body,footer,3,&info,version,&bitmap_work,pd)) return false;
        xx_rt_snprintf(name,sizeof(name),"batch-%u-metadata.bin",i); if(!pm_add(f,s,name,(int64_t)at,meta)) return false;
        xx_rt_snprintf(name,sizeof(name),"batch-%u-body.bin",i); if(!pm_add(f,s,name,(int64_t)at+meta,(int64_t)body)) return false; last=at+meta+body;
    }
    /* Stream terminator between the last message and footer is optional. */
    if(last!=footer && (footer-last!=8 || !fd_equal(f,(int64_t)last,"\xff\xff\xff\xff\0\0\0\0",8))) return false;
    if(!pm_add(f,s,"arrow-footer.flatbuffer",(int64_t)footer,footerlen)) return false;
    s->size=(int64_t)end; return true;
}

void xx_apache_arrow_file_init(xx_apache_arrow_file *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_APACHE_ARROW_FILE,"apache_arrow_file"); } }
xx_apache_arrow_file *xx_apache_arrow_file_create(xx_io_device *d,int64_t b) { xx_apache_arrow_file *r=(xx_apache_arrow_file *)xx_mem_alloc(sizeof(*r)); if(r) xx_apache_arrow_file_init(r,d,b); return r; }
void xx_apache_arrow_file_destroy(xx_apache_arrow_file *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_apache_arrow_file_free(xx_apache_arrow_file *r) { if(r) { xx_apache_arrow_file_destroy(r); xx_mem_free(r); } }
bool xx_apache_arrow_file_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_apache_arrow_file_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
