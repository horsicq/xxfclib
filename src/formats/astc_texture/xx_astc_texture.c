/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ARM-software/astc-encoder/main/Docs/FileFormat.md
 * ASTC 2D containers with standard block footprints, positive 24-bit dimensions and an exact complete encoded block array. Blocks remain encoded; 3D textures and texture decoding are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/astc_texture/xx_astc_texture.h"
#include "../common/xx_texture_font_components.h"
static bool texture_font_quick(Abstractformat *f,uint64_t n) {uint8_t b[16];return texture_font_probe(f,n,b,16)&&pm_tag(b,"\x13\xab\xa1\x5c",4);}
static bool texture_font_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 static const uint8_t footprints[14][2]={{4,4},{5,4},{5,5},{6,5},{6,6},{8,5},{8,6},{8,8},{10,5},{10,6},{10,8},{10,10},{12,10},{12,12}};
 uint32_t x=xx_data_get_u24(b+7, 3, 0, false),y=xx_data_get_u24(b+10, 3, 0, false);uint64_t blocks;unsigned i;bool found=false;
 if(texture_font_stop(pd)||b[6]!=1||xx_data_get_u24(b+13, 3, 0, false)!=1||!x||!y)return false;
 for(i=0;i<14;++i) {if(b[4]==footprints[i][0]&&b[5]==footprints[i][1])found=true; } if(!found)return false;
 blocks=((uint64_t)x+b[4]-1)/b[4]*(((uint64_t)y+b[5]-1)/b[5]);if(blocks>(67108864-16)/16||n!=16+blocks*16)return false;
 if(!texture_font_emit(f,s,"astc-header.bin",0,16,n)||!texture_font_emit(f,s,"texture-blocks.astc",16,n-16,n)) {return false; } s->size=(int64_t)n;return true;
}

void xx_astc_texture_init(xx_astc_texture *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_ASTC_TEXTURE,"astc");}}
xx_astc_texture *xx_astc_texture_create(xx_io_device *d,int64_t at) {xx_astc_texture *r=(xx_astc_texture *)xx_mem_alloc(sizeof(*r));if(r)xx_astc_texture_init(r,d,at);return r;}
void xx_astc_texture_destroy(xx_astc_texture *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_astc_texture_free(xx_astc_texture *r) {if(r){xx_astc_texture_destroy(r);xx_mem_free(r);}}
bool xx_astc_texture_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_astc_texture_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
