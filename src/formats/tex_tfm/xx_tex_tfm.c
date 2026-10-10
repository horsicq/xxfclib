/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/TeX-Live/texlive-source/trunk/texk/web2c/tex.web
 * Standard TeX TFM with exact12-count framing, typed character/metric/ligature/kern/recipe references and finite fixed-point tables. Original metric sections exported; Japanese/JFM dialects, font lookup and rendering are unsupported. Signatureless detection is offset-zero only.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/tex_tfm/xx_tex_tfm.h"
#include "../common/xx_texture_font_components.h"
static bool texture_font_quick(Abstractformat *f,uint64_t n) {uint8_t b[24];return texture_font_probe(f,n,b,24)&&xx_data_get_u16(b, 2, 0, true)*4U==n&&xx_data_get_u16(b+2, 2, 0, true)>=2&&xx_data_get_u16(b+4, 2, 0, true)<=255&&xx_data_get_u16(b+6, 2, 0, true)<=255;}
static bool tf_char(const uint8_t *b,uint64_t chars,uint32_t bc,uint32_t ec,uint32_t c) {return c>=bc&&c<=ec&&b[chars+(c-bc)*4]!=0;}
static bool texture_font_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 static const char *labels[10]={"tfm-header.bin","character-info.bin","widths.bin","heights.bin","depths.bin","italics.bin","ligature-kern.bin","kerns.bin","extensible-recipes.bin","parameters.bin"};
 uint32_t counts[10],bc=xx_data_get_u16(b+4, 2, 0, true),ec=xx_data_get_u16(b+6, 2, 0, true),i,j;uint64_t at[10],p=24,total=6;uint32_t h=xx_data_get_u16(b+2, 2, 0, true),nc=ec>=bc?ec-bc+1:0;
 counts[0]=h;counts[1]=nc;for(i=2;i<10;++i)counts[i]=xx_data_get_u16(b+4+i*2, 2, 0, true);for(i=0;i<10;++i){at[i]=p;p+=(uint64_t)counts[i]*4;total+=counts[i];}
 if(total*4!=n||h>1024||!counts[2]||!counts[3]||!counts[4]||!counts[5]||counts[2]>256||counts[3]>16||counts[4]>16||counts[5]>64||counts[8]>256||counts[9]>256||xx_data_get_u32(b+28, 4, 0, true)<0x100000U||xx_data_get_u32(b+28, 4, 0, true)>0x7fffffffU)return false;
 for(i=2;i<=5;++i)if(xx_data_get_u32(b+at[i], 4, 0, true)!=0)return false;
 for(i=0;i<nc;++i){const uint8_t *c=b+at[1]+(uint64_t)i*4;uint32_t tag=c[2]&3,rem=c[3];if(texture_font_stop(pd)||c[0]>=counts[2]||(uint32_t)(c[1]>>4)>=counts[3]||(uint32_t)(c[1]&15)>=counts[4]||(uint32_t)(c[2]>>2)>=counts[5])return false;if(!c[0]){if(c[1]||c[2]||c[3])return false;continue;}if(tag==1&&rem>=counts[6])return false;if(tag==3&&rem>=counts[8])return false;if(tag==2){uint32_t target=rem,steps=0;while(true){const uint8_t *next;if(!tf_char(b,at[1],bc,ec,target)||++steps>nc)return false;next=b+at[1]+(target-bc)*4;if((next[2]&3)!=2)break;target=next[3];}}}
 for(i=0;i<counts[6];++i){const uint8_t *l=b+at[6]+(uint64_t)i*4;uint32_t rem=l[3];if(texture_font_stop(pd))return false;if(l[0]>128){uint32_t jump=(uint32_t)l[2]*256+rem;if(jump>=counts[6]&&!(i==0||i+1==counts[6]))return false;}else{if(l[0]<128&&i+l[0]+1>=counts[6])return false;if(!tf_char(b,at[1],bc,ec,l[1]))return false;if(l[2]>=128){if((uint32_t)(l[2]-128)*256+rem>=counts[7])return false;}else if(!tf_char(b,at[1],bc,ec,rem))return false;}}
 for(i=0;i<counts[8];++i)for(j=0;j<4;++j){uint32_t c=b[at[8]+(uint64_t)i*4+j];if((c||j==3)&&!tf_char(b,at[1],bc,ec,c))return false;}
 if(!texture_font_emit(f,s,"tfm-counts.bin",0,24,n)) {return false; } for(i=0;i<10;++i)if(counts[i]&&!texture_font_emit(f,s,labels[i],at[i],(uint64_t)counts[i]*4,n))return false;s->size=(int64_t)n;return true;
}

void xx_tex_tfm_init(xx_tex_tfm *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_TEX_TFM,"tfm");}}
xx_tex_tfm *xx_tex_tfm_create(xx_io_device *d,int64_t at) {xx_tex_tfm *r=(xx_tex_tfm *)xx_mem_alloc(sizeof(*r));if(r)xx_tex_tfm_init(r,d,at);return r;}
void xx_tex_tfm_destroy(xx_tex_tfm *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tex_tfm_free(xx_tex_tfm *r) {if(r){xx_tex_tfm_destroy(r);xx_mem_free(r);}}
bool xx_tex_tfm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tex_tfm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
