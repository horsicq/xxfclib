/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/1j01/anypalette.js/master/src/formats/PaintShopPro.coffee
 * JASC-PAL0100: exact declared RGB8 color rows and complete text framing. Original descriptor and palette rows exported; no rendering.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/jasc_palette/xx_jasc_palette.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[8];return n>=16&&pm_read(f,0,b,8)&&tg_tag(b,"JASC-PAL",8);}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tg_text q={b,0,n,0,0,0};int32_t count,rgb;unsigned i,j;char label[48];
 if(!tg_utf(b,n,true,pd)||!tg_line(&q)||!tg_word(&q,"JASC-PAL")||!tg_done(&q)||!tg_line(&q)||!tg_word(&q,"0100")||!tg_done(&q)||!tg_line(&q)||!tg_i(&q,&count)||count<1||count>4094||!tg_done(&q)||!tg_emit(f,s,"descriptor.pal",0,q.p,n))return false;
 for(i=0;i<(unsigned)count;++i){if(tg_stop(pd)||!tg_line(&q))return false;for(j=0;j<3;++j)if(!tg_i(&q,&rgb)||rgb<0||rgb>255)return false;if(!tg_done(&q))return false;xx_rt_snprintf(label,sizeof(label),"color-%u.pal",i);if(!tg_emit(f,s,label,q.start,q.p-q.start,n))return false;}
 {uint64_t at=q.p;while(q.p<n)if(!tg_line(&q)||!tg_done(&q))return false;if(at<n&&!tg_emit(f,s,"trailing.pal",at,n-at,n))return false;}
 s->size=(int64_t)n;return true;
}

void xx_jasc_palette_init(xx_jasc_palette *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_JASC_PALETTE,"pal");}}
xx_jasc_palette *xx_jasc_palette_create(xx_io_device *d,int64_t at) {xx_jasc_palette *r=(xx_jasc_palette *)xx_mem_alloc(sizeof(*r));if(r)xx_jasc_palette_init(r,d,at);return r;}
void xx_jasc_palette_destroy(xx_jasc_palette *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_jasc_palette_free(xx_jasc_palette *r) {if(r){xx_jasc_palette_destroy(r);xx_mem_free(r);}}
bool xx_jasc_palette_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_jasc_palette_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
