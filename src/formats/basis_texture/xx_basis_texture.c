/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/BinomialLLC/basis_universal/master/transcoder/basisu_file_headers.h
 * Basis header77 versions16-19, ETC1S/UASTC4x4 2D images with ordered mip slices and consistent ETC1S alpha-pair/UASTC alpha flags, complete disjoint codebook/table/descriptor/slice spans and header/data CRC16 (plus UASTC slice CRC16). ETC1S decoded-slice CRCs remain encoded. Compression remains encoded; external codebooks, extended data, HDR/XUASTC, other texture types and transcoding are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/basis_texture/xx_basis_texture.h"
#include "../astc_texture/xx_tenth_media.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[77];return tg_probe(f,n,b,77)&&pm_tag(b,"sB",2)&&pm_le16(b+4)==77;}
typedef struct ba_region { uint32_t at,size; } ba_region;
static bool ba_region_add(ba_region *r,uint32_t *count,uint32_t at,uint32_t size,uint64_t n) {uint32_t i;if(!size||*count>=4094||at<77||!tg_span(at,size,n))return false;for(i=0;i<*count;++i)if(at<(uint64_t)r[i].at+r[i].size&&r[i].at<(uint64_t)at+size)return false;r[*count].at=at;r[*count].size=size;++*count;return true;}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t slices=tg_le24(b+14),images=tg_le24(b+17),desc=pm_le32(b+65),format=b[20],flags=pm_le16(b+21),i,j,regions=0,lastImage=0,lastLevel=0,lastW=0,lastH=0;uint64_t total=77;ba_region *r;bool result=false,anyAlpha=false;char label[64];
 if(pm_le16(b+2)<16||pm_le16(b+2)>19||pm_le32(b+8)!=n-77||tg_basis_crc(b+8,69,pd)!=pm_le16(b+6)||tg_basis_crc(b+77,n-77,pd)!=pm_le16(b+12)||!slices||slices>4087||!images||images>slices||format>1||b[23]!=0||tg_le24(b+24)!=0||pm_le32(b+27)!=0||(flags&~23U)||(flags&8)||((flags&1)!=0)!=(format==0)||pm_le32(b+69)||pm_le32(b+73)||!tg_span(desc,(uint64_t)slices*23,n))return false;
 r=(ba_region *)xx_mem_alloc(4094*sizeof(*r));if(!r)return false;
 if(!ba_region_add(r,&regions,desc,slices*23,n)||!tg_emit(f,s,"basis-header.bin",0,77,n)||!tg_emit(f,s,"slice-directory.bin",desc,(uint64_t)slices*23,n))goto done;
 for(i=0;i<3;++i){uint32_t off=i==0?pm_le32(b+41):i==1?pm_le32(b+50):pm_le32(b+57),size=i==0?tg_le24(b+45):i==1?tg_le24(b+54):pm_le32(b+61),count=i==0?pm_le16(b+39):i==1?pm_le16(b+48):0;if(format==1){if(off||size||count)goto done;}else{if((i<2&&!count)||!ba_region_add(r,&regions,off,size,n))goto done;xx_rt_snprintf(label,sizeof(label),"%s.bin",i==0?"endpoint-codebook":i==1?"selector-codebook":"entropy-tables");if(!tg_emit(f,s,label,off,size,n))goto done;}}
 for(i=0;i<slices;++i){const uint8_t *d=b+desc+(uint64_t)i*23;uint32_t image=tg_le24(d),level=d[3],sf=d[4],w=pm_le16(d+5),h=pm_le16(d+7),x=pm_le16(d+9),y=pm_le16(d+11),off=pm_le32(d+13),size=pm_le32(d+17);bool alpha=format==0&&(flags&4)&&((i&1)!=0);
  if(tg_stop(pd)||image>=images||!w||!h||x!=(w+3)/4||y!=(h+3)/4||(sf&~1U)||(format==0&&((sf&1)!=0)!=alpha)||!ba_region_add(r,&regions,off,size,n)||(format==1&&tg_basis_crc(b+off,size,pd)!=pm_le16(d+21))||(format==1&&size!=(uint64_t)x*y*16))goto done;
  anyAlpha|=(sf&1)!=0;
  if(i==0){if(image||level||alpha)goto done;}else if(alpha){if(image!=lastImage||level!=lastLevel||w!=lastW||h!=lastH)goto done;}else if(image==lastImage){if(level!=lastLevel+1||w!=(lastW>1?lastW/2:1)||h!=(lastH>1?lastH/2:1))goto done;}else if(image!=lastImage+1||level!=0)goto done;
  xx_rt_snprintf(label,sizeof(label),"image-%u-level-%u-%s.basis",image,level,alpha?"alpha":"color");if(!tg_emit(f,s,label,off,size,n))goto done;lastImage=image;lastLevel=level;lastW=w;lastH=h;
 }
 if((format==1&&((flags&4)!=0)!=anyAlpha)||lastImage+1!=images||(format==0&&(flags&4)&&(slices&1)))goto done;
 for(j=0;j<regions;++j)total+=r[j].size;if(total!=n)goto done;s->size=(int64_t)n;result=true;
done:xx_mem_free(r);return result;
}

void xx_basis_texture_init(xx_basis_texture *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_BASIS_TEXTURE,"basis");}}
xx_basis_texture *xx_basis_texture_create(xx_io_device *d,int64_t at) {xx_basis_texture *r=(xx_basis_texture *)xx_mem_alloc(sizeof(*r));if(r)xx_basis_texture_init(r,d,at);return r;}
void xx_basis_texture_destroy(xx_basis_texture *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_basis_texture_free(xx_basis_texture *r) {if(r){xx_basis_texture_destroy(r);xx_mem_free(r);}}
bool xx_basis_texture_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_basis_texture_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
