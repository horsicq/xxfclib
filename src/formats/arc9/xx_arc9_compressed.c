/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native DCS v1 block stream. The public layout is documented by SXSEXP's
 * ProcessFileDCS; Windows' raw LZMS decoder handles the compressed blocks.
 * No subprocess, temporary file, or copy of the encoded input is used.
 */
#include "xx_arc9_compressed.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/crc/xx_crc.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define AC_MEMORY_LIMIT (256U * 1024U * 1024U)
#define AC_BLOCK_LIMIT (64U * 1024U * 1024U)
static bool ac_write(xx_io_device *,const uint8_t *,size_t,xx_pd_struct *);
static uint64_t ac_budget(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t budget=v?xx_var_get_u64(v):AC_MEMORY_LIMIT;
    return budget<AC_MEMORY_LIMIT?budget:AC_MEMORY_LIMIT;
}
static bool ac_room(Abstractformat *f,pm_stream *s,uint64_t reserve) {
    uint64_t used=(uint64_t)s->capacity*sizeof(pm_member); size_t i;
    for(i=0;i<s->count;++i) if(s->items[i].memory) used+=(uint64_t)s->items[i].size;
    return used<=ac_budget(f) && reserve<=ac_budget(f)-used;
}
static bool ac_add(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t n) {
    uint64_t growth=s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8U)*sizeof(pm_member):0;
    return ac_room(f,s,growth) && pm_add(f,s,name,at,n);
}
static uint32_t ac_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t ac_u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static void ac_put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
typedef struct ac_buffer { uint8_t *data; size_t size,capacity,limit; } ac_buffer;
static ssize_t ac_buffer_write(xx_io_device *d,const void *p,size_t n) {
    ac_buffer *b=(ac_buffer *)d->priv;
    if(n>b->limit-b->size) return -1;
    if(n>b->capacity-b->size) {
        size_t next=b->capacity?b->capacity:(b->limit<65536U?b->limit:65536U);
        uint8_t *memory;
        while(next<b->size+n) { if(next>b->limit/2U) { next=b->limit; break; } next*=2U; }
        memory=(uint8_t *)xx_mem_realloc(b->data,next);
        if(!memory) return -1;
        b->data=memory; b->capacity=next;
    }
    xx_mem_copy(b->data+b->size,p,n); b->size+=n; return (ssize_t)n;
}
/* Decode exactly one raw stream into a bounded RAM vector. The consumed byte
 * count is independent of input read-ahead, so trailers remain verifiable. */
static bool ac_inflate_reserved(Abstractformat *f,int64_t offset,int64_t size,bool zlib,
                       ac_buffer *b,int64_t *consumed,xx_pd_struct *pd,uint64_t reserve) {
    xx_io_device sink; uint8_t h[2],trailer[4]; int64_t used=0;
    xx_mem_zero(&sink,sizeof(sink)); sink.priv=b; sink.write=ac_buffer_write;
    /* Reserve decoder history and its bounded input/output buffers. */
    if(ac_budget(f)<524288U || reserve>ac_budget(f)-524288U) return false;
    b->limit=(size_t)(ac_budget(f)-524288U-reserve);
    if(offset<0 || size<0 || (zlib && (size<6 || !pm_read(f,offset,h,2) ||
       !xx_zlib_stream_header_is_valid(h,2)))) return false;
    if(!xx_deflate_unpack_device_ex(f->device,f->base_address+offset+(zlib?2:0),
        size-(zlib?2:0),&sink,false,0,NULL,0,&used,pd)) return false;
    if(zlib) {
        uint32_t stored;
        if(used>size-6 || !pm_read(f,offset+2+used,trailer,4)) return false;
        stored=((uint32_t)trailer[0]<<24)|((uint32_t)trailer[1]<<16)|((uint32_t)trailer[2]<<8)|trailer[3];
        if(stored!=xx_zlib_stream_adler32(b->data,b->size)) return false;
        used+=6;
    }
    if(consumed) *consumed=used;
    return !(pd && xx_pd_is_stopped(pd));
}
static bool ac_inflate(Abstractformat *f,int64_t offset,int64_t size,bool zlib,
                       ac_buffer *b,int64_t *consumed,xx_pd_struct *pd) {
    return ac_inflate_reserved(f,offset,size,zlib,b,consumed,pd,0);
}
typedef struct ac_cursor { const uint8_t *data; size_t size,at; } ac_cursor;
static bool ac_take(ac_cursor *c,size_t n,const uint8_t **out) {
    if(n>c->size-c->at) return false;
    if(out) *out=c->data+c->at;
    c->at+=n; return true;
}
static bool ac_word(ac_cursor *c,uint32_t *out) {
    const uint8_t *p;
    if(!ac_take(c,4,&p)) return false;
    *out=ac_u32(p); return true;
}
static bool ac_string(ac_cursor *c,char *name,size_t capacity) {
    const uint8_t *p; uint32_t n;
    if(!ac_word(c,&n) || n>4096U || !ac_take(c,n,&p)) return false;
    if(name && capacity) {
        size_t i,copy=n<capacity-1U?n:capacity-1U;
        for(i=0;i<copy;++i) name[i]=(p[i]>=32 && p[i]<127)?(char)p[i]:'_';
        name[copy]=0;
    }
    return true;
}
static bool ac_add_memory(Abstractformat *f,pm_stream *s,const char *name,const uint8_t *p,size_t n) {
    uint8_t *copy=(uint8_t *)xx_mem_alloc(n?n:1U);
    if(!copy) return false;
    if(n) xx_mem_copy(copy,p,n);
    if(!ac_add(f,s,name,0,0)) { xx_mem_free(copy); return false; }
    s->items[s->count-1U].memory=copy; s->items[s->count-1U].size=(int64_t)n;
    return true;
}
static bool ac_add_bitmap(Abstractformat *f,pm_stream *s,const char *name,const uint8_t *p,uint32_t w,uint32_t h) {
    uint64_t n=(uint64_t)w*h*4U;
    uint8_t *bmp; char label[96];
    if(!w || !h || w>INT32_MAX || h>INT32_MAX || n>AC_MEMORY_LIMIT-54U) return false;
    bmp=(uint8_t *)xx_mem_alloc((size_t)n+54U);
    if(!bmp) return false;
    xx_mem_zero(bmp,54); bmp[0]='B'; bmp[1]='M'; ac_put32(bmp+2,(uint32_t)n+54U);
    ac_put32(bmp+10,54); ac_put32(bmp+14,40); ac_put32(bmp+18,w);
    ac_put32(bmp+22,0U-h); bmp[26]=1; bmp[28]=32; ac_put32(bmp+34,(uint32_t)n);
    xx_mem_copy(bmp+54,p,(size_t)n);
    (void)xx_rt_snprintf(label,sizeof(label),"%s.bmp",name);
    if(!ac_add(f,s,label,0,0)) { xx_mem_free(bmp); return false; }
    s->items[s->count-1U].memory=bmp; s->items[s->count-1U].size=(int64_t)n+54;
    s->items[s->count-1U].compression_method=8;
    return true;
}
static bool ac_mskn1_entries_fit(ac_cursor c,uint32_t count,bool bitmap) {
    uint32_t i;
    for(i=0;i<count;++i) {
        uint32_t n,w;
        if(!ac_string(&c,NULL,0) || !ac_word(&c,&n)) return false;
        if(bitmap) {
            uint64_t bytes;
            w=n;
            if(!w || !ac_word(&c,&n) || !n) return false;
            bytes=(uint64_t)w*n*4U;
            if(bytes>AC_MEMORY_LIMIT || !ac_take(&c,(size_t)bytes,NULL)) return false;
        } else if(!ac_take(&c,n,NULL)) return false;
    }
    return true;
}

static bool ac_mskn_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[9]; size_t magic;
    ac_buffer b={0}; ac_cursor c;
    uint32_t i,count=0,flags=0; int64_t used=0,size=pm_available(f);
    bool result=false,raw_bitmaps=false;
    if(f->file_type==XX_FILE_TYPE_MSKN1) magic=4;
    else if(f->file_type==XX_FILE_TYPE_MSKN2) magic=9;
    else magic=7;
    if(size<(int64_t)magic+6 || !pm_read(f,0,h,magic) ||
       (magic==4 && xx_mem_compare(h,"KSLZ",4)) ||
       (magic==9 && xx_mem_compare(h,"_MCT\0KSLZ",9)) ||
       (magic==7 && xx_mem_compare(h,"KSMSSTL",7)) ||
       !ac_inflate(f,(int64_t)magic,size-(int64_t)magic,true,&b,&used,pd) || used!=size-(int64_t)magic ||
       !ac_room(f,s,b.capacity)) goto done;
    c.data=b.data; c.size=b.size; c.at=0;
    if(magic==7) {
        const uint8_t *ini; uint32_t n;
        if(!ac_string(&c,NULL,0) || !ac_word(&c,&n) || !ac_take(&c,n,&ini) ||
           !ac_room(f,s,(uint64_t)b.capacity+n+sizeof(pm_member)*8U) ||
           !ac_add_memory(f,s,"theme.ini",ini,n) || !ac_word(&c,&count) || count>65535U) goto done;
    } else {
        for(i=0;i<5;++i) if(!ac_string(&c,NULL,0)) goto done;
        if(magic==4) {
            if(!ac_word(&c,&flags)) goto done;
            count=flags&65535U;
            if((flags>>16)!=0 && (flags>>16)!=15U) goto done;
            raw_bitmaps=(flags>>16)==15U;
            /* Early SkinEngine II 2.0 writes raw BGRA images without the
             * later high-word marker. Accept that complete grammar only if
             * the stored-member grammar fails, never guess per image. */
            if(!raw_bitmaps && !ac_mskn1_entries_fit(c,count,false)) {
                if(!ac_mskn1_entries_fit(c,count,true)) goto done;
                raw_bitmaps=true;
            }
        } else {
            const uint8_t *p;
            if(!ac_take(&c,2,&p)) goto done;
            count=ac_u16(p)&4095U;
        }
    }
    for(i=0;i<count;++i) {
        char name[80]; const uint8_t *p;
        uint32_t w,n;
        if((pd && xx_pd_is_stopped(pd)) || (magic==9 && !ac_take(&c,2,NULL)) ||
           !ac_string(&c,name,sizeof(name))) goto done;
        if(magic!=4 || raw_bitmaps) {
            uint64_t bytes;
            if(!ac_word(&c,&w) || !ac_word(&c,&n)) goto done;
            bytes=(uint64_t)w*n*4U;
            if(bytes>AC_MEMORY_LIMIT || !ac_room(f,s,(uint64_t)b.capacity+bytes+54U+sizeof(pm_member)*(s->capacity?s->capacity:8U)) ||
               !ac_take(&c,(size_t)bytes,&p) ||
               !ac_add_bitmap(f,s,name,p,w,n)) goto done;
        } else {
            if(!ac_word(&c,&n) || !ac_room(f,s,(uint64_t)b.capacity+n+sizeof(pm_member)*(s->capacity?s->capacity:8U)) ||
               !ac_take(&c,n,&p) || !ac_add_memory(f,s,name,p,n)) goto done;
            s->items[s->count-1U].compression_method=8;
        }
        if(magic==7 && (!ac_string(&c,NULL,0) || !ac_take(&c,2,NULL))) goto done;
    }
    /* Remaining bytes describe theme objects; preserve this decoded metadata
     * alongside the images rather than silently dropping it. */
    if(c.at<c.size && (!ac_room(f,s,(uint64_t)b.capacity+c.size-c.at+sizeof(pm_member)*(s->capacity?s->capacity:8U)) ||
       !ac_add_memory(f,s,"theme-objects.bin",c.data+c.at,c.size-c.at))) goto done;
    if(!s->count) goto done;
    s->size=size;
    for(i=0;i<s->count;++i) s->items[i].packed_size=0;
    s->items[0].packed_size=size-(int64_t)magic;
    result=true;
done:
    xx_mem_free(b.data); return result;
}

static bool ac_csq_lzss(const uint8_t *src,size_t packed,uint8_t *dst,size_t expected,xx_pd_struct *pd) {
    uint8_t history[4096]; unsigned flags=0,position=0;
    size_t at=0,out=0;
    xx_mem_zero(history,sizeof(history));
    while(at<packed && out<expected) {
        unsigned token;
        if(pd && xx_pd_is_stopped(pd)) return false;
        flags>>=1;
        if(!(flags&256U)) { flags=src[at++]|0xff00U; if(at==packed) return false; }
        token=src[at++];
        if(flags&1U) {
            dst[out++]=history[position]=(uint8_t)token; position=(position+1U)&4095U;
        } else {
            unsigned high,offset,length,j;
            if(at==packed) return false;
            high=src[at++]; offset=(((high&240U)<<4)|token)+18U; length=(high&15U)+3U;
            if(length>expected-out) return false;
            for(j=0;j<length;++j) {
                uint8_t v=history[(offset+j)&4095U]; dst[out++]=history[position]=v;
                position=(position+1U)&4095U;
            }
        }
    }
    /* An exhausted eight-token group may be followed by an unused zero
     * control byte. TiGGER's writer emits it for exact group boundaries. */
    return out==expected && (at==packed ||
        (at+1U==packed && src[at]==0 && !((flags>>1)&256U)));
}
static bool ac_csq_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[9],e[26]; unsigned i,count;
    int64_t size=pm_available(f); uint64_t retained=0;
    if(size<9 || !pm_read(f,0,h,9) || xx_mem_compare(h,"TiGGER\1\7",8) || !(count=h[8]) ||
       9U+count*26U>(uint64_t)size) return false;
    for(i=0;i<count;++i) {
        uint32_t at,packed,plain; char name[13]; unsigned n;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,9+(int64_t)i*26,e,26)) return false;
        at=ac_u32(e); packed=ac_u32(e+4); plain=ac_u32(e+8); n=e[13];
        if(!n || n>12U || e[12]>1U || at<9U+count*26U || (uint64_t)at+packed>(uint64_t)size ||
           plain>AC_MEMORY_LIMIT || (e[12]==0 && packed!=plain)) return false;
        xx_mem_copy(name,e+14,n); name[n]=0;
        if(!ac_add(f,s,name,at,packed)) return false;
        s->items[s->count-1U].size=plain;
        {
            uint8_t *input,*output; bool okay; uint32_t j;
            retained+=plain;
            if(retained+packed+(uint64_t)s->capacity*sizeof(pm_member)+4096U>ac_budget(f)) return false;
            input=(uint8_t *)xx_mem_alloc(packed?packed:1U);
            output=(uint8_t *)xx_mem_alloc(plain?plain:1U);
            okay=input && output && pm_read(f,at,input,packed);
            /* TiGGER advances an 8-bit XOR counter from zero for each file,
             * for stored data as well as the optional LZSS stream. */
            if(okay) {
                for(j=0;j<packed;++j) input[j]^=(uint8_t)(j+1U);
                if(e[12]) okay=ac_csq_lzss(input,packed,output,plain,pd);
                else if(plain) xx_mem_copy(output,input,plain);
            }
            xx_mem_free(input);
            if(!okay) { xx_mem_free(output); return false; }
            s->items[s->count-1U].memory=output;
            s->items[s->count-1U].compression_method=e[12];
        }
    }
    s->size=size; return true;
}

static bool ac_zoot_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[12],e[235]; uint32_t length,dirs,dir_size,stride,count,i;
    uint16_t version;
    int64_t size=pm_available(f),base;
    if(size<12 || !pm_read(f,0,h,12) || xx_mem_compare(h,"ZOT3",4)) return false;
    length=ac_u32(h+4); version=ac_u16(h+8); dirs=ac_u16(h+10);
    if(!dirs || (version!=0x272e && version!=0x2738 && version!=0x2739)) return false;
    dir_size=version==0x272e?116U:76U; stride=version==0x2739?235U:137U;
    if(length<dirs*dir_size || (length-dirs*dir_size)%stride || (uint64_t)length+12U>(uint64_t)size) return false;
    count=(length-dirs*dir_size)/stride; base=12+(int64_t)dirs*dir_size;
    if(count>1000000U) return false;
    for(i=0;i<count;++i) {
        uint32_t offset,n; char name[65]; size_t j,end=60;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,base+(int64_t)i*stride,e,stride)) return false;
        if(ac_u16(e+103)==0xfffeU || (version!=0x272e && ac_u16(e+105)>=20U)) continue;
        offset=ac_u32(e+93); n=ac_u32(e+97);
        while(end && (e[end-1U]==' ' || e[end-1U]==0)) --end;
        for(j=0;j<end;++j) name[j]=(e[j]>=32 && e[j]<127)?(char)e[j]:'_';
        name[end]=0;
        if(!ac_add(f,s,name,(int64_t)length+offset+11,n)) return false;
    }
    s->size=size; return true;
}

static bool ac_zlib_member(Abstractformat *f,pm_member *m,xx_io_device *output,xx_pd_struct *pd) {
    ac_buffer b={0}; int64_t used=0; pm_stream *s=(pm_stream *)m->context;
    bool result=ac_inflate_reserved(f,m->offset-f->base_address,m->packed_size,true,&b,&used,pd,
        s?(uint64_t)s->capacity*sizeof(pm_member):0) &&
        used==m->packed_size && b.size==(uint64_t)m->size && ac_write(output,b.data,b.size,pd);
    xx_mem_free(b.data); return result;
}
static bool ac_rdfz_strings(ac_cursor *c,const uint8_t ***strings,uint32_t *count,uint64_t room) {
    uint32_t i,n;
    const uint8_t **list;
    if(!ac_word(c,&n) || n>1000000U || n>(c->size-c->at)/4U || (uint64_t)n*sizeof(*list)>room) return false;
    list=(const uint8_t **)xx_mem_calloc(n?n:1U,sizeof(*list));
    if(!list) return false;
    *strings=list; *count=n;
    for(i=0;i<n;++i) {
        uint32_t length;
        list[i]=c->data+c->at;
        if(!ac_word(c,&length) || length>4096U || !ac_take(c,length,NULL)) return false;
    }
    return true;
}
static bool ac_rdfz_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[20],e[20],small[4]; ac_buffer b={0}; ac_cursor c;
    const uint8_t **extensions=NULL,**names=NULL;
    uint32_t packed,expected,ext_count=0,name_count=0,count,i;
    int64_t size=pm_available(f),used,table;
    bool result=false;
    if(size<20 || !pm_read(f,0,h,20) || xx_mem_compare(h,"RDFZ\4\0\0\0Zlib",12)) return false;
    packed=ac_u32(h+12); expected=ac_u32(h+16);
    if(packed<10U || expected>AC_MEMORY_LIMIT || (uint64_t)packed+20U>(uint64_t)size ||
       !ac_inflate(f,20,packed-4,true,&b,&used,pd) || used!=packed-4 || b.size!=expected || b.size<24U) goto done;
    c.data=b.data; c.size=b.size; c.at=16;
    if(ac_u32(b.data)!=2U || ac_u32(b.data+4)!=0U || ac_u32(b.data+8)!=4U ||
       xx_mem_compare(b.data+12,"Zlib",4) || b.capacity>ac_budget(f) ||
       !ac_rdfz_strings(&c,&extensions,&ext_count,ac_budget(f)-b.capacity) ||
       !ac_rdfz_strings(&c,&names,&name_count,ac_budget(f)-b.capacity-(uint64_t)ext_count*sizeof(*extensions)) || c.at!=c.size) goto done;
    table=16+(int64_t)packed;
    if(!pm_read(f,table,small,4)) goto done;
    count=ac_u32(small); table+=4;
    if(!count || count>1000000U || (uint64_t)table+(uint64_t)count*20U>(uint64_t)size) goto done;
    for(i=0;i<count;++i) {
        uint32_t offset,length,ext_index,name_index,method,plain;
        char label[96],name[65],extension[24]; size_t j,n;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,table+(int64_t)i*20,e,20)) goto done;
        offset=ac_u32(e); length=ac_u32(e+4); ext_index=ac_u32(e+8); name_index=ac_u32(e+12); method=ac_u32(e+16);
        if(offset<(uint64_t)table+(uint64_t)count*20U || ext_index>=ext_count || name_index>=name_count ||
           method>1U || (uint64_t)offset+length>(uint64_t)size) goto done;
        n=ac_u32(names[name_index]); if(n>64U) n=64U;
        for(j=0;j<n;++j) { uint8_t v=names[name_index][4+j]; name[j]=(v>=32 && v<127)?(char)v:'_'; } name[n]=0;
        n=ac_u32(extensions[ext_index]); if(n>23U) n=23U;
        for(j=0;j<n;++j) { uint8_t v=extensions[ext_index][4+j]; extension[j]=(v>=32 && v<127)?(char)v:'_'; } extension[n]=0;
        (void)xx_rt_snprintf(label,sizeof(label),"%s.%s",name,extension);
        plain=length;
        if(method) {
            if(length<10U || !pm_read(f,offset,small,4)) goto done;
            plain=ac_u32(small); offset+=4; length-=4;
            if(plain>AC_MEMORY_LIMIT) goto done;
        }
        if(!ac_room(f,s,(uint64_t)b.capacity+(ext_count+(uint64_t)name_count)*sizeof(*extensions)+
            (s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8U)*sizeof(pm_member):0)) ||
            !ac_add(f,s,label,offset,length)) goto done;
        s->items[s->count-1U].size=plain;
        if(method) { s->items[s->count-1U].read_all=ac_zlib_member; s->items[s->count-1U].context=s; s->items[s->count-1U].compression_method=8; }
    }
    s->size=size; result=true;
done:
    xx_mem_free(extensions); xx_mem_free(names); xx_mem_free(b.data); return result;
}

static bool ac_gzip_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[12],tail[8]; ac_buffer b={0}; int64_t size=pm_available(f),used=0;
    bool result=false;
    if(size<=131082 || !pm_read(f,0,h,12) || xx_mem_compare(h,"\xebs\x90\xf8sdfS\xb8\1\xde\xbe",12) ||
       !pm_read(f,131072,h,10) || h[0]!=31 || h[1]!=139 || h[2]!=8 || h[3]!=0 ||
       !ac_inflate(f,131082,size-131082,false,&b,&used,pd) ||
       !pm_read(f,131082+used,tail,8) || ac_u32(tail+4)!=(uint32_t)b.size ||
       ac_u32(tail)!=xx_crc32(XX_CRC_TYPE_CRC32,b.data,b.size) ||
       !ac_room(f,s,(uint64_t)b.capacity+b.size+sizeof(pm_member)*8U) ||
       !ac_add_memory(f,s,"beos-boot-image.img",b.data,b.size)) goto done;
    s->items[0].packed_size=used+18; s->items[0].compression_method=8; s->size=size; result=true;
done:
    xx_mem_free(b.data); return result;
}

static bool ac_solaris_member(Abstractformat *f,pm_member *m,xx_io_device *output,xx_pd_struct *pd) {
    uint8_t h[12],e[12]; uint32_t count,i;
    pm_stream *s=(pm_stream *)m->context;
    int64_t base=m->offset-f->base_address; uint64_t total=0;
    if(!pm_read(f,base,h,12) || xx_mem_compare(h,"\x19\x9eTG",4)) return false;
    count=ac_u32(h+4);
    if(!count || count>262144U) return false;
    for(i=0;i<count;++i) {
        ac_buffer b={0}; int64_t used=0; uint32_t plain,packed,offset; bool okay;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,base+12+(int64_t)i*12,e,12)) return false;
        plain=ac_u32(e); packed=ac_u32(e+4); offset=ac_u32(e+8);
        if(!plain || plain>AC_BLOCK_LIMIT || !packed || offset<12U+count*12U ||
           (uint64_t)offset+packed>(uint64_t)m->packed_size || total+plain>(uint64_t)m->size) return false;
        /* This install-media dialect counts one alignment byte after each
         * raw Deflate stream. That byte is not compressed input or a CRC. */
        okay=ac_inflate_reserved(f,base+offset,packed,false,&b,&used,pd,s?(uint64_t)s->capacity*sizeof(pm_member):0) && (used==packed || used==packed-1U) &&
            b.size==plain && ac_write(output,b.data,b.size,pd);
        xx_mem_free(b.data);
        if(!okay) return false;
        total+=plain;
    }
    return total==(uint64_t)m->size;
}
static bool ac_solaris_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[25],e[12]; int64_t size=pm_available(f),position=0;
    unsigned objects=0;
    if(size<1024 || !pm_read(f,0,h,25) || xx_mem_compare(h,"# PaCkAgE DaTaStReAm:zip\n",25)) return false;
    while(position<=size-512) {
        uint32_t length,count,block,i; int64_t body; uint64_t total=0;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,position,h,12)) return false;
        if(xx_mem_compare(h,"\x19\x9eTL",4)) {
            if(!objects) { position+=512; continue; }
            /* Install media commonly pad the remainder with FF or zero. */
            if(h[0]!=0 && h[0]!=255U) return false;
            position+=512; continue;
        }
        length=ac_u32(h+4); body=position+512;
        if(length<12U || (uint64_t)body+length>(uint64_t)size || !pm_read(f,body,h,12) ||
           xx_mem_compare(h,"\x19\x9eTG",4)) return false;
        count=ac_u32(h+4); block=ac_u32(h+8);
        if(!count || count>262144U || !block || block>AC_BLOCK_LIMIT || (uint64_t)count*12U+12U>length) return false;
        for(i=0;i<count;++i) {
            uint32_t plain,packed,offset;
            if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,body+12+(int64_t)i*12,e,12)) return false;
            plain=ac_u32(e); packed=ac_u32(e+4); offset=ac_u32(e+8);
            if(!plain || plain>block || !packed || offset<count*12U+12U ||
               (uint64_t)offset+packed>length || total+plain>AC_MEMORY_LIMIT) return false;
            total+=plain;
        }
        if(!ac_add(f,s,"package.cpio",body,length)) return false;
        s->items[s->count-1U].size=(int64_t)total;
        s->items[s->count-1U].read_all=ac_solaris_member;
        s->items[s->count-1U].context=s;
        s->items[s->count-1U].compression_method=8;
        position=body+(((int64_t)length+511)&~511LL);
        if(++objects>4096U) return false;
    }
    s->size=size; return objects!=0;
}
static bool ac_write(xx_io_device *output, const uint8_t *p, size_t n, xx_pd_struct *pd) {
    while(n) {
        size_t piece=n<xx_get_file_buffer_size()?n:xx_get_file_buffer_size();
        ssize_t wrote;
        if(pd && xx_pd_is_stopped(pd)) return false;
        if(!output) return true;
        wrote=xx_io_write(output,p,piece);
        if(wrote<=0 || (size_t)wrote>piece) return false;
        p+=(size_t)wrote; n-=(size_t)wrote;
    }
    return !(pd && xx_pd_is_stopped(pd));
}

static bool ac_dcs_decode(Abstractformat *f, pm_member *m, xx_io_device *output, xx_pd_struct *pd) {
#ifdef _WIN32
    typedef BOOL (WINAPI *ac_create_fn)(DWORD, void *, void **);
    typedef BOOL (WINAPI *ac_decode_fn)(void *, const void *, SIZE_T, void *, SIZE_T, SIZE_T *);
    typedef BOOL (WINAPI *ac_close_fn)(void *);
    HMODULE api=NULL;
    ac_create_fn create=NULL; ac_decode_fn decode=NULL; ac_close_fn close=NULL;
    void *handle=NULL; FARPROC proc;
    uint8_t h[12],bh[8],*packed=NULL,*plain=NULL;
    uint64_t position=12,produced=0;
    uint32_t i,count,total;
    bool result=false;
    pm_stream *s=(pm_stream *)m->context;
    uint64_t budget=ac_budget(f),reserve=s?(uint64_t)s->capacity*sizeof(pm_member):0;
    if(reserve>budget) return false;
    budget-=reserve;
    if(!pm_read(f,0,h,sizeof(h)) || xx_mem_compare(h,"DCS\1",4)) return false;
    count=ac_u32(h+4); total=ac_u32(h+8);
    if(!count || count>65536U || total!=(uint64_t)m->size) return false;
    /* Search only the system directory; never load an adjacent Cabinet.dll. */
    api=LoadLibraryExW(L"cabinet.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!api) goto done;
    proc=GetProcAddress(api,"CreateDecompressor");
    if(!proc) goto done;
    xx_mem_copy(&create,&proc,sizeof(create));
    proc=GetProcAddress(api,"Decompress");
    if(!proc) goto done;
    xx_mem_copy(&decode,&proc,sizeof(decode));
    proc=GetProcAddress(api,"CloseDecompressor");
    if(!proc) goto done;
    xx_mem_copy(&close,&proc,sizeof(close));
    if(!create((1U<<29)|5U,NULL,&handle)) goto done;
    for(i=0;i<count;++i) {
        uint32_t stored,expected,packed_size; SIZE_T actual=0;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)position,bh,sizeof(bh))) goto done;
        stored=ac_u32(bh); expected=ac_u32(bh+4);
        if(stored<5U || !expected || expected>AC_BLOCK_LIMIT || stored-4U>AC_BLOCK_LIMIT ||
           (uint64_t)expected+stored-4U>budget || produced+expected>total ||
           position+stored+4U>(uint64_t)pm_available(f)) goto done;
        packed_size=stored-4U;
        packed=(uint8_t *)xx_mem_alloc(packed_size); plain=(uint8_t *)xx_mem_alloc(expected);
        if(!packed || !plain || !pm_read(f,(int64_t)position+8,packed,packed_size) ||
           !decode(handle,packed,packed_size,plain,expected,&actual) || actual!=expected ||
           (pd && xx_pd_is_stopped(pd)) || !ac_write(output,plain,expected,pd)) goto done;
        xx_mem_free(packed); packed=NULL; xx_mem_free(plain); plain=NULL;
        position+=(uint64_t)stored+4U; produced+=expected;
    }
    result=produced==total && position==(uint64_t)pm_available(f);
done:
    xx_mem_free(packed); xx_mem_free(plain);
    if(handle && close && !close(handle)) result=false;
    if(api) FreeLibrary(api);
    return result;
#else
    (void)f; (void)m; (void)output; (void)pd;
    return false;
#endif
}

static bool ac_dcs_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    uint8_t h[12],bh[8];
    uint32_t count,total,i;
    uint64_t position=12,produced=0;
    int64_t size=pm_available(f);
    if(size<12 || !pm_read(f,0,h,sizeof(h)) || xx_mem_compare(h,"DCS\1",4)) return false;
    count=ac_u32(h+4); total=ac_u32(h+8);
    if(!count || count>65536U || !total || total>AC_MEMORY_LIMIT) return false;
    for(i=0;i<count;++i) {
        uint32_t stored,expected;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)position,bh,sizeof(bh))) return false;
        stored=ac_u32(bh); expected=ac_u32(bh+4);
        if(stored<5U || !expected || expected>AC_BLOCK_LIMIT || stored-4U>AC_BLOCK_LIMIT ||
           position+stored+4U>(uint64_t)size || produced+expected>total) return false;
        produced+=expected; position+=(uint64_t)stored+4U;
    }
    if(produced!=total || position!=(uint64_t)size || !ac_add(f,s,"payload.bin",12,size-12)) return false;
    s->items[0].size=total; s->items[0].read_all=ac_dcs_decode;
    s->items[0].context=s;
    s->size=size;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    if(pd && xx_pd_is_stopped(pd)) return false;
    switch(f->file_type) {
    case XX_FILE_TYPE_DCS: return ac_dcs_parse(f,s,pd);
    case XX_FILE_TYPE_MSKN1: case XX_FILE_TYPE_MSKN2: case XX_FILE_TYPE_MSKN3:
        return ac_mskn_parse(f,s,pd);
    case XX_FILE_TYPE_CSQ: return ac_csq_parse(f,s,pd);
    case XX_FILE_TYPE_ZOOT1: return ac_zoot_parse(f,s,pd);
    case XX_FILE_TYPE_RDFZ: return ac_rdfz_parse(f,s,pd);
    case XX_FILE_TYPE_ZBEOS: return ac_gzip_parse(f,s,pd);
    case XX_FILE_TYPE_SOLARISPKG_ZIP: return ac_solaris_parse(f,s,pd);
    default: return false;
    }
}
Abstractformat *xx_arc9_compressed_create(xx_io_device *d,int64_t b,xx_file_type_t type) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    const char *extension=type==XX_FILE_TYPE_DCS?"dcs":type==XX_FILE_TYPE_CSQ?"csq":
        type==XX_FILE_TYPE_ZOOT1?"zot":type==XX_FILE_TYPE_RDFZ?"rdfz":type==XX_FILE_TYPE_ZBEOS?"img":
        type==XX_FILE_TYPE_SOLARISPKG_ZIP?"image":"mskn";
    if(f) pm_init(f,d,b,type,extension);
    return f;
}
void xx_arc9_compressed_free(Abstractformat *f) {
    if(f) { xx_format_destroy(f); xx_mem_free(f); }
}
bool xx_arc9_compressed_probe(xx_io_device *d,int64_t b,xx_file_type_t type) {
    Abstractformat f; bool valid;
    pm_init(&f,d,b,type,"dcs"); valid=pm_valid(&f,NULL); xx_format_destroy(&f); return valid;
}
xx_file_type_t xx_arc9_compressed_detect(xx_io_device *d,int64_t base) {
    Abstractformat f; uint8_t h[25]; int64_t cursor;
    xx_file_type_t type=XX_FILE_TYPE_UNKNOWN; size_t size;
    if(!d || base<0 || xx_io_size(d)<base) return type;
    cursor=xx_io_tell(d); xx_mem_zero(&f,sizeof(f)); f.device=d; f.base_address=base;
    size=(uint64_t)pm_available(&f)<sizeof(h)?(size_t)pm_available(&f):sizeof(h);
    if(!pm_read(&f,0,h,size)) goto done;
    if(size>=4 && !xx_mem_compare(h,"DCS\1",4)) type=XX_FILE_TYPE_DCS;
    else if(size>=4 && !xx_mem_compare(h,"KSLZ",4)) type=XX_FILE_TYPE_MSKN1;
    else if(size>=9 && !xx_mem_compare(h,"_MCT\0KSLZ",9)) type=XX_FILE_TYPE_MSKN2;
    else if(size>=7 && !xx_mem_compare(h,"KSMSSTL",7)) type=XX_FILE_TYPE_MSKN3;
    else if(size>=8 && !xx_mem_compare(h,"TiGGER\1\7",8)) type=XX_FILE_TYPE_CSQ;
    else if(size>=4 && !xx_mem_compare(h,"ZOT3",4)) type=XX_FILE_TYPE_ZOOT1;
    else if(size>=12 && !xx_mem_compare(h,"RDFZ\4\0\0\0Zlib",12)) type=XX_FILE_TYPE_RDFZ;
    else if(size>=12 && !xx_mem_compare(h,"\xebs\x90\xf8sdfS\xb8\1\xde\xbe",12)) type=XX_FILE_TYPE_ZBEOS;
    else if(size>=25 && !xx_mem_compare(h,"# PaCkAgE DaTaStReAm:zip\n",25)) type=XX_FILE_TYPE_SOLARISPKG_ZIP;
done:
    (void)xx_io_seek64(d,cursor,SEEK_SET); return type;
}
