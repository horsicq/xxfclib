/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/smacker.c
 * Smacker2/4 with complete Huffman-tree size descriptors, frame-size/type tables and bounded audio/palette/video packets. Original encoded trees and frames are exported; compressed codec/tree semantics remain encoded and unknown extensions are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/rad_smacker/xx_rad_smacker.h"
#include "../audio_dolby_ac3/xx_ninth_media.h"
static bool ng_quick(Abstractformat *f,uint64_t n) {uint8_t h[104];return ng_probe(f,n,h,sizeof(h))&&(pm_tag(h,"SMK2",4)||pm_tag(h,"SMK4",4));}
static bool sm_palette(const uint8_t *b,uint64_t *q,uint64_t end) {
 uint64_t at=*q,stop;uint32_t colors=0;if(!ng_span(at,1,end)||!b[at])return false;stop=at+(uint64_t)b[at]*4;if(stop>end)return false;++at;
 while(colors<256){uint32_t v,count;if(at>=stop)return false;v=b[at++];if(v&128U){count=(v&127U)+1;}else if(v&64U){count=(v&63U)+1;if(at>=stop||b[at++]+count>256)return false;}else{count=1;if(!ng_span(at,2,stop)||b[at]>63||b[at+1]>63)return false;at+=2;}if(count>256-colors)return false;colors+=count;}
 if(stop-at>3) {return false; } *q=stop;return true;
}
static bool ng_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t frames=xx_data_get_u32(b+12, 4, 0, false),flags=xx_data_get_u32(b+20, 4, 0, false),trees=xx_data_get_u32(b+52, 4, 0, false),i,j;uint64_t at,types;uint32_t rate[7],af[7],maximum[7];int32_t duration=(int32_t)xx_data_get_u32(b+16, 4, 0, false);
 if(!xx_data_get_u32(b+4, 4, 0, false)||xx_data_get_u32(b+4, 4, 0, false)>8192||!xx_data_get_u32(b+8, 4, 0, false)||xx_data_get_u32(b+8, 4, 0, false)>8192||!frames||frames>4092||flags>7||!duration||duration==(-2147483647-1)||duration>21474836||!trees||trees>16777216)return false;
 frames+=flags&1U;types=104+(uint64_t)frames*4;at=types+frames;if(!ng_span(at,trees,n))return false;
 for(i=0;i<4;++i)if(xx_data_get_u32(b+56+i*4, 4, 0, false)>16777216)return false;
 for(i=0;i<7;++i){uint32_t r=xx_data_get_u32(b+72+i*4, 4, 0, false);rate[i]=r&0xffffffU;af[i]=r>>24;maximum[i]=xx_data_get_u32(b+24+i*4, 4, 0, false);if(rate[i]&&(rate[i]>192000||(af[i]&3U)))return false;if(!rate[i]&&af[i])return false;}
 if(!ng_emit(f,s,"smacker_header_tables.bin",0,at,n)||!ng_emit(f,s,"smacker_encoded_trees.bin",at,trees,n)) {return false; } at+=trees;
 for(i=0;i<frames;++i){uint64_t begin=at,end,q;uint32_t size=xx_data_get_u32(b+104+(uint64_t)i*4, 4, 0, false)&~3U,type=b[types+i];if(ng_stop(pd)||!size||!ng_span(at,size,n))return false;end=at+size;q=at;if((type&1)&&!sm_palette(b,&q,end))return false;
  for(j=0;j<7;++j)if(type&(2U<<j)){uint32_t bytes,decoded;bool prefix=(af[j]&(0x80U|0x08U|0x04U))!=0||(af[j]&0x20U)==0;if(!rate[j]||!ng_span(q,4,end))return false;bytes=xx_data_get_u32(b+q, 4, 0, false);if(bytes<4+(prefix?4U:0U)||!ng_span(q,bytes,end))return false;decoded=prefix?xx_data_get_u32(b+q+4, 4, 0, false):bytes-4;if(!decoded||(maximum[j]&&decoded>maximum[j]))return false;if(!(af[j]&(0x80U|0x08U|0x04U))&&((bytes-4-(prefix?4U:0U))%(((af[j]&0x10U)?2U:1U)*((af[j]&0x20U)?2U:1U))))return false;q+=bytes;}
  if(q>=end||!ng_emit(f,s,"smacker_encoded_frame.bin",begin,size,n)) {return false; } at=end;
 }
 if(at!=n) {return false; } s->size=(int64_t)n;return true;
}

void xx_rad_smacker_init(xx_rad_smacker *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_RAD_SMACKER,"smk");}}
xx_rad_smacker *xx_rad_smacker_create(xx_io_device *d,int64_t at) {xx_rad_smacker *r=(xx_rad_smacker *)xx_mem_alloc(sizeof(*r));if(r)xx_rad_smacker_init(r,d,at);return r;}
void xx_rad_smacker_destroy(xx_rad_smacker *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_rad_smacker_free(xx_rad_smacker *r) {if(r){xx_rad_smacker_destroy(r);xx_mem_free(r);}}
bool xx_rad_smacker_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_rad_smacker_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
