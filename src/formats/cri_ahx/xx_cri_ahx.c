/* SPDX-License-Identifier: MIT.
 * CRI AHX framing: https://github.com/vgmstream/vgmstream/blob/master/src/meta/ahx.c
 * Extracts encoded audio, including encrypted audio as stored.
 */
#include "xxfclib/formats/cri_ahx/xx_cri_ahx.h"
#include "../xx_fifth_data.h"
#ifndef CRI_AHX
#define XX_FILE_TYPE_CRI_AHX ((xx_file_type_t)1509)
#endif
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {
    uint8_t h[24],mark[6],tail[10]; uint32_t start,rate,samples; int64_t n; bool trailer=false;
    if(fd_stop(pd) || (n=pm_available(f))<28 || !pm_read(f,0,h,sizeof(h)) ||
       h[0]!=0x80 || h[1]!=0 || (h[4]!=0x10 && h[4]!=0x11) ||
       h[5] || h[6] || h[7]!=1 || h[18]!=6) return false;
    start=(uint32_t)pm_be16(h+2)+4U; rate=pm_be32(h+8); samples=pm_be32(h+12);
    if(start<24 || start>65539U || start+4U>(uint64_t)n || !rate ||
       rate>384000 || !samples || samples>UINT32_C(1000000000) ||
       !pm_read(f,(int64_t)start-6,mark,6) || xx_rt_memcmp(mark,"(c)CRI",6)) return false;
    if(n>=(int64_t)start+10 && pm_read(f,n-10,tail,10) &&
       !xx_rt_memcmp(tail,"AHXE(c)CRI",10)) trailer=true;
    if((uint64_t)n-(trailer?10U:0U)<=start) return false;
    if(!pm_add(f,s,"ahx-header.bin",0,start) ||
       !pm_add(f,s,"coded-audio.bin",start,n-start-(trailer?10:0))) return false;
    if(trailer && !pm_add(f,s,"ahx-footer.bin",n-10,10)) return false;
    s->size=n; return true;
}
void xx_cri_ahx_init(xx_cri_ahx*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_CRI_AHX,"ahx");}}
xx_cri_ahx*xx_cri_ahx_create(xx_io_device*d,int64_t b) {xx_cri_ahx*r=(xx_cri_ahx*)xx_mem_alloc(sizeof(*r));if(r)xx_cri_ahx_init(r,d,b);return r;}
void xx_cri_ahx_destroy(xx_cri_ahx*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_cri_ahx_free(xx_cri_ahx*r) {if(r){xx_cri_ahx_destroy(r);xx_mem_free(r);}}
bool xx_cri_ahx_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_cri_ahx_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
