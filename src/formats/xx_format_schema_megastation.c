/* Original MegaStation/MSMIDI Delphi serializers; field evidence retained with family report. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxmega_envelope_fields[]={
 SX_FIELD("signature","uint8[4]",0,4,SX_SIGNATURE),
 SX_FIELD("file_version","char[4]",4,4,SX_STRING),
 SX_FIELD("storage_mode","char[4]",8,4,SX_STRING),
};
static const sx_layout sxmega_envelope={UINT32_C(0x60a20001),"megastation_envelope",sxmega_envelope_fields,SX_COUNT(sxmega_envelope_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_ms_info_fields[]={
 SX_FIELD("tempo","uint16",0,2,SX_NONE),
 SX_FIELD("ticks_per_beat","uint8",2,1,SX_COUNT_PROP),
 SX_FIELD("new_note_action","uint8",3,1,SX_ID),
 SX_FIELD("song_volume","uint8",4,1,SX_NONE),
};
static const sx_layout sxmega_ms_info={UINT32_C(0x60a20002),"megastation_ms_info",sxmega_ms_info_fields,SX_COUNT(sxmega_ms_info_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_midi_info_fields[]={
 SX_FIELD("tempo","uint16",0,2,SX_NONE),
 SX_FIELD("ticks_per_beat","uint8",2,1,SX_COUNT_PROP),
 SX_FIELD("new_note_action","uint8",3,1,SX_ID),
};
static const sx_layout sxmega_midi_info={UINT32_C(0x60a20003),"megastation_midi_info",sxmega_midi_info_fields,SX_COUNT(sxmega_midi_info_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_string_length_fields[]={
 SX_FIELD("byte_length","int32",0,4,SX_SIZE),
};
static const sx_layout sxmega_string_length={UINT32_C(0x60a20004),"megastation_string_length",sxmega_string_length_fields,SX_COUNT(sxmega_string_length_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_title_fields[]={
 SX_FIELD("title","char",0,0,SX_STRING),
};
static const sx_layout sxmega_title={UINT32_C(0x60a20005),"megastation_title",sxmega_title_fields,SX_COUNT(sxmega_title_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_author_fields[]={
 SX_FIELD("author","char",0,0,SX_STRING),
};
static const sx_layout sxmega_author={UINT32_C(0x60a20006),"megastation_author",sxmega_author_fields,SX_COUNT(sxmega_author_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_info_fields[]={
 SX_FIELD("info","char",0,0,SX_STRING),
};
static const sx_layout sxmega_info={UINT32_C(0x60a20007),"megastation_info",sxmega_info_fields,SX_COUNT(sxmega_info_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_sample_name_fields[]={
 SX_FIELD("sample_name","char",0,0,SX_STRING),
};
static const sx_layout sxmega_sample_name={UINT32_C(0x60a20008),"megastation_sample_name",sxmega_sample_name_fields,SX_COUNT(sxmega_sample_name_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_instrument_name_fields[]={
 SX_FIELD("instrument_name","char",0,0,SX_STRING),
};
static const sx_layout sxmega_instrument_name={UINT32_C(0x60a20009),"megastation_instrument_name",sxmega_instrument_name_fields,SX_COUNT(sxmega_instrument_name_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_pattern_name_fields[]={
 SX_FIELD("pattern_name","char",0,0,SX_STRING),
};
static const sx_layout sxmega_pattern_name={UINT32_C(0x60a2000a),"megastation_pattern_name",sxmega_pattern_name_fields,SX_COUNT(sxmega_pattern_name_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_soundfont_name_fields[]={
 SX_FIELD("soundfont_name","char",0,0,SX_STRING),
};
static const sx_layout sxmega_soundfont_name={UINT32_C(0x60a2000b),"megastation_soundfont_name",sxmega_soundfont_name_fields,SX_COUNT(sxmega_soundfont_name_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_sample_count_fields[]={
 SX_FIELD("sample_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxmega_sample_count={UINT32_C(0x60a2000c),"megastation_sample_count",sxmega_sample_count_fields,SX_COUNT(sxmega_sample_count_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_instrument_count_fields[]={
 SX_FIELD("instrument_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxmega_instrument_count={UINT32_C(0x60a2000d),"megastation_instrument_count",sxmega_instrument_count_fields,SX_COUNT(sxmega_instrument_count_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_pattern_count_fields[]={
 SX_FIELD("pattern_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxmega_pattern_count={UINT32_C(0x60a2000e),"megastation_pattern_count",sxmega_pattern_count_fields,SX_COUNT(sxmega_pattern_count_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_sequence_track_count_fields[]={
 SX_FIELD("sequence_track_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxmega_sequence_track_count={UINT32_C(0x60a2000f),"megastation_sequence_track_count",sxmega_sequence_track_count_fields,SX_COUNT(sxmega_sequence_track_count_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_track_entry_count_fields[]={
 SX_FIELD("track_entry_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxmega_track_entry_count={UINT32_C(0x60a20010),"megastation_track_entry_count",sxmega_track_entry_count_fields,SX_COUNT(sxmega_track_entry_count_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_soundfont_count_fields[]={
 SX_FIELD("soundfont_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxmega_soundfont_count={UINT32_C(0x60a20011),"megastation_soundfont_count",sxmega_soundfont_count_fields,SX_COUNT(sxmega_soundfont_count_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_sample_format_fields[]={
 SX_FIELD("bits_per_sample","uint8",0,1,SX_NONE),
 SX_FIELD("channel_count","uint8",1,1,SX_COUNT_PROP),
 SX_FIELD("sample_rate","int32",2,4,SX_NONE),
 SX_FIELD("frame_count","int32",6,4,SX_COUNT_PROP),
};
static const sx_layout sxmega_sample_format={UINT32_C(0x60a20012),"megastation_sample_format",sxmega_sample_format_fields,SX_COUNT(sxmega_sample_format_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_sample_loop_fields[]={
 SX_FIELD("loop_type","uint8",0,1,SX_ID),
 SX_FIELD("loop_start_frame","int32",1,4,SX_NONE),
 SX_FIELD("loop_end_frame","int32",5,4,SX_NONE),
 SX_FIELD("default_volume","uint8",9,1,SX_NONE),
 SX_FIELD("default_pan","int8",10,1,SX_NONE),
};
static const sx_layout sxmega_sample_loop={UINT32_C(0x60a20013),"megastation_sample_loop",sxmega_sample_loop_fields,SX_COUNT(sxmega_sample_loop_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_midi_instrument_fields[]={
 SX_FIELD("channel","uint8",0,1,SX_NONE),
 SX_FIELD("bank","uint8",1,1,SX_NONE),
 SX_FIELD("patch","uint8",2,1,SX_NONE),
 SX_FIELD("default_velocity","uint8",3,1,SX_NONE),
};
static const sx_layout sxmega_midi_instrument={UINT32_C(0x60a20014),"megastation_midi_instrument",sxmega_midi_instrument_fields,SX_COUNT(sxmega_midi_instrument_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_pattern_dimensions_fields[]={
 SX_FIELD("channel_count","int32",0,4,SX_COUNT_PROP),
 SX_FIELD("row_count","int32",4,4,SX_COUNT_PROP),
};
static const sx_layout sxmega_pattern_dimensions={UINT32_C(0x60a20015),"megastation_pattern_dimensions",sxmega_pattern_dimensions_fields,SX_COUNT(sxmega_pattern_dimensions_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_ms_cell_fields[]={
 SX_FIELD("note","uint8",0,1,SX_ID),
 SX_FIELD("instrument","int32",1,4,SX_ID),
 SX_FIELD("volume","uint8",5,1,SX_NONE),
 SX_FIELD("pan","int8",6,1,SX_NONE),
};
static const sx_layout sxmega_ms_cell={UINT32_C(0x60a20016),"megastation_ms_cell",sxmega_ms_cell_fields,SX_COUNT(sxmega_ms_cell_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_midi_cell_fields[]={
 SX_FIELD("note","uint8",0,1,SX_ID),
 SX_FIELD("instrument","int32",1,4,SX_ID),
 SX_FIELD("velocity","uint8",5,1,SX_NONE),
};
static const sx_layout sxmega_midi_cell={UINT32_C(0x60a20017),"megastation_midi_cell",sxmega_midi_cell_fields,SX_COUNT(sxmega_midi_cell_fields),SX_LE};
static const xx_data_struct_field_desc sxmega_sequence_entry_fields[]={
 SX_FIELD("pattern_index","int32",0,4,SX_ID),
};
static const sx_layout sxmega_sequence_entry={UINT32_C(0x60a20018),"megastation_sequence_entry",sxmega_sequence_entry_fields,SX_COUNT(sxmega_sequence_entry_fields),SX_LE};
static const sx_layout *const sx_megastation_layouts[]={&sxmega_envelope,&sxmega_ms_info,&sxmega_midi_info,&sxmega_string_length,&sxmega_title,&sxmega_author,&sxmega_info,&sxmega_sample_name,&sxmega_instrument_name,&sxmega_pattern_name,&sxmega_soundfont_name,&sxmega_sample_count,&sxmega_instrument_count,&sxmega_pattern_count,&sxmega_sequence_track_count,&sxmega_track_entry_count,&sxmega_soundfont_count,&sxmega_sample_format,&sxmega_sample_loop,&sxmega_midi_instrument,&sxmega_pattern_dimensions,&sxmega_ms_cell,&sxmega_midi_cell,&sxmega_sequence_entry};
static bool sx_megastation_comment(xx_data_struct_record*r,uint32_t id){if(!r||!r->name)return true;const wchar_t*text=NULL;switch(id){
 case 0x60a20001:
  if(!xx_rt_wcscmp(r->name,L"signature"))text=L"Original producer signature: SONG or SNGM.";
  if(!xx_rt_wcscmp(r->name,L"file_version"))text=L"Exact supported original serialization version: MS 0.12 or MSM 0.10; application release versions differ.";
  if(!xx_rt_wcscmp(r->name,L"storage_mode"))text=L"NORM stores following records directly; COMP stores one zlib stream. Compressed body is raw and no decoded coordinates are exposed.";
 break;
 case 0x60a20002:
  if(!xx_rt_wcscmp(r->name,L"tempo"))text=L"Beats per minute, stored Word.";
  if(!xx_rt_wcscmp(r->name,L"ticks_per_beat"))text=L"Ticks per beat, stored Byte.";
  if(!xx_rt_wcscmp(r->name,L"new_note_action"))text=L"Original TNNA enum stored as one byte (compiler Z=1): 0 none; 1 channel cut; 2 channel off.";
  if(!xx_rt_wcscmp(r->name,L"song_volume"))text=L"Stored song volume. Original setter accepts 0..100; byte preserved without clamping.";
 break;
 case 0x60a20003:
  if(!xx_rt_wcscmp(r->name,L"tempo"))text=L"Beats per minute, stored Word.";
  if(!xx_rt_wcscmp(r->name,L"ticks_per_beat"))text=L"Ticks per beat, stored Byte.";
  if(!xx_rt_wcscmp(r->name,L"new_note_action"))text=L"Original TNNA enum stored as one byte (compiler Z=1): 0 none; 1 channel off.";
 break;
 case 0x60a20004:
  if(!xx_rt_wcscmp(r->name,L"byte_length"))text=L"Signed Delphi Integer byte length of the following ANSI string; no terminator is serialized. Negative or unbounded strings end the bounded metadata prefix.";
 break;
 case 0x60a20005:
  if(!xx_rt_wcscmp(r->name,L"title"))text=L"Original ANSI string bytes; no encoding conversion. Display ends at embedded NUL but raw value preserves the full physical span. Strings exceeding 4096 bytes stay raw.";
 break;
 case 0x60a20006:
  if(!xx_rt_wcscmp(r->name,L"author"))text=L"Original ANSI string bytes; no encoding conversion. Display ends at embedded NUL but raw value preserves the full physical span. Strings exceeding 4096 bytes stay raw.";
 break;
 case 0x60a20007:
  if(!xx_rt_wcscmp(r->name,L"info"))text=L"Original ANSI string bytes; no encoding conversion. Display ends at embedded NUL but raw value preserves the full physical span. Strings exceeding 4096 bytes stay raw.";
 break;
 case 0x60a20008:
  if(!xx_rt_wcscmp(r->name,L"sample_name"))text=L"Original ANSI string bytes; no encoding conversion. Display ends at embedded NUL but raw value preserves the full physical span. Strings exceeding 4096 bytes stay raw.";
 break;
 case 0x60a20009:
  if(!xx_rt_wcscmp(r->name,L"instrument_name"))text=L"Original ANSI string bytes; no encoding conversion. Display ends at embedded NUL but raw value preserves the full physical span. Strings exceeding 4096 bytes stay raw.";
 break;
 case 0x60a2000a:
  if(!xx_rt_wcscmp(r->name,L"pattern_name"))text=L"Original ANSI string bytes; no encoding conversion. Display ends at embedded NUL but raw value preserves the full physical span. Strings exceeding 4096 bytes stay raw.";
 break;
 case 0x60a2000b:
  if(!xx_rt_wcscmp(r->name,L"soundfont_name"))text=L"Original ANSI string bytes; no encoding conversion. Display ends at embedded NUL but raw value preserves the full physical span. Strings exceeding 4096 bytes stay raw.";
 break;
 case 0x60a2000c:
  if(!xx_rt_wcscmp(r->name,L"sample_count"))text=L"Signed Delphi Integer count of sample records. Negative counts retain this word and leave the following tail raw.";
 break;
 case 0x60a2000d:
  if(!xx_rt_wcscmp(r->name,L"instrument_count"))text=L"Signed Delphi Integer count of MIDI instrument records. Negative counts retain this word and leave the following tail raw.";
 break;
 case 0x60a2000e:
  if(!xx_rt_wcscmp(r->name,L"pattern_count"))text=L"Signed Delphi Integer count of patterns. Negative counts retain this word and leave the following tail raw.";
 break;
 case 0x60a2000f:
  if(!xx_rt_wcscmp(r->name,L"sequence_track_count"))text=L"Signed Delphi Integer count of sequence tracks. Negative counts retain this word and leave the following tail raw.";
 break;
 case 0x60a20010:
  if(!xx_rt_wcscmp(r->name,L"track_entry_count"))text=L"Signed Delphi Integer count of signed pattern indices in this track. Negative counts retain this word and leave the following tail raw.";
 break;
 case 0x60a20011:
  if(!xx_rt_wcscmp(r->name,L"soundfont_count"))text=L"Signed Delphi Integer count of soundfont filename strings. Negative counts retain this word and leave the following tail raw.";
 break;
 case 0x60a20012:
  if(!xx_rt_wcscmp(r->name,L"bits_per_sample"))text=L"Stored sample precision: original reader accepts 8 or 16 bits.";
  if(!xx_rt_wcscmp(r->name,L"channel_count"))text=L"Stored PCM channels: original reader accepts 1 or 2.";
  if(!xx_rt_wcscmp(r->name,L"sample_rate"))text=L"Signed Delphi Integer sample rate in Hz.";
  if(!xx_rt_wcscmp(r->name,L"frame_count"))text=L"Sample frames; physical PCM byte count is frame_count * channels * (bits / 8). PCM payload remains raw.";
 break;
 case 0x60a20013:
  if(!xx_rt_wcscmp(r->name,L"loop_type"))text=L"0 none; 1 normal; 2 bidirectional. Stored enum byte.";
  if(!xx_rt_wcscmp(r->name,L"loop_start_frame"))text=L"One-based sample-frame position used by the original player; not a byte offset.";
  if(!xx_rt_wcscmp(r->name,L"loop_end_frame"))text=L"Sample-frame loop endpoint used by the original player; not a byte offset.";
  if(!xx_rt_wcscmp(r->name,L"default_volume"))text=L"Stored default sample volume.";
  if(!xx_rt_wcscmp(r->name,L"default_pan"))text=L"Signed default pan; original setter clamps -100..100, stored byte is preserved.";
 break;
 case 0x60a20014:
  if(!xx_rt_wcscmp(r->name,L"channel"))text=L"Original MIDI channel selection, 1..16.";
  if(!xx_rt_wcscmp(r->name,L"bank"))text=L"Stored MIDI bank number.";
  if(!xx_rt_wcscmp(r->name,L"patch"))text=L"Stored MIDI patch number.";
  if(!xx_rt_wcscmp(r->name,L"default_velocity"))text=L"Default note velocity.";
 break;
 case 0x60a20015:
  if(!xx_rt_wcscmp(r->name,L"channel_count"))text=L"Signed number of pattern channels.";
  if(!xx_rt_wcscmp(r->name,L"row_count"))text=L"Signed rows in each channel. Cells are serialized channel-major, then row-major within that channel.";
 break;
 case 0x60a20016:
  if(!xx_rt_wcscmp(r->name,L"note"))text=L"Stored note code; 255 means no note.";
  if(!xx_rt_wcscmp(r->name,L"instrument"))text=L"Signed one-based instrument/sample number; invalid selections remain their original stored value.";
  if(!xx_rt_wcscmp(r->name,L"volume"))text=L"Stored volume; 255 uses the sample default.";
  if(!xx_rt_wcscmp(r->name,L"pan"))text=L"Signed pan; 127 uses the sample default.";
 break;
 case 0x60a20017:
  if(!xx_rt_wcscmp(r->name,L"note"))text=L"Stored note code; 255 means no note.";
  if(!xx_rt_wcscmp(r->name,L"instrument"))text=L"Signed one-based instrument/sample number; invalid selections remain their original stored value.";
  if(!xx_rt_wcscmp(r->name,L"velocity"))text=L"Stored note velocity; 255 uses default.";
 break;
 case 0x60a20018:
  if(!xx_rt_wcscmp(r->name,L"pattern_index"))text=L"Signed one-based pattern selection; original player skips values outside 1..pattern_count.";
 break;
 default:break;}return !text||xx_data_struct_record_set_comment(r,text);}
/* Bounded original Delphi serialization. Parsing never decompresses COMP.
 * A bounded optional prefix is retained when a later record is malformed.
 * Physical I/O failure or cancellation fails enumeration rather than hiding it. */
typedef struct sxmega_view {sx_context*c;uint64_t at,end,entries;unsigned work;} sxmega_view;
static bool sxmega_span(sxmega_view*v,uint64_t n){return !xx_pd_is_stopped(v->c->pd)&&v->at<=v->end&&n<=v->end-v->at;}
static bool sxmega_get(sxmega_view*v,unsigned n,uint64_t*x){uint8_t b[4];if(++v->work>100000||n>4||!n||!sxmega_span(v,n)||!sx_read(v->c,(int64_t)v->at,b,n))return false;*x=0;for(unsigned i=0;i<n;i++)*x|=(uint64_t)b[i]<<(8*i);return true;}
static bool sxmega_row(sxmega_view*v,const sx_layout*l,uint64_t n){if(v->entries>=24000||!n||!sxmega_span(v,n))return false;if(!sx_add(v->c,l,(int64_t)v->at,(int64_t)n))return false;v->entries++;v->at+=n;return true;}
static bool sxmega_word(sxmega_view*v,const sx_layout*l,uint64_t*x){return sxmega_get(v,4,x)&&sxmega_row(v,l,4)&&*x<=INT32_MAX;}
static bool sxmega_string(sxmega_view*v,const sx_layout*l){uint64_t n;if(!sxmega_word(v,&sxmega_string_length,&n)||!sxmega_span(v,n))return false;if(!n)return true;if(n<=4096)return sxmega_row(v,l,n);v->at+=n;return true;}
static bool sxmega_table(sxmega_view*v,const sx_layout*l,unsigned width,uint64_t count){if(!width||count>(v->end-v->at)/width)return false;if(!count)return true;uint64_t shown=count;if(shown>24000-v->entries)shown=24000-v->entries;if(!shown)return false;if(!sx_table(v->c,l,(int64_t)v->at,width,shown))return false;v->at+=shown*width;v->entries+=shown;return shown==count;}
static void sxmega_body(sxmega_view*v,bool midi){uint64_t n,x,y,z;
 if(!sxmega_row(v,midi?&sxmega_midi_info:&sxmega_ms_info,midi?4:5)||!sxmega_string(v,&sxmega_title)||!sxmega_string(v,&sxmega_author)||!sxmega_string(v,&sxmega_info))return;
 if(!sxmega_word(v,midi?&sxmega_instrument_count:&sxmega_sample_count,&n))return;
 for(uint64_t i=0;i<n;i++){
  if(!sxmega_string(v,midi?&sxmega_instrument_name:&sxmega_sample_name))return;
  if(midi){if(!sxmega_row(v,&sxmega_midi_instrument,4))return;}
  else{uint8_t b[10];if(!sxmega_span(v,10)||!sx_read(v->c,(int64_t)v->at,b,10)||!sxmega_row(v,&sxmega_sample_format,10))return;
   x=xx_data_get_u32(b,10,6,false);y=xx_data_get_u32(b,10,2,false);
   if((b[0]!=8&&b[0]!=16)||(b[1]!=1&&b[1]!=2)||x>INT32_MAX||!y||y>INT32_MAX)return;
   z=x*(b[0]/8)*b[1];if(!sxmega_span(v,z))return;v->at+=z;if(!sxmega_row(v,&sxmega_sample_loop,11))return;
  }
 }
 if(!sxmega_word(v,&sxmega_pattern_count,&n))return;
 for(uint64_t i=0;i<n;i++){
  uint8_t b[8];if(!sxmega_string(v,&sxmega_pattern_name)||!sxmega_span(v,8)||!sx_read(v->c,(int64_t)v->at,b,8)||!sxmega_row(v,&sxmega_pattern_dimensions,8))return;
  x=xx_data_get_u32(b,8,0,false);y=xx_data_get_u32(b,8,4,false);if(!x||x>INT32_MAX||y>INT32_MAX)return;
  if(!sxmega_table(v,midi?&sxmega_midi_cell:&sxmega_ms_cell,midi?6:7,x*y))return;
 }
 if(!sxmega_word(v,&sxmega_sequence_track_count,&n))return;
 for(uint64_t i=0;i<n;i++)if(!sxmega_word(v,&sxmega_track_entry_count,&x)||!sxmega_table(v,&sxmega_sequence_entry,4,x))return;
 if(midi){if(!sxmega_word(v,&sxmega_soundfont_count,&n))return;for(uint64_t i=0;i<n;i++)if(!sxmega_string(v,&sxmega_soundfont_name))return;}
}
static int sxmega_compare(const void*aa,const void*bb){const xx_data_struct*a=aa,*b=bb;return a->offset<b->offset?-1:a->offset>b->offset?1:0;}
static bool sxmega_finish(sx_context*c){size_t n=c->count;int64_t end=0;for(size_t i=0;i<n;i++){int64_t p=c->items[i].offset-c->f->base_address,z=c->items[i].total_size;if(p<end)return false;if(p>end&&!sx_raw(c,end,p-end))return false;end=p+z;}if(end<c->size&&!sx_raw(c,end,c->size-end))return false;xx_rt_qsort(c->items,c->count,sizeof(*c->items),sxmega_compare);return !c->failed&&!xx_pd_is_stopped(c->pd);}
static bool sx_megastation_supports(xx_file_type_t t){switch(t){case XX_FILE_TYPE_DIE_MUSIC_MEGASTATION_MS:case XX_FILE_TYPE_DIE_MUSIC_MEGASTATION_MIDI:return true;default:return false;}}
static bool sx_megastation_build(sx_context*c){if(!c||!c->f||c->size<=0||c->f->base_address<0||c->f->base_address>INT64_MAX-c->size||(!c->f->base_info_handled&&!xx_format_handle_base_info(c->f,c->pd))||!c->f->is_valid||xx_pd_is_stopped(c->pd))return false;
 uint8_t b[12];bool midi=c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_MEGASTATION_MIDI;sxmega_view v={c,0,(uint64_t)c->size,0,0};
 if(c->size>=12){if(!sx_read(c,0,b,12))return false;
  if(!xx_rt_memcmp(b,midi?"SNGM0.10":"SONG0.12",8)&&(!xx_rt_memcmp(b+8,"NORM",4)||!xx_rt_memcmp(b+8,"COMP",4))){if(!sxmega_row(&v,&sxmega_envelope,12))return false;if(!xx_rt_memcmp(b+8,"NORM",4))sxmega_body(&v,midi);}
 }
 if(c->failed||xx_pd_is_stopped(c->pd))return false;return c->count?sxmega_finish(c):sx_raw(c,0,c->size);
}
const sx_family sx_megastation_family={sx_megastation_supports,sx_megastation_build,sx_megastation_layouts,SX_COUNT(sx_megastation_layouts),sx_megastation_comment};
