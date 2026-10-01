/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/avs.c
 * AVS X image: positive bounded big-endian dimensions and exact8-bit ARGB raster; descriptor and original ARGB row components exported; image sequences declined. Signatureless fallback after structured readers.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/avs_image/xx_avs_image.h"
#include "../imagemagick_miff/xx_sixteenth_games.h"
static bool hg_quick(Abstractformat *f,uint64_t n) {uint8_t b[8];return n>=12&&pm_read(f,0,b,8)&&pm_be32(b)&&pm_be32(b)<=8192&&pm_be32(b+4)&&pm_be32(b+4)<=4095;}
static bool hg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {uint32_t w,h,y;char label[48];if(n<12||!(w=pm_be32(b))||w>8192||!(h=pm_be32(b+4))||h>4095||(uint64_t)w*h>8388608||n!=8+(uint64_t)w*h*4||!hg_emit(f,s,"descriptor.avs",0,8,n))return false;for(y=0;y<h;++y){if(hg_stop(pd))return false;xx_rt_snprintf(label,sizeof(label),"argb-row-%u.bin",y);if(!hg_emit(f,s,label,8+(uint64_t)y*w*4,(uint64_t)w*4,n))return false;}return true;}

void xx_avs_image_init(xx_avs_image *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_AVS_IMAGE,"avs");}}
xx_avs_image *xx_avs_image_create(xx_io_device *d,int64_t at) {xx_avs_image *r=(xx_avs_image *)xx_mem_alloc(sizeof(*r));if(r)xx_avs_image_init(r,d,at);return r;}
void xx_avs_image_destroy(xx_avs_image *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_avs_image_free(xx_avs_image *r) {if(r){xx_avs_image_destroy(r);xx_mem_free(r);}}
bool xx_avs_image_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_avs_image_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
