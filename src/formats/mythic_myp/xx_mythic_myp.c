/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/ClassicUO/ClassicUO/blob/master/src/ClassicUO.IO/UOFileUop.cs
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/mythic_myp/xx_mythic_myp.h"
#include "../makeself/xx_fourth_wrapper_table.h"

static bool wg_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[34]; uint64_t block,previous=28,metadata=28; uint32_t version,capacity,declared,actual=0; unsigned blocks=0,pass; int64_t limit=pm_available(f);
    if(!pm_read(f,0,h,28) || xx_rt_memcmp(h,"MYP\0",4) || ((version=xx_data_get_u32(h+4, 4, 0, false))!=4 && version!=5) || !(capacity=xx_data_get_u32(h+20, 4, 0, false)) || capacity>65536 || !(declared=xx_data_get_u32(h+24, 4, 0, false)) || declared>65536) { return false; } block=wg64(h+12);
    for(pass=0;pass<2;++pass) { uint64_t first=block; previous=28; blocks=0;
        while(first) { uint32_t count,i; uint64_t next,stop; if(wg_stop(pd) || ++blocks>1024 || first<previous || !wg_range(limit,first,12) || !pm_read(f,(int64_t)first,h,12)) return false;
            count=xx_data_get_u32(h, 4, 0, false); next=wg64(h+4); if(count>capacity || !wg_range(limit,first+12,(uint64_t)count*34)) return false; stop=first+12+(uint64_t)count*34; if(next && next<stop) return false; if(stop>metadata) metadata=stop;
            for(i=0;i<count;++i) { uint64_t offset; uint32_t header,packed,raw; char label[48]; if(wg_stop(pd) || !pm_read(f,(int64_t)(first+12+(uint64_t)i*34),h,34)) return false; offset=wg64(h); if(!offset) continue;
                header=xx_data_get_u32(h+8, 4, 0, false); packed=xx_data_get_u32(h+12, 4, 0, false); raw=xx_data_get_u32(h+16, 4, 0, false); if(xx_data_get_u16(h+32, 2, 0, false) || packed!=raw || !wg_range(limit,offset,(uint64_t)header+packed)) return false;
                if(pass) { if(offset<metadata || ++actual>declared) return false; xx_rt_snprintf(label,sizeof(label),"resource-%u.bin",actual-1); if(!pm_add(f,s,label,(int64_t)(offset+header),raw)) return false; s->items[s->count-1].packed_size=(int64_t)header+packed; }
            } previous=stop; first=next;
        }
    }
    if(actual!=declared) { return false; } s->size=(int64_t)metadata;
    { wg_extent *ranges=(wg_extent *)xx_mem_alloc(s->count*sizeof(*ranges)); size_t i; bool ok; if(!ranges) return false;
      for(i=0;i<s->count;++i) { ranges[i].lo=s->items[i].offset-(s->items[i].packed_size-s->items[i].size); ranges[i].hi=s->items[i].offset+s->items[i].size; s->items[i].packed_size=s->items[i].size; }
      ok=wg_extents(ranges,s->count,pd); xx_mem_free(ranges); if(!ok) return false;
    }
    { size_t i; for(i=0;i<s->count;++i) { int64_t end=s->items[i].offset-f->base_address+s->items[i].size; if(end>s->size) s->size=end; } } return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return wg_parse(f,s,pd) && wg_members(s,pd); }
void xx_mythic_myp_init(xx_mythic_myp *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MYTHIC_MYP,"uop"); } }
xx_mythic_myp *xx_mythic_myp_create(xx_io_device *d,int64_t b) { xx_mythic_myp *r=(xx_mythic_myp *)xx_mem_alloc(sizeof(*r)); if(r) xx_mythic_myp_init(r,d,b); return r; }
void xx_mythic_myp_destroy(xx_mythic_myp *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mythic_myp_free(xx_mythic_myp *r) { if(r) { xx_mythic_myp_destroy(r); xx_mem_free(r); } }
bool xx_mythic_myp_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mythic_myp_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
