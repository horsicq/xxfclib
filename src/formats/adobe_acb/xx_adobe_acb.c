/* SPDX-License-Identifier: MIT
 * Primary reference: https://ates.dev/pages/acb-spec/
 * Adobe Color Book v1: complete UTF16BE descriptor and bounded RGB/CMYK/Lab color records, page parameters and optional typed spot/process trailer. Original encoded descriptor/colors exported; no color conversion.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/adobe_acb/xx_adobe_acb.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[6];return n>=32&&pm_read(f,0,b,6)&&tg_tag(b,"8BCB\0\1",6);}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tg_bin q={b,8,n,pd};const uint8_t *p;uint16_t count,space,page;unsigned i;uint64_t start;char label[48];
 if(n<32||!tg_tag(b,"8BCB\0\1",6))return false;
 for(i=0;i<4;++i)if(!tg_pstring(&q))return false;
 if(!tg_take(&q,8,&p))return false;count=pm_be16(p);page=pm_be16(p+2);space=pm_be16(p+6);
 if(!count||count>4094||!page||page>9||(space!=0&&space!=2&&space!=7)||!tg_emit(f,s,"descriptor.acb",0,q.p,n))return false;
 for(i=0;i<count;++i){unsigned j;bool named;start=q.p;if(!tg_span(q.p,4,n))return false;named=pm_be32(b+q.p)!=0;if(!tg_pstring(&q)||!tg_take(&q,space==2?10:9,&p))return false;if(named)for(j=0;j<6;++j)if(p[j]<32||p[j]>126)return false;xx_rt_snprintf(label,sizeof(label),"color-%u.acb",i);if(!tg_emit(f,s,label,start,q.p-start,n))return false;}
 if(q.p<n){if(n-q.p!=8||(!tg_tag(b+q.p,"spflspot",8)&&!tg_tag(b+q.p,"spflproc",8))||!tg_emit(f,s,"spot-process.acb",q.p,8,n))return false;q.p=n;}
 s->size=(int64_t)n;return q.p==n;
}

void xx_adobe_acb_init(xx_adobe_acb *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_ADOBE_ACB,"acb");}}
xx_adobe_acb *xx_adobe_acb_create(xx_io_device *d,int64_t at) {xx_adobe_acb *r=(xx_adobe_acb *)xx_mem_alloc(sizeof(*r));if(r)xx_adobe_acb_init(r,d,at);return r;}
void xx_adobe_acb_destroy(xx_adobe_acb *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adobe_acb_free(xx_adobe_acb *r) {if(r){xx_adobe_acb_destroy(r);xx_mem_free(r);}}
bool xx_adobe_acb_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adobe_acb_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
