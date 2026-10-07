/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/OpenMW/openmw/master/components/bsa/compressedbsafile.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/bethesda_bsa/xx_bethesda_bsa.h"
#include "../bethesda_bsa/xx_game_table.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[36],r[24],z; uint32_t ver,flags,folders,files,flens,nlens,i,j,seen=0; uint64_t table,at,used,namesleft,folderleft,recend; int64_t total=pm_available(f);
    if(!gm_read(f,total,0,h,36) || xx_rt_memcmp(h,"BSA\0",4)) return false;
    ver=xx_data_get_u32(h+4, 4, 0, false); table=xx_data_get_u32(h+8, 4, 0, false); flags=xx_data_get_u32(h+12, 4, 0, false); folders=xx_data_get_u32(h+16, 4, 0, false); files=xx_data_get_u32(h+20, 4, 0, false); flens=xx_data_get_u32(h+24, 4, 0, false); nlens=xx_data_get_u32(h+28, 4, 0, false);
    if((ver!=103 && ver!=104 && ver!=105) || table<36 || folders>65536 || files>65536 || flens>16777216 || nlens>16777216 || (flags&~0x3ffU) || ((flags&1)==0 && flens) || ((flags&2)==0 && nlens)) return false;
    recend=table+(uint64_t)folders*(ver==105?24:16); if(!gm_range(total,table,recend-table)) return false;
    at=recend; folderleft=flens;
    /* Standard sequential folder blocks; offsets include FileNamesLength. */
    for(i=0;i<folders;++i) { uint32_t count; uint64_t declared;
        if(gm_stopped(pd) || !gm_read(f,total,table+(uint64_t)i*(ver==105?24:16),r,ver==105?24:16)) return false;
        declared=ver==105 ? xx_data_get_u64(r+16, 8, 0, false) : xx_data_get_u32(r+12, 4, 0, false);
        if(declared!=at+nlens) return false;
        count=xx_data_get_u32(r+8, 4, 0, false); if(count>files-seen) return false;
        if(flags&1) { if(!gm_read(f,total,at,&z,1) || !z || z>folderleft) return false; ++at;
            if(!gm_string(f,total,at,z,&used) || used!=z) { return false; } at+=z; folderleft-=z; }
        for(j=0;j<count;++j) { uint32_t packed,n; uint64_t off;
            if(gm_stopped(pd) || !gm_read(f,total,at,r,16)) { return false; } at+=16;
            packed=xx_data_get_u32(r+8, 4, 0, false); n=packed&0x3fffffffU; off=xx_data_get_u32(r+12, 4, 0, false);
            if((packed&0x80000000U) || (((flags&4)!=0)!=((packed&0x40000000U)!=0)) || !gm_range(total,off,n)) return false;
            if(ver!=103 && (flags&0x100)) { if(!gm_read(f,total,off,&z,1) || (uint32_t)z+1>n) return false; off+=(uint32_t)z+1; n-=(uint32_t)z+1; }
            if(!gm_add(f,s,"member.bin",off,n,recend,total)) { return false; } ++seen;
        }
    }
    if(seen!=files || folderleft!=0) { return false; } namesleft=nlens;
    if(flags&2) for(i=0;i<files;++i) { if(gm_stopped(pd) || !gm_string(f,total,at,namesleft,&used) || used==1) return false; at+=used; namesleft-=used; }
    if(namesleft || !gm_range(total,0,at)) return false;
    for(i=0;i<s->count;++i) if((uint64_t)(s->items[i].offset-f->base_address)<at) return false;
    if(at>(uint64_t)s->size) { s->size=(int64_t)at; } return true;
}
void xx_bethesda_bsa_init(xx_bethesda_bsa *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_BETHESDA_BSA,"bin"); } }
xx_bethesda_bsa *xx_bethesda_bsa_create(xx_io_device *d,int64_t b) { xx_bethesda_bsa *r=(xx_bethesda_bsa *)xx_mem_alloc(sizeof(*r)); if(r) xx_bethesda_bsa_init(r,d,b); return r; }
void xx_bethesda_bsa_destroy(xx_bethesda_bsa *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_bethesda_bsa_free(xx_bethesda_bsa *r) { if(r) { xx_bethesda_bsa_destroy(r); xx_mem_free(r); } }
bool xx_bethesda_bsa_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_bethesda_bsa_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
