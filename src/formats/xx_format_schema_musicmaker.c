/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Physical music header and table metadata; compressed and unknown bodies stay raw. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxmmkr_musicmaker_form_fields[]={
 SX_FIELD("signature","bytes",0,4,SX_SIGNATURE),
 SX_FIELD("form_bytes","uint32",4,4,SX_SIZE),
 SX_FIELD("form_type","bytes",8,4,SX_ID),
};
static const sx_layout sxmmkr_musicmaker_form={UINT32_C(0x60A40001),"musicmaker_form",sxmmkr_musicmaker_form_fields,SX_COUNT(sxmmkr_musicmaker_form_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_chunk_fields[]={
 SX_FIELD("chunk_id","char[4]",0,4,SX_ID),
 SX_FIELD("payload_bytes","uint32",4,4,SX_SIZE),
};
static const sx_layout sxmmkr_musicmaker_chunk={UINT32_C(0x60A40002),"musicmaker_chunk",sxmmkr_musicmaker_chunk_fields,SX_COUNT(sxmmkr_musicmaker_chunk_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_sdata_length_fields[]={
 SX_FIELD("song_bytes","uint32",0,4,SX_SIZE),
};
static const sx_layout sxmmkr_musicmaker_sdata_length={UINT32_C(0x60A40003),"musicmaker_sdata_length",sxmmkr_musicmaker_sdata_length_fields,SX_COUNT(sxmmkr_musicmaker_sdata_length_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_standard_song_fields[]={
 SX_FIELD("song_marker","bytes",0,2,SX_SIGNATURE),
 SX_FIELD("title","char[20]",2,20,SX_STRING),
 SX_FIELD("format_control","uint8",22,1,SX_NONE),
 SX_FIELD("pattern_length","uint8",23,1,SX_COUNT_PROP),
 SX_FIELD("speed","uint16",24,2,SX_NONE),
};
static const sx_layout sxmmkr_musicmaker_standard_song={UINT32_C(0x60A40004),"musicmaker_standard_song",sxmmkr_musicmaker_standard_song_fields,SX_COUNT(sxmmkr_musicmaker_standard_song_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_extended_song_fields[]={
 SX_FIELD("song_marker","bytes",0,2,SX_SIGNATURE),
 SX_FIELD("title","char[20]",2,20,SX_STRING),
 SX_FIELD("extended_marker","uint8",22,1,SX_ID),
 SX_FIELD("enabled_channels","uint8",23,1,SX_FLAGS),
 SX_FIELD("mixer_type","uint8",24,1,SX_ID),
 SX_FIELD("highest_note_control","uint8",25,1,SX_NONE),
};
static const sx_layout sxmmkr_musicmaker_extended_song={UINT32_C(0x60A40005),"musicmaker_extended_song",sxmmkr_musicmaker_extended_song_fields,SX_COUNT(sxmmkr_musicmaker_extended_song_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_standard_extension_fields[]={
 SX_FIELD("extension_bytes","uint16",0,2,SX_SIZE),
};
static const sx_layout sxmmkr_musicmaker_standard_extension={UINT32_C(0x60A40006),"musicmaker_standard_extension",sxmmkr_musicmaker_standard_extension_fields,SX_COUNT(sxmmkr_musicmaker_standard_extension_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_extended_extension_fields[]={
 SX_FIELD("extension_marker","uint8",0,1,SX_ID),
 SX_FIELD("opaque_control","bytes",1,1,SX_NONE),
 SX_FIELD("extension_bytes","uint16",2,2,SX_SIZE),
};
static const sx_layout sxmmkr_musicmaker_extended_extension={UINT32_C(0x60A40007),"musicmaker_extended_extension",sxmmkr_musicmaker_extended_extension_fields,SX_COUNT(sxmmkr_musicmaker_extended_extension_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_author_fields[]={
 SX_FIELD("author","char[32]",0,32,SX_STRING),
};
static const sx_layout sxmmkr_musicmaker_author={UINT32_C(0x60A40008),"musicmaker_author",sxmmkr_musicmaker_author_fields,SX_COUNT(sxmmkr_musicmaker_author_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_pattern_speed_fields[]={
 SX_FIELD("pattern_length","uint16",0,2,SX_COUNT_PROP),
 SX_FIELD("speed","uint16",2,2,SX_NONE),
};
static const sx_layout sxmmkr_musicmaker_pattern_speed={UINT32_C(0x60A40009),"musicmaker_pattern_speed",sxmmkr_musicmaker_pattern_speed_fields,SX_COUNT(sxmmkr_musicmaker_pattern_speed_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_order_controls_fields[]={
 SX_FIELD("start_order_index","uint16",0,2,SX_NONE),
 SX_FIELD("loop_order_index","uint16",2,2,SX_NONE),
};
static const sx_layout sxmmkr_musicmaker_order_controls={UINT32_C(0x60A4000A),"musicmaker_order_controls",sxmmkr_musicmaker_order_controls_fields,SX_COUNT(sxmmkr_musicmaker_order_controls_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_order_fields[]={
 SX_FIELD("macro_index_or_end","uint16",0,2,SX_ID),
};
static const sx_layout sxmmkr_musicmaker_order={UINT32_C(0x60A4000B),"musicmaker_order",sxmmkr_musicmaker_order_fields,SX_COUNT(sxmmkr_musicmaker_order_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_instrument_header_fields[]={
 SX_FIELD("signature","bytes",0,6,SX_SIGNATURE),
 SX_FIELD("instrument_count","uint16",6,2,SX_COUNT_PROP),
};
static const sx_layout sxmmkr_musicmaker_instrument_header={UINT32_C(0x60A4000C),"musicmaker_instrument_header",sxmmkr_musicmaker_instrument_header_fields,SX_COUNT(sxmmkr_musicmaker_instrument_header_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_instrument_fields[]={
 SX_FIELD("sample_bytes","uint16",0,2,SX_SIZE),
 SX_FIELD("initial_bytes","uint16",2,2,SX_SIZE),
 SX_FIELD("loop_start","uint16",4,2,SX_OFFSET),
 SX_FIELD("repeat_words","uint16",6,2,SX_NONE),
};
static const sx_layout sxmmkr_musicmaker_instrument={UINT32_C(0x60A4000D),"musicmaker_instrument",sxmmkr_musicmaker_instrument_fields,SX_COUNT(sxmmkr_musicmaker_instrument_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_sample_state_fields[]={
 SX_FIELD("sample_processing_state","int32",0,4,SX_NONE),
};
static const sx_layout sxmmkr_musicmaker_sample_state={UINT32_C(0x60A4000E),"musicmaker_sample_state",sxmmkr_musicmaker_sample_state_fields,SX_COUNT(sxmmkr_musicmaker_sample_state_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_packing_fields[]={
 SX_FIELD("delta_bits","uint16",0,2,SX_NONE),
};
static const sx_layout sxmmkr_musicmaker_packing={UINT32_C(0x60A4000F),"musicmaker_packing",sxmmkr_musicmaker_packing_fields,SX_COUNT(sxmmkr_musicmaker_packing_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_delta_fields[]={
 SX_FIELD("delta","int8",0,1,SX_NONE),
};
static const sx_layout sxmmkr_musicmaker_delta={UINT32_C(0x60A40010),"musicmaker_delta",sxmmkr_musicmaker_delta_fields,SX_COUNT(sxmmkr_musicmaker_delta_fields),SX_BE};
static const xx_data_struct_field_desc sxmmkr_musicmaker_lfo_length_fields[]={
 SX_FIELD("lfo_bytes","uint16",0,2,SX_SIZE),
};
static const sx_layout sxmmkr_musicmaker_lfo_length={UINT32_C(0x60A40011),"musicmaker_lfo_length",sxmmkr_musicmaker_lfo_length_fields,SX_COUNT(sxmmkr_musicmaker_lfo_length_fields),SX_BE};
static const sx_layout *const sx_musicmaker_layouts[]={
 &sxmmkr_musicmaker_form,
 &sxmmkr_musicmaker_chunk,
 &sxmmkr_musicmaker_sdata_length,
 &sxmmkr_musicmaker_standard_song,
 &sxmmkr_musicmaker_extended_song,
 &sxmmkr_musicmaker_standard_extension,
 &sxmmkr_musicmaker_extended_extension,
 &sxmmkr_musicmaker_author,
 &sxmmkr_musicmaker_pattern_speed,
 &sxmmkr_musicmaker_order_controls,
 &sxmmkr_musicmaker_order,
 &sxmmkr_musicmaker_instrument_header,
 &sxmmkr_musicmaker_instrument,
 &sxmmkr_musicmaker_sample_state,
 &sxmmkr_musicmaker_packing,
 &sxmmkr_musicmaker_delta,
 &sxmmkr_musicmaker_lfo_length,
};
/* Physical original MusicMaker serialization. Bounded prefixes survive optional
 * damaged tails; no unpacked samples or runtime addresses become file fields. */
typedef struct sxmmkr_view {sx_context*c;uint64_t end,shown;unsigned work;} sxmmkr_view;
static bool sxmmkr_span(sxmmkr_view*v,uint64_t at,uint64_t n,uint64_t end){return !xx_pd_is_stopped(v->c->pd)&&end<=v->end&&at<=end&&n<=end-at;}
static bool sxmmkr_get(sxmmkr_view*v,uint64_t at,unsigned n,uint64_t end,uint64_t*x){uint8_t b[4];if(++v->work>100000||!n||n>4||!sxmmkr_span(v,at,n,end)||!sx_read(v->c,(int64_t)at,b,n))return false;*x=0;for(unsigned i=0;i<n;i++)*x=(*x<<8)|b[i];return true;}
static bool sxmmkr_table(sxmmkr_view*v,const sx_layout*l,uint64_t at,unsigned z,uint64_t n,uint64_t end){if(!n)return true;if(!z||n>24000-v->shown||n>UINT64_MAX/z||!sxmmkr_span(v,at,n*z,end))return false;if(!sx_table(v->c,l,(int64_t)at,z,n))return false;v->shown+=n;return true;}
static bool sxmmkr_row(sxmmkr_view*v,const sx_layout*l,uint64_t at,unsigned n,uint64_t end){return sxmmkr_table(v,l,at,n,1,end);}
static bool sxmmkr_orders(sxmmkr_view*v,uint64_t at,unsigned channels,uint64_t end){
 for(unsigned k=0;k<channels;k++){uint64_t begin=at,x;do{if(!sxmmkr_get(v,at,2,end,&x))return false;at+=2;}while(x!=999);if(!sxmmkr_table(v,&sxmmkr_musicmaker_order,begin,2,(at-begin)/2,end))return false;}
 return true;
}
static bool sxmmkr_song(sxmmkr_view*v,uint64_t at,uint64_t end){
 uint64_t x,kind,n,channels=4,orders;uint8_t marker[2];if(!sxmmkr_span(v,at,26,end)||!sx_read(v->c,(int64_t)at,marker,2)||xx_rt_memcmp(marker,"SE",2)||!sxmmkr_get(v,at+22,1,end,&kind))return false;
 if(kind==255){
  if(!sxmmkr_row(v,&sxmmkr_musicmaker_extended_song,at,26,end)||!sxmmkr_get(v,at+23,1,end,&x))return false;channels=0;for(unsigned k=0;k<8;k++)channels+=(x>>k)&1;
  uint64_t p=at+26;if(!sxmmkr_get(v,p,1,end,&x))return false;
  if(x==254){if(!sxmmkr_get(v,p+2,2,end,&n)||!sxmmkr_row(v,&sxmmkr_musicmaker_extended_extension,p,4,end)||n<4||n>=32768||!sxmmkr_span(v,p,n,end))return false;if(n>=36&&!sxmmkr_row(v,&sxmmkr_musicmaker_author,p+4,32,end))return false;p+=n;}
  /* Original loader skips four mixer bytes and 226 legacy mixer-table bytes. */
  if(!sxmmkr_span(v,p,234,end)||!sxmmkr_row(v,&sxmmkr_musicmaker_pattern_speed,p+230,4,end))return false;orders=p+234;
 }else{
  if(!sxmmkr_row(v,&sxmmkr_musicmaker_standard_song,at,26,end))return false;orders=at+26;
 }
 if(!sxmmkr_row(v,&sxmmkr_musicmaker_order_controls,orders,4,end))return false;orders+=4;
 if(kind==254){if(!sxmmkr_get(v,orders,2,end,&n)||n<2||!sxmmkr_span(v,orders,n,end)||!sxmmkr_row(v,&sxmmkr_musicmaker_standard_extension,orders,2,end))return false;orders+=n;}
 return sxmmkr_orders(v,orders,(unsigned)channels,end);
}
static bool sxmmkr_instruments(sxmmkr_view*v,uint64_t at,uint64_t end,bool packed){
 uint8_t sig[6];uint64_t count,n,bits=0,p;if(!sxmmkr_span(v,at,8,end)||!sx_read(v->c,(int64_t)at,sig,6)||xx_rt_memcmp(sig,"SEI1XX",6)||!sxmmkr_get(v,at+6,2,end,&count)||!sxmmkr_row(v,&sxmmkr_musicmaker_instrument_header,at,8,end))return false;
 if(!count||count>36||!sxmmkr_table(v,&sxmmkr_musicmaker_instrument,at+8,8,count,end))return false;p=at+8+8*count;
 if(packed){if(!sxmmkr_get(v,p,2,end,&bits)||bits<1||bits>8||!sxmmkr_row(v,&sxmmkr_musicmaker_packing,p,2,end))return false;p+=2;n=UINT64_C(1)<<bits;if(!sxmmkr_table(v,&sxmmkr_musicmaker_delta,p,1,n,end))return false;p+=n;}
 else{if(!sxmmkr_row(v,&sxmmkr_musicmaker_sample_state,p,4,end))return false;p+=4;}
 for(uint64_t i=0;i<count;i++){if(!sxmmkr_get(v,at+8+i*8,2,end,&n))return false;if(packed&&n)n=n*bits/8+1;if(!sxmmkr_span(v,p,n,end))return false;p+=n;}
 if(!packed&&(p&1)){if(!sxmmkr_span(v,p,1,end))return false;p++;}
 if(!sxmmkr_table(v,&sxmmkr_musicmaker_lfo_length,p,2,15,end))return false;uint64_t data=p+30;
 for(unsigned i=0;i<15;i++){if(!sxmmkr_get(v,p+i*2,2,end,&n)||!sxmmkr_span(v,data,n,end))return false;data+=n;}
 return true;
}
static bool sxmmkr_container(sxmmkr_view*v){uint8_t sig[12];uint64_t n,at=12,end;if(!sxmmkr_span(v,0,12,v->end)||!sx_read(v->c,0,sig,12)||xx_rt_memcmp(sig,"FORM",4)||xx_rt_memcmp(sig+8,"MMV8",4)||!sxmmkr_get(v,4,4,v->end,&n)||n<4||!sxmmkr_span(v,8,n,v->end)||!sxmmkr_row(v,&sxmmkr_musicmaker_form,0,12,v->end))return false;end=8+n;
 while(at<end){uint8_t id[4];if(!sxmmkr_span(v,at,8,end)||!sx_read(v->c,(int64_t)at,id,4)||!sxmmkr_get(v,at+4,4,end,&n)||!sxmmkr_span(v,at+8,n,end)||!sxmmkr_row(v,&sxmmkr_musicmaker_chunk,at,8,end))return false;at+=8;uint64_t ce=at+n;
  if(!xx_rt_memcmp(id,"SDAT",4)){uint64_t len;if(sxmmkr_get(v,at,4,ce,&len)&&sxmmkr_row(v,&sxmmkr_musicmaker_sdata_length,at,4,ce)&&sxmmkr_span(v,at+4,len,ce))sxmmkr_song(v,at+4,at+4+len);}
  else if(!xx_rt_memcmp(id,"PINS",4))sxmmkr_instruments(v,at,ce,true);
  else if(!xx_rt_memcmp(id,"INST",4))sxmmkr_instruments(v,at,ce,false);
  if(v->c->failed||xx_pd_is_stopped(v->c->pd))return false;at=ce;
 }return true;
}
static int sxmmkr_compare(const void*a,const void*b){const xx_data_struct*x=a,*y=b;return x->offset<y->offset?-1:x->offset>y->offset?1:0;}
static bool sxmmkr_finish(sx_context*c){xx_rt_qsort(c->items,c->count,sizeof(*c->items),sxmmkr_compare);size_t n=c->count;int64_t end=0;for(size_t i=0;i<n;i++){int64_t p=c->items[i].offset-c->f->base_address,z=c->items[i].total_size;if(p<end)return false;if(p>end&&!sx_raw(c,end,p-end))return false;end=p+z;}if(end<c->size&&!sx_raw(c,end,c->size-end))return false;xx_rt_qsort(c->items,c->count,sizeof(*c->items),sxmmkr_compare);return !c->failed&&!xx_pd_is_stopped(c->pd);}
static bool sx_musicmaker_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_WINISCHHOFER_MUSICMAKER_EXT||t==XX_FILE_TYPE_DIE_MUSIC_MUSICMAKER_STD_IP;}
static bool sx_musicmaker_build(sx_context*c){if(!c||!c->f||c->size<=0||c->f->base_address<0||c->f->base_address>INT64_MAX-c->size||(!c->f->base_info_handled&&!xx_format_handle_base_info(c->f,c->pd))||!c->f->is_valid||xx_pd_is_stopped(c->pd))return false;sxmmkr_view v={c,(uint64_t)c->size,0,0};if(c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_WINISCHHOFER_MUSICMAKER_EXT)sxmmkr_container(&v);else sxmmkr_instruments(&v,0,v.end,true);if(c->failed||xx_pd_is_stopped(c->pd))return false;return c->count?sxmmkr_finish(c):sx_raw(c,0,c->size);}
static bool sx_musicmaker_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<UINT32_C(0x60a40001)||id>UINT32_C(0x60a4ffff)||r->value.type==XX_VAR_TYPE_FLOAT||r->value.type==XX_VAR_TYPE_DOUBLE)return true;
 const wchar_t*n=r->name,*s=NULL;
 if(!xx_rt_wcscmp(n,L"form_bytes"))s=L"FORM payload byte length, including MMV8 type and all chunk headers/payloads; total envelope span is this value plus eight bytes.";
 if(!xx_rt_wcscmp(n,L"form_type"))s=L"Original MusicMaker module form type MMV8.";
 if(!xx_rt_wcscmp(n,L"chunk_id"))s=L"Original chunk identifier: SDAT song data, INST uncompressed instruments, PINS packed instruments, INAM editor names. Unknown contents stay raw.";
 if(!xx_rt_wcscmp(n,L"payload_bytes"))s=L"Physical chunk payload byte length. Original chunk traversal adds this exact stored size; any producer padding is included, not synthesized.";
 if(!xx_rt_wcscmp(n,L"song_bytes"))s=L"Internal SDAT song-data byte length, excluding this four-byte length prefix and any outer chunk padding.";
 if(!xx_rt_wcscmp(n,L"format_control"))s=L"Original standard-song format byte; 0xFE selects the extension whose length word follows the two order controls. 0xFF instead selects extended-song framing.";
 if(!xx_rt_wcscmp(n,L"extended_marker"))s=L"0xFF selects the original extended eight-channel serialization; it is not a channel count.";
 if(!xx_rt_wcscmp(n,L"enabled_channels"))s=L"One enable bit per playback channel; bit0 through bit7 select channels0 through7. Only enabled channels have order lists.";
 if(!xx_rt_wcscmp(n,L"mixer_type"))s=L"Stored original mixer selection/control byte; the 2017 player may substitute a different runtime mixer.";
 if(!xx_rt_wcscmp(n,L"highest_note_control"))s=L"Stored highest-note/mixer-table control byte, retained before runtime mixer calculations.";
 if(!xx_rt_wcscmp(n,L"pattern_length"))s=L"Stored pattern length in playback rows; standard songs store one byte, extended songs store a big-endian word.";
 if(!xx_rt_wcscmp(n,L"speed"))s=L"Original timer/speed control word; not beats per minute. Player derives hardware timing from this stored value.";
 if(!xx_rt_wcscmp(n,L"extension_marker"))s=L"0xFE marks the extended-song optional metadata block.";
 if(!xx_rt_wcscmp(n,L"extension_bytes"))s=id==sxmmkr_musicmaker_standard_extension.id?L"Byte length starting at this length word, skipped before the first standard-song order list.":L"Byte length starting at the 0xFE marker, including its four-byte prefix; the author string occupies the next32 bytes when present. Original ADDA.W sign-extends this value, so high-bit extents are preserved but not followed.";
 if(!xx_rt_wcscmp(n,L"opaque_control"))s=L"Original physically stored extension control byte; no unsupported meaning is assigned.";
 if(!xx_rt_wcscmp(n,L"author"))s=L"Original fixed32-byte author text; bytes after the first NUL remain preserved in raw_value.";
 if(!xx_rt_wcscmp(n,L"title"))s=L"Original fixed20-byte song title; bytes after the first NUL remain preserved in raw_value.";
 if(!xx_rt_wcscmp(n,L"start_order_index")||!xx_rt_wcscmp(n,L"loop_order_index"))s=L"Stored per-channel order-list index. Original player multiplies by two to address word entries; this is not a stored byte offset.";
 if(!xx_rt_wcscmp(n,L"macro_index_or_end"))s=L"Original macro index; 999 ends this channel's order list. Variable macro command bytes remain raw.";
 if(!xx_rt_wcscmp(n,L"instrument_count"))s=L"Number of eight-byte instrument descriptors in the marked SEI1XX bank; bounded supported source scope is1..36.";
 if(!xx_rt_wcscmp(n,L"sample_bytes"))s=L"Decoded sample byte length. For PINS/IP the physical packed bytes remain raw and no decoded positions are exposed.";
 if(!xx_rt_wcscmp(n,L"initial_bytes"))s=L"Initial sample segment length in bytes; the original player halves it before writing the DMA word-length register. Zero selects the one-shot path.";
 if(!xx_rt_wcscmp(n,L"loop_start"))s=L"Byte offset relative to this instrument's decoded sample start; unlike repeat_words, this value is measured in bytes.";
 if(!xx_rt_wcscmp(n,L"repeat_words"))s=L"Repeat length in two-byte hardware words, preserved without conversion to bytes.";
 if(!xx_rt_wcscmp(n,L"sample_processing_state"))s=L"Original signed processing-state word before uncompressed samples (decompressor initializes zero; player changes it to -1 after preparation).";
 if(!xx_rt_wcscmp(n,L"delta_bits"))s=L"Bits per packed delta index, scoped to1..8. Each nonempty sample occupies floor(bits*decoded_sample_bytes/8)+1 physical bytes in the original decoder, including its extra byte at exact boundaries.";
 if(!xx_rt_wcscmp(n,L"delta"))s=L"Stored signed eight-bit modular sample delta selected by packed indices; delta table has 2^delta_bits entries.";
 if(!xx_rt_wcscmp(n,L"lfo_bytes"))s=L"Byte length of this LFO byte stream; fifteen length words precede the concatenated raw LFO streams.";
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_musicmaker_family={sx_musicmaker_supports,sx_musicmaker_build,sx_musicmaker_layouts,SX_COUNT(sx_musicmaker_layouts),sx_musicmaker_comment};
