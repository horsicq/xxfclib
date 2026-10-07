/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/dterracino/UnEgg/blob/master/EGG_Specification.pdf
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/egg/xx_egg.h"
#include "../sfx_imp/xx_seventh_wrapper_table.h"
static bool w7_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {
    uint8_t*b;size_t n,p=18,files=0,blocks=0;uint32_t ids[W7_COUNT];uint64_t output=0;bool ok=false;
    b=w7_load(f,&n,pd);if(!b)return false;if(n<22 || xx_rt_memcmp(b,"EGGA",4) || xx_data_get_u16(b+4, 2, 0, false)!=0x100 || !xx_data_get_u32(b+6, 4, 0, false) || xx_data_get_u32(b+10, 4, 0, false) || xx_data_get_u32(b+14, 4, 0, false)!=0x08e28222)goto done;
    while(w7_range(n,p,4)) {
        uint32_t magic=xx_data_get_u32(b+p, 4, 0, false),id;uint64_t raw;size_t i,at=0;char name[256];bool named=false;uint8_t*plain=NULL;
        if(magic==0x08e28222) {ok=files!=0 && p+4==n;break;}if(magic!=0x0a8590e3 || files>=W7_COUNT || !w7_range(n,p,16))goto done;id=xx_data_get_u32(b+p+4, 4, 0, false);for(i=0;i<files;++i)if(ids[i]==id)goto done;ids[files]=id;raw=xx_data_get_u64(b+p+8, 8, 0, false);if(raw>W7_LIMIT-output)goto done;p+=16;name[0]=0;
        for(;;) {uint32_t tag;uint16_t len;if(wg_stop(pd) || !w7_range(n,p,4))goto done;tag=xx_data_get_u32(b+p, 4, 0, false);if(tag==0x08e28222){p+=4;break;}if((tag!=0x0a8591ac && tag!=0x04c63672) || !w7_range(n,p,7) || b[p+4])goto done;len=xx_data_get_u16(b+p+5, 2, 0, false);if(!w7_range(n,p+7,len) || len>4096)goto done;if(tag==0x0a8591ac){if(named || !len || len>255)goto done;for(i=0;i<len;++i)if(!b[p+7+i])goto done;xx_rt_memcpy(name,b+p+7,len);name[len]=0;named=true;}p+=7+len; }
        if(!named) {goto done; } plain=(uint8_t*)xx_mem_alloc(raw ? (size_t)raw:1);if(!plain)goto done;
        while(w7_range(n,p,4) && xx_data_get_u32(b+p, 4, 0, false)==0x02b50c13) {uint32_t size,packed,crc;if(wg_stop(pd) || ++blocks>W7_COUNT || !w7_range(n,p,22) || b[p+4] || b[p+5] || xx_data_get_u32(b+p+18, 4, 0, false)!=0x08e28222) {xx_mem_free(plain);goto done;}size=xx_data_get_u32(b+p+6, 4, 0, false);packed=xx_data_get_u32(b+p+10, 4, 0, false);crc=xx_data_get_u32(b+p+14, 4, 0, false);if(size!=packed || size>raw-at || !w7_range(n,p+22,packed) || !w6_crc_checked(b+p+22,packed,crc,pd)) {xx_mem_free(plain);goto done;}if(size)xx_rt_memcpy(plain+at,b+p+22,size);at+=size;p+=22+packed; }
        if(at!=raw || !pm_add(f,s,name,0,(int64_t)raw)) {xx_mem_free(plain);goto done;}s->items[s->count-1].memory=plain;s->items[s->count-1].packed_size=0;output+=raw;++files;
    }
    if(ok)s->size=(int64_t)n;
done:xx_mem_free(b);return ok && !wg_stop(pd);
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w7_parse(f,s,pd) && true; }
void xx_egg_init(xx_egg *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_EGG,"egg"); } }
xx_egg *xx_egg_create(xx_io_device *d,int64_t b) { xx_egg *r=(xx_egg *)xx_mem_alloc(sizeof(*r)); if(r) xx_egg_init(r,d,b); return r; }
void xx_egg_destroy(xx_egg *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_egg_free(xx_egg *r) { if(r) { xx_egg_destroy(r); xx_mem_free(r); } }
bool xx_egg_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_egg_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
