/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded NSIS2/3 extract-file reader. Framing facts are defined by the
 * official NSIS Source/exehead/fileform.{h,c}; command-table facts also match
 * the project's MIT XArchive/installers/xnsis.cpp. No scripts execute.
 * Modified BZip2 decoding retains its own bzip2 license in this directory. */
#include "xxfclib/formats/nsis/xx_nsis.h"
#include "../xx_legacy_archive.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "nsis_bzip2.h"
typedef struct ns_alloc { uint64_t size; } ns_alloc;
static void *ns_allocate(void *opaque,int items,int bytes) {
    ac_blob *b=(ac_blob *)opaque; uint64_t n; ns_alloc *p;
    if(items<0 || bytes<0 || (n=(uint64_t)(unsigned)items*(unsigned)bytes)>AC_MAX_BYTES-sizeof(*p)) return NULL;
    p=(ns_alloc *)ac_alloc(b,(uint32_t)n+sizeof(*p)); if(!p) return NULL; p->size=n+sizeof(*p); return p+1;
}
static void ns_free(void *opaque,void *data) { ns_alloc *p; if(!data) return; p=((ns_alloc *)data)-1; ac_release((ac_blob *)opaque,(uint8_t *)p,(uint32_t)p->size); }
static bool ns_lzma(const uint8_t *p,uint32_t n) { return n>=7U && p[0]==0x5DU && !p[1] && !p[2] && !p[5] && !(p[6]&128U); }
static bool ns_decode(ac_blob *b,const uint8_t *in,uint32_t packed,uint8_t *out,uint32_t cap,uint32_t *written,unsigned method) {
    xx_io_device *src; size_t n=0; bool ok=false;
    if(!ac_poll(b)) return false;
    if(!method) { if(packed>cap) return false; xx_rt_memcpy(out,in,packed); *written=packed; return true; }
    if(method==3U) {
        nsis_bzstream z; int status=BZ_OK; uint32_t at=0;
        xx_mem_zero(&z,sizeof(z)); z.bzalloc=ns_allocate; z.bzfree=ns_free; z.opaque=b;
        if(nsis_BZ2_bzDecompressInit(&z,0,0)!=BZ_OK) return false;
        z.next_in=(unsigned char *)in; z.avail_in=packed;
        while(status==BZ_OK) {
            unsigned chunk=cap-at; unsigned before=z.avail_in; if(chunk>4096U) chunk=4096U;
            if(!chunk || !ac_poll(b)) { break; } z.next_out=out+at; z.avail_out=chunk;
            status=nsis_BZ2_bzDecompress(&z); at+=chunk-z.avail_out;
            if(status==BZ_OK && before==z.avail_in && chunk==z.avail_out) break;
        }
        ok=status==BZ_STREAM_END && ac_poll(b); nsis_BZ2_bzDecompressEnd(&z); if(ok) *written=at; return ok;
    }
    if(method==2U) {
        if(packed>=8U && in[0]<=1U && ns_lzma(in+1U,packed-1U)) { if(in[0]) return ac_error(b,"NSIS BCJ filter is unsupported"); ++in; --packed; }
        if(packed<5U || xx_data_get_u32(in+1U, 4, 0, false)>16U*1024U*1024U || (uint64_t)xx_data_get_u32(in+1U, 4, 0, false)+1024U*1024U>b->limit-b->used) return false;
    }
    src=xx_io_mem_open_ro(in,packed); if(!src) return false;
    if(method==2U) ok=xx_lzma_unpack_device_to_memory(src,5,packed-5U,in,5,-1,out,cap,&n,b->pd);
    else ok=xx_deflate_unpack_device_to_memory(src,0,packed,out,cap,&n,false,b->pd);
    xx_io_close(src); if(ok && n<=cap && ac_poll(b)) { *written=(uint32_t)n; return true; } return false;
}
static unsigned ns_method(const uint8_t *p,uint32_t n) {
    if(ns_lzma(p,n) || (n>=8U && p[0]<=1U && ns_lzma(p+1U,n-1U))) return 2U;
    if(n>=2U && p[0]==0x31U && p[1]<14U) { return 3U; } return 1U;
}
static bool ns_name(char name[96],const uint8_t *header,uint32_t strings,uint32_t end,uint32_t offset,bool unicode) {
    uint32_t at; unsigned n=0;
    if(offset>(end-strings)/(unicode?2U:1U)) { return false; } at=strings+offset*(unicode?2U:1U);
    while(at<end) {
        uint32_t value=unicode?(at+1U<end?xx_data_get_u16(header+at, 2, 0, false):UINT32_MAX):header[at]; at+=unicode?2U:1U;
        if(!value) { if(!n) return false; name[n]=0; return true; }
        if(n>=90U) return false;
        /* Variable-expansion instructions are retained as safe placeholders;
         * output paths never execute or resolve installer variables. */
        name[n++]=(char)(value>=32U && value<=126U?value:'_');
    }
    return false;
}
static bool ns_parse(Abstractformat *f,pm_stream *s,ac_blob *b) {
    uint32_t start=0,flags,size,header_size,data_size,pos,header_packed,header_n=0,cap,solid_capacity=0,solid_n=0,entries,count,strings,lang,i;
    uint8_t *header=NULL,*solid=NULL; bool non_solid,unicode; unsigned stride=8U,method; const uint8_t *data;
    const char *failure="NSIS header layout invalid/unsupported";
    if(b->n<32U) return false;
    if(b->p[0]=='M' && b->p[1]=='Z') {
        bool found=false; for(start=512U;start<=b->n-28U;start+=512U) { if(!ac_poll(b)) return false;
            if(xx_data_get_u32(b->p+start+4U, 4, 0, false)==0xDEADBEEFU && !xx_rt_memcmp(b->p+start+8U,"NullsoftInst",12U)) { found=true; break; } }
        if(!found) return false;
    }
    if(xx_data_get_u32(b->p+start+4U, 4, 0, false)!=0xDEADBEEFU || xx_rt_memcmp(b->p+start+8U,"NullsoftInst",12U)) return false;
    flags=xx_data_get_u32(b->p+start, 4, 0, false); header_size=xx_data_get_u32(b->p+start+20U, 4, 0, false); size=xx_data_get_u32(b->p+start+24U, 4, 0, false);
    if(flags&~15U || size<32U || !header_size || header_size>16U*1024U*1024U || !ac_span(b,start,size)) return false;
    data_size=size-28U;
    if((flags&8U) || !(flags&4U)) {
        uint32_t check_start=start?512U:0U;
        if(data_size<4U || (ac_crc32(b->p+check_start,start+size-4U-check_start,UINT32_MAX)^UINT32_MAX) != xx_data_get_u32(b->p+start+size-4U, 4, 0, false)) return ac_error(b,"NSIS archive CRC mismatch");
        data_size-=4U;
    }
    if(data_size<4U) return false;
    data=b->p+start+28U; header_packed=xx_data_get_u32(data, 4, 0, false);
    /* An 8MiB solid LZMA dictionary makes properties byte3==0x80 too.
     * Authenticate the properties grammar BEFORE interpreting the framing
     * high bit as a non-solid compressed-header length. */
    non_solid=header_packed==header_size || ((header_packed&0x80000000U)!=0U &&
        !ns_lzma(data,data_size) && !(data_size>=8U && data[0]<=1U && ns_lzma(data+1U,data_size-1U)));
    method=ns_method(data+(non_solid?4U:0U),data_size-(non_solid?4U:0U));
    if(non_solid) {
        header_packed&=0x7FFFFFFFU; if(header_packed>data_size-4U) return false;
        header=ac_alloc(b,header_size); if(!header) return false;
        if(!ns_decode(b,data+4U,header_packed,header,header_size,&header_n,(xx_data_get_u32(data, 4, 0, false)&0x80000000U)?method:0U) || header_n!=header_size) goto fail;
        pos=4U+header_packed;
    } else {
        cap=32U*1024U*1024U; if(b->used>=b->limit) return false; if(cap>b->limit-b->used) cap=(uint32_t)(b->limit-b->used);
        solid_capacity=cap; solid=ac_alloc(b,cap); if(!solid) return false;
        if(!ns_decode(b,data,data_size,solid,cap,&solid_n,method) || solid_n<4U+header_size || xx_data_get_u32(solid, 4, 0, false)!=header_size) { failure="NSIS solid stream decode failed"; goto fail; }
        header=solid+4U; header_n=header_size; pos=4U+header_size;
    }
    if(header_n<68U) goto fail;
    if(header_n>=100U) {
        bool wide=true; for(i=0;i<8U;++i) if(xx_data_get_u32(header+8U+i*12U, 4, 0, false)) wide=false; if(wide) stride=12U;
    }
    if(header_n<4U+stride*8U) goto fail;
    entries=xx_data_get_u32(header+4U+stride*2U, 4, 0, false); count=xx_data_get_u32(header+stride*3U, 4, 0, false);
    strings=xx_data_get_u32(header+4U+stride*3U, 4, 0, false); lang=xx_data_get_u32(header+4U+stride*4U, 4, 0, false);
    if(entries>header_n || count>(header_n-entries)/28U || strings>=lang || lang>header_n || lang-strings<2U || header[lang-1U]) goto fail;
    unicode=xx_data_get_u16(header+strings, 2, 0, false)==0U;
    if(unicode && (((lang-strings)&1U) || header[lang-2U])) goto fail;
    for(i=0;i<count;++i) {
        const uint8_t *command=header+entries+i*28U; char name[96]; uint32_t offset,packed,n,at; uint8_t *out;
        failure="NSIS extract-file command/name/payload invalid"; if(!ac_poll(b)) goto fail;
        if(xx_data_get_u32(command, 4, 0, false)!=20U) continue;
        if(!ns_name(name,header,strings,lang,xx_data_get_u32(command+8U, 4, 0, false),unicode)) goto fail;
        offset=xx_data_get_u32(command+12U, 4, 0, false);
        if(non_solid) {
            if(offset>data_size-pos || data_size-pos-offset<4U) { goto fail; } at=pos+offset;
            packed=xx_data_get_u32(data+at, 4, 0, false); n=packed&0x7FFFFFFFU;
            if(n>data_size-at-4U) goto fail;
            if(!(packed&0x80000000U)) { if(!ac_emit(f,s,b,name,start+28U+at+4U,n)) goto fail; continue; }
            cap=16U*1024U*1024U;
            { const xx_var *v=ac_option(f,XX_META_ID_OPT_MAX_MEMBER_SIZE); if(v && xx_var_get_u64(v)<cap) cap=(uint32_t)xx_var_get_u64(v); }
            if(b->used>=b->limit) { goto fail; } if(cap>b->limit-b->used) cap=(uint32_t)(b->limit-b->used);
            out=ac_alloc(b,cap); if(!out) goto fail;
            if(!ns_decode(b,data+at+4U,n,out,cap,&packed,ns_method(data+at+4U,n))) { ac_release(b,out,cap); goto fail; }
            if(!ac_compact(b,&out,cap,packed)) { ac_release(b,out,cap); goto fail; }
            if(!ac_memory(f,s,b,name,out,packed,n,(uint16_t)method)) goto fail;
        } else {
            if(offset>solid_n-pos || solid_n-pos-offset<4U) { goto fail; } at=pos+offset; n=xx_data_get_u32(solid+at, 4, 0, false);
            if(n>solid_n-at-4U) { goto fail; } out=ac_alloc(b,n); if(!out) goto fail; xx_rt_memcpy(out,solid+at+4U,n);
            if(!ac_memory(f,s,b,name,out,n,0,(uint16_t)method)) goto fail;
        }
    }
    ((xx_nsis *)f)->note="NSIS2/3 standard extract-file commands; decoded payloads, no installer execution; names use safe unresolved-variable placeholders";
    if(solid) ac_release(b,solid,solid_capacity); else ac_release(b,header,header_size);
    return s->count!=0;
fail:
    if(solid) ac_release(b,solid,solid_capacity); else if(header) ac_release(b,header,header_size);
    return ac_error(b,failure);
}
AC_PARSE(ns_parse)
AC_DEFINE(nsis,XX_FILE_TYPE_NSIS,"exe")
