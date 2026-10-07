/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.rfc-editor.org/rfc/rfc9639.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/flac/xx_flac.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

typedef struct fl_bits { Abstractformat *f; int64_t bit,end,cache_at; size_t cached,capacity; uint8_t *cache; xx_pd_struct *pd; } fl_bits;
static bool fl_get(fl_bits *r,unsigned n,uint32_t *v) {
    unsigned i; uint32_t value=0;
    if(n>32 || r->bit<0 || r->bit>r->end || n>(uint64_t)(r->end-r->bit)) return false;
    for(i=0;i<n;++i) {
        int64_t at=r->bit/8; unsigned shift=7U-(unsigned)(r->bit&7);
        if(!r->cache) { r->cache=(uint8_t *)xx_mem_alloc(r->capacity); if(!r->cache) return false; }
        if(at<r->cache_at || at-r->cache_at>=(int64_t)r->cached) {
            int64_t left=(r->end+7)/8-at; size_t take=(uint64_t)left>r->capacity ? r->capacity : (size_t)left;
            if((r->pd && xx_pd_is_stopped(r->pd)) || !pm_read(r->f,at,r->cache,take)) return false;
            r->cache_at=at; r->cached=take;
        }
        value=(value<<1)|((r->cache[(size_t)(at-r->cache_at)]>>shift)&1U); ++r->bit;
    }
    *v=value; return true;
}
static bool fl_skip(fl_bits *r,uint64_t n) { if(r->bit>r->end || n>(uint64_t)(r->end-r->bit)) return false; r->bit+=(int64_t)n; return true; }
static bool fl_unary(fl_bits *r,uint32_t *v) {
    uint32_t bit,zeros=0; do { if(!fl_get(r,1,&bit) || zeros==UINT32_MAX) return false; if(!bit) ++zeros; } while(!bit);
    *v=zeros; return true;
}
static bool fl_residual(fl_bits *r,uint32_t samples,unsigned order) {
    uint32_t method,p,part; unsigned width; uint32_t each;
    if(!fl_get(r,2,&method) || method>1 || !fl_get(r,4,&p) || (samples&((1U<<p)-1U))) return false;
    width=method ? 5U : 4U; each=samples>>p; if(each<order) return false;
    for(part=0;part<(1U<<p);++part) {
        uint32_t k,n=each-(part==0 ? order : 0U),j;
        if(!fl_get(r,width,&k)) return false;
        if(k==((1U<<width)-1U)) { uint32_t raw; if(!fl_get(r,5,&raw) || !fl_skip(r,(uint64_t)n*raw)) return false; }
        else for(j=0;j<n;++j) { uint32_t q; if(!fl_unary(r,&q) || !fl_skip(r,k)) return false; }
    }
    return true;
}
static bool fl_crc(Abstractformat *f,int64_t at,int64_t end,unsigned width,uint32_t expected) {
    size_t capacity=xx_get_file_buffer_size();uint8_t *buf=NULL;bool ok=false;uint8_t crc8=0U;xx_crc_context ctx;
    if(width!=8U && !xx_crc_context_init_type(&ctx,XX_CRC_TYPE_CRC16_BUYPASS))return false;
    while(at<end) {
        size_t n;
        if(!buf) {if((uint64_t)(end-at)<capacity)capacity=(size_t)(end-at);buf=(uint8_t *)xx_mem_alloc(capacity);if(!buf)goto done;}
        n=(uint64_t)(end-at)>capacity ? capacity:(size_t)(end-at);
        if(!pm_read(f,at,buf,n))goto done;
        if(width==8U)crc8=xx_crc8_calc(crc8,buf,n);else xx_crc_context_update(&ctx,buf,n);
        at+=(int64_t)n;
    }
    ok=(width==8U ? crc8:(uint32_t)xx_crc_context_final(&ctx))==expected;
done: xx_mem_free(buf);return ok;
}
static bool fl_frame(Abstractformat *f,int64_t at,int64_t limit,uint32_t rate,unsigned channels,unsigned depth,uint64_t frame,uint64_t preceding,uint32_t *block,int64_t *end,xx_pd_struct *pd) {
    uint8_t h[32]; size_t size=limit-at>32 ? 32U : (size_t)(limit-at),n=4; unsigned bc,rc,ca,dc,len=1,i; uint64_t number; uint32_t bs,sr,bits; fl_bits r={0}; bool buffer_result=false; const size_t io_capacity=xx_get_file_buffer_size();
    static const uint32_t rates[12]={0,88200,176400,192000,8000,16000,22050,24000,32000,44100,48000,96000};
    static const unsigned depths[8]={0,8,12,0,16,20,24,32};
    if(size<8 || !pm_read(f,at,h,size) || h[0]!=255 || (h[1]&0xFE)!=0xF8 || (h[3]&1)) { buffer_result = (false); goto buffer_done; }
    bc=h[2]>>4; rc=h[2]&15; ca=h[3]>>4; dc=(h[3]>>1)&7;
    if(!bc || rc==15 || ca>10 || dc==3 || (ca<8 ? ca+1 : 2U)!=channels || (dc && depths[dc]!=depth)) { buffer_result = (false); goto buffer_done; }
    if(h[n]<128) number=h[n++]; else {
        uint8_t mask=128; while(h[n]&mask) { ++len; mask>>=1; }
        --len; if(len<2 || len>7 || n+len>size || (!(h[1]&1) && len>6)) { buffer_result = (false); goto buffer_done; }
        number=h[n++]&((1U<<(7-len))-1U);
        for(i=1;i<len;++i) { if((h[n]&192)!=128) { buffer_result = (false); goto buffer_done; } number=(number<<6)|(h[n++]&63); }
        if(number<(len==2 ? 128ULL : 1ULL<<(5U*len-4U))) { buffer_result = (false); goto buffer_done; }
    }
    if(number!=((h[1]&1) ? preceding : frame)) { buffer_result = (false); goto buffer_done; }
    bs=bc==1 ? 192U : bc<=5 ? 576U<<(bc-2) : bc>=8 ? 256U<<(bc-8) : 0;
    if(bc==6) { if(n>=size) { buffer_result = (false); goto buffer_done; } bs=(uint32_t)h[n++]+1; }
    if(bc==7) { if(n+2>size) { buffer_result = (false); goto buffer_done; } bs=(uint32_t)xx_data_get_u16(h+n, 2, 0, true)+1; n+=2; if(bs==65536) { buffer_result = (false); goto buffer_done; } }
    sr=rc<12 ? rates[rc] : 0;
    if(rc==12) { if(n>=size) { buffer_result = (false); goto buffer_done; } sr=(uint32_t)h[n++]*1000; }
    if(rc==13 || rc==14) { if(n+2>size) { buffer_result = (false); goto buffer_done; } sr=xx_data_get_u16(h+n, 2, 0, true); n+=2; if(rc==14) sr*=10; }
    if((rc && sr!=rate) || n>=size || !fl_crc(f,at,at+(int64_t)n,8,h[n])) { buffer_result = (false); goto buffer_done; }
    ++n; xx_mem_zero(&r,sizeof(r)); r.f=f; r.bit=(at+(int64_t)n)*8; r.end=limit*8; r.cache_at=-1; r.pd=pd;r.capacity=io_capacity;
    for(i=0;i<channels;++i) {
        uint32_t sh,waste=0; unsigned effective=depth,type,order;
        if((ca==8 && i==1) || (ca==9 && i==0) || (ca==10 && i==1)) ++effective;
        if(!fl_get(&r,8,&sh) || (sh&128)) { buffer_result = (false); goto buffer_done; }
        type=(sh>>1)&63;
        if(sh&1) { if(!fl_unary(&r,&waste) || waste>=effective-1) { buffer_result = (false); goto buffer_done; } effective-=waste+1; }
        if(type==0) { if(!fl_skip(&r,effective)) { buffer_result = (false); goto buffer_done; } }
        else if(type==1) { if(!fl_skip(&r,(uint64_t)bs*effective)) { buffer_result = (false); goto buffer_done; } }
        else if(type>=8 && type<=12) { order=type-8; if(order>bs || !fl_skip(&r,(uint64_t)order*effective) || !fl_residual(&r,bs,order)) { buffer_result = (false); goto buffer_done; } }
        else if(type>=32) { order=type-31; if(order>bs || !fl_skip(&r,(uint64_t)order*effective) || !fl_get(&r,4,&bits) || bits==15 || !fl_skip(&r,5U+(uint64_t)order*(bits+1U)) || !fl_residual(&r,bs,order)) { buffer_result = (false); goto buffer_done; } }
        else { buffer_result = (false); goto buffer_done; }
    }
    if(r.bit&7) { if(!fl_get(&r,8U-(unsigned)(r.bit&7),&bits) || bits) { buffer_result = (false); goto buffer_done; } }
    if(!fl_skip(&r,16)) { buffer_result = (false); goto buffer_done; }
    *end=r.bit/8; *block=bs; { buffer_result = (fl_crc(f,at,*end,16,0)); goto buffer_done; }

buffer_done:
    xx_mem_free(r.cache);
    return buffer_result;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[38]; int64_t at=4,limit=pm_available(f); unsigned last=0,type; uint32_t size,rate=0,minblock=0,maxblock=0; unsigned channels=0,depth=0; uint64_t total=0,samples=0,frames=0;
    if(limit<42 || limit>INT64_MAX/8 || !pm_read(f,0,h,4) || xx_rt_memcmp(h,"fLaC",4)) return false;
    do { char name[40]; if(!pm_read(f,at,h,4)) return false; last=h[0]>>7; type=h[0]&127; size=(uint32_t)h[1]<<16|(uint32_t)h[2]<<8|h[3]; at+=4;
        if(size>(uint64_t)(limit-at) || type>6 || (!rate && type!=0)) return false;
        if(type==0) { uint64_t v; if(rate || size!=34 || !pm_read(f,at,h,34)) return false; minblock=xx_data_get_u16(h, 2, 0, true); maxblock=xx_data_get_u16(h+2, 2, 0, true); v=xx_data_get_u64(h+10, 8, 0, true); rate=(uint32_t)(v>>44); channels=(unsigned)((v>>41)&7)+1; depth=(unsigned)((v>>36)&31)+1; total=v&0xFFFFFFFFFULL;
            if(minblock<16 || maxblock<minblock || !rate || depth<4) return false;
        }
        if(type==2 && size<4) return false;
        if(type==3 && size%18) return false;
        if(type!=1) { xx_rt_snprintf(name,sizeof(name),"metadata-%u.bin",type); if(!pm_add(f,s,name,at,size)) return false; }
        at+=size;
    } while(!last);
    while(at<limit && (!total || samples<total)) { uint32_t block; int64_t end; char name[40];
        if(!fl_frame(f,at,limit,rate,channels,depth,frames,samples,&block,&end,pd) || block>maxblock || (block<minblock && (!total || samples+block!=total))) return false;
        xx_rt_snprintf(name,sizeof(name),"frame-%u.flac-frame",(unsigned)frames);
        if(!pm_add(f,s,name,at,end-at)) { return false; } samples+=block; ++frames; at=end;
    }
    if((total && samples!=total) || (!frames && total)) { return false; } s->size=at; return true;
}

void xx_flac_init(xx_flac *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_FLAC,"flac"); } }
xx_flac *xx_flac_create(xx_io_device *d,int64_t b) { xx_flac *r=(xx_flac *)xx_mem_alloc(sizeof(*r)); if(r) xx_flac_init(r,d,b); return r; }
void xx_flac_destroy(xx_flac *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_flac_free(xx_flac *r) { if(r) { xx_flac_destroy(r); xx_mem_free(r); } }
bool xx_flac_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_flac_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
