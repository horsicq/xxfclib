/* SPDX-License-Identifier: MIT
 * Images Music System physical metadata. Reference: retained UADE 3.05
 * Images Music System_v3.asm, Check2, InitPlayer, SampleInit, lbC0000DE. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxam37_images_title_fields[]={
 SX_FIELD("title","char[20]",0,20,SX_NONE),
};
static const sx_layout sxam37_images_title={0x60ac0001U,"images_title",sxam37_images_title_fields,SX_COUNT(sxam37_images_title_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_images_sample_fields[]={
 SX_FIELD("name","char[22]",0,22,SX_NONE),
 SX_FIELD("sample_length_words","uint16",22,2,SX_NONE),
 SX_FIELD("volume","uint16",24,2,SX_NONE),
 SX_FIELD("repeat_offset_words","uint16",26,2,SX_NONE),
 SX_FIELD("repeat_length_words","uint16",28,2,SX_NONE),
};
static const sx_layout sxam37_images_sample={0x60ac0002U,"images_sample",sxam37_images_sample_fields,SX_COUNT(sxam37_images_sample_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_images_sequence_fields[]={
 SX_FIELD("song_length","uint8",0,1,SX_COUNT_PROP),
 SX_FIELD("restart_position","uint8",1,1,SX_NONE),
};
static const sx_layout sxam37_images_sequence={0x60ac0003U,"images_sequence",sxam37_images_sequence_fields,SX_COUNT(sxam37_images_sequence_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_images_order_fields[]={
 SX_FIELD("pattern","uint8",0,1,SX_NONE),
};
static const sx_layout sxam37_images_order={0x60ac0004U,"images_order",sxam37_images_order_fields,SX_COUNT(sxam37_images_order_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_images_sample_data_fields[]={
 SX_FIELD("sample_data_offset","uint32",0,4,SX_OFFSET),
};
static const sx_layout sxam37_images_sample_data={0x60ac0005U,"images_sample_data",sxam37_images_sample_data_fields,SX_COUNT(sxam37_images_sample_data_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_images_event_fields[]={
 SX_FIELD("note_sample_high","uint8",0,1,SX_NONE),
 SX_FIELD("sample_low_effect","uint8",1,1,SX_NONE),
 SX_FIELD("effect_parameter","uint8",2,1,SX_NONE),
};
static const sx_layout sxam37_images_event={0x60ac0006U,"images_event",sxam37_images_event_fields,SX_COUNT(sxam37_images_event_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_paulshields_sample_compact_fields[]={
 SX_FIELD("reserved","bytes",0,2,SX_NONE),
 SX_FIELD("sample_length_words","uint16",2,2,SX_NONE),
 SX_FIELD("repeat_offset_bytes","int16",4,2,SX_OFFSET),
 SX_FIELD("repeat_length_words","uint16",6,2,SX_NONE),
 SX_FIELD("replay_parameter","uint16",8,2,SX_NONE),
};
static const sx_layout sxam37_paulshields_sample_compact={0x60ac0007U,"paulshields_sample_compact",sxam37_paulshields_sample_compact_fields,SX_COUNT(sxam37_paulshields_sample_compact_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_paulshields_sample_named_fields[]={
 SX_FIELD("name","char[22]",0,22,SX_NONE),
 SX_FIELD("sample_length_words","uint16",22,2,SX_NONE),
 SX_FIELD("replay_parameter","uint16",24,2,SX_NONE),
 SX_FIELD("repeat_offset_bytes","int16",26,2,SX_OFFSET),
 SX_FIELD("repeat_length_words","uint16",28,2,SX_NONE),
 SX_FIELD("replay_parameter_2","uint16",30,2,SX_NONE),
};
static const sx_layout sxam37_paulshields_sample_named={0x60ac0008U,"paulshields_sample_named",sxam37_paulshields_sample_named_fields,SX_COUNT(sxam37_paulshields_sample_named_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_paulshields_tables_fields[]={
 SX_FIELD("replay_table_1_offset","uint16",0,2,SX_OFFSET),
 SX_FIELD("replay_table_2_offset","uint16",2,2,SX_OFFSET),
};
static const sx_layout sxam37_paulshields_tables={0x60ac0009U,"paulshields_tables",sxam37_paulshields_tables_fields,SX_COUNT(sxam37_paulshields_tables_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_paulshields_table_fields[]={
 SX_FIELD("replay_table_offset","int16",0,2,SX_OFFSET),
};
static const sx_layout sxam37_paulshields_table={0x60ac000aU,"paulshields_table",sxam37_paulshields_table_fields,SX_COUNT(sxam37_paulshields_table_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_paulshields_channel_unsigned_fields[]={
 SX_FIELD("initial_state","uint16",0,2,SX_NONE),
 SX_FIELD("sequence_offset","uint16",2,2,SX_OFFSET),
};
static const sx_layout sxam37_paulshields_channel_unsigned={0x60ac000bU,"paulshields_channel_unsigned",sxam37_paulshields_channel_unsigned_fields,SX_COUNT(sxam37_paulshields_channel_unsigned_fields),SX_BE};
static const xx_data_struct_field_desc sxam37_paulshields_channel_signed_fields[]={
 SX_FIELD("initial_state","uint16",0,2,SX_NONE),
 SX_FIELD("sequence_offset","int16",2,2,SX_OFFSET),
};
static const sx_layout sxam37_paulshields_channel_signed={0x60ac000cU,"paulshields_channel_signed",sxam37_paulshields_channel_signed_fields,SX_COUNT(sxam37_paulshields_channel_signed_fields),SX_BE};
static const sx_layout*const sx_music37_amiga_layouts[]={&sxam37_images_title,&sxam37_images_sample,&sxam37_images_sequence,&sxam37_images_order,&sxam37_images_sample_data,&sxam37_images_event,&sxam37_paulshields_sample_compact,&sxam37_paulshields_sample_named,&sxam37_paulshields_tables,&sxam37_paulshields_table,&sxam37_paulshields_channel_unsigned,&sxam37_paulshields_channel_signed};
static bool sx_music37_amiga_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_CROSSLEY_IMAGESMUSICSYSTEM||t==XX_FILE_TYPE_DIE_MUSIC_PAULSHIELDS;}
static bool sxam37_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p;}
static bool sxam37_read(sx_context*c,uint64_t p,void*out,size_t n){return sxam37_span(c,p,n)&&sx_read(c,(int64_t)p,out,n);}
static uint16_t sxam37_be16(const uint8_t*b){return(uint16_t)(((uint16_t)b[0]<<8)|b[1]);}
static uint32_t sxam37_be32(const uint8_t*b){return((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];}
static bool sxam37_images(sx_context*c){
 uint8_t h[6],orders[128],b[8];
 if(!sxam37_span(c,0,1852)||!sxam37_read(c,950,h,2)||!sxam37_read(c,952,orders,128)||!sxam37_read(c,1080,h+2,4))return false;
 uint64_t p=sxam37_be32(h+2);unsigned song=h[0];
 /* Check2 uses DBF after loading song_length and therefore inspects one
  * additional physical order byte. Preserve that guard while displaying all
  * 128 entries of the fixed order directory. */
 if(song>127||p<=1084||(p-1084)%768||!sxam37_span(c,0,p))return false;
 unsigned max=0;for(unsigned i=0;i<=song;i++){if(orders[i]>127)return false;if(orders[i]>max)max=orders[i];}
 if((p-1084)/768!=(uint64_t)max+1)return false;
 uint64_t samples=0;
 for(unsigned i=0;i<31;i++){if(!sxam37_read(c,20+30*i+22,b,8))return false;samples+=(uint64_t)sxam37_be16(b)*2;}
 if(!sxam37_span(c,p,samples))return false;
 return sx_add(c,&sxam37_images_title,0,20)&&sx_table(c,&sxam37_images_sample,20,30,31)&&sx_add(c,&sxam37_images_sequence,950,2)&&sx_table(c,&sxam37_images_order,952,1,128)&&sx_add(c,&sxam37_images_sample_data,1080,4)&&sx_table(c,&sxam37_images_event,1084,3,(int64_t)((p-1084)/3))&&(p==(uint64_t)c->size||sx_raw(c,(int64_t)p,c->size-(int64_t)p));
}
static bool sxam37_paulshields(sx_context*c){
 uint8_t h[532],b[4];if(!sxam37_read(c,0,h,sizeof(h)))return false;
 for(unsigned i=0;i<10;i++)if(h[i])return false;
 int variant=99;uint64_t hs=0,ch=0;uint16_t q=sxam37_be16(h+160);
 if(q&&q<32768&&!(q&1)&&sxam37_be16(h+164)==sxam37_be16(h+168)&&sxam37_be16(h+164)==sxam37_be16(h+172)&&sxam37_be16(h+164)==sxam37_be16(h+176)&&sxam37_span(c,q,4)){
  if(!sxam37_read(c,q,b,4))return false;if(sxam37_be32(b)==0x00b400b6U){variant=1;hs=160;ch=164;}
 }
 if(variant==99){q=sxam37_be16(h+512);if(q&&q<32768&&!(q&1)&&sxam37_be16(h+516)==sxam37_be16(h+520)&&sxam37_be16(h+516)==sxam37_be16(h+524)&&sxam37_be16(h+516)==sxam37_be16(h+528)&&sxam37_span(c,q,4)){
  if(!sxam37_read(c,q,b,4))return false;if(sxam37_be32(b)==0x02140216U){variant=-1;hs=512;ch=516;}
 }}
 if(variant==99){q=sxam37_be16(h+516);if(q&&q<32768&&!(q&1)&&sxam37_be16(h+514)==sxam37_be16(h+518)&&sxam37_be16(h+514)==sxam37_be16(h+522)&&sxam37_be16(h+514)==sxam37_be16(h+526)&&q>=2&&sxam37_span(c,q-2,2)){
  if(!sxam37_read(c,q-2,b,2))return false;uint16_t stop=sxam37_be16(b);if(stop==0xffec||stop==0xffe8){variant=0;hs=512;ch=514;}
 }}
 if(variant==99)return false;
 uint64_t max=0;for(unsigned i=0;i<4;i++){uint16_t v=sxam37_be16(h+ch+2+4*i);if((variant==0&&v>=32768)||(v&1)||v<ch+16||!sxam37_span(c,v,2))return false;if(v>max)max=v;}
 if(max<2||!sxam37_read(c,max-2,b,2))return false;uint16_t stop=sxam37_be16(b);uint64_t end=max;
 do{if(!sxam37_read(c,end,b,2))return false;end+=2;}while(sxam37_be16(b)!=stop);
 uint64_t sample_bytes=0;for(unsigned i=0;i<16;i++)sample_bytes+=(uint64_t)sxam37_be16(h+i*(variant==1?10:32)+(variant==1?2:22))*2;
 if(!sxam37_span(c,end,sample_bytes))return false;
 return sx_table(c,variant==1?&sxam37_paulshields_sample_compact:&sxam37_paulshields_sample_named,0,variant==1?10:32,16)&&sx_add(c,variant==0?&sxam37_paulshields_table:&sxam37_paulshields_tables,(int64_t)hs,variant==0?2:4)&&sx_table(c,variant==0?&sxam37_paulshields_channel_signed:&sxam37_paulshields_channel_unsigned,(int64_t)ch,4,4)&&sx_raw(c,(int64_t)(ch+16),c->size-(int64_t)(ch+16));
}
/* A native-valid replay revision can have incomplete or unsupported physical
 * metadata. Preserve its bytes only if no records were emitted, no read failed,
 * and a real sentinel read succeeds. Cancellation must never become raw success. */
static bool sxam37_raw(sx_context*c){uint8_t v;if(!c||c->count||c->failed||c->size<=0||xx_pd_is_stopped(c->pd)||!sxam37_read(c,0,&v,1)||xx_pd_is_stopped(c->pd))return false;return sx_raw(c,0,c->size);}
static bool sx_music37_amiga_build(sx_context*c){
 if(!c||!c->f||!sx_music37_amiga_supports(c->f->file_type))return false;
 bool ok=c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_PAULSHIELDS?sxam37_paulshields(c):sxam37_images(c);
 if(!ok)ok=sxam37_raw(c);
 return ok&&!c->failed&&!xx_pd_is_stopped(c->pd);
}
static bool sx_music37_amiga_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60ac0001U||id>0x60ac000cU||r->value.type==XX_VAR_TYPE_FLOAT||r->value.type==XX_VAR_TYPE_DOUBLE)return true;
 const wchar_t*s=NULL;
 if(id==0x60ac0002U){if(!xx_rt_wcscmp(r->name,L"sample_length_words")||!xx_rt_wcscmp(r->name,L"repeat_length_words"))s=L"Length in two-byte audio words; multiply by two for bytes.";else if(!xx_rt_wcscmp(r->name,L"repeat_offset_words"))s=L"Loop start in two-byte words relative to this sample; this is a word index, not a file byte offset.";else if(!xx_rt_wcscmp(r->name,L"volume"))s=L"Initial 16-bit volume value copied to the replay sample descriptor.";}
 else if(id==0x60ac0003U){if(!xx_rt_wcscmp(r->name,L"song_length"))s=L"Number of active order positions; Check2 also checks the following order byte.";else s=L"Order position selected when playback reaches the song length.";}
 else if(id==0x60ac0004U)s=L"Zero-based pattern number in the fixed 128-byte order directory.";
 else if(id==0x60ac0005U)s=L"Byte offset of concatenated PCM sample data relative to the module start; patterns begin at 0x43C and occupy 0x300 bytes each.";
 else if(id==0x60ac0006U){if(!xx_rt_wcscmp(r->name,L"note_sample_high"))s=L"Low six bits encode the note; high two bits provide sample number bits 4 and 5.";else if(!xx_rt_wcscmp(r->name,L"sample_low_effect"))s=L"High nibble contains sample number bits 0 to 3; low nibble contains the effect command.";else s=L"Eight-bit effect argument paired with the low nibble of sample_low_effect.";}
 if(id==0x60ac0007U||id==0x60ac0008U){if(sx_wcsstr(r->name,L"length_words"))s=L"Length in two-byte PCM words; multiply by two for bytes.";else if(!xx_rt_wcscmp(r->name,L"repeat_offset_bytes"))s=L"Signed byte displacement from the sample start, added by the replay using ADDA.W.";else if(!xx_rt_wcscmp(r->name,L"replay_parameter")||!xx_rt_wcscmp(r->name,L"replay_parameter_2"))s=L"Physical replay parameter copied unchanged into the replay sample descriptor.";else if(!xx_rt_wcscmp(r->name,L"reserved"))s=L"Two-byte sample metadata prefix; its meaning is not established by the retained replay.";}
 else if(id==0x60ac0009U||id==0x60ac000aU)s=L"Replay table byte offset relative to the module start; InitSong stores the relocated pointer in its channel-independent state.";
 else if(id==0x60ac000bU||id==0x60ac000cU){if(!xx_rt_wcscmp(r->name,L"initial_state"))s=L"Initial channel state word copied to the replay's two channel counters.";else s=L"Sequence byte offset relative to the module start. The final replay variant adds this value with signed ADDA.W.";}
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_music37_amiga_family={sx_music37_amiga_supports,sx_music37_amiga_build,sx_music37_amiga_layouts,SX_COUNT(sx_music37_amiga_layouts),sx_music37_amiga_comment};
