/* SPDX-License-Identifier: MIT
 * Physical Valtonen/ObsM SNG metadata, independently described from the
 * AdPlug CsngPlayer reader. This does not execute OPL writes or play audio. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxop37_sng_header_fields[]={
 SX_FIELD("signature","char[4]",0,4,SX_SIGNATURE),
 SX_FIELD("data_length","uint16",4,2,SX_SIZE),
 SX_FIELD("start_offset","uint16",6,2,SX_OFFSET),
 SX_FIELD("loop_offset","uint16",8,2,SX_OFFSET),
 SX_FIELD("initial_delay","uint8",10,1,SX_NONE),
 SX_FIELD("compressed_delays","uint8",11,1,SX_FLAGS),
};
static const sx_layout sxop37_sng_header={0x60ae0001U,"obsm_sng_header",sxop37_sng_header_fields,SX_COUNT(sxop37_sng_header_fields),SX_LE};
static const xx_data_struct_field_desc sxop37_sng_event_fields[]={
 SX_FIELD("value","uint8",0,1,SX_NONE),
 SX_FIELD("register","uint8",1,1,SX_ID),
};
static const sx_layout sxop37_sng_event={0x60ae0002U,"obsm_sng_event",sxop37_sng_event_fields,SX_COUNT(sxop37_sng_event_fields),SX_LE};
static const sx_layout*const sx_opllogs_layouts[]={&sxop37_sng_header,&sxop37_sng_event};
static bool sx_opllogs_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_VALTONEN_SNGPLAYER;}
static bool sxop37_raw(sx_context*c){uint8_t v;if(!c||c->count||c->failed||c->size<=0||!sx_read(c,0,&v,1)||xx_pd_is_stopped(c->pd))return false;return sx_raw(c,0,c->size);}
static bool sx_opllogs_build(sx_context*c){
 uint8_t h[12];if(!c||!c->f||!sx_opllogs_supports(c->f->file_type))return false;
 if(c->size<12)return sxop37_raw(c);if(!sx_read(c,0,h,sizeof(h)))return false;
 if(xx_rt_memcmp(h,"ObsM",4))return sxop37_raw(c);
 uint16_t length=(uint16_t)((uint16_t)h[4]|((uint16_t)h[5]<<8));
 if(!sx_add(c,&sxop37_sng_header,0,12))return false;
 /* The source consumes length/2 two-byte records. Odd or truncated declared
  * streams retain their stored length word and an entirely raw data region. */
 int64_t end=12;
 if(length&&!(length&1)&&(int64_t)length<=c->size-12){if(!sx_table(c,&sxop37_sng_event,12,2,length/2))return false;end+=length;}
 return sx_raw(c,end,c->size-end)&&!c->failed&&!xx_pd_is_stopped(c->pd);
}
static bool sx_opllogs_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60ae0001U||id>0x60ae0002U)return true;
 const wchar_t*s=NULL;
 if(id==0x60ae0001U){
  if(!xx_rt_wcscmp(r->name,L"signature"))s=L"ObsM identifier used by the original SNG reader.";
  else if(!xx_rt_wcscmp(r->name,L"data_length"))s=L"Stored byte length of the two-byte value/register stream, excluding this 12-byte header.";
  else if(!xx_rt_wcscmp(r->name,L"start_offset")||!xx_rt_wcscmp(r->name,L"loop_offset"))s=L"Byte offset relative to the event stream beginning after the 12-byte header. The player divides the stored word by two; no relocated address is substituted.";
  else if(!xx_rt_wcscmp(r->name,L"initial_delay"))s=L"Initial countdown for compressed-delay playback at the player's 70 Hz update rate; uncompressed playback ignores this initial countdown.";
  else if(!xx_rt_wcscmp(r->name,L"compressed_delays"))s=L"0=UNCOMPRESSED; nonzero=COMPRESSED_DELAYS. Both forms store value/register pairs; this flag changes delay handling.";
 }else if(!xx_rt_wcscmp(r->name,L"register"))s=L"OPL2 register address; zero marks a timing record whose value controls the player countdown.";
 else if(!xx_rt_wcscmp(r->name,L"value"))s=L"Stored OPL register value or countdown byte for a zero-register timing record.";
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_opllogs_family={sx_opllogs_supports,sx_opllogs_build,sx_opllogs_layouts,SX_COUNT(sx_opllogs_layouts),sx_opllogs_comment};
