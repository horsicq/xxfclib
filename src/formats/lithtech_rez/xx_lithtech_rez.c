/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/bibendovsky/ltjs/blob/master/engine/libs/rezmgr/rezmgr.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/lithtech_rez/xx_lithtech_rez.h"
#include "../makeself/xx_fourth_wrapper_table.h"

typedef struct rz_dir { uint32_t offset,size; } rz_dir;
static bool wg_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[168]; rz_dir queue[1024]; unsigned used=0,count=1; uint32_t boundary; int64_t limit=pm_available(f),extent=168;
    if(!pm_read(f,0,h,168) || h[0]!=13 || h[1]!=10 || xx_rt_memcmp(h+2,"RezMgr Version 1 Copyright (C) 1995 MONOLITH INC.",47) || h[62]!=13 || h[63]!=10 || h[124]!=13 || h[125]!=10 || h[126]!=26 || xx_data_get_u32(h+127, 4, 0, false)!=1 || h[167]>1 || (boundary=xx_data_get_u32(h+143, 4, 0, false))<168 || boundary>(uint64_t)limit) return false;
    queue[0].offset=xx_data_get_u32(h+131, 4, 0, false); queue[0].size=xx_data_get_u32(h+135, 4, 0, false);
    while(used<count) { rz_dir dir=queue[used++]; int64_t at=dir.offset,end; if(dir.offset<boundary || !dir.size || !wg_range(limit,dir.offset,dir.size)) return false; end=at+dir.size; if(end>extent) extent=end;
        while(at<end) { uint32_t kind,offset,bytes; char name[4097],comment[4097],label[64];
            if(wg_stop(pd) || end-at<16 || !pm_read(f,at,h,16) || (kind=xx_data_get_u32(h, 4, 0, false))>1) { return false; } offset=xx_data_get_u32(h+4, 4, 0, false); bytes=xx_data_get_u32(h+8, 4, 0, false);
            if(kind==1) { unsigned i; at+=16; if(count==1024 || !wg_string(f,&at,end,name,sizeof(name)) || !name[0] || offset<boundary || !bytes || !wg_range(limit,offset,bytes)) return false; for(i=0;i<count;++i) if((uint64_t)offset<queue[i].offset+(uint64_t)queue[i].size && (uint64_t)queue[i].offset<offset+(uint64_t)bytes) return false; queue[count].offset=offset; queue[count++].size=bytes; }
            else { uint32_t id,type,keys; if(end-at<28 || !pm_read(f,at,h,28)) return false; id=xx_data_get_u32(h+16, 4, 0, false); type=xx_data_get_u32(h+20, 4, 0, false); keys=xx_data_get_u32(h+24, 4, 0, false); at+=28;
                if(keys>4096 || !wg_string(f,&at,end,name,sizeof(name)) || !name[0] || !wg_string(f,&at,end,comment,sizeof(comment)) || (uint64_t)keys*4>(uint64_t)(end-at) || offset<168 || !wg_range(boundary,offset,bytes)) { return false; } at+=(int64_t)keys*4;
                xx_rt_snprintf(label,sizeof(label),"resource-%u-type-%08X.bin",id,type); if(!pm_add(f,s,label,offset,bytes)) return false;
            }
        }
    } s->size=extent; return s->count>0;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return wg_parse(f,s,pd) && wg_members(s,pd); }
void xx_lithtech_rez_init(xx_lithtech_rez *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LITHTECH_REZ,"rez"); } }
xx_lithtech_rez *xx_lithtech_rez_create(xx_io_device *d,int64_t b) { xx_lithtech_rez *r=(xx_lithtech_rez *)xx_mem_alloc(sizeof(*r)); if(r) xx_lithtech_rez_init(r,d,b); return r; }
void xx_lithtech_rez_destroy(xx_lithtech_rez *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_lithtech_rez_free(xx_lithtech_rez *r) { if(r) { xx_lithtech_rez_destroy(r); xx_mem_free(r); } }
bool xx_lithtech_rez_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_lithtech_rez_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
