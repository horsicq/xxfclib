/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded Wwise RIFF/RIFX chunk reader, based on container layout in
 * https://github.com/vgmstream/vgmstream/blob/master/src/meta/wwise.c
 * Exports stored metadata and encoded payload chunks, without codec decoding.
 * Plain WAV/RIFF streams without Wwise-specific evidence stay with WAV readers.
 */
#include "xxfclib/formats/audio_wwise_wem/xx_audio_wwise_wem.h"
#include "../xx_payload_members.h"

#ifndef XX_FILE_TYPE_AUDIO_WWISE_WEM
#define XX_FILE_TYPE_AUDIO_WWISE_WEM ((xx_file_type_t)1521)
#endif

static uint16_t ww16(const uint8_t *p,bool be) { return be ? pm_be16(p):pm_le16(p); }
static uint32_t ww32(const uint8_t *p,bool be) { return be ? pm_be32(p):pm_le32(p); }
static bool ww_tag(const uint8_t *p,const char *tag) { return !xx_rt_memcmp(p,tag,4); }
static bool ww_printable(const uint8_t *p) { unsigned i; for(i=0;i<4;++i) if(p[i]<0x20 || p[i]>0x7e) return false; return true; }
static bool ww_codec(unsigned v)
{
    switch(v) {
    case 0x3039: case 0x3040: case 0x3041: case 0x8311:
    case 0xaac0: case 0xfff0: case 0xfffb: case 0xfffc:
    case 0xffff: return true;
    default: return false;
    }
}
static bool ww_fmt(const uint8_t *p,bool be,unsigned *format)
{
    unsigned channels=ww16(p+2,be),rate=ww32(p+4,be);
    if (!channels || channels>32 || rate<1000 || rate>384000) return false;
    *format=ww16(p,be);
    return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    uint8_t h[12],ch[8],fmt[16];
    uint64_t p=12,limit;
    bool be,has_fmt=false,has_data=false,distinct=false,has_xma=false;
    unsigned chunks=0,format=0;
    char label[32];
    int64_t available=pm_available(f);
    if (available<36 || !pm_read(f,0,h,sizeof(h))) return false;
    be=ww_tag(h,"RIFX");
    if (!be && !ww_tag(h,"RIFF")) return false;
    if (!ww_tag(h+8,"WAVE") && !ww_tag(h+8,"XWMA")) return false;
    if (ww32(h+4,be)<4) return false;
    limit=(uint64_t)available;
    while (p<limit) {
        uint64_t size,next;
        if (++chunks>4096U || (pd && xx_pd_is_stopped(pd)) ||
            limit-p<8 || !pm_read(f,(int64_t)p,ch,8) || !ww_printable(ch)) return false;
        size=ww32(ch+4,be);
        if (size>limit-p-8) return false;
        next=p+8+size;
        if (ww_tag(ch,"fact")) return false; /* Wwise does not use fact. */
        if (ww_tag(ch,"fmt ")) {
            if (has_fmt || size<16 || !pm_read(f,(int64_t)(p+8),fmt,16)) return false;
            has_fmt=true;
            /* vgmstream uses RIFX endianness for fmt, while DIE's Wwise
             * detector observes little-endian fmt fields even in RIFX.
             * Accept the mixed variant when BE fields are not plausible.
             * Wwise Vorbis/Opus/AAC legitimately use zero block alignment. */
            if (!ww_fmt(fmt,be,&format) && (!be || !ww_fmt(fmt,false,&format))) return false;
            if (ww_codec(format)) distinct=true;
        } else if (ww_tag(ch,"data")) {
            if (has_data) return false;
            has_data=true;
        } else if (ww_tag(ch,"XMA2")) {
            if (has_xma || size<16) return false;
            has_xma=true; distinct=true;
        } else if (ww_tag(ch,"vorb") || ww_tag(ch,"WiiH") ||
                   ww_tag(ch,"akd ")) {
            distinct=true;
        }
        (void)xx_rt_snprintf(label,sizeof(label),"%c%c%c%c.bin",ch[0],ch[1],ch[2],ch[3]);
        if (!pm_add(f,s,label,(int64_t)(p+8),(int64_t)size)) return false;
        /* Wwise usually omits RIFF's odd-byte pad. Accept an actual zero pad
         * when present, but never infer a pad from the declared RIFF size. */
        if ((size&1U) && next<limit) {
            uint8_t c;
            if (!pm_read(f,(int64_t)next,&c,1)) return false;
            if (!c) ++next;
        }
        p=next;
    }
    if (p!=limit || !has_data || (!has_fmt && !has_xma) || !distinct) return false;
    s->size=(int64_t)limit;
    return true;
}

void xx_audio_wwise_wem_init(xx_audio_wwise_wem *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_AUDIO_WWISE_WEM,"wem"); } }
xx_audio_wwise_wem *xx_audio_wwise_wem_create(xx_io_device *d,int64_t b) { xx_audio_wwise_wem *r=(xx_audio_wwise_wem *)xx_mem_alloc(sizeof(*r)); if(r) xx_audio_wwise_wem_init(r,d,b); return r; }
void xx_audio_wwise_wem_destroy(xx_audio_wwise_wem *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_audio_wwise_wem_free(xx_audio_wwise_wem *r) { if(r) { xx_audio_wwise_wem_destroy(r); xx_mem_free(r); } }
bool xx_audio_wwise_wem_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_audio_wwise_wem_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
