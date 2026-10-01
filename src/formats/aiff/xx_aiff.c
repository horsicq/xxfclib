/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/Docs/AIFF-1.3.pdf, https://raw.githubusercontent.com/libsndfile/libsndfile/master/src/aiff.c
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/aiff/xx_aiff.h"
#include "../xx_payload_members.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[32]; int64_t end,at=12,audio=-1,bytes=0; bool aifc,comm=false,sound=false,fver=false,pcm=true; uint64_t needed=0; uint32_t frames=0;
    if(!pm_read(f,0,h,12) || xx_rt_memcmp(h,"FORM",4)) return false;
    aifc=!xx_rt_memcmp(h+8,"AIFC",4); if(!aifc && xx_rt_memcmp(h+8,"AIFF",4)) return false;
    end=8+(int64_t)pm_be32(h+4); if(end<12 || end>pm_available(f)) return false;
    while(at<end) { uint32_t n; int64_t body; char name[16]; unsigned i;
        if((pd && xx_pd_is_stopped(pd)) || end-at<8 || !pm_read(f,at,h,8)) return false; n=pm_be32(h+4); body=at+8;
        if(n>(uint64_t)(end-body) || (int64_t)n+(n&1U)>end-body) return false;
        for(i=0;i<4;++i) name[i]=h[i]>=32 && h[i]<127 ? (char)h[i] : '_'; name[4]=0; xx_rt_memcpy(name+4,".bin",5);
        if(!xx_rt_memcmp(h,"COMM",4)) { unsigned ch,bits; uint16_t exponent;
            if(comm || n<(aifc ? 23U : 18U) || (!aifc && n!=18) || !pm_read(f,body,h,aifc ? 23 : 18)) return false;
            ch=pm_be16(h); frames=pm_be32(h+2); bits=pm_be16(h+6); exponent=pm_be16(h+8);
            if(!ch || !bits || bits>32 || (exponent&0x8000) || !(exponent&0x7FFF) || (exponent&0x7FFF)==0x7FFF || !(h[10]&128)) return false;
            needed=(uint64_t)frames*ch*((bits+7U)/8U); pcm=!aifc || !xx_rt_memcmp(h+18,"NONE",4) || !xx_rt_memcmp(h+18,"sowt",4);
            if(aifc && (uint32_t)23+h[22]>n) return false;
            if(!pm_add(f,s,"COMM.bin",body,n)) return false; comm=true;
        } else if(!xx_rt_memcmp(h,"SSND",4)) { uint32_t offset;
            if(sound || n<8 || !pm_read(f,body,h,8) || (offset=pm_be32(h))>n-8) return false;
            audio=body+8+offset; bytes=(int64_t)n-8-offset; sound=true;
        } else { if(!xx_rt_memcmp(h,"FVER",4)) { if(fver || n!=4 || !pm_read(f,body,h,4) || pm_be32(h)!=0xA2805140U) return false; fver=true; }
            if(!pm_add(f,s,name,body,n)) return false;
        }
        at=body+n+(n&1U);
    }
    if(!comm || (aifc && !fver) || (frames && !sound) || (pcm && needed>(uint64_t)bytes)) return false;
    if(sound && !pm_add(f,s,"sound.encoded",audio,pcm ? (int64_t)needed : bytes)) return false;
    s->size=end; return at==end;
}

void xx_aiff_init(xx_aiff *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_AIFF,"aiff"); } }
xx_aiff *xx_aiff_create(xx_io_device *d,int64_t b) { xx_aiff *r=(xx_aiff *)xx_mem_alloc(sizeof(*r)); if(r) xx_aiff_init(r,d,b); return r; }
void xx_aiff_destroy(xx_aiff *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_aiff_free(xx_aiff *r) { if(r) { xx_aiff_destroy(r); xx_mem_free(r); } }
bool xx_aiff_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_aiff_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
