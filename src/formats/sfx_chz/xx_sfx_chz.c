/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xchz.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_chz/xx_sfx_chz.h"
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"

static bool w6_chz_at(Abstractformat *f,pm_stream *s,int64_t at,xx_pd_struct *pd) {
    int64_t limit=pm_available(f);uint8_t h[24];unsigned depth=0,records=0;
    while(at<limit) { uint32_t tag;uint64_t n;uint16_t name;char label[48];
        if(wg_stop(pd) || ++records>4096 || !pm_read(f,at,h,4)) return false;tag=pm_le32(h);
        if(tag==0x46684353) { uint32_t raw;if(!pm_read(f,at,h,24) || (n=pm_le32(h+4))>INT32_MAX || (raw=pm_le32(h+8))>INT32_MAX || !(name=pm_le16(h+22)) || name>4096 || n<24U+name || !wg_range(limit,at,n) || h[20]>1 || (h[20]==0 && n-24-name!=raw) || (raw && n==24U+name)) return false;
            { uint8_t namebuf[4096];unsigned j;if(!pm_read(f,at+24,namebuf,name)) return false;for(j=0;j<name;++j) if(namebuf[j]<32 || namebuf[j]==127) return false; }
            xx_rt_snprintf(label,sizeof(label),"member-%u.%s",(unsigned)s->count,h[20] ? "charc":"bin");if(!pm_add(f,s,label,at+24+name,(int64_t)n-24-name)) return false;at+=(int64_t)n;
        } else if(tag==0x44684353) { unsigned j;if(depth==64 || !pm_read(f,at,h,10) || h[8] || !(name=h[9]) || !wg_range(limit,at,10U+name)) return false;
            for(j=0;j<name;++j) { uint8_t v;if(!pm_read(f,at+10+j,&v,1) || v<32 || v==127) return false; }++depth;at+=10+name;
        } else if(tag==0x64684353) { if(!depth) return false;--depth;at+=4; } else return false;
    } s->size=limit;return !depth && s->count!=0;
}
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { uint8_t h[64],newh[2];uint32_t at;static const uint8_t sig[]={'S','C','h'};if(!pm_read(f,0,h,64) || (xx_rt_memcmp(h,"MZ",2) && xx_rt_memcmp(h,"ZM",2))) return false;at=pm_le32(h+60);if(pm_le16(h+24)>=64 && at && pm_read(f,at,newh,2) && (!xx_rt_memcmp(newh,"PE",2) || !xx_rt_memcmp(newh,"NE",2) || !xx_rt_memcmp(newh,"LE",2) || !xx_rt_memcmp(newh,"LX",2))) return false;return w6_first(f,s,sig,3,0,false,false,w6_chz_at,pd); }



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_chz_init(xx_sfx_chz *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_CHZ,"exe"); } }
xx_sfx_chz *xx_sfx_chz_create(xx_io_device *d,int64_t b) { xx_sfx_chz *r=(xx_sfx_chz *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_chz_init(r,d,b); return r; }
void xx_sfx_chz_destroy(xx_sfx_chz *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_chz_free(xx_sfx_chz *r) { if(r) { xx_sfx_chz_destroy(r); xx_mem_free(r); } }
bool xx_sfx_chz_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_chz_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
