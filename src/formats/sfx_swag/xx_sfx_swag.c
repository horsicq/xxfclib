/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: src/formats/swag/xx_swag.c
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_swag/xx_sfx_swag.h"
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"

static bool w6_swag_at(Abstractformat *f,pm_stream *s,int64_t base,xx_pd_struct *pd) {
    uint8_t h[257],footer[129];int64_t at=base,end=pm_available(f)-129;unsigned count,i;
    if(end<=base || !pm_read(f,end,footer,129) || footer[0]>60 || footer[61]>65 || !(count=pm_le16(footer+127)) || count>4096) return false;
    for(i=0;i<count;++i) { unsigned size,name,j,sum=0;uint32_t packed;
        if(wg_stop(pd) || end-at<187 || !pm_read(f,at,h,187) || xx_rt_memcmp(h+2,"-sw1-",5) || !(name=h[186]) || name>12 || (size=h[0])!=187+name || end-at<(int64_t)size+2 || !pm_read(f,at,h,size+2)) return false;
        for(j=2;j<size+2;++j) { sum+=h[j]; } if((sum&255)!=h[1]) return false;for(j=0;j<name;++j) if(h[187+j]<32 || h[187+j]>126) return false;
        packed=pm_le32(h+7);if(packed>INT32_MAX || pm_le32(h+11)>67108864 || !wg_range(end,at+size+2,packed)) return false;at+=size+2+packed;
    }if(at==end-1) { uint8_t marker;if(!pm_read(f,at,&marker,1) || marker) return false;++at; }return at==end && w6_component(f,s,base,end+129-base,"payload.swg");
}
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={'-','s','w','1','-'};return w6_scan(f,s,sig,5,-2,false,false,w6_swag_at,pd); }



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_swag_init(xx_sfx_swag *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_SWAG,"exe"); } }
xx_sfx_swag *xx_sfx_swag_create(xx_io_device *d,int64_t b) { xx_sfx_swag *r=(xx_sfx_swag *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_swag_init(r,d,b); return r; }
void xx_sfx_swag_destroy(xx_sfx_swag *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_swag_free(xx_sfx_swag *r) { if(r) { xx_sfx_swag_destroy(r); xx_mem_free(r); } }
bool xx_sfx_swag_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_swag_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
