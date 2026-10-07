/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original bounded archive adapters. Container grammars are implemented by
 * each reader; upstream sources are references for wire-format facts only. */
#ifndef XX_LEGACY_ARCHIVE_HELPERS_H
#define XX_LEGACY_ARCHIVE_HELPERS_H
#include "xxfclib/formats/legacy_archive/xx_legacy_archive.h"
#include "xx_payload_members.h"
#include "xxfclib/algo/ampk/xx_ampk.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/compress/xx_compress.h"
#include "xxfclib/formats/pp20/xx_pp20.h"
#include "xxfclib/data/xx_data.h"
#define AC_MAX_BYTES (64U * 1024U * 1024U)
#define AC_MAX_MEMBERS 65535U
typedef struct ac_blob { uint8_t *p; uint32_t n; uint64_t used, limit; xx_pd_struct *pd; } ac_blob;
static bool ac_poll(const ac_blob *b) { return !b->pd || !xx_pd_is_stopped(b->pd); }
static bool ac_error(ac_blob *b, const char *why) { xx_pd_set_error(b->pd, 1, why); return false; }
static bool ac_span(const ac_blob *b, uint32_t at, uint32_t n) { return at <= b->n && n <= b->n-at; }
static const xx_var *ac_option(Abstractformat *f, xx_meta_id_t id) {
    return xx_format_resolve_extra_parameter(f, ((xx_legacy_archive_info *)f)->parse_options, id);
}
static bool ac_load(Abstractformat *f, ac_blob *b, xx_pd_struct *pd) {
    const xx_var *v; int64_t n=pm_available(f); uint32_t at=0;
    xx_mem_zero(b,sizeof(*b)); b->pd=pd; b->limit=UINT64_C(256)*1024U*1024U;
    v=ac_option(f,XX_META_ID_OPT_MEMORY_LIMIT); if(v) b->limit=xx_var_get_u64(v);
    if(n<=0 || n>AC_MAX_BYTES || (uint64_t)n+sizeof(pm_stream)>b->limit || !ac_poll(b)) return false;
    b->p=(uint8_t *)xx_mem_alloc((size_t)n); if(!b->p) return false;
    b->n=(uint32_t)n; b->used=(uint64_t)n+sizeof(pm_stream);
    while(at<b->n) {
        uint32_t size=b->n-at; if(size>4096U) size=4096U;
        if(!ac_poll(b) || !pm_read(f,at,b->p+at,size) || !ac_poll(b)) { xx_mem_free(b->p); b->p=NULL; return false; }
        at+=size;
    }
    return true;
}
static uint8_t *ac_alloc(ac_blob *b, uint32_t n) {
    uint8_t *p;
    if(n>AC_MAX_BYTES || b->used>b->limit || n>b->limit-b->used || !ac_poll(b)) { ac_error(b,"archive memory limit exceeded"); return NULL; }
    p=(uint8_t *)xx_mem_alloc(n ? n : 1U); if(p) b->used+=n; return p;
}
static void ac_release(ac_blob *b, uint8_t *p, uint32_t n) { if(p) { xx_mem_free(p); b->used-=n; } }
static XXFC_MAYBE_UNUSED bool ac_compact(ac_blob *b,uint8_t **p,uint32_t old,uint32_t size) {
    uint8_t *next; if(size>old || !p || !*p) return false;
    next=(uint8_t *)xx_mem_realloc(*p,size?size:1U); if(!next) return false;
    *p=next; b->used-=old-size; return true;
}
static bool ac_emit(Abstractformat *f, pm_stream *s, ac_blob *b, const char *name, uint32_t at, uint32_t n) {
    const xx_var *v=ac_option(f,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    uint64_t growth=s->count==s->capacity?(s->capacity?s->capacity:8U)*sizeof(pm_member):0U;
    if(s->count>=AC_MAX_MEMBERS || (v && n>xx_var_get_u64(v)) || !ac_poll(b) || !ac_span(b,at,n) ||
        b->used>b->limit || growth>b->limit-b->used || !pm_add(f,s,name,at,n)) return false;
    b->used+=growth; return true;
}
static bool ac_memory(Abstractformat *f, pm_stream *s, ac_blob *b, const char *name, uint8_t *p, uint32_t n, uint32_t packed, uint16_t method) {
    const xx_var *v=ac_option(f,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if((v && n>xx_var_get_u64(v)) || !ac_emit(f,s,b,name,0,0)) { ac_release(b,p,n); return false; }
    s->items[s->count-1U].memory=p; s->items[s->count-1U].size=n;
    s->items[s->count-1U].packed_size=packed; s->items[s->count-1U].offset=-1;
    s->items[s->count-1U].compression_method=method; return true;
}
static XXFC_MAYBE_UNUSED bool ac_name(char *out, size_t capacity, const uint8_t *p, uint32_t n) {
    uint32_t i; if(!n || n>=capacity) return false;
    for(i=0;i<n && p[i];++i) { if(p[i]<32U || p[i]==127U) return false; out[i]=(char)p[i]; }
    if(!i) { return false; } out[i]=0; return true;
}
static XXFC_MAYBE_UNUSED uint16_t ac_crc16(const uint8_t *p, uint32_t n) {
    uint16_t c=0; uint32_t i; unsigned k;
    for(i=0;i<n;++i) { c^=p[i]; for(k=0;k<8;++k) c=(uint16_t)((c>>1)^((c&1U)?0xA001U:0)); }
    return c;
}
static XXFC_MAYBE_UNUSED uint32_t ac_crc32(const uint8_t *p, uint32_t n, uint32_t c) {
    uint32_t i; unsigned k;
    for(i=0;i<n;++i) { c^=p[i]; for(k=0;k<8;++k) c=(c>>1)^((c&1U)?UINT32_C(0xEDB88320):0); }
    return c;
}
/* CompDisk's historical Olaf checksum uses byte injection after shifting,
 * unlike the ordinary CCITT byte XOR into the high octet. */
static XXFC_MAYBE_UNUSED uint16_t ac_olaf(const uint8_t *p, uint32_t n) {
    uint16_t c=0; uint32_t i; unsigned k;
    for(i=0;i<n;++i) { uint16_t high=(uint16_t)(c&0xFF00U);
        for(k=0;k<8;++k) high=(uint16_t)((high<<1)^((high&0x8000U)?0x1021U:0));
        c=(uint16_t)(high^(c<<8)^p[i]); }
    return c;
}
/* Unix compress streams in these containers omit the .Z transport header. */
static bool ac_unix_ex(ac_blob *b, const uint8_t *in, uint32_t packed, uint8_t *out, uint32_t n, uint8_t width, uint32_t *written) {
    uint8_t *framed; xx_io_device *src=NULL,*dst=NULL; int64_t size=0; bool ok=false; uint64_t workspace;
    if(width<9U || width>16U || packed>AC_MAX_BYTES-3U) return false;
    workspace=4U*(UINT64_C(1)<<width)+2U*(uint64_t)xx_get_file_buffer_size();
    if(!xx_get_file_buffer_size()) workspace+=2U*XX_DEFAULT_FILE_BUFFER_SIZE;
    if(workspace>b->limit-b->used) return ac_error(b,"Unix compress workspace exceeds archive memory limit");
    b->used+=workspace;
    framed=ac_alloc(b,packed+3U); if(!framed) return false;
    framed[0]=0x1F; framed[1]=0x9D; framed[2]=(uint8_t)(width|0x80U); xx_rt_memcpy(framed+3,in,packed);
    src=xx_io_mem_open_ro(framed,packed+3U); dst=xx_io_mem_open(out,n);
    if(src && dst) ok=xx_compress_decode_device(src,0,packed+3U,dst,&size,b->pd) && size>=0 && (uint64_t)size<=n && ac_poll(b);
    if(ok && written) *written=(uint32_t)size;
    if(src) { xx_io_close(src); } if(dst) xx_io_close(dst); ac_release(b,framed,packed+3U); b->used-=workspace; return ok;
}
static XXFC_MAYBE_UNUSED bool ac_unix(ac_blob *b,const uint8_t *in,uint32_t packed,uint8_t *out,uint32_t n,uint8_t width) {
    uint32_t written=0; return ac_unix_ex(b,in,packed,out,n,width,&written) && written==n;
}
typedef struct ac_bits { const uint8_t *p; uint32_t n; uint64_t bit; bool failed; bool lsb; } ac_bits;
static uint32_t ac_bits_get(ac_bits *r,unsigned n) {
    uint32_t v=0; unsigned i;
    if(n>32U || r->bit>(uint64_t)r->n*8U || n>(uint64_t)r->n*8U-r->bit) { r->failed=true; return 0; }
    for(i=0;i<n;++i) { uint32_t b=(r->p[(size_t)(r->bit>>3U)]>>(r->lsb?(r->bit&7U):(7U-(r->bit&7U))))&1U;
        ++r->bit; if(r->lsb) v|=b<<i; else v=(v<<1U)|b; }
    return v;
}
static bool ac_rle90(ac_blob *b,const uint8_t *in,uint32_t packed,uint8_t *out,uint32_t n) {
    uint32_t i=0,at=0;
    while(i<packed && at<n) {
        uint8_t c=in[i++]; uint32_t count=1;
        if((at&4095U)==0 && !ac_poll(b)) return false;
        if(c==0x90U) { if(i>=packed) return false; count=in[i++];
            if(!count) { c=0x90U; count=1; } else { if(!at) return false; c=out[at-1U]; --count; } }
        if(count>n-at) { return false; } while(count--) out[at++]=c;
    }
    return at==n && i==packed && ac_poll(b);
}
/* AMPK/Amiga Plus LZSS: the same MIT codec grammar as algo/ampk, with
 * cancellation inside the token walk and strict declared output bounds. */
static XXFC_MAYBE_UNUSED bool ac_ampk(ac_blob *b,const uint8_t *in,uint32_t packed,uint8_t *out,uint32_t n) {
    uint8_t ring[4096]; uint32_t at=0,pos=0,head=4078,flags=0,left=0;
    xx_rt_memset(ring,' ',4078); xx_mem_zero(ring+4078,18);
    while(at<n) {
        uint32_t length=1,source=0; uint8_t value;
        if(!ac_poll(b)) return false;
        if(!left) { if(pos>=packed) return false; flags=in[pos++]; left=8; }
        if(flags&1U) { if(pos>=packed) return false; value=in[pos++]; }
        else { if(packed-pos<2U) return false; source=in[pos]|((uint32_t)(in[pos+1U]&0xF0U)<<4U); length=(in[pos+1U]&15U)+3U; pos+=2U; value=0; }
        if(length>n-at) return false;
        while(length--) { uint8_t c=flags&1U?value:ring[source++&4095U]; out[at++]=c; ring[head++&4095U]=c; }
        flags>>=1U; --left;
    }
    return ac_poll(b);
}
/* Squeeze's explicit little-endian binary tree, with -257 as EOF. */
static bool ac_squeeze(ac_blob *b,const uint8_t *in,uint32_t packed,uint8_t *out,uint32_t cap,uint32_t *written) {
    int16_t child[512]; uint16_t nodes; uint32_t i,at=0; ac_bits bits;
    if(packed<2U || (nodes=xx_data_get_u16(in, 2, 0, false))>256U || 2U+4U*nodes>packed) return false;
    child[0]=child[1]=-257;
    for(i=0;i<2U*nodes;++i) { int16_t v=(int16_t)xx_data_get_u16(in+2U+i*2U, 2, 0, false);
        if(v>=0 ? (uint16_t)v>=nodes : v< -257) { return false; } child[i]=v; }
    bits.p=in+2U+4U*nodes; bits.n=packed-2U-4U*nodes; bits.bit=0; bits.failed=false; bits.lsb=true;
    for(;;) { int node=0; unsigned depth=0;
        if((at&4095U)==0 && !ac_poll(b)) return false;
        do { if(depth++>nodes || bits.failed) return false; node=child[(unsigned)node*2U+ac_bits_get(&bits,1)]; } while(node>=0);
        if(bits.failed) { return false; } if(node==-257) break;
        if(at>=cap) { return false; } out[at++]=(uint8_t)(-node-1);
    }
    *written=at; return ac_poll(b);
}
/* Warp applies RLE90 after its optional LZW or Squeeze transport. */
static XXFC_MAYBE_UNUSED bool ac_warp_decode(ac_blob *b,const uint8_t *in,uint32_t packed,uint8_t *out,uint32_t n,uint16_t method) {
    uint8_t *mid; uint32_t cap,written=0; bool ok=false;
    if(!method) { if(packed!=n) return false; xx_rt_memcpy(out,in,n); return true; }
    if(method==3U) return ac_rle90(b,in,packed,out,n);
    if(n>(AC_MAX_BYTES-512U)/2U) { return false; } cap=n*2U+512U; mid=ac_alloc(b,cap); if(!mid) return false;
    if(method==1U && packed && in[0]==12U) ok=ac_unix_ex(b,in+1U,packed-1U,mid,cap,12U,&written);
    else if(method==2U) ok=ac_squeeze(b,in,packed,mid,cap,&written);
    if(ok) ok=ac_rle90(b,mid,written,out,n);
    ac_release(b,mid,cap); return ok;
}
typedef struct ac_reverse_bits { const uint8_t *p; uint32_t pos,buffer; unsigned left; bool failed; } ac_reverse_bits;
static uint32_t ac_reverse_get(ac_reverse_bits *r,unsigned width) {
    uint32_t value=0; while(width--) { if(!r->left) { if(!r->pos) { r->failed=true; return 0; } r->buffer=r->p[--r->pos]; r->left=8; }
        value=(value<<1U)|(r->buffer&1U); r->buffer>>=1U; --r->left; } return value;
}
/* Backwards PowerPacker grammar, using the library's MIT PP20 decoder facts.
 * No synthesized transport or hidden duplicate input/output allocations. */
static XXFC_MAYBE_UNUSED bool ac_pp(ac_blob *b,const uint8_t *in,uint32_t packed,uint8_t *out,uint32_t n,const uint8_t widths[4]) {
    ac_reverse_bits bits; uint32_t at=n,i;
    if(packed<8U || (packed&3U) || xx_data_get_u32(in+packed-4U, 4, 0, true)>>8U!=n || in[packed-1U]>31U) return false;
    for(i=0;i<4U;++i) if(widths[i]<9U || widths[i]>16U || (i && widths[i]<widths[i-1U])) return false;
    xx_mem_zero(&bits,sizeof(bits)); bits.p=in; bits.pos=packed-4U; ac_reverse_get(&bits,in[packed-1U]);
    while(at) {
        uint32_t value,length=0,cls,offset; unsigned width;
        if(bits.failed || !ac_poll(b)) return false;
        if(!ac_reverse_get(&bits,1U)) {
            do { value=ac_reverse_get(&bits,2U); if(bits.failed || value>at-length) return false; length+=value; } while(value==3U);
            if(length>=at) { return false; } ++length;
            while(length--) { if((at&4095U)==0 && !ac_poll(b)) return false; out[--at]=(uint8_t)ac_reverse_get(&bits,8U); }
            if(bits.failed) { return false; } if(!at) break;
        }
        cls=ac_reverse_get(&bits,2U); width=widths[cls]; length=cls+2U;
        if(cls==3U) { if(!ac_reverse_get(&bits,1U)) width=7U; offset=ac_reverse_get(&bits,width);
            do { value=ac_reverse_get(&bits,3U); if(bits.failed || value>at || length>at-value) return false; length+=value; } while(value==7U);
        } else offset=ac_reverse_get(&bits,width);
        if(bits.failed || length>at) return false;
        while(length--) { if((at&4095U)==0 && !ac_poll(b)) return false; if(offset>=n-at) return false; out[at-1U]=out[at+offset]; --at; }
    }
    return !bits.failed && ac_poll(b);
}
static xx_archive_record_state *ac_records(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    xx_legacy_archive_info *r=(xx_legacy_archive_info *)f; const xx_list_s *old=r->parse_options;
    xx_archive_record_state *st; r->parse_options=options; st=pm_create_records(f,options,pd); r->parse_options=old; return st;
}
static const xx_archive_record *ac_current(Abstractformat *f,xx_archive_record_state *st) {
    const xx_archive_record *r=pm_current(f,st); const char *note=((xx_legacy_archive_info *)f)->note;
    if(r && note && !xx_archive_record_set_meta_str(&st->current_record,XX_META_ID_COMMENT,note)) return NULL;
    return r;
}
#define AC_PARSE(parser) \
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { \
    ac_blob b; bool ok; xx_legacy_archive_info *r=(xx_legacy_archive_info *)f; \
    r->incomplete=false; r->note=NULL; r->number_of_records=0; if(!ac_load(f,&b,pd)) return false; \
    ok=parser(f,s,&b) && ac_poll(&b); if(ok) { s->size=b.n; r->number_of_records=s->count; } \
    else if(pd && !pd->last_error && !xx_pd_is_stopped(pd)) xx_pd_set_error(pd,1,"malformed archive or unsupported compression variant"); \
    xx_mem_free(b.p); return ok; }
#define AC_DEFINE(stem,type,ext) \
void xx_##stem##_init(xx_##stem *r,xx_io_device *d,int64_t base) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,base,type,ext); r->format.create_archive_records_reading=ac_records; r->format.get_current_archive_record=ac_current; } } \
xx_##stem *xx_##stem##_create(xx_io_device *d,int64_t base) { xx_##stem *r=(xx_##stem *)xx_mem_alloc(sizeof(*r)); if(r) xx_##stem##_init(r,d,base); return r; } \
void xx_##stem##_destroy(xx_##stem *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); } \
void xx_##stem##_free(xx_##stem *r) { if(r) { xx_##stem##_destroy(r); xx_mem_free(r); } }
#endif
