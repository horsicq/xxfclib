/* SPDX-License-Identifier: MIT
 * Primary reference: https://surferhelp.goldensoftware.com/topics/ascii_grid_file_format.htm
 * Golden Software Surfer DSAA ASCII grids: complete dimensions, increasing XY ranges, finite nodata/sample count and verified actual Z minimum/maximum (relative tolerance1e-12). Original descriptor and raster rows exported; binary/fault records declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/surfer_grid/xx_surfer_grid.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[4];return n>=24&&pm_read(f,0,b,4)&&tg_tag(b,"DSAA",4);}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tg_text q={b,0,n,0,0,0};int32_t w,h;double a,z,xlo,xhi,ylo,yhi,lo=1.70141e38,hi=1.70141e38;
 if(!tg_utf(b,n,true,pd)||!tg_line(&q)||!tg_word(&q,"DSAA")||!tg_done(&q)||!tg_next(&q)||!tg_i(&q,&w)||!tg_i(&q,&h)||w<2||h<2||w>65536||h>65536||(uint64_t)w*h>16000000||!tg_done(&q)||!tg_next(&q)||!tg_num(&q,&xlo)||!tg_num(&q,&xhi)||xlo>=xhi||!tg_done(&q)||!tg_next(&q)||!tg_num(&q,&ylo)||!tg_num(&q,&yhi)||ylo>=yhi||!tg_done(&q)||!tg_next(&q)||!tg_num(&q,&a)||!tg_num(&q,&z)||a>z||!tg_done(&q)||!tg_emit(f,s,"descriptor.grd",0,q.p,n)||!tg_grid_rows(f,s,b,n,q.p,(uint64_t)w*h,1.70141e38,&lo,&hi,pd,false))return false;
 if(!tg_near(lo,a)||!tg_near(hi,z)) {return false; } s->size=(int64_t)n;return true;
}

void xx_surfer_grid_init(xx_surfer_grid *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_SURFER_GRID,"grd");}}
xx_surfer_grid *xx_surfer_grid_create(xx_io_device *d,int64_t at) {xx_surfer_grid *r=(xx_surfer_grid *)xx_mem_alloc(sizeof(*r));if(r)xx_surfer_grid_init(r,d,at);return r;}
void xx_surfer_grid_destroy(xx_surfer_grid *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_surfer_grid_free(xx_surfer_grid *r) {if(r){xx_surfer_grid_destroy(r);xx_mem_free(r);}}
bool xx_surfer_grid_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_surfer_grid_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
