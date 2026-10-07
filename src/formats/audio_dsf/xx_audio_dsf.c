/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/audio_dsf/xx_audio_dsf.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef AUDIO_DSF
#define XX_FILE_TYPE_AUDIO_DSF ((xx_file_type_t)813)
#endif
static bool e8_parse(e8_blob*c) {
 uint32_t channels,type,block,bits,rate;uint64_t samples,data,expected;unsigned channel_counts[]={0,1,2,3,4,4,5,6};
 if(!e8_range(c,0,92) || !e8_eq(c,0,"DSD ",4) || xx_data_get_u64(c->b+4, 8, 0, false)!=28 || xx_data_get_u64(c->b+12, 8, 0, false)!=c->n || xx_data_get_u64(c->b+20, 8, 0, false) || !e8_eq(c,28,"fmt ",4) || xx_data_get_u64(c->b+32, 8, 0, false)!=52 || xx_data_get_u32(c->b+40, 4, 0, false)!=1 || xx_data_get_u32(c->b+44, 4, 0, false))return false;
 type=xx_data_get_u32(c->b+48, 4, 0, false);channels=xx_data_get_u32(c->b+52, 4, 0, false);rate=xx_data_get_u32(c->b+56, 4, 0, false);bits=xx_data_get_u32(c->b+60, 4, 0, false);samples=xx_data_get_u64(c->b+64, 8, 0, false);block=xx_data_get_u32(c->b+72, 4, 0, false);
 if(type<1 || type>7 || channels!=channel_counts[type] || !rate || rate>24576000 || (rate&7) || (bits!=1 && bits!=8) || !samples || samples>E8_LIMIT*8U || !block || block>65536 || xx_data_get_u32(c->b+76, 4, 0, false) || !e8_eq(c,80,"data",4) || (data=xx_data_get_u64(c->b+84, 8, 0, false))<12 || data!=c->n-80)return false;
 expected=(((samples+7U)/8U+block-1U)/block)*block*channels;if(expected!=data-12)return false;
 return e8_add(c,"header-format.bin",0,80) && e8_add(c,"dsd-channel-blocks.bin",80,(size_t)data);
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_audio_dsf_init(xx_audio_dsf*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AUDIO_DSF,"bin");}}
xx_audio_dsf*xx_audio_dsf_create(xx_io_device*d,int64_t b) {xx_audio_dsf*r=(xx_audio_dsf*)xx_mem_alloc(sizeof(*r));if(r)xx_audio_dsf_init(r,d,b);return r;}
void xx_audio_dsf_destroy(xx_audio_dsf*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_audio_dsf_free(xx_audio_dsf*r) {if(r){xx_audio_dsf_destroy(r);xx_mem_free(r);}}
bool xx_audio_dsf_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_audio_dsf_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
