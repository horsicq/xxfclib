/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/audio_hca/xx_audio_hca.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef AUDIO_HCA
#define XX_FILE_TYPE_AUDIO_HCA ((xx_file_type_t)818)
#endif
static bool e8_parse(e8_blob*c) {
 size_t header,p=40;uint32_t frames,rate;unsigned channels,block;uint16_t version;unsigned last=0;
 if(!e8_range(c,0,42) || !e8_eq(c,0,"HCA\0",4) || (version=xx_data_get_u16(c->b+4, 2, 0, true))!=0x200 || (header=xx_data_get_u16(c->b+6, 2, 0, true))<42 || !e8_range(c,0,header) || e8_crc(c->b,header,c->pd) || !e8_eq(c,8,"fmt\0",4) || !(channels=c->b[12]) || channels>16 || (rate=((uint32_t)c->b[13]<<16)|((uint32_t)c->b[14]<<8)|c->b[15])<8000 || rate>192000 || !(frames=xx_data_get_u32(c->b+16, 4, 0, true)) || frames>4095 || (uint32_t)xx_data_get_u16(c->b+20, 2, 0, true)+xx_data_get_u16(c->b+22, 2, 0, true)>frames*1024U || !e8_eq(c,24,"comp",4) || (block=xx_data_get_u16(c->b+28, 2, 0, true))<8 || c->b[30]!=1 || c->b[31]!=15 || c->b[32]>channels || !c->b[34] || c->b[34]>128 || c->b[35]+c->b[36]>c->b[34] || c->b[39] || (uint64_t)frames*block!=c->n-header)return false;
 while(p<header-2){unsigned order;size_t z;if(e8_eq(c,p,"ath\0",4)){order=1;z=6;if(!e8_range(c,p,z) || xx_data_get_u16(c->b+p+4, 2, 0, true)>1)return false;}else if(e8_eq(c,p,"loop",4)){order=2;z=16;if(!e8_range(c,p,z) || xx_data_get_u32(c->b+p+4, 4, 0, true)>xx_data_get_u32(c->b+p+8, 4, 0, true) || xx_data_get_u32(c->b+p+8, 4, 0, true)>=frames)return false;}else if(e8_eq(c,p,"ciph",4)){order=3;z=6;if(!e8_range(c,p,z) || xx_data_get_u16(c->b+p+4, 2, 0, true))return false;}else if(e8_eq(c,p,"rva\0",4)){order=4;z=8;if(!e8_range(c,p,z) || (xx_data_get_u32(c->b+p+4, 4, 0, true)&0x7f800000U)==0x7f800000U)return false;}else if(e8_eq(c,p,"comm",4)){order=5;if(!e8_range(c,p,5))return false;z=5U+c->b[p+4];}else if(e8_eq(c,p,"pad\0",4)){if(!e8_zero(c,p+4,header-2-p-4))return false;p=header-2;break;}else return false;if(order<=last || z>header-2-p)return false;last=order;p+=z;}
 if(p!=header-2 || !e8_add(c,"header.bin",0,header)) {return false; } for(p=header;p<c->n;p+=block)if(c->b[p]!=255 || c->b[p+1]!=255 || e8_crc(c->b+p,block,c->pd) || !e8_add(c,"encoded-hca-frame.bin",p,block))return false;return true;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_audio_hca_init(xx_audio_hca*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AUDIO_HCA,"bin");}}
xx_audio_hca*xx_audio_hca_create(xx_io_device*d,int64_t b) {xx_audio_hca*r=(xx_audio_hca*)xx_mem_alloc(sizeof(*r));if(r)xx_audio_hca_init(r,d,b);return r;}
void xx_audio_hca_destroy(xx_audio_hca*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_audio_hca_free(xx_audio_hca*r) {if(r){xx_audio_hca_destroy(r);xx_mem_free(r);}}
bool xx_audio_hca_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_audio_hca_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
