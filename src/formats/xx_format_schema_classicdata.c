/* SPDX-License-Identifier: MIT
 * Physical Infogrames DUM and The Musical Enlightenment metadata.
 * Original implementation from documented source layouts; payloads remain raw. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxcd_infogrames_subsong_offset_fields[]={
 SX_FIELD("subsong_offset","uint16",0,2,SX_OFFSET),
};
static const sx_layout sxcd_infogrames_subsong_offset={0x60a50001U,"infogrames_subsong_offset",sxcd_infogrames_subsong_offset_fields,SX_COUNT(sxcd_infogrames_subsong_offset_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_infogrames_song_header_fields[]={
 SX_FIELD("speed","uint16",0,2,SX_NONE),
 SX_FIELD("volume_envelope_offset","uint16",2,2,SX_OFFSET),
 SX_FIELD("period_envelope_offset","uint16",4,2,SX_OFFSET),
 SX_FIELD("channel_0_order_offset","uint16",6,2,SX_OFFSET),
 SX_FIELD("channel_1_order_offset","uint16",8,2,SX_OFFSET),
 SX_FIELD("channel_2_order_offset","uint16",10,2,SX_OFFSET),
 SX_FIELD("channel_3_order_offset","uint16",12,2,SX_OFFSET),
 SX_FIELD("unused_word","uint16",14,2,SX_NONE),
};
static const sx_layout sxcd_infogrames_song_header={0x60a50002U,"infogrames_song_header",sxcd_infogrames_song_header_fields,SX_COUNT(sxcd_infogrames_song_header_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_infogrames_command_offset_fields[]={
 SX_FIELD("command_offset","uint16",0,2,SX_OFFSET),
};
static const sx_layout sxcd_infogrames_command_offset={0x60a50003U,"infogrames_command_offset",sxcd_infogrames_command_offset_fields,SX_COUNT(sxcd_infogrames_command_offset_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_infogrames_order_fields[]={
 SX_FIELD("command_block_index","uint8",0,1,SX_NONE),
};
static const sx_layout sxcd_infogrames_order={0x60a50004U,"infogrames_order",sxcd_infogrames_order_fields,SX_COUNT(sxcd_infogrames_order_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_infogrames_order_end_fields[]={
 SX_FIELD("end_marker","uint8",0,1,SX_ID),
};
static const sx_layout sxcd_infogrames_order_end={0x60a50005U,"infogrames_order_end",sxcd_infogrames_order_end_fields,SX_COUNT(sxcd_infogrames_order_end_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_infogrames_volume_envelope_fields[]={
 SX_FIELD("control","uint8",0,1,SX_FLAGS),
 SX_FIELD("phase_0_repeat","uint8",1,1,SX_NONE),
 SX_FIELD("phase_0_delta","int8",2,1,SX_NONE),
 SX_FIELD("phase_0_delay","uint8",3,1,SX_NONE),
 SX_FIELD("phase_1_repeat","uint8",4,1,SX_NONE),
 SX_FIELD("phase_1_delta","int8",5,1,SX_NONE),
 SX_FIELD("phase_1_delay","uint8",6,1,SX_NONE),
 SX_FIELD("phase_2_repeat","uint8",7,1,SX_NONE),
 SX_FIELD("phase_2_delta","int8",8,1,SX_NONE),
 SX_FIELD("phase_2_delay","uint8",9,1,SX_NONE),
 SX_FIELD("phase_3_repeat","uint8",10,1,SX_NONE),
 SX_FIELD("phase_3_delta","int8",11,1,SX_NONE),
 SX_FIELD("phase_3_delay","uint8",12,1,SX_NONE),
};
static const sx_layout sxcd_infogrames_volume_envelope={0x60a50006U,"infogrames_volume_envelope",sxcd_infogrames_volume_envelope_fields,SX_COUNT(sxcd_infogrames_volume_envelope_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_infogrames_period_envelope_fields[]={
 SX_FIELD("unused_byte","uint8",0,1,SX_NONE),
 SX_FIELD("phase_0_repeat","uint8",1,1,SX_NONE),
 SX_FIELD("phase_0_delta","int8",2,1,SX_NONE),
 SX_FIELD("phase_0_delay","uint8",3,1,SX_NONE),
 SX_FIELD("phase_1_repeat","uint8",4,1,SX_NONE),
 SX_FIELD("phase_1_delta","int8",5,1,SX_NONE),
 SX_FIELD("phase_1_delay","uint8",6,1,SX_NONE),
 SX_FIELD("phase_2_repeat","uint8",7,1,SX_NONE),
 SX_FIELD("phase_2_delta","int8",8,1,SX_NONE),
 SX_FIELD("phase_2_delay","uint8",9,1,SX_NONE),
 SX_FIELD("phase_3_repeat","uint8",10,1,SX_NONE),
 SX_FIELD("phase_3_delta","int8",11,1,SX_NONE),
 SX_FIELD("phase_3_delay","uint8",12,1,SX_NONE),
};
static const sx_layout sxcd_infogrames_period_envelope={0x60a50007U,"infogrames_period_envelope",sxcd_infogrames_period_envelope_fields,SX_COUNT(sxcd_infogrames_period_envelope_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_tme_file_header_fields[]={
 SX_FIELD("song_byte_length","uint32",0,4,SX_SIZE),
};
static const sx_layout sxcd_tme_file_header={0x60a50008U,"tme_file_header",sxcd_tme_file_header_fields,SX_COUNT(sxcd_tme_file_header_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_tme_subsong_fields[]={
 SX_FIELD("start_entry","uint8",0,1,SX_NONE),
 SX_FIELD("end_entry","uint8",1,1,SX_NONE),
 SX_FIELD("speed","uint8",2,1,SX_NONE),
 SX_FIELD("channel_mask","uint8",3,1,SX_FLAGS),
};
static const sx_layout sxcd_tme_subsong={0x60a50009U,"tme_subsong",sxcd_tme_subsong_fields,SX_COUNT(sxcd_tme_subsong_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_tme_instrument_fields[]={
 SX_FIELD("stored_sample_address","uint32",0,4,SX_ADDRESS),
 SX_FIELD("sample_byte_length","uint32",4,4,SX_SIZE),
 SX_FIELD("initial_length_words","uint32",8,4,SX_NONE),
 SX_FIELD("repeat_offset_words","uint32",12,4,SX_OFFSET),
 SX_FIELD("repeat_length_words","uint32",16,4,SX_NONE),
 SX_FIELD("offset_length_encoded","uint16",20,2,SX_NONE),
 SX_FIELD("sample_flags","uint16",22,2,SX_FLAGS),
 SX_FIELD("name","char[32]",24,32,SX_NONE),
 SX_FIELD("volume","uint8",56,1,SX_NONE),
 SX_FIELD("unused_byte","uint8",57,1,SX_NONE),
 SX_FIELD("attack_rate","uint8",58,1,SX_NONE),
 SX_FIELD("decay_rate","uint8",59,1,SX_NONE),
 SX_FIELD("release_rate","uint8",60,1,SX_NONE),
 SX_FIELD("attack_level","uint8",61,1,SX_NONE),
 SX_FIELD("decay_level","uint8",62,1,SX_NONE),
 SX_FIELD("release_level","uint8",63,1,SX_NONE),
};
static const sx_layout sxcd_tme_instrument={0x60a5000aU,"tme_instrument",sxcd_tme_instrument_fields,SX_COUNT(sxcd_tme_instrument_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_tme_instruction_fields[]={
 SX_FIELD("opcode","uint8",0,1,SX_FLAGS),
 SX_FIELD("value","uint8",1,1,SX_NONE),
 SX_FIELD("time","uint16",2,2,SX_NONE),
};
static const sx_layout sxcd_tme_instruction={0x60a5000bU,"tme_instruction",sxcd_tme_instruction_fields,SX_COUNT(sxcd_tme_instruction_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_tme_arpeggio_fields[]={
 SX_FIELD("length_speed","uint8",0,1,SX_FLAGS),
 SX_FIELD("note_steps","int8[8]",1,8,SX_NONE),
};
static const sx_layout sxcd_tme_arpeggio={0x60a5000cU,"tme_arpeggio",sxcd_tme_arpeggio_fields,SX_COUNT(sxcd_tme_arpeggio_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_tme_lfo_fields[]={
 SX_FIELD("waveform","int8[128]",0,128,SX_NONE),
};
static const sx_layout sxcd_tme_lfo={0x60a5000dU,"tme_lfo",sxcd_tme_lfo_fields,SX_COUNT(sxcd_tme_lfo_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_tme_counts_fields[]={
 SX_FIELD("entry_count","uint16",0,2,SX_COUNT_PROP),
 SX_FIELD("event_count","uint16",2,2,SX_COUNT_PROP),
 SX_FIELD("instructions_word","uint16",4,2,SX_NONE),
 SX_FIELD("name","char[32]",6,32,SX_NONE),
};
static const sx_layout sxcd_tme_counts={0x60a5000eU,"tme_counts",sxcd_tme_counts_fields,SX_COUNT(sxcd_tme_counts_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_tme_entry_fields[]={
 SX_FIELD("channel_0_track","uint8",0,1,SX_NONE),
 SX_FIELD("channel_0_instrument_add","int8",1,1,SX_NONE),
 SX_FIELD("channel_0_note_add","int8",2,1,SX_NONE),
 SX_FIELD("channel_1_track","uint8",3,1,SX_NONE),
 SX_FIELD("channel_1_instrument_add","int8",4,1,SX_NONE),
 SX_FIELD("channel_1_note_add","int8",5,1,SX_NONE),
 SX_FIELD("channel_2_track","uint8",6,1,SX_NONE),
 SX_FIELD("channel_2_instrument_add","int8",7,1,SX_NONE),
 SX_FIELD("channel_2_note_add","int8",8,1,SX_NONE),
 SX_FIELD("channel_3_track","uint8",9,1,SX_NONE),
 SX_FIELD("channel_3_instrument_add","int8",10,1,SX_NONE),
 SX_FIELD("channel_3_note_add","int8",11,1,SX_NONE),
};
static const sx_layout sxcd_tme_entry={0x60a5000fU,"tme_entry",sxcd_tme_entry_fields,SX_COUNT(sxcd_tme_entry_fields),SX_BE};
static const xx_data_struct_field_desc sxcd_tme_event_fields[]={
 SX_FIELD("note","uint8",0,1,SX_FLAGS),
 SX_FIELD("sample","uint8",1,1,SX_NONE),
 SX_FIELD("effect_list","uint8",2,1,SX_NONE),
 SX_FIELD("volume_add","int8",3,1,SX_NONE),
 SX_FIELD("flags","uint8",4,1,SX_FLAGS),
 SX_FIELD("parameter","uint8",5,1,SX_NONE),
};
static const sx_layout sxcd_tme_event={0x60a50010U,"tme_event",sxcd_tme_event_fields,SX_COUNT(sxcd_tme_event_fields),SX_BE};
static const sx_layout*const sx_classicdata_layouts[]={&sxcd_infogrames_subsong_offset,&sxcd_infogrames_song_header,&sxcd_infogrames_command_offset,&sxcd_infogrames_order,&sxcd_infogrames_order_end,&sxcd_infogrames_volume_envelope,&sxcd_infogrames_period_envelope,&sxcd_tme_file_header,&sxcd_tme_subsong,&sxcd_tme_instrument,&sxcd_tme_instruction,&sxcd_tme_arpeggio,&sxcd_tme_lfo,&sxcd_tme_counts,&sxcd_tme_entry,&sxcd_tme_event};
static bool sx_classicdata_supports(xx_file_type_t type)
{return type==XX_FILE_TYPE_DIE_MUSIC_INFOGRAMES_RH2_DUM||type==XX_FILE_TYPE_DIE_MUSIC_LUURING_THEMUSICALENLIGHTENMENT;}
static bool sxcd_fits(sx_context*c,int64_t off,uint64_t n)
{return off>=0&&off<=c->size&&n<=(uint64_t)(c->size-off);}
static int sxcd_order(const void*a,const void*b)
{const xx_data_struct*x=a,*y=b;return x->offset<y->offset?-1:x->offset>y->offset;}
static bool sxcd_finish(sx_context*c)
{
 size_t n=c->count;int64_t end=0;xx_rt_qsort(c->items,n,sizeof(*c->items),sxcd_order);
 for(size_t i=0;i<n;i++){int64_t off=c->items[i].offset-c->f->base_address,z=c->items[i].total_size;
  if(off<end||z<=0||!sxcd_fits(c,off,(uint64_t)z))return false;
  if(off>end&&!sx_raw(c,end,off-end))return false;end=off+z;
 }
 if(end<c->size&&!sx_raw(c,end,c->size-end))return false;
 xx_rt_qsort(c->items,c->count,sizeof(*c->items),sxcd_order);return !c->failed&&!xx_pd_is_stopped(c->pd);
}
static int64_t sxcd_limit(int64_t off,const int64_t*points,size_t n,int64_t end)
{for(size_t i=0;i<n;i++)if(points[i]>off&&points[i]<end)end=points[i];return end;}
static bool sxcd_infogrames(sx_context*c)
{
 uint16_t song,h[8];int64_t points[262];size_t np=0;
 if(c->size<18||!sx_u16(c,0,true,&song)||song!=2)return false;
 for(unsigned i=0;i<8;i++)if(!sx_u16(c,2+2*i,true,&h[i]))return false;
 /* One native-recognized subsong. Short offsets with bit15 set have
  * conflicting signed/unsigned player semantics and are not followed. */
 int64_t first=c->size;
 for(unsigned i=1;i<7;i++){if(h[i]<16||h[i]>=0x8000||!sxcd_fits(c,2+h[i],1))return false;points[np++]=2+h[i];if(2+h[i]<first)first=2+h[i];}
 if(first<18||((first-18)&1)||(first-18)/2>256)return false;
 uint64_t count=(uint64_t)(first-18)/2;
 for(uint64_t i=0;i<count;i++){uint16_t off;if(!sx_u16(c,18+(int64_t)i*2,true,&off))return false;
  if(off>=0x8000||2+off<first||!sxcd_fits(c,2+off,1))return false;points[np++]=2+off;
 }
 if(!sx_add(c,&sxcd_infogrames_subsong_offset,0,2)||!sx_add(c,&sxcd_infogrames_song_header,2,16)||!sx_table(c,&sxcd_infogrames_command_offset,18,2,count))return false;
 for(unsigned channel=0;channel<4;channel++){
  int64_t at=2+h[channel+3],end=sxcd_limit(at,points,np,c->size);bool shared=false;
  for(unsigned j=0;j<channel;j++)if(h[j+3]==h[channel+3])shared=true;
  /* Conflicting region aliases stay raw; never invent overlapping fields. */
  if(shared||at==2+h[1]||at==2+h[2])continue;
  for(uint64_t j=0;j<count;j++)if(points[6+j]==at)shared=true;
  if(shared)continue;
  int64_t p=at;uint8_t v=0;while(p<end&&p-at<4096){if(!sx_read(c,p,&v,1))return false;if(v==255)break;if(v>=count)break;p++;}
  if(p<end&&v==255){if(!sx_table(c,&sxcd_infogrames_order,at,1,(uint64_t)(p-at))||!sx_add(c,&sxcd_infogrames_order_end,p,1))return false;}
 }
 /* Only the first explicitly located envelope is described. No count is
  * inferred from padding or bytecode; later envelopes remain raw. */
 for(unsigned i=1;i<=2;i++){
  int64_t at=2+h[i],end=sxcd_limit(at,points,np,c->size);bool alias=false;
  for(size_t j=0;j<np;j++)if(j!=(size_t)(i-1)&&points[j]==at)alias=true;
  if(!alias&&end-at>=13&&!sx_add(c,i==1?&sxcd_infogrames_volume_envelope:&sxcd_infogrames_period_envelope,at,13))return false;
 }
 return true;
}
static bool sxcd_tme(sx_context*c)
{
 uint32_t total;uint16_t entries,events;int64_t at;uint64_t budget=24000,work=0;
 if(c->size<6826||!sx_u32(c,0,true,&total)||total<6826||total>(uint64_t)c->size)return false;
 if(!sx_u16(c,6788,true,&entries)||!sx_u16(c,6790,true,&events))return false;
 uint64_t dynamic=(uint64_t)entries*12+(uint64_t)events*6;
 if(dynamic>total-6826)return false;
 if(!sx_add(c,&sxcd_tme_file_header,0,4)||!sx_table(c,&sxcd_tme_subsong,4,4,16))return false;
 for(unsigned i=0;i<32;i++)if(!sx_add(c,&sxcd_tme_instrument,68+128*i,64)||!sx_table(c,&sxcd_tme_instruction,132+128*i,4,16))return false;
 if(!sx_table(c,&sxcd_tme_arpeggio,4164,9,64)||!sx_table(c,&sxcd_tme_lfo,4740,128,16)||!sx_add(c,&sxcd_tme_counts,6788,38))return false;
 uint64_t shown=entries<budget?entries:budget;
 if(!sx_table(c,&sxcd_tme_entry,6826,12,shown))return false;budget-=shown;at=6826+(int64_t)entries*12;
 if(!budget)return true;shown=events<budget?events:budget;
 if(!sx_table(c,&sxcd_tme_event,at,6,shown))return false;budget-=shown;at+=(int64_t)events*6;
 if(!budget)return true;
 /* Original InitData walks exactly256 zero-terminated instruction lists,
  * independently of instructions_word. Parse metadata, never execute it. */
 for(unsigned i=0;i<256;i++){
  int64_t p=at;uint8_t op=1;
  while(p<=total&&total-p>=4&&p-at<16384){if(work++>=budget)return true;if(!sx_read(c,p,&op,1))return false;p+=4;if(!op)break;}
  if(op||p-at>16384)return true; /* incomplete/oversized tail remains raw */
  if(!sx_table(c,&sxcd_tme_instruction,at,4,(uint64_t)(p-at)/4))return false;at=p;
 }
 /* Sample bodies remain raw. Stored runtime address slots are never followed. */
 return true;
}
static bool sx_classicdata_build(sx_context*c)
{
 if(!c||!c->f||!sx_classicdata_supports(c->f->file_type)||c->size<=0||c->size>64*1024*1024||c->failed||xx_pd_is_stopped(c->pd))return false;
 if((!c->f->base_info_handled&&!xx_format_handle_base_info(c->f,c->pd))||!c->f->is_valid)return false;
 uint8_t probe;if(!sx_read(c,0,&probe,1))return false;
 bool ok=c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_INFOGRAMES_RH2_DUM?sxcd_infogrames(c):sxcd_tme(c);
 if(!ok){if(c->failed||xx_pd_is_stopped(c->pd)||c->count)return false;return sx_raw(c,0,c->size);}
 return sxcd_finish(c);
}
static bool sx_classicdata_comment(xx_data_struct_record*r,uint32_t id)
{
 if(!r||!r->name||id<0x60a50001U||id>=0x60a60000U||r->value.type==XX_VAR_TYPE_FLOAT||r->value.type==XX_VAR_TYPE_DOUBLE)return true;
 const wchar_t*n=r->name;uint64_t v=xx_var_get_u64(&r->value);
#define CD_NAME(s) (!xx_rt_wcscmp(n,L##s))
#define CD_TEXT(s) return xx_data_struct_record_set_comment(r,L##s)
 if(id==sxcd_infogrames_subsong_offset.id){CD_TEXT("Absolute file offset of the subsong header. Current native identity recognizes the single-subsong layout with this word equal to2.");}
 if(id==sxcd_infogrames_song_header.id){
  if(CD_NAME("speed")){CD_TEXT("Stored playback speed word. The original player uses its low byte; the physical16-bit value is retained.");}
  if(CD_NAME("unused_word")){CD_TEXT("Stored word skipped by the original player; no address or size meaning is inferred from the detector heuristic.");}
  CD_TEXT("Stored byte displacement relative to this16-byte subsong header. Original ADDA.W and maintained unsigned loaders differ for bit15; those targets are not followed.");
 }
 if(id==sxcd_infogrames_command_offset.id){CD_TEXT("Stored byte displacement from the subsong header to command bytecode. The pointer table ends at the first explicit region; bytecode remains raw.");}
 if(id==sxcd_infogrames_order.id){CD_TEXT("Zero-based index into the subsong command-offset table.0xFF is a separate end marker.");}
 if(id==sxcd_infogrames_order_end.id){CD_TEXT("ORDER_END (0xFF). The player restarts from the channel order list.");}
 if(id==sxcd_infogrames_volume_envelope.id||id==sxcd_infogrames_period_envelope.id){
  if(CD_NAME("control"))return xx_data_struct_record_set_comment(r,v&128?L"CONTINUOUS (bit7); low7 bits are the amount subtracted on each envelope cycle.":L"ONE_SHOT (bit7 clear); low7 bits are the envelope cycle adjustment amount.");
  if(CD_NAME("unused_byte")){CD_TEXT("Stored leading period-envelope byte skipped by the player. One-shot/continuous mode comes from command bytecode.");}
  if(sx_wcsstr(n,L"_delta")){CD_TEXT("Signed8-bit increment applied to the accumulated slide value.");}
  if(sx_wcsstr(n,L"_repeat")){CD_TEXT("Number of delay intervals before advancing to the next envelope phase; encoded byte is retained.");}
  CD_TEXT("Number of ticks per interval before incrementing the phase repeat counter; encoded byte is retained.");
 }
 if(id==sxcd_tme_file_header.id){CD_TEXT("Stored total song byte length, including the physical header, tables, instruction lists and sample payloads.");}
 if(id==sxcd_tme_subsong.id){
  if(CD_NAME("speed")){CD_TEXT("Stored countdown reload; the playback interval is this byte plus one.");}
  if(CD_NAME("channel_mask")){CD_TEXT("CHANNEL_0=0x01|CHANNEL_1=0x02|CHANNEL_2=0x04|CHANNEL_3=0x08|LED_FILTER=0x10. Higher bits remain unknown.");}
  if(CD_NAME("end_entry")){CD_TEXT("Stored exclusive end sequence-entry index; NewTrack compares the advanced entry pointer to this boundary.");}
  CD_TEXT("Stored first sequence-entry index; this is an index, not a file offset.");
 }
 if(id==sxcd_tme_instrument.id){
  if(CD_NAME("stored_sample_address")){CD_TEXT("Serialized32-bit runtime address slot, overwritten by InitData. Never followed as a file offset or synthesized pointer.");}
  if(CD_NAME("sample_byte_length")){CD_TEXT("Stored sample payload byte count. Sample data follows the256 instruction lists and remains raw.");}
  if(CD_NAME("initial_length_words")||CD_NAME("repeat_length_words")){CD_TEXT("Stored length in two-byte hardware words. The original playback path uses the low16 bits; the complete32-bit field is retained.");}
  if(CD_NAME("repeat_offset_words")){CD_TEXT("Stored sample-relative displacement in two-byte words; the player shifts it left once. This is not a byte offset or an absolute address.");}
  if(CD_NAME("offset_length_encoded")){CD_TEXT("Declared samp_offlen word. Its unit is not established by the preserved player; value remains unconverted.");}
  if(CD_NAME("sample_flags")){CD_TEXT("Declared samp_flags word; no named bit meanings are established by the preserved player.");}
  if(CD_NAME("name")){CD_TEXT("Fixed32-byte instrument name; a nonzero first byte indicates a stored sample payload to InitData.");}
  if(CD_NAME("volume")){CD_TEXT("Stored instrument base-volume byte; event volume_add is added before signed extension.");}
  if(CD_NAME("unused_byte")){CD_TEXT("Second byte of the two-byte inst_volume allocation; the player reads only the first byte.");}
  if(sx_wcsstr(n,L"_rate")){CD_TEXT("Stored envelope countdown; the player uses this rate plus one.");}
  CD_TEXT("Stored envelope target level byte for the corresponding attack, decay or release phase.");
 }
 if(id==sxcd_tme_instruction.id){
  if(CD_NAME("opcode")){
   static const wchar_t*const names[]={L"STOP",L"VIBRATO_LEVEL",L"VIBRATO_SPEED",L"TREMOLO_LEVEL",L"TREMOLO_SPEED",L"SPECIAL_LEVEL",L"SPECIAL_SPEED",L"DELAY",L"GOTO"};
   wchar_t text[192];const wchar_t*name=v>>4<SX_COUNT(names)?names[v>>4]:L"UNKNOWN";
   text[0]=0;sx_wappend(text,SX_COUNT(text),name);sx_wappend(text,SX_COUNT(text),L" (high nibble). Low nibble selects LFO for level commands. Exactly0x00 terminates a serialized list.");return xx_data_struct_record_set_comment(r,text);
  }
  if(CD_NAME("value")){CD_TEXT("Opcode-dependent level, speed or target instruction-list index. It is not a file offset.");}
  CD_TEXT("Stored instruction duration word; level/speed slides use this duration word plus one, while DELAY uses this word directly.");
 }
 if(id==sxcd_tme_arpeggio.id){
  if(CD_NAME("length_speed")){CD_TEXT("High nibble is arpeggio length (zero means8); low nibble is speed with countdown interval value+1.");}
  CD_TEXT("Eight stored pitch-step bytes; added with8-bit arithmetic to the base note, without interpreting runtime pitch.");
 }
 if(id==sxcd_tme_lfo.id){CD_TEXT("128 stored signed8-bit waveform samples; the player indexes them modulo128.");}
 if(id==sxcd_tme_counts.id){
  if(CD_NAME("entry_count")){CD_TEXT("Number of12-byte sequence entries immediately following this fixed header.");}
  if(CD_NAME("event_count")){CD_TEXT("Number of6-byte events after the sequence entries; this is not the number of32-row tracks.");}
  if(CD_NAME("instructions_word")){CD_TEXT("Stored song_instructions word. InitData nevertheless walks256 zero-terminated lists; this word is not used as their count.");}
  CD_TEXT("Fixed32-byte song name.");
 }
 if(id==sxcd_tme_entry.id){
  if(sx_wcsstr(n,L"_track")){CD_TEXT("Zero-based32-row track index into the event table; not a byte offset.");}
  CD_TEXT("Stored8-bit transposition/addition applied modulo256 by the player. Signed display preserves negative adjustments.");
 }
 if(id==sxcd_tme_event.id){
  if(CD_NAME("note")){CD_TEXT("Low7 bits:0=no note,126=RELEASE,127=VOICE_OFF; bit7 suppresses retriggering the envelope/sample.");}
  if(CD_NAME("sample")){CD_TEXT("Stored one-based instrument selector; zero keeps the current instrument.");}
  if(CD_NAME("effect_list")){CD_TEXT("Stored instruction-list index; zero selects the instrument's16 embedded instructions.");}
  if(CD_NAME("volume_add")){CD_TEXT("Signed8-bit volume adjustment; addition to instrument volume is modulo256 before signed extension.");}
  if(CD_NAME("flags")){CD_TEXT("ARPEGGIO=0x01|SIMPLEGLIDE=0x02|NONOTEADD=0x04|NOINSTADD=0x08|SUPERGLIDE=0x10|ARPONCE=0x20|ARPBLOW=0x40; bit7 is unknown.");}
  CD_TEXT("Opcode/flag-dependent parameter: signed8-bit increment for SIMPLEGLIDE, unsigned arpeggio index or glide duration otherwise.");
 }
#undef CD_NAME
#undef CD_TEXT
 return true;
}
const sx_family sx_classicdata_family={sx_classicdata_supports,sx_classicdata_build,sx_classicdata_layouts,SX_COUNT(sx_classicdata_layouts),sx_classicdata_comment};
