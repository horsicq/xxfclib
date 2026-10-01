/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xszddsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_szdd/xx_sfx_szdd.h"
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"

static bool w6_sz_at(Abstractformat *f,pm_stream *s,int64_t at,xx_pd_struct *pd) {
    uint8_t h[14],window[4096],*input;size_t header,n,pos=0,out=0;uint32_t raw;unsigned wp=4080;bool ok=false;int64_t available=pm_available(f)-at;
    if(!pm_read(f,at,h,14)) return false;
    if(!xx_rt_memcmp(h,"SZDD\x88\xf0\x27\x33",8) && h[8]=='A') { header=14;raw=pm_le32(h+10); }
    else if(!xx_rt_memcmp(h,"ZDD\x88\xf0\x27\x33" "A",8)) { header=12;raw=pm_le32(h+8); } else return false;
    if(!raw || raw>67108864 || available<=(int64_t)header) return false;n=available-(int64_t)header>33554432 ? 33554432U:(size_t)(available-(int64_t)header);
    input=(uint8_t *)xx_mem_alloc(n);if(!input || !pm_read(f,at+(int64_t)header,input,n)) { if(input) xx_mem_free(input);return false; }xx_rt_memset(window,32,sizeof(window));
    while(out<raw) { unsigned bit;uint8_t flags;if(wg_stop(pd) || pos==n) goto done;flags=input[pos++];
        for(bit=0;bit<8 && out<raw;++bit) { unsigned length,rp;if(flags&(1U<<bit)) { if(pos==n) goto done;window[wp]=input[pos++];wp=(wp+1)&4095U;++out; }
            else { if(n-pos<2) goto done;rp=input[pos]|((unsigned)(input[pos+1]&240)<<4);length=(input[pos+1]&15)+3;pos+=2;if(length>raw-out) goto done;
                while(length--) { uint8_t v=window[rp];window[wp]=v;wp=(wp+1)&4095U;rp=(rp+1)&4095U;++out; }
            }
        }
    }ok=w6_component(f,s,at,(int64_t)(header+pos),"payload.szdd");
done:xx_mem_free(input);return ok;
}
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t standard[]={'S','Z','D','D',0x88,0xf0,0x27,0x33},legacy[]={'Z','D','D',0x88,0xf0,0x27,0x33,'A'};return w6_scan(f,s,standard,sizeof(standard),0,true,false,w6_sz_at,pd) || (!s->count && w6_scan(f,s,legacy,sizeof(legacy),0,true,false,w6_sz_at,pd)); }



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_szdd_init(xx_sfx_szdd *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_SZDD,"exe"); } }
xx_sfx_szdd *xx_sfx_szdd_create(xx_io_device *d,int64_t b) { xx_sfx_szdd *r=(xx_sfx_szdd *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_szdd_init(r,d,b); return r; }
void xx_sfx_szdd_destroy(xx_sfx_szdd *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_szdd_free(xx_sfx_szdd *r) { if(r) { xx_sfx_szdd_destroy(r); xx_mem_free(r); } }
bool xx_sfx_szdd_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_szdd_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
