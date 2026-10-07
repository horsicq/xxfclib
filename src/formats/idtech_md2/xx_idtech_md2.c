/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake-2/master/qcommon/qfiles.h
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/idtech_md2/xx_idtech_md2.h"
#include "../bethesda_bsa/xx_game_table.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[68],r[12]; uint32_t c[6],o[6],i,j,frame; uint64_t n[5]; int64_t total=pm_available(f); const char *labels[]={"skins.bin","texcoords.bin","triangles.bin","frames.bin","glcommands.bin"};
    if(!gm_read(f,total,0,h,68) || xx_rt_memcmp(h,"IDP2",4) || xx_data_get_u32(h+4, 4, 0, false)!=8 || !xx_data_get_u32(h+8, 4, 0, false) || !xx_data_get_u32(h+12, 4, 0, false)) return false;
    for(i=0;i<6;++i) { c[i]=xx_data_get_u32(h+20+i*4, 4, 0, false); o[i]=xx_data_get_u32(h+44+i*4, 4, 0, false); }
    frame=xx_data_get_u32(h+16, 4, 0, false);
    if(c[0]>32 || !c[1] || c[1]>2048 || c[2]>65536 || c[3]>4096 || c[4]>16777216 || !c[5] || c[5]>512 || frame!=40+c[1]*4 || o[5]<68 || !gm_range(total,0,o[5])) return false;
    n[0]=(uint64_t)c[0]*64; n[1]=(uint64_t)c[2]*4; n[2]=(uint64_t)c[3]*12; n[3]=(uint64_t)c[5]*frame; n[4]=(uint64_t)c[4]*4;
    s->size=o[5];
    for(i=0;i<5;++i) { if(gm_stopped(pd) || o[i]<68 || o[i]>o[i+1] || n[i]>(uint64_t)o[i+1]-o[i] || !gm_range(o[5],o[i],n[i])) return false; if(n[i] && !gm_add(f,s,labels[i],o[i],n[i],68,o[5])) return false; }
    for(i=0;i<c[3];++i) { if(!gm_read(f,o[5],o[2]+(uint64_t)i*12,r,12)) return false; for(j=0;j<3;++j) if(xx_data_get_u16(r+j*2, 2, 0, false)>=c[1] || xx_data_get_u16(r+6+j*2, 2, 0, false)>=c[2]) return false; }
    return true;
}
void xx_idtech_md2_init(xx_idtech_md2 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_IDTECH_MD2,"bin"); } }
xx_idtech_md2 *xx_idtech_md2_create(xx_io_device *d,int64_t b) { xx_idtech_md2 *r=(xx_idtech_md2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_idtech_md2_init(r,d,b); return r; }
void xx_idtech_md2_destroy(xx_idtech_md2 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_idtech_md2_free(xx_idtech_md2 *r) { if(r) { xx_idtech_md2_destroy(r); xx_mem_free(r); } }
bool xx_idtech_md2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_idtech_md2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
