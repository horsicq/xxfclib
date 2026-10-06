/* SPDX-License-Identifier: MIT
 * Primary reference: https://doc.esri.com/en/arcgis-pro/latest/tool-reference/conversion/raster-to-ascii.html
 * Esri ASCII grid: complete case-insensitive NCOLS/NROWS, paired corner or center georeference, positive cell size, optional finite nodata, exact finite sample count; fixed NCOLS/NROWS then XY/cellsize header order. Original descriptor and raster rows exported; NaN/Inf and nonuniform DX/DY extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/esri_ascii_grid/xx_esri_ascii_grid.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[5];return n>=24&&pm_read(f,0,b,5)&&((b[0]=='n'||b[0]=='N')&&(b[1]=='c'||b[1]=='C')&&(b[2]=='o'||b[2]=='O')&&(b[3]=='l'||b[3]=='L')&&(b[4]=='s'||b[4]=='S'));}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tg_text q={b,0,n,0,0,0};int32_t w,h;double x,y,cell,nodata=-9999,lo=nodata,hi=nodata;bool center=false;uint64_t at;
 if(!tg_utf(b,n,true,pd)||!tg_line(&q)||!tg_word_ci(&q,"ncols")||!tg_i(&q,&w)||w<1||w>65536||!tg_done(&q)||!tg_line(&q)||!tg_word_ci(&q,"nrows")||!tg_i(&q,&h)||h<1||h>65536||!tg_done(&q)||(uint64_t)w*h>16000000||!tg_line(&q))return false;
 if(tg_word_ci(&q,"xllcenter"))center=true;else if(!tg_word_ci(&q,"xllcorner"))return false;
 if(!tg_num(&q,&x)||!tg_done(&q)||!tg_line(&q)||!tg_word_ci(&q,center?"yllcenter":"yllcorner")||!tg_num(&q,&y)||!tg_done(&q)||!tg_line(&q)||!tg_word_ci(&q,"cellsize")||!tg_num(&q,&cell)||cell<=0||!tg_done(&q))return false;
 at=q.p;if(!tg_line(&q))return false;if(tg_word_ci(&q,"nodata_value")){if(!tg_num(&q,&nodata)||!tg_done(&q))return false;at=q.p;}
 if(!tg_emit(f,s,"descriptor.asc",0,at,n)||!tg_grid_rows(f,s,b,n,at,(uint64_t)w*h,nodata,&lo,&hi,pd,false)) {return false; } s->size=(int64_t)n;return true;
}

void xx_esri_ascii_grid_init(xx_esri_ascii_grid *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_ESRI_ASCII_GRID,"asc");}}
xx_esri_ascii_grid *xx_esri_ascii_grid_create(xx_io_device *d,int64_t at) {xx_esri_ascii_grid *r=(xx_esri_ascii_grid *)xx_mem_alloc(sizeof(*r));if(r)xx_esri_ascii_grid_init(r,d,at);return r;}
void xx_esri_ascii_grid_destroy(xx_esri_ascii_grid *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_esri_ascii_grid_free(xx_esri_ascii_grid *r) {if(r){xx_esri_ascii_grid_destroy(r);xx_mem_free(r);}}
bool xx_esri_ascii_grid_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_esri_ascii_grid_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
