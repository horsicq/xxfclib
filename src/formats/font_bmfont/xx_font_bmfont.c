/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://www.angelcode.com/products/bmfont/doc/file_format.html
 * Binary BMFont3 with complete info/common/pages/chars/kerning blocks, bounded atlas rectangles, unique scalar character IDs and page/kerning references (up to4096 characters/kerning pairs). Original typed blocks exported; text/XML variants, external texture loading and rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/font_bmfont/xx_font_bmfont.h"
#include "../common/xx_texture_font_components.h"
static bool texture_font_quick(Abstractformat *f,uint64_t n) {uint8_t b[4];return texture_font_probe(f,n,b,4)&&pm_tag(b,"BMF\3",4);}
static bool bm_find(const uint8_t *b,uint64_t at,uint32_t count,uint32_t id) {uint32_t i;for(i=0;i<count;++i)if(xx_data_get_u32(b+at+(uint64_t)i*20, 4, 0, false)==id)return true;return false;}
static bool texture_font_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=4,pos[5]={0},len[5]={0},q,end,nameEnd;uint32_t i,j,count,pages,w,h;char label[64];
 if(!texture_font_emit(f,s,"bmfont-header.bin",0,4,n))return false;
 for(i=1;p<n;++i){uint32_t size;if(i>5||!texture_font_span(p,5,n)||b[p]!=i)return false;size=xx_data_get_u32(b+p+1, 4, 0, false);if(!texture_font_span(p+5,size,n))return false;pos[i-1]=p+5;len[i-1]=size;xx_rt_snprintf(label,sizeof(label),"block-%u.bin",i);if(!texture_font_emit(f,s,label,p,5+(uint64_t)size,n))return false;p+=5+(uint64_t)size;}
 if(!len[0]||len[1]!=15||!len[2]||!len[3]||len[3]%20||len[4]%10||len[4]/10>4096)return false;
 q=pos[0];end=q+len[0];if(len[0]<15||!xx_data_get_u16(b+q, 2, 0, false)||(b[q+2]&7)||!xx_data_get_u16(b+q+4, 2, 0, false)||!b[q+6]||!texture_font_nul(b,q+14,end,&nameEnd)||nameEnd!=end)return false;
 q=pos[1];pages=xx_data_get_u16(b+q+8, 2, 0, false);w=xx_data_get_u16(b+q+4, 2, 0, false);h=xx_data_get_u16(b+q+6, 2, 0, false);if(!xx_data_get_u16(b+q, 2, 0, false)||xx_data_get_u16(b+q+2, 2, 0, false)>xx_data_get_u16(b+q, 2, 0, false)||!w||!h||!pages||pages>256||(b[q+10]&127))return false;for(i=11;i<15;++i)if(b[q+i]>4)return false;
 q=pos[2];end=q+len[2];if(!texture_font_nul(b,q,end,&nameEnd)||nameEnd==q+1)return false;{uint64_t size=nameEnd-q;if(len[2]!=(uint64_t)pages*size)return false;for(i=0;i<pages;++i){if(!texture_font_nul(b,q,end,&nameEnd)||nameEnd-q!=size)return false;q=nameEnd;}}
 count=(uint32_t)(len[3]/20);if(count>4096)return false;
 for(i=0;i<count;++i){uint32_t id;q=pos[3]+(uint64_t)i*20;id=xx_data_get_u32(b+q, 4, 0, false);if(texture_font_stop(pd)||(!texture_font_scalar(id)&&id!=0xffffffffU)||xx_data_get_u16(b+q+4, 2, 0, false)+(uint32_t)xx_data_get_u16(b+q+8, 2, 0, false)>w||xx_data_get_u16(b+q+6, 2, 0, false)+(uint32_t)xx_data_get_u16(b+q+10, 2, 0, false)>h||b[q+18]>=pages||!b[q+19]||(b[q+19]&240))return false;for(j=0;j<i;++j)if(xx_data_get_u32(b+pos[3]+(uint64_t)j*20, 4, 0, false)==id)return false;}
 for(q=pos[4];q<pos[4]+len[4];q+=10){uint64_t z;if(texture_font_stop(pd)||!bm_find(b,pos[3],count,xx_data_get_u32(b+q, 4, 0, false))||!bm_find(b,pos[3],count,xx_data_get_u32(b+q+4, 4, 0, false))||!xx_data_get_u16(b+q+8, 2, 0, false))return false;for(z=pos[4];z<q;z+=10)if(pm_tag(b+z,(const char *)(b+q),8))return false;}
 s->size=(int64_t)n;return true;
}

void xx_font_bmfont_init(xx_font_bmfont *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_FONT_BMFONT,"fnt");}}
xx_font_bmfont *xx_font_bmfont_create(xx_io_device *d,int64_t at) {xx_font_bmfont *r=(xx_font_bmfont *)xx_mem_alloc(sizeof(*r));if(r)xx_font_bmfont_init(r,d,at);return r;}
void xx_font_bmfont_destroy(xx_font_bmfont *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_font_bmfont_free(xx_font_bmfont *r) {if(r){xx_font_bmfont_destroy(r);xx_mem_free(r);}}
bool xx_font_bmfont_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_font_bmfont_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
