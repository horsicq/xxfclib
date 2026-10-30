/* SPDX-License-Identifier: MIT
 * Source-backed partial Dave Lowe New and Steve Barrett physical metadata. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxam38_dln_earlier_header_fields[]={
 SX_FIELD("channel_table_base_offset","uint16",0,2,SX_OFFSET),
 SX_FIELD("subsong_boundary_offset","uint16",2,2,SX_OFFSET),
};
static const sx_layout sxam38_dln_earlier_header={0x60af0001U,"dln_earlier_header",sxam38_dln_earlier_header_fields,SX_COUNT(sxam38_dln_earlier_header_fields),SX_BE};
static const xx_data_struct_field_desc sxam38_dln_later_header_fields[]={
 SX_FIELD("channel_table_offset","uint16",0,2,SX_OFFSET),
 SX_FIELD("secondary_word","uint16",2,2,SX_NONE),
};
static const sx_layout sxam38_dln_later_header={0x60af0002U,"dln_later_header",sxam38_dln_later_header_fields,SX_COUNT(sxam38_dln_later_header_fields),SX_BE};
static const xx_data_struct_field_desc sxam38_dln_earlier_song_fields[]={
 SX_FIELD("initial_replay_word","uint32",0,4,SX_NONE),
 SX_FIELD("channel_1_offset","uint32",4,4,SX_OFFSET),
 SX_FIELD("channel_2_offset","uint32",8,4,SX_OFFSET),
 SX_FIELD("channel_3_offset","uint32",12,4,SX_OFFSET),
 SX_FIELD("channel_4_offset","uint32",16,4,SX_OFFSET),
 SX_FIELD("reserved","bytes",20,12,SX_NONE),
};
static const sx_layout sxam38_dln_earlier_song={0x60af0003U,"dln_earlier_song",sxam38_dln_earlier_song_fields,SX_COUNT(sxam38_dln_earlier_song_fields),SX_BE};
static const xx_data_struct_field_desc sxam38_dln_later_song_fields[]={
 SX_FIELD("channel_1_offset","uint32",0,4,SX_OFFSET),
 SX_FIELD("channel_2_offset","uint32",4,4,SX_OFFSET),
 SX_FIELD("channel_3_offset","uint32",8,4,SX_OFFSET),
 SX_FIELD("channel_4_offset","uint32",12,4,SX_OFFSET),
};
static const sx_layout sxam38_dln_later_song={0x60af0004U,"dln_later_song",sxam38_dln_later_song_fields,SX_COUNT(sxam38_dln_later_song_fields),SX_BE};
static const xx_data_struct_field_desc sxam38_dln_sample_fields[]={
 SX_FIELD("kind","uint16",0,2,SX_ID),
 SX_FIELD("sample_offset","uint32",2,4,SX_OFFSET),
 SX_FIELD("sample_length_words","uint16",6,2,SX_NONE),
 SX_FIELD("repeat_offset","uint32",8,4,SX_OFFSET),
 SX_FIELD("repeat_length_words","uint16",12,2,SX_NONE),
};
static const sx_layout sxam38_dln_sample={0x60af0005U,"dln_sample",sxam38_dln_sample_fields,SX_COUNT(sxam38_dln_sample_fields),SX_BE};
static const xx_data_struct_field_desc sxam38_barrett_form_fields[]={
 SX_FIELD("signature","char[4]",0,4,SX_SIGNATURE),
 SX_FIELD("form_size","uint32",4,4,SX_SIZE),
 SX_FIELD("form_type","char[4]",8,4,SX_ID),
};
static const sx_layout sxam38_barrett_form={0x60af0006U,"barrett_form",sxam38_barrett_form_fields,SX_COUNT(sxam38_barrett_form_fields),SX_BE};
static const xx_data_struct_field_desc sxam38_barrett_chunk_fields[]={
 SX_FIELD("chunk_id","char[4]",0,4,SX_ID),
 SX_FIELD("chunk_size","uint32",4,4,SX_SIZE),
};
static const sx_layout sxam38_barrett_chunk={0x60af0007U,"barrett_chunk",sxam38_barrett_chunk_fields,SX_COUNT(sxam38_barrett_chunk_fields),SX_BE};
static const xx_data_struct_field_desc sxam38_barrett_sample_name_fields[]={
 SX_FIELD("name","char[20]",0,20,SX_STRING),
};
static const sx_layout sxam38_barrett_sample_name={0x60af0008U,"barrett_sample_name",sxam38_barrett_sample_name_fields,SX_COUNT(sxam38_barrett_sample_name_fields),SX_BE};
static const sx_layout*const sx_music38_amiga_layouts[]={&sxam38_dln_earlier_header,&sxam38_dln_later_header,&sxam38_dln_earlier_song,&sxam38_dln_later_song,&sxam38_dln_sample,&sxam38_barrett_form,&sxam38_barrett_chunk,&sxam38_barrett_sample_name};
static bool sx_music38_amiga_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_DAVELOWE_NEW||t==XX_FILE_TYPE_DIE_MUSIC_STEVEBARRETT;}
static bool sxam38_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p;}
static bool sxam38_read(sx_context*c,uint64_t p,void*out,size_t n){return sxam38_span(c,p,n)&&sx_read(c,(int64_t)p,out,n);}
static uint16_t sxam38_be16(const uint8_t*b){return(uint16_t)(((uint16_t)b[0]<<8)|b[1]);}
static uint32_t sxam38_be32(const uint8_t*b){return((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];}
static bool sxam38_u16(sx_context*c,uint64_t p,uint16_t*v){uint8_t b[2];if(!sxam38_read(c,p,b,2))return false;*v=sxam38_be16(b);return true;}
static bool sxam38_u32(sx_context*c,uint64_t p,uint32_t*v){uint8_t b[4];if(!sxam38_read(c,p,b,4))return false;*v=sxam38_be32(b);return true;}
static bool sxam38_gap(sx_context*c,uint64_t*end,uint64_t p){if(p<*end||!sxam38_span(c,*end,p-*end))return false;if(p>*end&&!sx_raw(c,(int64_t)*end,(int64_t)(p-*end)))return false;*end=p;return true;}
/* All scanning loops issue sx_read calls, which check cancellation. Explicit
 * budgets bound native-valid unusual revisions before any fields are emitted:
 * 16384 later channel pointers,4096 backward/forward samples,32768 IFF chunks. */
static bool sxam38_dln(sx_context*c){
 uint8_t h[28],b[14];if(!sxam38_read(c,0,h,sizeof(h)))return false;
 uint16_t base=sxam38_be16(h),second=sxam38_be16(h+2);if(base!=4&&base!=8)return false;
 bool earlier=sxam38_be32(h+24)==0;
 /* Check2 and InitSound disagree for the earlier format with base8. Only
  * base4 has a proved table start8; leave the other native-valid form raw. */
 if(earlier&&base!=4)return false;
 uint64_t ch=earlier?8:base,songs=0,width=earlier?32:16;
 if(earlier){if(second<40||(second-8)%32)return false;songs=(second-8)/32;}
 else{uint64_t n=0;uint32_t v;
  for(uint64_t p=ch;sxam38_span(c,p,4);p+=4){if(!sxam38_u32(c,p,&v))return false;if(!v||v>=32768||(v&1))break;if(n>=16384)return false;n++;}
  if(!n||n%4)return false;songs=n/4;
 }
 if(!songs||!sxam38_span(c,ch,width*songs))return false;uint64_t chend=ch+width*songs;
 /* Check2 follows the first long in each channel's order sequence until it
  * finds the source's table command12 followed by sample command4. */
 uint64_t anchor=0;
 for(unsigned k=0;k<4;k++){
  uint32_t seq,command;if(!sxam38_u32(c,ch+4*k,&seq))return false;if(seq<chend||(seq&1)||!sxam38_u32(c,seq,&command))return false;uint64_t q=command;
  uint16_t op;if((q&1)||!sxam38_u16(c,q,&op))return false;if(op==0x30){q+=2;if(!sxam38_u16(c,q,&op))return false;}
  if(op!=12)continue;if(!sxam38_read(c,q,b,12))return false;
  if(sxam38_be16(b+6)==4){anchor=sxam38_be32(b+8);break;}
 }
 uint16_t anchor_tag;if(!anchor||anchor<chend||!sxam38_u16(c,anchor,&anchor_tag)||anchor_tag!=1)return false;
 uint64_t first=anchor;uint16_t tag;unsigned reverse_steps=0;
 while(first>=14){if(!sxam38_u16(c,first-14,&tag))return false;if(tag!=1)break;if(reverse_steps++>=4096)return false;first-=14;}
 uint16_t prev;if(first<2||!sxam38_u16(c,first-2,&prev))return false;if(prev!=0&&prev!=8)first+=14;
 if(first<chend)return false;uint64_t count=0,p=first;
 while(sxam38_span(c,p,2)){
  if(!sxam38_u16(c,p,&tag))return false;if(tag!=1)break;if(count>=4096||!sxam38_read(c,p,b,14))return false;
  uint64_t so=sxam38_be32(b+2),sn=(uint64_t)sxam38_be16(b+6)*2,ro=sxam38_be32(b+8),rn=(uint64_t)sxam38_be16(b+12)*2;
  if(!sxam38_span(c,so,sn)||!sxam38_span(c,ro,rn))return false;p+=14;count++;
 }
 if(!count||!sxam38_span(c,p,2))return false;
 uint64_t end=0;if(!sx_add(c,earlier?&sxam38_dln_earlier_header:&sxam38_dln_later_header,0,4))return false;end=4;
 uint64_t directory=earlier?base:ch;
 if(!sxam38_gap(c,&end,directory)||!sx_table(c,earlier?&sxam38_dln_earlier_song:&sxam38_dln_later_song,(int64_t)directory,(int64_t)width,songs))return false;end=directory+width*songs;
 if(!sxam38_gap(c,&end,first)||!sx_table(c,&sxam38_dln_sample,(int64_t)first,14,count))return false;end=p;
 return sxam38_gap(c,&end,(uint64_t)c->size);
}
/* The original Barrett ripper advances by104+BODY length, while the loader
 * exports the20-byte sample name at+48. Decode the framed IFF headers inside
 * those proven sample extents and leave voice payload/PCM bytes raw. */
static bool sxam38_barrett_walk(sx_context*c,uint64_t sample,bool emit){
 uint64_t p=sample,end=0;unsigned samples=0,chunks=0;
 if(emit&&!sxam38_gap(c,&end,p))return false;
 while(sxam38_span(c,p,4)){
  uint8_t h[104];uint32_t magic;if(!sxam38_u32(c,p,&magic))return false;if(magic!=0x464f524dU)break;
  if(!sxam38_read(c,p,h,104)||sxam38_be32(h+8)!=0x38535658U||sxam38_be32(h+12)!=0x56484452U||sxam38_be32(h+96)!=0x424f4459U)return false;
  uint64_t length=sxam38_be32(h+100),total=104+length,form=sxam38_be32(h+4);
  if(form+8!=total||!sxam38_span(c,p,total)||samples>=4096)return false;
  if(emit){if(!sx_add(c,&sxam38_barrett_form,(int64_t)p,12))return false;end=p+12;}
  uint64_t q=p+12,stop=p+total;
  while(q<stop){if(chunks++>=32768)return false;uint8_t ch[8];if(stop-q<8||!sxam38_read(c,q,ch,8))return false;uint64_t n=sxam38_be32(ch+4),padded=n+(n&1);if(padded>stop-q-8)return false;
   if(emit){if(!sxam38_gap(c,&end,q)||!sx_add(c,&sxam38_barrett_chunk,(int64_t)q,8))return false;end=q+8;
    if(q==p+40&&sxam38_be32(ch)==0x4e414d45U&&n>=20){if(!sx_add(c,&sxam38_barrett_sample_name,(int64_t)end,20))return false;end+=20;}
    if(!sxam38_gap(c,&end,q+8+padded))return false;
   }
   q+=8+padded;
  }
  if(q!=stop)return false;samples++;p=stop;
 }
 if(!samples)return false;return !emit||sxam38_gap(c,&end,(uint64_t)c->size);
}
static bool sxam38_barrett(sx_context*c){
 uint8_t h[16];if(!sxam38_read(c,0,h,16))return false;for(unsigned i=0;i<4;i++)if(sxam38_be16(h+4*i)!=0x6000)return false;
 int64_t init=2+(int16_t)sxam38_be16(h+2);if(init<16||!sxam38_span(c,(uint64_t)init,16))return false;
 uint64_t sample=0;
 for(uint64_t p=(uint64_t)init;p<(uint64_t)init+16;p+=2){uint8_t b[4];if(!sxam38_read(c,p,b,4))return false;if(sxam38_be16(b)==0x41fa){int64_t target=(int64_t)p+2+(int16_t)sxam38_be16(b+2);if(target<0)return false;sample=(uint64_t)target;break;}}
 return sample&&sxam38_barrett_walk(c,sample,false)&&sxam38_barrett_walk(c,sample,true);
}
static bool sxam38_raw(sx_context*c){uint8_t b;if(!c||c->count||c->failed||c->size<=0||xx_pd_is_stopped(c->pd)||!sxam38_read(c,0,&b,1)||xx_pd_is_stopped(c->pd))return false;return sx_raw(c,0,c->size);}
static bool sx_music38_amiga_build(sx_context*c){if(!c||!c->f||!sx_music38_amiga_supports(c->f->file_type))return false;bool ok=c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_DAVELOWE_NEW?sxam38_dln(c):sxam38_barrett(c);if(!ok)ok=sxam38_raw(c);return ok&&!c->failed&&!xx_pd_is_stopped(c->pd);}
static bool sx_music38_amiga_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60af0001U||id>0x60af0008U)return true;const wchar_t*s=NULL;
 if(id==0x60af0001U){if(!xx_rt_wcscmp(r->name,L"channel_table_base_offset"))s=L"Stored byte base used by the earlier replay; InitSound adds four to find the first channel table.";else s=L"Stored module-relative boundary offset; InitPlayer derives the earlier subsong count as (value-8)/32.";}
 else if(id==0x60af0002U){if(!xx_rt_wcscmp(r->name,L"channel_table_offset"))s=L"Byte offset of the later replay's contiguous four-channel subsong directory, relative to the module start.";else s=L"Second physical header word; no additional semantic interpretation is inferred for this replay variant.";}
 else if(id==0x60af0003U||id==0x60af0004U){if(!xx_rt_wcscmp(r->name,L"reserved"))s=L"Twelve bytes remaining after the earlier replay control word and four channel offsets; preserved without a claimed musical meaning.";else if(!xx_rt_wcscmp(r->name,L"initial_replay_word"))s=L"Unrelocated initial replay control word copied to lbL0097D0 by Init_2; no file-offset interpretation is inferred.";else s=L"Byte offset of this channel's sequence of 32-bit pattern offsets, relative to the module start.";}
 else if(id==0x60af0005U){if(!xx_rt_wcscmp(r->name,L"kind"))s=L"Stored tag1 identifies a sampled-instrument record; the loader counts consecutive14-byte records with this value.";else if(sx_wcsstr(r->name,L"length_words"))s=L"Length in two-byte PCM words; multiply by two for bytes.";else s=L"Byte offset of sample or repeat PCM data relative to the module start; preserved before the replay adds its load base.";}
 else if(id==0x60af0006U){if(!xx_rt_wcscmp(r->name,L"form_size"))s=L"Big-endian byte length following the eight-byte FORM identifier and size. The Barrett ripper advances by104 plus the BODY byte length.";else if(!xx_rt_wcscmp(r->name,L"form_type"))s=L"8SVX sampled-audio form carried by this Barrett module.";else s=L"FORM identifier of a physical sampled-audio container.";}
 else if(id==0x60af0007U){if(!xx_rt_wcscmp(r->name,L"chunk_id"))s=L"Physical chunk identifier within this sample's FORM, including VHDR, NAME and BODY.";else s=L"Big-endian payload byte length, excluding the eight-byte chunk header; odd payloads require one padding byte.";}
 else if(id==0x60af0008U)s=L"First twenty name bytes at sample offset+48, exported by the original Barrett SampleInit routine.";
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_music38_amiga_family={sx_music38_amiga_supports,sx_music38_amiga_build,sx_music38_amiga_layouts,SX_COUNT(sx_music38_amiga_layouts),sx_music38_amiga_comment};
