/* SPDX-License-Identifier: MIT
 * EdLib S01 prefix and embedded Creative VOC descriptor, independently
 * implemented from JCH's Mplayer.doc, original S01 files, and libsndfile voc.c.
 * Only the first VOC sound-data header is described; all sample bodies and
 * subsequent unverified S01 data retain raw bytes. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxe38_prefix_fields[]={
 SX_FIELD("signature","bytes",0,6,SX_SIGNATURE),
 SX_FIELD("block_type","uint8",6,1,SX_ID),
};
static const sx_layout sxe38_prefix={0x60b00001U,"edlib_s01_header",sxe38_prefix_fields,SX_COUNT(sxe38_prefix_fields),SX_LE};
static const xx_data_struct_field_desc sxe38_voc_fields[]={
 SX_FIELD("signature","bytes",0,20,SX_SIGNATURE),
 SX_FIELD("first_block_offset","uint16",20,2,SX_OFFSET),
 SX_FIELD("version","uint16",22,2,SX_NONE),
 SX_FIELD("version_checksum","uint16",24,2,SX_CHECKSUM),
};
static const sx_layout sxe38_voc={0x60b00002U,"edlib_voc_header",sxe38_voc_fields,SX_COUNT(sxe38_voc_fields),SX_LE};
static const xx_data_struct_field_desc sxe38_sound_fields[]={
 SX_FIELD("type","uint8",0,1,SX_ID),
 SX_FIELD("payload_size","uint24",1,3,SX_SIZE),
 SX_FIELD("time_constant","uint8",4,1,SX_NONE),
 SX_FIELD("codec","uint8",5,1,SX_ID),
};
static const sx_layout sxe38_sound={0x60b00003U,"edlib_voc_sound_header",sxe38_sound_fields,SX_COUNT(sxe38_sound_fields),SX_LE};
static const sx_layout*const sx_edlibsamples_layouts[]={&sxe38_prefix,&sxe38_voc,&sxe38_sound};
static bool sx_edlibsamples_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_JCH_EDLIB_S01;}
static bool sxe38_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p;}
static bool sxe38_read(sx_context*c,uint64_t p,void*out,size_t n){return sxe38_span(c,p,n)&&sx_read(c,(int64_t)p,out,n);}
static bool sxe38_raw_all(sx_context*c){uint8_t sentinel;if(!c||c->count||c->failed||c->size<=0||xx_pd_is_stopped(c->pd)||!sxe38_read(c,0,&sentinel,1))return false;return sx_raw(c,0,c->size);}
static bool sxe38_raw_tail(sx_context*c,uint64_t p){uint8_t sentinel;if(!c||c->failed||p>(uint64_t)c->size||xx_pd_is_stopped(c->pd))return false;if(p==(uint64_t)c->size)return c->count>0;if(!sxe38_read(c,p,&sentinel,1))return false;return sx_raw(c,(int64_t)p,c->size-(int64_t)p);}
static bool sx_edlibsamples_build(sx_context*c){
 uint8_t h[96];if(!c||!c->f||!sx_edlibsamples_supports(c->f->file_type)||c->failed||xx_pd_is_stopped(c->pd))return false;
 if(!sxe38_span(c,0,sizeof(h)))return sxe38_raw_all(c);if(!sxe38_read(c,0,h,sizeof(h)))return false;
 if(memcmp(h,"JCH\x26\x02\x66\x01",7)||memcmp(h+64,"Creative Voice File\x1a",20)||h[84]!=26||h[85]||h[86]!=10||h[87]!=1||h[88]!=0x29||h[89]!=0x11||h[90]!=1||h[94]!=0xc3||h[95])return sxe38_raw_all(c);
 uint64_t length=(uint64_t)h[91]|(uint64_t)h[92]<<8|(uint64_t)h[93]<<16;
 /* Stored header fields survive truncated PCM bodies. A sound payload shorter
  * than its two-byte codec prefix cannot establish this physical layout. */
 if(length<2)return sxe38_raw_all(c);
 return sx_add(c,&sxe38_prefix,0,7)&&sx_raw(c,7,57)&&sx_add(c,&sxe38_voc,64,26)&&sx_add(c,&sxe38_sound,90,6)&&sxe38_raw_tail(c,96)&&!c->failed&&!xx_pd_is_stopped(c->pd);
}
static bool sx_edlibsamples_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60b00001U||id>0x60b00003U||r->value.type==XX_VAR_TYPE_FLOAT||r->value.type==XX_VAR_TYPE_DOUBLE)return true;
 const wchar_t*s=NULL;
 if(id==0x60b00001U)s=!xx_rt_wcscmp(r->name,L"signature")?L"JCH signature followed by the author's 26/02/66 birth-date detection bytes.":L"1: sample-data block in original S01 files; JCH's player documentation assigns block type 0 to music data.";
 else if(id==0x60b00002U){if(!xx_rt_wcscmp(r->name,L"signature"))s=L"Creative Voice File signature and 0x1A terminator.";else if(!xx_rt_wcscmp(r->name,L"first_block_offset"))s=L"Byte offset relative to the embedded VOC descriptor at S01 offset 0x40; native S01 files store 0x1A.";else if(!xx_rt_wcscmp(r->name,L"version"))s=L"Creative VOC version: high byte is major, low byte is minor; native S01 stores 0x010A (1.10).";else s=L"Creative VOC version checksum: (~version + 0x1234) modulo 65536.";}
 else{if(!xx_rt_wcscmp(r->name,L"type"))s=L"VOC_SOUND_DATA (1): first Creative VOC sound-data block.";else if(!xx_rt_wcscmp(r->name,L"payload_size"))s=L"Stored little-endian 24-bit sound-block payload byte length, including the time-constant and codec bytes. This does not validate the remaining S01 sample data.";else if(!xx_rt_wcscmp(r->name,L"time_constant"))s=L"Creative VOC time constant; the sample-rate formula is 1000000 / (256 - time_constant) Hz.";else s=L"0: 8-bit unsigned PCM. Encoded sample bytes remain raw.";}
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_edlibsamples_family={sx_edlibsamples_supports,sx_edlibsamples_build,sx_edlibsamples_layouts,SX_COUNT(sx_edlibsamples_layouts),sx_edlibsamples_comment};
