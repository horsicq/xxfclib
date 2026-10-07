/* SPDX-License-Identifier: MIT. BBI4 chromosome/interval wire validation.
 * Reference: UCSC Kent bbiFile/bptFile/cirTree and upstream libBigWig. */
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/data/xx_data.h"
typedef struct tb_context {bool be;uint32_t chroms,ids[512],sizes[512];unsigned fields,cached;uint64_t decoded_left,records_left;} tb_context;
typedef struct tb_bounds {uint32_t sc,sb,ec,eb,lc,lp;uint64_t count;} tb_bounds;
static uint32_t tb_u32(const tb_context *t,const uint8_t *p) {return xx_data_get_u32(p, 4, 0, t->be);}
static uint64_t tb_u64(const tb_context *t,const uint8_t *p) {return xx_data_get_u64(p, 8, 0, t->be);}
static bool tb_before(uint32_t c,uint32_t p,uint32_t d,uint32_t q) {return c<d || (c==d && p<q);}
static bool tb_interval(tb_context *t,uint32_t c,uint32_t start,uint32_t end) {unsigned i=t->cached;if(i<t->chroms && t->ids[i]==c) return start<end && end<=t->sizes[i];for(i=0;i<t->chroms;++i) if(t->ids[i]==c) {t->cached=i;return start<end && end<=t->sizes[i];}return false;}
static bool tb_range(tb_context *t,tb_bounds *r,uint32_t c,uint32_t start,uint32_t end) {if(!t->records_left) return false;--t->records_left;if(!r->count || tb_before(c,start,r->sc,r->sb)) {r->sc=c;r->sb=start;}if(!r->count || tb_before(r->ec,r->eb,c,end)) {r->ec=c;r->eb=end;}r->lc=c;r->lp=start;++r->count;return true;}
static bool tb_float32(const tb_context *t,const uint8_t *p) {return (tb_u32(t,p)&0x7f800000U)!=0x7f800000U;}
static bool tb_block(tb_context *t,const uint8_t *p,uint64_t size,tb_bounds *r,xx_pd_struct *pd) {
    uint64_t at=0;uint32_t prevc=0,prevpos=0;bool first=true;xx_mem_zero(r,sizeof(*r));
    while(at<size) {
#if TB_BIGBED
        uint32_t c,start,end;uint64_t text;unsigned tabs=0;if(!eh_span(at,13,size)) return false;c=tb_u32(t,p+(size_t)at);start=tb_u32(t,p+(size_t)at+4);end=tb_u32(t,p+(size_t)at+8);if(!tb_interval(t,c,start,end) || (!first && tb_before(c,start,prevc,prevpos))) return false;at+=12;text=at;
        while(at<size && p[(size_t)at]) {if(p[(size_t)at]=='\t') ++tabs;else if(p[(size_t)at]<32) return false;if(at-text>65536) return false;++at;}if(at==size || !fourth_utf8(p+(size_t)text,(size_t)(at-text),pd) || (t->fields==3 ? at!=text:tabs!=t->fields-4)) return false;++at;if(!tb_range(t,r,c,start,end)) return false;prevc=c;prevpos=start;first=false;
#else
        uint32_t c,start,end,step,span;uint16_t count;uint8_t type;unsigned width,i;uint64_t bytes;uint32_t last=0;
        if(!eh_span(at,24,size)) { return false; } c=tb_u32(t,p+(size_t)at);start=tb_u32(t,p+(size_t)at+4);end=tb_u32(t,p+(size_t)at+8);step=tb_u32(t,p+(size_t)at+12);span=tb_u32(t,p+(size_t)at+16);type=p[(size_t)at+20];count=xx_data_get_u16(p+(size_t)at+22, 2, 0, t->be);width=type==1 ? 12:type==2 ? 8:type==3 ? 4:0;
        if(!width || p[(size_t)at+21] || !count || !tb_interval(t,c,start,end) || (type==1 ? (step || span):(span==0 || (type==2 ? step!=0:step==0)))) { return false; } at+=24;bytes=(uint64_t)count*width;if(!eh_span(at,bytes,size)) return false;
        for(i=0;i<count;++i) {uint64_t pos=at+(uint64_t)i*width;uint32_t a,z;uint64_t wide;
            if(type==1) {a=tb_u32(t,p+(size_t)pos);z=tb_u32(t,p+(size_t)pos+4);}else if(type==2) {a=tb_u32(t,p+(size_t)pos);wide=(uint64_t)a+span;if(wide>UINT32_MAX) return false;z=(uint32_t)wide;}else {wide=(uint64_t)start+(uint64_t)i*step;if(wide+span>UINT32_MAX) return false;a=(uint32_t)wide;z=(uint32_t)(wide+span);}
            if(a<start || z>end || !tb_interval(t,c,a,z) || (i && a<last) || (!first && tb_before(c,a,prevc,prevpos)) || !tb_float32(t,p+(size_t)pos+width-4)) { return false; } last=a;if(!tb_range(t,r,c,a,z)) return false;prevc=c;prevpos=a;first=false;
        }at+=bytes;
#endif
        if(r->count>1000000 || fd_stop(pd)) return false;
    }return r->count!=0;
}
static bool tb_zoom_block(tb_context *t,const uint8_t *p,uint64_t size,tb_bounds *r,xx_pd_struct *pd) {
    uint64_t at;unsigned i;uint32_t lastc=0,lastp=0;xx_mem_zero(r,sizeof(*r));if(!size || size%32) return false;
    for(at=0;at<size;at+=32) {uint32_t c=tb_u32(t,p+(size_t)at),start=tb_u32(t,p+(size_t)at+4),end=tb_u32(t,p+(size_t)at+8),valid=tb_u32(t,p+(size_t)at+12);
        if(!tb_interval(t,c,start,end) || !valid || valid>end-start || (at && tb_before(c,start,lastc,lastp)) || fd_stop(pd)) { return false; } for(i=0;i<4;++i) if(!tb_float32(t,p+(size_t)at+16+i*4)) return false;if(!tb_range(t,r,c,start,end)) return false;lastc=c;lastp=start;
    }return true;
}
static bool tb_decode(nh_blob *b,uint64_t at,uint64_t size,uint32_t cap,tb_context *t,tb_bounds *range,bool zoom) {
    uint8_t *buffer=NULL;xx_io_device *out=NULL;size_t used=0;bool ok=false;uint64_t n;uint32_t a=1,c=0,adler;uint64_t i;
    if(!cap) {if(size>t->decoded_left) return false;t->decoded_left-=size;return nh_span(b,at,size) && (zoom ? tb_zoom_block(t,b->p+(size_t)at,size,range,b->pd):tb_block(t,b->p+(size_t)at,size,range,b->pd));}
    if(cap>8388608 || size<6 || !nh_span(b,at,size)) return false;
    if((b->p[(size_t)at]&15)!=8 || (b->p[(size_t)at]>>4)>7 || (((unsigned)b->p[(size_t)at]<<8)|b->p[(size_t)at+1])%31 || (b->p[(size_t)at+1]&32)) return false;
    if(!t->decoded_left) { return false; } if(cap>t->decoded_left) cap=(uint32_t)t->decoded_left;buffer=(uint8_t *)xx_mem_alloc(cap);if(!buffer) goto done;out=xx_io_mem_open(buffer,cap);if(!out) goto done;
    if(!xx_deflate_unpack_memory_to_device_ex(b->p+(size_t)at+2,(size_t)size-6,out,&used,false,b->pd) || used!=size-6 || xx_io_tell(out)<0) { goto done; } n=(uint64_t)xx_io_tell(out);
    if(!n || n>cap || n>t->decoded_left) { goto done; } t->decoded_left-=n;for(i=0;i<n;++i) {a=(a+buffer[(size_t)i])%65521;c=(c+a)%65521;if(!(i&65535U) && fd_stop(b->pd)) goto done;}adler=(c<<16)|a;
    if(adler!=xx_data_get_u32(b->p+(size_t)(at+size-4), 4, 0, true)) { goto done; } ok=zoom ? tb_zoom_block(t,buffer,n,range,b->pd):tb_block(t,buffer,n,range,b->pd);
done:if(out) xx_io_close(out);xx_mem_free(buffer);return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};tb_context t;tb_bounds overall={0};bool ok=false;uint32_t magic=TB_BIGBED ? 0x8789f2ebU:0x888ffc26U,key,block,cap,reduction=0;uint64_t chrom,data,index,sql,summary,ext,tree_end,index_end,records,total=0,next,limit;unsigned count,i,j,z;uint16_t leaves,zooms;
    xx_mem_zero(&t,sizeof(t));t.decoded_left=67108864;t.records_left=1000000;NH_NEED(nh_load(f,&b,pd) && b.n>=152);t.be=xx_data_get_u32(b.p, 4, 0, true)==magic;zooms=xx_data_get_u16(b.p+6, 2, 0, t.be);NH_NEED(tb_u32(&t,b.p)==magic && xx_data_get_u16(b.p+4, 2, 0, t.be)==4 && zooms<=10 && nh_span(&b,64,(uint64_t)zooms*24));
    chrom=tb_u64(&t,b.p+8);data=tb_u64(&t,b.p+16);index=tb_u64(&t,b.p+24);t.fields=xx_data_get_u16(b.p+32, 2, 0, t.be);sql=tb_u64(&t,b.p+36);summary=tb_u64(&t,b.p+44);cap=tb_u32(&t,b.p+52);ext=tb_u64(&t,b.p+56);
    NH_NEED((TB_BIGBED ? (t.fields>=3 && t.fields<=64 && xx_data_get_u16(b.p+34, 2, 0, t.be)>=3 && xx_data_get_u16(b.p+34, 2, 0, t.be)<=12 && xx_data_get_u16(b.p+34, 2, 0, t.be)<=t.fields):(!t.fields && !xx_data_get_u16(b.p+34, 2, 0, t.be) && !sql)) && cap<=8388608 && chrom>=64+(uint64_t)zooms*24 && data>chrom && index>data && index<b.n && nh_span(&b,chrom,36));
    if(sql) {uint64_t at=sql;NH_NEED(sql>=64 && sql<chrom);while(at<chrom && b.p[(size_t)at]) ++at;NH_NEED(at<chrom && fourth_utf8(b.p+(size_t)sql,(size_t)(at-sql),pd));}
    if(summary) NH_NEED(summary>=64 && eh_span(summary,40,chrom) && nh_floats(&b,summary+8,32,8,t.be));
    if(ext) NH_NEED(ext>=64 && eh_span(ext,64,chrom) && xx_data_get_u16(b.p+(size_t)ext, 2, 0, t.be)==64 && !xx_data_get_u16(b.p+(size_t)ext+2, 2, 0, t.be) && !tb_u64(&t,b.p+(size_t)ext+4) && nh_zero(&b,ext+12,52));
    NH_NEED(tb_u32(&t,b.p+(size_t)chrom)==0x78ca8c91U && tb_u32(&t,b.p+(size_t)chrom+12)==8 && nh_zero(&b,chrom+24,8));block=tb_u32(&t,b.p+(size_t)chrom+4);key=tb_u32(&t,b.p+(size_t)chrom+8);records=tb_u64(&t,b.p+(size_t)chrom+16);count=xx_data_get_u16(b.p+(size_t)chrom+34, 2, 0, t.be);
    NH_NEED(block && block<=65536 && key && key<=128 && records && records<=512 && count==records && count<=block && b.p[(size_t)chrom+32]==1 && !b.p[(size_t)chrom+33]);tree_end=chrom+36+(uint64_t)count*(key+8);NH_NEED(tree_end<=data && nh_zero(&b,tree_end,data-tree_end));t.chroms=count;
    for(i=0;i<count;++i) {uint64_t at=chrom+36+(uint64_t)i*(key+8);const uint8_t *p=b.p+(size_t)at;size_t len=0;while(len<key && p[len]) ++len;NH_NEED(len && fourth_utf8(p,len,pd) && nh_zero(&b,at+len,key-len));t.ids[i]=tb_u32(&t,p+key);t.sizes[i]=tb_u32(&t,p+key+4);NH_NEED(t.sizes[i]);for(j=0;j<i;++j) NH_NEED(t.ids[j]!=t.ids[i]);if(i) NH_NEED(xx_rt_memcmp(p-(key+8),p,key)<0);}
    NH_NEED(nh_span(&b,data,8) && nh_span(&b,index,52) && tb_u32(&t,b.p+(size_t)index)==0x2468ace0U && nh_zero(&b,index+44,4));block=tb_u32(&t,b.p+(size_t)index+4);records=tb_u64(&t,b.p+(size_t)index+8);leaves=xx_data_get_u16(b.p+(size_t)index+50, 2, 0, t.be);
    NH_NEED(block && block<=65536 && leaves && leaves<=1024 && leaves<=block && records==leaves && tb_u64(&t,b.p+(size_t)index+32)==index && tb_u32(&t,b.p+(size_t)index+40) && b.p[(size_t)index+48]==1 && !b.p[(size_t)index+49]);index_end=index+52+(uint64_t)leaves*32;
    limit=zooms ? tb_u64(&t,b.p+72):b.n-4;NH_NEED(limit<=b.n-4 && index_end<=limit && limit-index_end<=(uint64_t)block*32 && nh_zero(&b,index_end,limit-index_end) && tb_u32(&t,b.p+(size_t)b.n-4)==magic && nh_add(f,s,&b,"metadata",0,data) && nh_add(f,s,&b,"data-count",data,8));next=data+8;
    for(i=0;i<leaves;++i) {uint64_t at=index+52+(uint64_t)i*32,pos=tb_u64(&t,b.p+(size_t)at+16),size=tb_u64(&t,b.p+(size_t)at+24);tb_bounds r;NH_NEED(pos==next && size && eh_span(pos,size,index) && tb_decode(&b,pos,size,cap,&t,&r,false));
        NH_NEED(r.sc==tb_u32(&t,b.p+(size_t)at) && r.sb==tb_u32(&t,b.p+(size_t)at+4) && r.ec==tb_u32(&t,b.p+(size_t)at+8) && r.eb==tb_u32(&t,b.p+(size_t)at+12));
        if(!i) overall=r;else {NH_NEED(!tb_before(r.sc,r.sb,overall.lc,overall.lp));if(tb_before(overall.ec,overall.eb,r.ec,r.eb)) {overall.ec=r.ec;overall.eb=r.eb;}overall.lc=r.lc;overall.lp=r.lp;}total+=r.count;NH_NEED(nh_add(f,s,&b,cap ? "zlib-block":"data-block",pos,size));next=pos+size;
    }
    NH_NEED(next==index && overall.sc==tb_u32(&t,b.p+(size_t)index+16) && overall.sb==tb_u32(&t,b.p+(size_t)index+20) && overall.ec==tb_u32(&t,b.p+(size_t)index+24) && overall.eb==tb_u32(&t,b.p+(size_t)index+28));
    NH_NEED(tb_u64(&t,b.p+(size_t)data)==(TB_BIGBED ? total:(uint64_t)leaves) && nh_add(f,s,&b,"interval-index",index,limit-index));
    for(z=0;z<zooms;++z) {uint64_t head=64+(uint64_t)z*24,zd=tb_u64(&t,b.p+(size_t)head+8),zi=tb_u64(&t,b.p+(size_t)head+16);uint32_t red=tb_u32(&t,b.p+(size_t)head);uint64_t expected_count;
        NH_NEED(red>reduction && !tb_u32(&t,b.p+(size_t)head+4) && zd==limit && zi>zd && nh_span(&b,zd,4) && nh_span(&b,zi,52));reduction=red;expected_count=tb_u32(&t,b.p+(size_t)zd);NH_NEED(expected_count && tb_u32(&t,b.p+(size_t)zi)==0x2468ace0U && nh_zero(&b,zi+44,4));block=tb_u32(&t,b.p+(size_t)zi+4);records=tb_u64(&t,b.p+(size_t)zi+8);leaves=xx_data_get_u16(b.p+(size_t)zi+50, 2, 0, t.be);
        /* Kent/libBigWig count summary items; Biopython counts leaf blocks. */
        NH_NEED(block && block<=65536 && leaves && leaves<=1024 && leaves<=block && (records==leaves || records==expected_count) && tb_u64(&t,b.p+(size_t)zi+32)==zi && tb_u32(&t,b.p+(size_t)zi+40) && b.p[(size_t)zi+48]==1 && !b.p[(size_t)zi+49]);index_end=zi+52+(uint64_t)leaves*32;limit=z+1<zooms ? tb_u64(&t,b.p+(size_t)head+32):b.n-4;
        NH_NEED(limit<=b.n-4 && index_end<=limit && limit-index_end<=(uint64_t)block*32 && nh_zero(&b,index_end,limit-index_end) && nh_add(f,s,&b,"zoom-count",zd,4));next=zd+4;total=0;xx_mem_zero(&overall,sizeof(overall));
        for(i=0;i<leaves;++i) {uint64_t at=zi+52+(uint64_t)i*32,pos=tb_u64(&t,b.p+(size_t)at+16),size=tb_u64(&t,b.p+(size_t)at+24);tb_bounds r;NH_NEED(pos==next && size && eh_span(pos,size,zi) && tb_decode(&b,pos,size,cap,&t,&r,true));NH_NEED(r.sc==tb_u32(&t,b.p+(size_t)at) && r.sb==tb_u32(&t,b.p+(size_t)at+4) && r.ec==tb_u32(&t,b.p+(size_t)at+8) && r.eb==tb_u32(&t,b.p+(size_t)at+12));if(!i) overall=r;else {NH_NEED(!tb_before(r.sc,r.sb,overall.lc,overall.lp));if(tb_before(overall.ec,overall.eb,r.ec,r.eb)) {overall.ec=r.ec;overall.eb=r.eb;}overall.lc=r.lc;overall.lp=r.lp;}total+=r.count;NH_NEED(nh_add(f,s,&b,cap ? "zoom-zlib-block":"zoom-block",pos,size));next=pos+size;}
        NH_NEED(next==zi && total==expected_count && overall.sc==tb_u32(&t,b.p+(size_t)zi+16) && overall.sb==tb_u32(&t,b.p+(size_t)zi+20) && overall.ec==tb_u32(&t,b.p+(size_t)zi+24) && overall.eb==tb_u32(&t,b.p+(size_t)zi+28) && nh_add(f,s,&b,"zoom-index",zi,limit-zi));
    }
    NH_NEED(nh_add(f,s,&b,"magic",b.n-4,4));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
