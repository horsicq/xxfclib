/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/freetype/freetype/master/src/winfonts/winfnt.c
 * Standalone Windows FNT2/3 raster fonts with complete glyph directory, bounded column-major bitmap extents and device/face strings. Original glyphs and font metadata are exported; vector fonts, .FON/NE wrappers and rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/font_windows_fnt/xx_font_windows_fnt.h"
#include "../astc_texture/xx_tenth_media.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[118];return tg_probe(f,n,b,118)&&(xx_data_get_u16(b, 2, 0, false)==0x200||xx_data_get_u16(b, 2, 0, false)==0x300)&&xx_data_get_u32(b+2, 4, 0, false)==n;}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t header=xx_data_get_u16(b, 2, 0, false)==0x200?118U:148U,entry=header==118?4U:6U,count=(uint32_t)b[96]-b[95]+1,height=xx_data_get_u16(b+88, 2, 0, false),i;uint64_t p,table,end,face=xx_data_get_u32(b+105, 4, 0, false),device=xx_data_get_u32(b+101, 4, 0, false),after;char label[64];
 if(n<header||b[96]<b[95]||!height||height>4096||xx_data_get_u16(b+74, 2, 0, false)>height||xx_data_get_u16(b+66, 2, 0, false)!=0||b[80]>1||b[81]>1||b[82]>1||xx_data_get_u16(b+83, 2, 0, false)>1000||!xx_data_get_u16(b+91, 2, 0, false)||!xx_data_get_u16(b+93, 2, 0, false)||b[97]>=count||b[98]>=count||b[117]||xx_data_get_u32(b+109, 4, 0, false)!=0)return false;
 if(header==148&&(xx_data_get_u32(b+118, 4, 0, false)&~31U))return false;
 table=header+(uint64_t)(count+1)*entry;if(table>n||xx_data_get_u32(b+113, 4, 0, false)!=table||!tg_emit(f,s,"fnt-header-directory.bin",0,table,n))return false;p=table;
 for(i=0;i<=count;++i){uint64_t q=header+(uint64_t)i*entry;uint32_t width=xx_data_get_u16(b+q, 2, 0, false),off=entry==4?xx_data_get_u16(b+q+2, 2, 0, false):xx_data_get_u32(b+q+2, 4, 0, false);uint64_t bytes=(uint64_t)((width+7)/8)*height;if(tg_stop(pd)||width>4096||width>xx_data_get_u16(b+93, 2, 0, false)||off!=p||!tg_span(off,bytes,n))return false;if(bytes){xx_rt_snprintf(label,sizeof(label),i==count?"sentinel.bitmap":"glyph-%u.bitmap",i+b[95]);if(!tg_emit(f,s,label,off,bytes,n))return false;}p+=bytes;}
 end=p;if(face<end||!tg_nul(b,face,n,&after)||after-face>256)return false;if(device&&(device<end||!tg_nul(b,device,n,&after)||after-device>256))return false;
 /* Producer string tables and their zero alignment bytes remain encoded. */
 if(end>=n||!tg_emit(f,s,"fnt-name-metadata.bin",end,n-end,n)) {return false; } s->size=(int64_t)n;return true;
}

void xx_font_windows_fnt_init(xx_font_windows_fnt *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_FONT_WINDOWS_FNT,"fnt");}}
xx_font_windows_fnt *xx_font_windows_fnt_create(xx_io_device *d,int64_t at) {xx_font_windows_fnt *r=(xx_font_windows_fnt *)xx_mem_alloc(sizeof(*r));if(r)xx_font_windows_fnt_init(r,d,at);return r;}
void xx_font_windows_fnt_destroy(xx_font_windows_fnt *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_font_windows_fnt_free(xx_font_windows_fnt *r) {if(r){xx_font_windows_fnt_destroy(r);xx_mem_free(r);}}
bool xx_font_windows_fnt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_font_windows_fnt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
