/* SPDX-License-Identifier: MIT
 * Physical Shroom v2 metadata, independently described from original
 * modshroom and Mariopants save/load functions. No audio is executed. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxmd38_shroom_header_fields[]={
 SX_FIELD("signature","char[4]",0,4,SX_SIGNATURE),
 SX_FIELD("version","uint16",4,2,SX_NONE),
 SX_FIELD("compression","uint8",6,1,SX_FLAGS),
 SX_FIELD("title","char[32]",7,32,SX_STRING),
 SX_FIELD("author","char[32]",39,32,SX_STRING),
 SX_FIELD("preferred_instrument_set","char[32]",71,32,SX_STRING),
};
static const sx_layout sxmd38_shroom_header={0x60b10001U,"shroom_header",sxmd38_shroom_header_fields,SX_COUNT(sxmd38_shroom_header_fields),SX_LE};
static const xx_data_struct_field_desc sxmd38_shroom_note_fields[]={
 SX_FIELD("pitch","uint8",0,1,SX_ID),
 SX_FIELD("instrument","uint8",1,1,SX_ID),
};
static const sx_layout sxmd38_shroom_note={0x60b10002U,"shroom_note",sxmd38_shroom_note_fields,SX_COUNT(sxmd38_shroom_note_fields),SX_LE};
static const xx_data_struct_field_desc sxmd38_shroom_tempo_fields[]={SX_FIELD("tempo","uint8",0,1,SX_NONE)};
static const sx_layout sxmd38_shroom_tempo={0x60b10003U,"shroom_tempo",sxmd38_shroom_tempo_fields,SX_COUNT(sxmd38_shroom_tempo_fields),SX_LE};
static const xx_data_struct_field_desc sxmd38_shroom_extension_fields[]={
 SX_FIELD("song_length","uint8",0,1,SX_COUNT_PROP),
 SX_FIELD("loop_enabled","uint8",1,1,SX_FLAGS),
 SX_FIELD("metre","uint8",2,1,SX_NONE),
};
static const sx_layout sxmd38_shroom_extension={0x60b10004U,"shroom_extension",sxmd38_shroom_extension_fields,SX_COUNT(sxmd38_shroom_extension_fields),SX_LE};
static const sx_layout*const sx_music38modern_layouts[]={&sxmd38_shroom_header,&sxmd38_shroom_note,&sxmd38_shroom_tempo,&sxmd38_shroom_extension};
static bool sx_music38modern_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_MARIOPAINT_SHROOM;}
static bool sxmd38_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p;}
static bool sxmd38_raw(sx_context*c){uint8_t b;if(!c||c->count||c->failed||c->size<=0||xx_pd_is_stopped(c->pd)||!sx_read(c,0,&b,1))return false;return sx_raw(c,0,c->size);}
static bool sxmd38_tail(sx_context*c,uint64_t p){uint8_t b;if(!c||c->failed||!sxmd38_span(c,p,0)||xx_pd_is_stopped(c->pd))return false;if(p==(uint64_t)c->size)return c->count>0;if(!sx_read(c,(int64_t)p,&b,1))return false;return sx_raw(c,(int64_t)p,c->size-(int64_t)p);}
static bool sx_music38modern_build(sx_context*c){
 uint8_t h[103],tail[3];if(!c||!c->f||!sx_music38modern_supports(c->f->file_type))return false;
 if(!sxmd38_span(c,0,sizeof(h)))return sxmd38_raw(c);if(!sx_read(c,0,h,sizeof(h)))return false;
 if(xx_rt_memcmp(h,"shro\2\0\0",7))return sxmd38_raw(c);
 if(!sx_add(c,&sxmd38_shroom_header,0,103))return false;uint64_t end=103;
 /* Original v2 writers save96 rows with3 pitch/instrument pairs per row,
  * regardless of the optional logical song length. Complete bounded metadata
  * is displayed on short native-valid inputs, and the remainder stays raw. */
 if(sxmd38_span(c,103,576)){if(!sx_table(c,&sxmd38_shroom_note,103,2,288))return false;end=679;}
 if(end==679&&sxmd38_span(c,679,1)){if(!sx_read(c,679,tail,1)||!sx_add(c,&sxmd38_shroom_tempo,679,1))return false;end=680;}
 if(end==680&&sxmd38_span(c,680,3)){if(!sx_read(c,680,tail,3)||!sx_add(c,&sxmd38_shroom_extension,680,3))return false;end=683;}
 return sxmd38_tail(c,end)&&!c->failed&&!xx_pd_is_stopped(c->pd);
}
static bool sx_music38modern_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60b10001U||id>0x60b10004U||r->value.type==XX_VAR_TYPE_FLOAT||r->value.type==XX_VAR_TYPE_DOUBLE)return true;const wchar_t*s=NULL;
 if(id==0x60b10001U){if(!xx_rt_wcscmp(r->name,L"signature"))s=L"shro physical file signature.";else if(!xx_rt_wcscmp(r->name,L"version"))s=L"Little-endian version2. The native format detector currently recognizes this version; Mariopants version3 is outside this provider.";else if(!xx_rt_wcscmp(r->name,L"compression"))s=L"0=UNCOMPRESSED. The Mariopants loader rejects nonzero compression modes.";else if(!xx_rt_wcscmp(r->name,L"preferred_instrument_set"))s=L"Fixed32-byte preferred SHI instrument-set name. Mariopants writes default.shi and its loader ignores this preference.";else s=L"Fixed32-byte stored metadata buffer, preserved without modifying the original text bytes.";}
 else if(id==0x60b10002U){if(!xx_rt_wcscmp(r->name,L"pitch"))s=xx_var_get_u64(&r->value)==255?L"0xFF=EMPTY_NOTE.":L"Original staff pitch range is1..13;0xFF is empty. Other stored values are preserved. Three consecutive note records form one song row.";else s=xx_var_get_u64(&r->value)==223?L"0xDF=EMPTY_INSTRUMENT.":L"Original instrument-bank range is0..14;0xDF is empty. Other stored values are preserved without imposing a different sample bank.";}
 else if(id==0x60b10003U)s=L"Original Mario Paint tempo selector is normally0..159; other stored values remain visible without an inferred BPM conversion.";
 else if(!xx_rt_wcscmp(r->name,L"song_length"))s=L"Logical song length in rows (normally1..96). The physical v2 file always stores all96 rows.";
 else if(!xx_rt_wcscmp(r->name,L"loop_enabled"))s=L"0=NO_LOOP; nonzero=LOOP_ENABLED in the original loader.";
 else s=L"0=THREE_BEAT_METRE; nonzero=FOUR_BEAT_METRE in the original loader.";
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_music38modern_family={sx_music38modern_supports,sx_music38modern_build,sx_music38modern_layouts,SX_COUNT(sx_music38modern_layouts),sx_music38modern_comment};
