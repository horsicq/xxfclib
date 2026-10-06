/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/Ericsson/ETCPACK/master/source/etcpack.cxx
 * PKM1 ETC1 and PKM2 ETC2/EAC formats0,1,3-11, canonical four-pixel rounded geometry and exact encoded block counts. Deprecated type2, mipmaps and texture decoding are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/pkm_texture/xx_pkm_texture.h"
#include "../astc_texture/xx_tenth_media.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[16];return tg_probe(f,n,b,16)&&(pm_tag(b,"PKM 10",6)||pm_tag(b,"PKM 20",6));}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t type=pm_be16(b+6),x=pm_be16(b+12),y=pm_be16(b+14),cx=pm_be16(b+8),cy=pm_be16(b+10),bytes;uint64_t size;
 if(tg_stop(pd)||type>11||type==2||(b[4]=='1'&&type!=0)||!x||!y||cx!=(x+3)/4*4||cy!=(y+3)/4*4)return false;
 bytes=(type==3||type==6||type==8||type==10)?16U:8U;size=(uint64_t)(cx/4)*(cy/4)*bytes;if(n!=16+size)return false;
 if(!tg_emit(f,s,"pkm-header.bin",0,16,n)||!tg_emit(f,s,"texture-blocks.pkm",16,size,n)) {return false; } s->size=(int64_t)n;return true;
}

void xx_pkm_texture_init(xx_pkm_texture *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_PKM_TEXTURE,"pkm");}}
xx_pkm_texture *xx_pkm_texture_create(xx_io_device *d,int64_t at) {xx_pkm_texture *r=(xx_pkm_texture *)xx_mem_alloc(sizeof(*r));if(r)xx_pkm_texture_init(r,d,at);return r;}
void xx_pkm_texture_destroy(xx_pkm_texture *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_pkm_texture_free(xx_pkm_texture *r) {if(r){xx_pkm_texture_destroy(r);xx_mem_free(r);}}
bool xx_pkm_texture_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_pkm_texture_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
