/* SPDX-License-Identifier: MIT
 * Original-loader physical subsong and sample metadata, without replay-code headers. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxam39_sean_song_fields[]={
 SX_FIELD("sequence_offset","uint16",0,2,SX_OFFSET),
 SX_FIELD("replay_data","bytes",2,6,SX_NONE),
};
static const sx_layout sxam39_sean_song={0x60b20001U,"sean_song",sxam39_sean_song_fields,SX_COUNT(sxam39_sean_song_fields),SX_BE};
static const xx_data_struct_field_desc sxam39_ashley_song_fields[]={
 SX_FIELD("channel_1_word","uint32",0,4,SX_NONE),
 SX_FIELD("channel_2_word","uint32",4,4,SX_NONE),
 SX_FIELD("channel_3_word","uint32",8,4,SX_NONE),
 SX_FIELD("channel_4_word","uint32",12,4,SX_NONE),
};
static const sx_layout sxam39_ashley_song={0x60b20002U,"ashley_song",sxam39_ashley_song_fields,SX_COUNT(sxam39_ashley_song_fields),SX_BE};
static const xx_data_struct_field_desc sxam39_ashley_sample_fields[]={
 SX_FIELD("replay_data_1","bytes",0,32,SX_NONE),
 SX_FIELD("sample_offset","int32",32,4,SX_OFFSET),
 SX_FIELD("replay_data_2","bytes",36,4,SX_NONE),
 SX_FIELD("sample_length_words","uint16",40,2,SX_NONE),
 SX_FIELD("replay_data_3","bytes",42,2,SX_NONE),
};
static const sx_layout sxam39_ashley_sample={0x60b20003U,"ashley_sample",sxam39_ashley_sample_fields,SX_COUNT(sxam39_ashley_sample_fields),SX_BE};
static const sx_layout*const sx_music39_amiga_layouts[]={&sxam39_sean_song,&sxam39_ashley_song,&sxam39_ashley_sample};
static bool sx_music39_amiga_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_SEANCONRAN_SCR||t==XX_FILE_TYPE_DIE_MUSIC_ASHLEYHOGG_ASH;}
static bool sxam39_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p;}
static bool sxam39_read(sx_context*c,uint64_t p,void*out,size_t n){return sxam39_span(c,p,n)&&sx_read(c,(int64_t)p,out,n);}
static uint16_t sxam39_be16(const uint8_t*b){return(uint16_t)(((uint16_t)b[0]<<8)|b[1]);}
static uint32_t sxam39_be32(const uint8_t*b){return((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];}
static bool sxam39_u16(sx_context*c,uint64_t p,uint16_t*v){uint8_t b[2];if(!sxam39_read(c,p,b,2))return false;*v=sxam39_be16(b);return true;}
static bool sxam39_relative(sx_context*c,uint64_t p,uint64_t*out){uint16_t w;if(!sxam39_u16(c,p,&w))return false;uint64_t target;if(w&0x8000U){uint64_t magnitude=65536U-w;if(p<magnitude)return false;target=p-magnitude;}else target=p+w;if(!sxam39_span(c,target,1))return false;*out=target;return true;}
/* All opcode searches only locate original data pointers. They are bounded
 * source-qualified searches; no executable bytes are emitted as data fields.
 * Every read checks cancellation, and all profiles preflight before emission. */
static bool sxam39_find(sx_context*c,uint64_t p,uint16_t needle,uint64_t*out){
 for(unsigned steps=0;steps<4096&&sxam39_span(c,p,2);steps++,p+=2){uint16_t v;if(!sxam39_u16(c,p,&v))return false;if(v==needle){*out=p;return true;}}
 return false;
}
static bool sxam39_gap(sx_context*c,uint64_t*end,uint64_t p){if(p<*end||!sxam39_span(c,*end,p-*end))return false;if(p>*end&&!sx_raw(c,(int64_t)*end,(int64_t)(p-*end)))return false;*end=p;return true;}
static bool sxam39_sean(sx_context*c){
 uint8_t h[12];if(!sxam39_read(c,0,h,12))return false;uint64_t p;
 if((sxam39_be32(h)==0x0fff0fe2U||sxam39_be32(h)==0x10000fe2U)&&sxam39_be32(h+4)==0x0fc40fa7U&&sxam39_be32(h+8)==0x0f8b0f6eU)p=292;
 else if(sxam39_be32(h)==0x0f1c0f0eU&&sxam39_be32(h+4)==0x0f000ef2U&&sxam39_be32(h+8)==0x0ee40ed6U)p=452;
 else return false;
 bool found=false;uint16_t op;
 for(unsigned steps=0;steps<4096&&sxam39_span(c,p,4);steps++,p+=2){uint8_t b[4];if(!sxam39_read(c,p,b,4))return false;op=sxam39_be16(b);if(op==0x45fa||op==0x43f9||op==0x41f9){found=true;p+=2;break;}if(!sxam39_be32(b))return false;}
 if(!found)return false;found=false;
 for(unsigned steps=0;steps<4096&&sxam39_span(c,p,2);steps++,p+=2){if(!sxam39_u16(c,p,&op))return false;if(op==0x7000)return false;/* Single-song source has no authenticated framed song table. */if(op==0x7200){found=true;p+=2;break;}}
 if(!found)return false;uint64_t table=0;
 for(unsigned steps=0;steps<4096&&sxam39_span(c,p,4);steps++,p+=2){uint8_t b[4];if(!sxam39_read(c,p,b,4))return false;op=sxam39_be16(b);if(!sxam39_be32(b)||sxam39_be32(b)==0x21c80070U)return false;/* Relocated interrupt/Twin profiles remain raw. */
  if(op==0x43fa){if(!sxam39_relative(c,p+2,&table))return false;break;}
  if(op==0x43ea){int16_t v=(int16_t)sxam39_be16(b+2);if(v<=0)return false;table=(uint16_t)v;break;}
 }
 uint16_t first;if(!table||!sxam39_u16(c,table,&first)||first>=32768||first<=table||(first-table)%8)return false;
 uint64_t count=(first-table)/8;if(!count||count>4096||!sxam39_span(c,table,count*8))return false;
 unsigned bytes_seen=0;
 for(uint64_t i=0;i<count;i++){
  uint16_t off;if(!sxam39_u16(c,table+i*8,&off)||off>=32768||off<first||!sxam39_span(c,off,1))return false;bool terminated=false;
  for(uint64_t q=off;sxam39_span(c,q,1)&&bytes_seen<65536;q++,bytes_seen++){uint8_t b;if(!sxam39_read(c,q,&b,1))return false;if(b==0xff){bytes_seen++;terminated=true;break;}}
  if(!terminated)return false;
 }
 uint64_t end=0;if(!sxam39_gap(c,&end,table)||!sx_table(c,&sxam39_sean_song,(int64_t)table,8,count))return false;end=table+count*8;return sxam39_gap(c,&end,(uint64_t)c->size);
}
static bool sxam39_ashley(sx_context*c){
 uint8_t h[24];if(!sxam39_read(c,0,h,24))return false;
 for(unsigned i=0;i<4;i++)if(sxam39_be16(h+4*i)!=0x6000)return false;
 /* Old-only profile, without the later external sample-file/base relocation. */
 if(sxam39_be32(h+16)!=0x303c0000U||sxam39_be32(h+20)!=0x662233c0U)return false;
 uint64_t init,songs_end_op,songs_op,songs,songs_end;
 if(!sxam39_relative(c,2,&init)||!sxam39_find(c,init,0x1970,&songs_end_op)||songs_end_op<2||!sxam39_relative(c,songs_end_op-2,&songs_end)||!sxam39_find(c,songs_end_op+2,0x41fa,&songs_op)||!sxam39_relative(c,songs_op+2,&songs))return false;
 if(songs_end<=songs||(songs_end-songs)%16)return false;uint64_t song_count=(songs_end-songs)/16;if(song_count>2048||!sxam39_span(c,songs,song_count*16))return false;
 uint64_t info_op,info,pcm_op,pcm,end1_op,end2_op,info_end;
 if(!sxam39_find(c,16,0xc2fc,&info_op)||!sxam39_relative(c,info_op+6,&info)||!sxam39_find(c,info_op+6,0x47fa,&pcm_op)||!sxam39_relative(c,pcm_op+2,&pcm)||!sxam39_find(c,pcm_op+2,0x49fa,&end1_op)||!sxam39_find(c,end1_op+2,0x49fa,&end2_op)||!sxam39_relative(c,end2_op+2,&info_end))return false;
 if(info<songs_end||info_end<=info||(info_end-info)%44||pcm<info_end)return false;uint64_t samples=(info_end-info)/44;if(samples>4096||!sxam39_span(c,info,samples*44))return false;
 for(uint64_t i=0;i<samples;i++){
  uint8_t b[12];if(!sxam39_read(c,info+i*44+32,b,12))return false;int32_t off=(int32_t)sxam39_be32(b);uint64_t bytes=(uint64_t)sxam39_be16(b+8)*2;
  if(off>=0&&(!sxam39_span(c,pcm,(uint32_t)off)||!sxam39_span(c,pcm+(uint32_t)off,bytes)))return false;
 }
 uint64_t end=0;if(!sxam39_gap(c,&end,songs)||!sx_table(c,&sxam39_ashley_song,(int64_t)songs,16,song_count))return false;end=songs_end;
 if(!sxam39_gap(c,&end,info)||!sx_table(c,&sxam39_ashley_sample,(int64_t)info,44,samples))return false;end=info_end;return sxam39_gap(c,&end,(uint64_t)c->size);
}
static bool sxam39_raw(sx_context*c){uint8_t b;if(!c||c->count||c->failed||c->size<=0||xx_pd_is_stopped(c->pd)||!sxam39_read(c,0,&b,1)||xx_pd_is_stopped(c->pd))return false;return sx_raw(c,0,c->size);}
static bool sx_music39_amiga_build(sx_context*c){if(!c||!c->f||!sx_music39_amiga_supports(c->f->file_type))return false;bool ok=c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_SEANCONRAN_SCR?sxam39_sean(c):sxam39_ashley(c);if(!ok)ok=sxam39_raw(c);return ok&&!c->failed&&!xx_pd_is_stopped(c->pd);}
static bool sx_music39_amiga_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60b20001U||id>0x60b20003U)return true;const wchar_t*s=NULL;
 if(id==0x60b20001U){if(!xx_rt_wcscmp(r->name,L"sequence_offset"))s=L"Module-relative byte offset of the selected song's initial sequence, loaded by InitSound before scanning for its0xFF terminator.";else s=L"Six physical bytes remaining in the source's8-byte subsong record; no musical meaning is inferred.";}
 else if(id==0x60b20002U)s=L"Physical32-bit per-channel value in the old replay's16-byte song record. InitSound tests each value for zero; no unproved address relocation is applied.";
 else if(id==0x60b20003U){if(!xx_rt_wcscmp(r->name,L"sample_offset"))s=L"Signed byte offset relative to the PCM sample-data base located by the original old replay. Negative values are skipped by SampleInit; this is not a module-relative byte offset.";else if(!xx_rt_wcscmp(r->name,L"sample_length_words"))s=L"PCM length in two-byte audio words; the original SampleInit doubles this stored value.";else s=L"Physical instrument metadata preserved as bytes because its musical meaning is not established by the retained loader.";}
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_music39_amiga_family={sx_music39_amiga_supports,sx_music39_amiga_build,sx_music39_amiga_layouts,SX_COUNT(sx_music39_amiga_layouts),sx_music39_amiga_comment};
