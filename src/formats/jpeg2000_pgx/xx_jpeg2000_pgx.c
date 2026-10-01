/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/uclouvain/openjpeg/master/src/bin/jp2/convert.c
 * PGX gray planes: exact ML/LM signed or unsigned1..16bit descriptor, bounded dimensions and precision-consistent encoded sample range. Original encoded plane and descriptor exported; no color rendering.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/jpeg2000_pgx/xx_jpeg2000_pgx.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[3];return n>=14&&pm_read(f,0,b,3)&&tg_tag(b,"PG ",3);}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tg_text q={b,0,n,0,0,0};int32_t prec,w,h;bool be,sign;uint64_t i,count,z;unsigned bytes;
 if(!tg_line(&q)||!tg_word(&q,"PG"))return false;if(tg_word(&q,"ML"))be=true;else if(tg_word(&q,"LM"))be=false;else return false;
 tg_space(&q);if(q.t==q.stop||(q.b[q.t]!='+'&&q.b[q.t]!='-'))return false;sign=q.b[q.t++]=='-';
 if(!tg_i(&q,&prec)||prec<1||prec>16||!tg_i(&q,&w)||!tg_i(&q,&h)||w<1||h<1||w>65536||h>65536||!tg_done(&q))return false;
 count=(uint64_t)w*h;bytes=prec<=8?1:2;z=count*bytes;if(count>16000000||z!=n-q.p)return false;
 for(i=0;i<count;++i){uint32_t u=bytes==1?b[q.p+i]:be?pm_be16(b+q.p+i*2):pm_le16(b+q.p+i*2);int32_t v=sign?(bytes==1?(int32_t)(int8_t)u:(int32_t)(int16_t)u):(int32_t)u;
  if((i&4095)==0&&tg_stop(pd))return false;if(sign?(v<-(1<<(prec-1))||v>(1<<(prec-1))-1):(u>((1U<<prec)-1)))return false;}
 if(!tg_emit(f,s,"descriptor.pgx",0,q.p,n)||!tg_emit(f,s,"plane.pgx",q.p,z,n))return false;s->size=(int64_t)n;return true;
}

void xx_jpeg2000_pgx_init(xx_jpeg2000_pgx *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_JPEG2000_PGX,"pgx");}}
xx_jpeg2000_pgx *xx_jpeg2000_pgx_create(xx_io_device *d,int64_t at) {xx_jpeg2000_pgx *r=(xx_jpeg2000_pgx *)xx_mem_alloc(sizeof(*r));if(r)xx_jpeg2000_pgx_init(r,d,at);return r;}
void xx_jpeg2000_pgx_destroy(xx_jpeg2000_pgx *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_jpeg2000_pgx_free(xx_jpeg2000_pgx *r) {if(r){xx_jpeg2000_pgx_destroy(r);xx_mem_free(r);}}
bool xx_jpeg2000_pgx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_jpeg2000_pgx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
