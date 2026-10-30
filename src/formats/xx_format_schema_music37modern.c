/* SPDX-License-Identifier: MIT
 * Original producer SaveFile and Instrument_Changed methods from Buzzic 1.0/1.1,
 * plus generated TinySynth.h and the author's manual. See music37modern_sources.json.
 * These are physical editable project records; no synthesis or sound execution. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxmd37_buzzic10_instrument_fields[]={
 SX_FIELD("name","char[128]",0,128,SX_NONE),
 SX_FIELD("muted","uint8",128,1,SX_NONE),
 SX_FIELD("note","int8",129,1,SX_NONE),
 SX_FIELD("note_end","int8",130,1,SX_NONE),
 SX_FIELD("note_length","uint8",131,1,SX_NONE),
 SX_FIELD("volume","int8",132,1,SX_NONE),
 SX_FIELD("oscillator","uint8",133,1,SX_NONE),
 SX_FIELD("detune","int8",134,1,SX_NONE),
 SX_FIELD("attack","uint8",135,1,SX_NONE),
 SX_FIELD("decay","uint8",136,1,SX_NONE),
 SX_FIELD("sustain","uint8",137,1,SX_NONE),
 SX_FIELD("filter_cutoff","int8",138,1,SX_NONE),
 SX_FIELD("filter_cutoff_modulation","int8",139,1,SX_NONE),
 SX_FIELD("filter_resonance","int8",140,1,SX_NONE),
 SX_FIELD("echo_delay","uint8",141,1,SX_NONE),
 SX_FIELD("echo_repeats","uint8",142,1,SX_NONE),
 SX_FIELD("echo_damping","uint8",143,1,SX_NONE),
 SX_FIELD("wrap_level","int8",144,1,SX_NONE),
 SX_FIELD("hard_level","int8",145,1,SX_NONE),
 SX_FIELD("compressor_low","uint8",146,1,SX_NONE),
 SX_FIELD("compressor_high","uint8",147,1,SX_NONE),
};
static const sx_layout sxmd37_buzzic10_instrument={0x60ad0001U,"buzzic10_instrument",sxmd37_buzzic10_instrument_fields,SX_COUNT(sxmd37_buzzic10_instrument_fields),SX_LE};
static const xx_data_struct_field_desc sxmd37_buzzic11_instrument_fields[]={
 SX_FIELD("name","char[128]",0,128,SX_NONE),
 SX_FIELD("muted","uint8",128,1,SX_NONE),
 SX_FIELD("note","int8",129,1,SX_NONE),
 SX_FIELD("note_end","int8",130,1,SX_NONE),
 SX_FIELD("note_length","int8",131,1,SX_NONE),
 SX_FIELD("volume","int8",132,1,SX_NONE),
 SX_FIELD("oscillator","uint8",133,1,SX_NONE),
 SX_FIELD("detune","int8",134,1,SX_NONE),
 SX_FIELD("attack","uint8",135,1,SX_NONE),
 SX_FIELD("decay","uint8",136,1,SX_NONE),
 SX_FIELD("sustain","uint8",137,1,SX_NONE),
 SX_FIELD("filter_cutoff","int8",138,1,SX_NONE),
 SX_FIELD("filter_cutoff_modulation","int8",139,1,SX_NONE),
 SX_FIELD("filter_resonance","int8",140,1,SX_NONE),
 SX_FIELD("echo_delay","uint8",141,1,SX_NONE),
 SX_FIELD("echo_repeats","uint8",142,1,SX_NONE),
 SX_FIELD("echo_damping","uint8",143,1,SX_NONE),
 SX_FIELD("wrap_level","int8",144,1,SX_NONE),
 SX_FIELD("hard_level","int8",145,1,SX_NONE),
 SX_FIELD("compressor_low","uint8",146,1,SX_NONE),
 SX_FIELD("compressor_high","uint8",147,1,SX_NONE),
 SX_FIELD("panning","int8",148,1,SX_NONE),
 SX_FIELD("resonant_filter_frequency","int8",149,1,SX_NONE),
 SX_FIELD("resonant_filter_modulation","int8",150,1,SX_NONE),
 SX_FIELD("resonant_filter_resonance","int8",151,1,SX_NONE),
};
static const sx_layout sxmd37_buzzic11_instrument={0x60ad0002U,"buzzic11_instrument",sxmd37_buzzic11_instrument_fields,SX_COUNT(sxmd37_buzzic11_instrument_fields),SX_LE};
static const xx_data_struct_field_desc sxmd37_buzzic_instrument_count_fields[]={
 SX_FIELD("instrument_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxmd37_buzzic_instrument_count={0x60ad0003U,"buzzic_instrument_count",sxmd37_buzzic_instrument_count_fields,SX_COUNT(sxmd37_buzzic_instrument_count_fields),SX_LE};
static const xx_data_struct_field_desc sxmd37_buzzic_byte_pattern_fields[]={
 SX_FIELD("values","uint8[16]",0,16,SX_NONE),
};
static const sx_layout sxmd37_buzzic_byte_pattern={0x60ad0004U,"buzzic_byte_pattern",sxmd37_buzzic_byte_pattern_fields,SX_COUNT(sxmd37_buzzic_byte_pattern_fields),SX_LE};
static const xx_data_struct_field_desc sxmd37_buzzic_bit_patterns_fields[]={
 SX_FIELD("legacy_bit_patterns","uint8[128]",0,128,SX_NONE),
};
static const sx_layout sxmd37_buzzic_bit_patterns={0x60ad0005U,"buzzic_bit_patterns",sxmd37_buzzic_bit_patterns_fields,SX_COUNT(sxmd37_buzzic_bit_patterns_fields),SX_LE};
static const xx_data_struct_field_desc sxmd37_buzzic_byte_sequence_fields[]={
 SX_FIELD("pattern_indices","uint8[128]",0,128,SX_NONE),
};
static const sx_layout sxmd37_buzzic_byte_sequence={0x60ad0006U,"buzzic_byte_sequence",sxmd37_buzzic_byte_sequence_fields,SX_COUNT(sxmd37_buzzic_byte_sequence_fields),SX_LE};
static const xx_data_struct_field_desc sxmd37_buzzic_bit_sequence_fields[]={
 SX_FIELD("legacy_bit_pattern_indices","uint8[128]",0,128,SX_NONE),
};
static const sx_layout sxmd37_buzzic_bit_sequence={0x60ad0007U,"buzzic_bit_sequence",sxmd37_buzzic_bit_sequence_fields,SX_COUNT(sxmd37_buzzic_bit_sequence_fields),SX_LE};
static const xx_data_struct_field_desc sxmd37_buzzic_song_fields[]={
 SX_FIELD("byte_pattern_count","int32",0,4,SX_COUNT_PROP),
 SX_FIELD("bit_pattern_count","int32",4,4,SX_COUNT_PROP),
 SX_FIELD("byte_sequence_count","int32",8,4,SX_COUNT_PROP),
 SX_FIELD("bit_sequence_count","int32",12,4,SX_COUNT_PROP),
 SX_FIELD("master_volume","float",16,4,SX_NONE),
 SX_FIELD("tempo_bpm","int32",20,4,SX_NONE),
 SX_FIELD("start_sequence","int32",24,4,SX_NONE),
 SX_FIELD("end_sequence","int32",28,4,SX_NONE),
 SX_FIELD("looping","int32",32,4,SX_NONE),
};
static const sx_layout sxmd37_buzzic_song={0x60ad0008U,"buzzic_song",sxmd37_buzzic_song_fields,SX_COUNT(sxmd37_buzzic_song_fields),SX_LE};
static const xx_data_struct_field_desc sxmd37_buzzic_sequence_name_fields[]={
 SX_FIELD("name","char[64]",0,64,SX_NONE),
};
static const sx_layout sxmd37_buzzic_sequence_name={0x60ad0009U,"buzzic_sequence_name",sxmd37_buzzic_sequence_name_fields,SX_COUNT(sxmd37_buzzic_sequence_name_fields),SX_LE};
static const xx_data_struct_field_desc sxmd37_buzzic_pattern_name_fields[]={
 SX_FIELD("name","char[64]",0,64,SX_NONE),
};
static const sx_layout sxmd37_buzzic_pattern_name={0x60ad000aU,"buzzic_pattern_name",sxmd37_buzzic_pattern_name_fields,SX_COUNT(sxmd37_buzzic_pattern_name_fields),SX_LE};
static const sx_layout*const sx_music37modern_layouts[]={&sxmd37_buzzic10_instrument,&sxmd37_buzzic11_instrument,&sxmd37_buzzic_instrument_count,&sxmd37_buzzic_byte_pattern,&sxmd37_buzzic_bit_patterns,&sxmd37_buzzic_byte_sequence,&sxmd37_buzzic_bit_sequence,&sxmd37_buzzic_song,&sxmd37_buzzic_sequence_name,&sxmd37_buzzic_pattern_name};
static bool sx_music37modern_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_STEPANOV_BUZZIC10||t==XX_FILE_TYPE_DIE_MUSIC_STEPANOV_BUZZIC11;}
static bool sxmd37_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p;}
static bool sxmd37_raw(sx_context*c){uint8_t b;return c&&!c->failed&&!c->count&&c->size>0&&sx_read(c,0,&b,1)&&!xx_pd_is_stopped(c->pd)&&sx_raw(c,0,c->size);}
static bool sx_music37modern_build(sx_context*c){
 if(!c||!c->f||!sx_music37modern_supports(c->f->file_type)||xx_pd_is_stopped(c->pd))return false;
 bool newer=c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_STEPANOV_BUZZIC11;
 uint64_t width=newer?152:148,insbytes=128*width,p=insbytes;
 /* Both writers serialize all fixed-capacity slots, including unused slots.
  * Counts describe logical use and never control allocation or table extents. */
 if(!sxmd37_span(c,0,insbytes+4+1024+128+8192+8192+36))return sxmd37_raw(c);
 if(!sx_table(c,newer?&sxmd37_buzzic11_instrument:&sxmd37_buzzic10_instrument,0,(int64_t)width,128)||
    !sx_add(c,&sxmd37_buzzic_instrument_count,(int64_t)p,4))return false;p+=4;
 if(!sx_table(c,&sxmd37_buzzic_byte_pattern,(int64_t)p,16,64))return false;p+=1024;
 if(!sx_add(c,&sxmd37_buzzic_bit_patterns,(int64_t)p,128))return false;p+=128;
 if(!sx_table(c,&sxmd37_buzzic_byte_sequence,(int64_t)p,128,64))return false;p+=8192;
 if(!sx_table(c,&sxmd37_buzzic_bit_sequence,(int64_t)p,128,64))return false;p+=8192;
 if(!sx_add(c,&sxmd37_buzzic_song,(int64_t)p,36))return false;p+=36;
 /* Partial name tables are retained as raw; complete fixed slots are safe. */
 uint64_t names=sxmd37_span(c,p,4096)?64:0;
 if(names){if(!sx_table(c,&sxmd37_buzzic_sequence_name,(int64_t)p,64,64))return false;p+=4096;}
 if(names&&sxmd37_span(c,p,4096)){if(!sx_table(c,&sxmd37_buzzic_pattern_name,(int64_t)p,64,64))return false;p+=4096;}
 return sx_raw(c,(int64_t)p,c->size-(int64_t)p)&&!c->failed&&!xx_pd_is_stopped(c->pd);
}
static bool sx_music37modern_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60ad0001U||id>0x60ad000aU)return true;
 if(id==0x60ad0008U&&!xx_rt_wcscmp(r->name,L"master_volume"))return xx_data_struct_record_set_comment(r,L"Master volume as a 32-bit floating point multiplier; 1.0 represents unity gain.");
 if(r->value.type==XX_VAR_TYPE_FLOAT||r->value.type==XX_VAR_TYPE_DOUBLE)return true;
 const wchar_t*s=NULL;
 if(id==0x60ad0001U||id==0x60ad0002U){
  if(!xx_rt_wcscmp(r->name,L"oscillator")){uint64_t v=xx_var_get_u64(&r->value);s=v==0?L"Sine oscillator":v==1?L"Sawtooth oscillator":v==2?L"Square oscillator":v==3?L"Noise oscillator":L"Unknown oscillator value";}
  else if(!xx_rt_wcscmp(r->name,L"muted"))s=L"Nonzero mutes this instrument in the editor; inactive capacity slots are also stored.";
  else if(!xx_rt_wcscmp(r->name,L"name"))s=L"Instrument name in a fixed 128-byte producer buffer.";
  else if(!xx_rt_wcscmp(r->name,L"note_length"))s=id==0x60ad0001U?L"Unsigned length in hundredths of a second.":L"Length in hundredths of a second; negative values select a sequence.";
  else if(!xx_rt_wcscmp(r->name,L"detune"))s=L"Signed pitch offset in semitones.";
  else if(!xx_rt_wcscmp(r->name,L"attack")||!xx_rt_wcscmp(r->name,L"decay")||!xx_rt_wcscmp(r->name,L"sustain"))s=L"Envelope boundary as a percentage of note length.";
  else if(!xx_rt_wcscmp(r->name,L"note")||!xx_rt_wcscmp(r->name,L"note_end"))s=L"Nonnegative pitch value; a negative value selects sequence (-value)-1. Zero note suppresses sound; zero ending pitch preserves the starting pitch.";
  else if(!xx_rt_wcscmp(r->name,L"volume"))s=L"Signed source value; nonnegative volume is in hundredths, negative values select a sequence.";
  else s=L"Stored synthesizer parameter from the original editor; signed values preserve sequence references and modulation signs.";
 }else if(id==0x60ad0003U)s=L"Logical active instrument count; the producer always stores capacity for 128 instruments.";
 else if(id==0x60ad0004U)s=L"Sixteen unsigned pattern values (0 to 127), independent of note or modulation use.";
 else if(id==0x60ad0005U||id==0x60ad0007U)s=L"Legacy bit-pattern storage explicitly saved by the producer; packed bit semantics are left uninterpreted.";
 else if(id==0x60ad0006U)s=L"Sequence of 128 byte-pattern references; zero is empty and positive values address pattern index minus one.";
 else if(id==0x60ad0008U){if(!xx_rt_wcscmp(r->name,L"looping"))s=L"32-bit producer boolean: zero disables looping, one enables it.";else if(!xx_rt_wcscmp(r->name,L"start_sequence")||!xx_rt_wcscmp(r->name,L"end_sequence"))s=L"Zero-based play range position; ending position is inclusive.";else if(!xx_rt_wcscmp(r->name,L"tempo_bpm"))s=L"Tempo in beats per minute, stored as a full 32-bit integer.";else s=L"Logical producer count; physical fixed-capacity table sizes are independent of this value.";}
 else s=L"Fixed 64-byte producer name slot, including unused capacity slots.";
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_music37modern_family={sx_music37modern_supports,sx_music37modern_build,sx_music37modern_layouts,SX_COUNT(sx_music37modern_layouts),sx_music37modern_comment};
