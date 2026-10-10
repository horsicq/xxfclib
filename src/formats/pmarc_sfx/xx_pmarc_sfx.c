/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/Algos/xdearkmodule_lha_p.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/pmarc_sfx/xx_pmarc_sfx.h"
#include "../common/xx_executable_carrier.h"

static bool executable_carrier_pma_at(Abstractformat *f,pm_stream *s,int64_t base,xx_pd_struct *pd) {
    uint8_t h[257];int64_t at=base,limit=pm_available(f);unsigned records=0;
    for(;;) { unsigned size,name,j,sum=0;uint32_t packed,raw;
        if(carrier_stop(pd) || !pm_read(f,at,h,1)) { return false; } if(!h[0]) { ++at;break; }
        if(++records>4096 || (size=h[0]+2)<24 || !pm_read(f,at,h,size) || xx_rt_memcmp(h+2,"-pm",3) || h[5]<'0' || h[5]>'2' || h[6]!='-' || h[20] || !(name=h[21]) || size<24+name) return false;
        for(j=2;j<size;++j) { sum+=h[j]; } if((sum&255)!=h[1]) return false;for(j=0;j<name;++j) if(h[22+j]<32 || h[22+j]==127) return false;
        packed=xx_data_get_u32(h+7, 4, 0, false);raw=xx_data_get_u32(h+11, 4, 0, false);if(packed>INT32_MAX || raw>INT32_MAX || (h[5]=='0' && packed!=raw) || !carrier_range(limit,at+size,packed)) return false;at+=size+packed;
    }
    if(!records || limit-at>127) { return false; } while(at<limit) { uint8_t b;if(!pm_read(f,at++,&b,1) || b!=26) return false; }
    return executable_carrier_component(f,s,base,limit-base,"payload.pma");
}
static bool sfx_carrier_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[7],p[4];unsigned locations[2],count=0,i;uint16_t first;
    if(!pm_read(f,0,h,7) || xx_rt_memcmp(h+2,"-pms-",5)) { return false; } first=xx_data_get_u16(h, 2, 0, true);
    if(first==0x180a) { locations[count++]=75;locations[count++]=87; } else if(first==0x1879) locations[count++]=198;else if(first==0xeb18) { locations[count++]=2663;locations[count++]=2626; } else return false;
    for(i=0;i<count;++i) { int64_t base;if(!pm_read(f,locations[i]-1,p,4) || p[0]!=0x21 || p[3]!=0x22 || xx_data_get_u16(p+1, 2, 0, false)<256) continue;base=xx_data_get_u16(p+1, 2, 0, false)-256;if(base>(int64_t)locations[i]+2 && executable_carrier_pma_at(f,s,base,pd)) return true;if(s->count) break; }return false;
}



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return sfx_carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_pmarc_sfx_init(xx_pmarc_sfx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PMARC_SFX,"com"); } }
xx_pmarc_sfx *xx_pmarc_sfx_create(xx_io_device *d,int64_t b) { xx_pmarc_sfx *r=(xx_pmarc_sfx *)xx_mem_alloc(sizeof(*r)); if(r) xx_pmarc_sfx_init(r,d,b); return r; }
void xx_pmarc_sfx_destroy(xx_pmarc_sfx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_pmarc_sfx_free(xx_pmarc_sfx *r) { if(r) { xx_pmarc_sfx_destroy(r); xx_mem_free(r); } }
bool xx_pmarc_sfx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_pmarc_sfx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
