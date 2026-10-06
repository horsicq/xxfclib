/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/scummvm/scummvm/master/engines/mohawk/resource.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/mohawk_mhk/xx_mohawk_mhk.h"
#include "../bethesda_bsa/xx_game_table.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[28],r[10],b[8]; uint32_t count,i,j; uint16_t types,strings; uint64_t base,ft,end,used,rt,nt,metadata_end; int64_t total=pm_available(f);
    if(!gm_read(f,total,0,h,28) || xx_rt_memcmp(h,"MHWK",4) || xx_rt_memcmp(h+8,"RSRC",4) || pm_be16(h+12)!=0x100) return false;
    base=pm_be32(h+20); ft=base+pm_be16(h+24);
    if(base<28 || !gm_read(f,total,ft,b,4) || !gm_read(f,total,base,b+4,4)) return false;
    count=pm_be32(b); strings=pm_be16(b+4); types=pm_be16(b+6);
    if(count>65536 || types>4096 || !gm_range(total,ft+4,(uint64_t)count*10) || !gm_range(total,base+4,(uint64_t)types*8)) return false;
    end=ft+4+(uint64_t)count*10; if(base+4+(uint64_t)types*8>end) end=base+4+(uint64_t)types*8; s->size=(int64_t)end; metadata_end=end;
    for(i=0;i<count;++i) { uint64_t offset,n;
        if(gm_stopped(pd) || !gm_read(f,total,ft+4+(uint64_t)i*10,r,10)) return false;
        offset=pm_be32(r); n=pm_be16(r+4) | (uint32_t)r[6]<<16 | (uint32_t)(r[7]&7)<<24;
        if(offset<28 || !gm_range(total,offset,n)) return false;
        if(offset+n>(uint64_t)s->size) s->size=(int64_t)(offset+n);
    }
    for(i=0;i<types;++i) { uint16_t resources,names,k;
        if(gm_stopped(pd) || !gm_read(f,total,base+4+(uint64_t)i*8,b,8)) return false;
        if(!xx_rt_memcmp(b,"tMOV",4)) return false;
        rt=base+pm_be16(b+4); nt=base+pm_be16(b+6);
        if(!gm_read(f,total,rt,b,2)) { return false; } resources=pm_be16(b); if(!gm_range(total,rt+2,(uint64_t)resources*4)) return false;
        if(rt+2+(uint64_t)resources*4>(uint64_t)s->size) s->size=(int64_t)(rt+2+(uint64_t)resources*4);
        if(rt+2+(uint64_t)resources*4>metadata_end) metadata_end=rt+2+(uint64_t)resources*4;
        if(!gm_read(f,total,nt,b,2)) { return false; } names=pm_be16(b); if(!gm_range(total,nt+2,(uint64_t)names*4)) return false;
        if(nt+2+(uint64_t)names*4>(uint64_t)s->size) s->size=(int64_t)(nt+2+(uint64_t)names*4);
        if(nt+2+(uint64_t)names*4>metadata_end) metadata_end=nt+2+(uint64_t)names*4;
        for(k=0;k<names;++k) { uint64_t str,remain;
            if(!gm_read(f,total,nt+2+(uint64_t)k*4,b,4) || !pm_be16(b+2) || pm_be16(b+2)>count) return false;
            str=base+strings+pm_be16(b); if(str>(uint64_t)total) return false; remain=(uint64_t)total-str; if(remain>4096) remain=4096;
            if(!gm_string(f,total,str,remain,&used)) { return false; } if(str+used>(uint64_t)s->size) s->size=(int64_t)(str+used);
            if(str+used>metadata_end) metadata_end=str+used;
        }
    }
    /* All referenced data must lie outside the complete metadata region. */
    for(i=0;i<types;++i) { uint16_t resources;
        if(gm_stopped(pd) || !gm_read(f,total,base+4+(uint64_t)i*8,b,8)) return false;
        rt=base+pm_be16(b+4); if(!gm_read(f,total,rt,b,2)) return false; resources=pm_be16(b);
        for(j=0;j<resources;++j) { uint16_t index; uint64_t at,n;
            if(gm_stopped(pd) || !gm_read(f,total,rt+2+(uint64_t)j*4,b,4)) { return false; } index=pm_be16(b+2); if(!index || index>count) return false;
            if(!gm_read(f,total,ft+4+(uint64_t)(index-1)*10,r,10)) return false;
            at=pm_be32(r); n=pm_be16(r+4) | (uint32_t)r[6]<<16 | (uint32_t)(r[7]&7)<<24;
            if(at<metadata_end && at+n>base) return false;
            if(!gm_add(f,s,"resource.bin",at,n,28,total)) return false;
        }
    }
    return s->count!=0;
}
void xx_mohawk_mhk_init(xx_mohawk_mhk *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MOHAWK_MHK,"bin"); } }
xx_mohawk_mhk *xx_mohawk_mhk_create(xx_io_device *d,int64_t b) { xx_mohawk_mhk *r=(xx_mohawk_mhk *)xx_mem_alloc(sizeof(*r)); if(r) xx_mohawk_mhk_init(r,d,b); return r; }
void xx_mohawk_mhk_destroy(xx_mohawk_mhk *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mohawk_mhk_free(xx_mohawk_mhk *r) { if(r) { xx_mohawk_mhk_destroy(r); xx_mem_free(r); } }
bool xx_mohawk_mhk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mohawk_mhk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
