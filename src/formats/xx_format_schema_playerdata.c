/* SPDX-License-Identifier: MIT
 * Physical original-player song and sample metadata. Playback bytecode and sample bodies remain raw.
 * See docs/data_struct_playerdata_scope.txt for format-specific limits. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxplayerdata_rho_subsong_fields[]={
 SX_FIELD("channel_0_sequence_offset","uint32",0,4,SX_OFFSET),
 SX_FIELD("channel_1_sequence_offset","uint32",4,4,SX_OFFSET),
 SX_FIELD("channel_2_sequence_offset","uint32",8,4,SX_OFFSET),
};
static const sx_layout sxplayerdata_rho_subsong={UINT32_C(0x60A30001),"rho_subsong",sxplayerdata_rho_subsong_fields,SX_COUNT(sxplayerdata_rho_subsong_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_rho_sequence_pointer_fields[]={
 SX_FIELD("track_offset","uint32",0,4,SX_OFFSET),
};
static const sx_layout sxplayerdata_rho_sequence_pointer={UINT32_C(0x60A30002),"rho_sequence_pointer",sxplayerdata_rho_sequence_pointer_fields,SX_COUNT(sxplayerdata_rho_sequence_pointer_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_rho_sequence_end_fields[]={
 SX_FIELD("end_marker","uint32",0,4,SX_NONE),
};
static const sx_layout sxplayerdata_rho_sequence_end={UINT32_C(0x60A30003),"rho_sequence_end",sxplayerdata_rho_sequence_end_fields,SX_COUNT(sxplayerdata_rho_sequence_end_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_jason_page_header_fields[]={
 SX_FIELD("format_word","uint16",0,2,SX_SIGNATURE),
 SX_FIELD("sample_lengths_offset","int16",2,2,SX_OFFSET),
 SX_FIELD("sound_index_offset","int16",4,2,SX_OFFSET),
 SX_FIELD("sound_data_offset","int16",6,2,SX_OFFSET),
 SX_FIELD("song_timing_offset","int16",8,2,SX_OFFSET),
 SX_FIELD("sound_priority_offset","int16",10,2,SX_OFFSET),
 SX_FIELD("song_index_0_offset","int16",12,2,SX_OFFSET),
 SX_FIELD("song_index_1_offset","int16",14,2,SX_OFFSET),
 SX_FIELD("song_index_2_offset","int16",16,2,SX_OFFSET),
 SX_FIELD("song_index_3_offset","int16",18,2,SX_OFFSET),
 SX_FIELD("song_index_4_offset","int16",20,2,SX_OFFSET),
 SX_FIELD("song_index_5_offset","int16",22,2,SX_OFFSET),
 SX_FIELD("song_index_6_offset","int16",24,2,SX_OFFSET),
 SX_FIELD("song_index_7_offset","int16",26,2,SX_OFFSET),
 SX_FIELD("song_data_0_offset","int16",28,2,SX_OFFSET),
 SX_FIELD("song_data_1_offset","int16",30,2,SX_OFFSET),
 SX_FIELD("song_data_2_offset","int16",32,2,SX_OFFSET),
 SX_FIELD("song_data_3_offset","int16",34,2,SX_OFFSET),
 SX_FIELD("song_data_4_offset","int16",36,2,SX_OFFSET),
 SX_FIELD("song_data_5_offset","int16",38,2,SX_OFFSET),
 SX_FIELD("song_data_6_offset","int16",40,2,SX_OFFSET),
 SX_FIELD("song_data_7_offset","int16",42,2,SX_OFFSET),
 SX_FIELD("track_index_offset","int16",44,2,SX_OFFSET),
 SX_FIELD("track_data_offset","int16",46,2,SX_OFFSET),
 SX_FIELD("music_end_offset","uint16",48,2,SX_OFFSET),
};
static const sx_layout sxplayerdata_jason_page_header={UINT32_C(0x60A30004),"jason_page_header",sxplayerdata_jason_page_header_fields,SX_COUNT(sxplayerdata_jason_page_header_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_jason_sample_length_fields[]={
 SX_FIELD("byte_length","uint32",0,4,SX_SIZE),
};
static const sx_layout sxplayerdata_jason_sample_length={UINT32_C(0x60A30005),"jason_sample_length",sxplayerdata_jason_sample_length_fields,SX_COUNT(sxplayerdata_jason_sample_length_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_jason_old_sample_offset_fields[]={
 SX_FIELD("sample_offset","uint32",0,4,SX_OFFSET),
};
static const sx_layout sxplayerdata_jason_old_sample_offset={UINT32_C(0x60A30006),"jason_old_sample_offset",sxplayerdata_jason_old_sample_offset_fields,SX_COUNT(sxplayerdata_jason_old_sample_offset_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_jason_old_music_end_fields[]={
 SX_FIELD("final_word_offset","uint32",0,4,SX_OFFSET),
};
static const sx_layout sxplayerdata_jason_old_music_end={UINT32_C(0x60A30007),"jason_old_music_end",sxplayerdata_jason_old_music_end_fields,SX_COUNT(sxplayerdata_jason_old_music_end_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_soede_sample_directory_fields[]={
 SX_FIELD("table_byte_length","int16",0,2,SX_SIZE),
 SX_FIELD("table_displacement","int32",2,4,SX_OFFSET),
};
static const sx_layout sxplayerdata_soede_sample_directory={UINT32_C(0x60A30008),"soede_sample_directory",sxplayerdata_soede_sample_directory_fields,SX_COUNT(sxplayerdata_soede_sample_directory_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_soede_new_sample_fields[]={
 SX_FIELD("sample_displacement","int32",0,4,SX_OFFSET),
 SX_FIELD("length_words","uint16",4,2,SX_NONE),
 SX_FIELD("opaque_tail","bytes",6,4,SX_NONE),
};
static const sx_layout sxplayerdata_soede_new_sample={UINT32_C(0x60A30009),"soede_new_sample",sxplayerdata_soede_new_sample_fields,SX_COUNT(sxplayerdata_soede_new_sample_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_soede_old_table_base_fields[]={
 SX_FIELD("table_displacement","int32",0,4,SX_OFFSET),
};
static const sx_layout sxplayerdata_soede_old_table_base={UINT32_C(0x60A3000A),"soede_old_table_base",sxplayerdata_soede_old_table_base_fields,SX_COUNT(sxplayerdata_soede_old_table_base_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_soede_old_sample_offset_fields[]={
 SX_FIELD("sample_displacement","int32",0,4,SX_OFFSET),
};
static const sx_layout sxplayerdata_soede_old_sample_offset={UINT32_C(0x60A3000B),"soede_old_sample_offset",sxplayerdata_soede_old_sample_offset_fields,SX_COUNT(sxplayerdata_soede_old_sample_offset_fields),SX_BE};
static const xx_data_struct_field_desc sxplayerdata_soede_old_sample_length_fields[]={
 SX_FIELD("length_words","uint16",0,2,SX_NONE),
};
static const sx_layout sxplayerdata_soede_old_sample_length={UINT32_C(0x60A3000C),"soede_old_sample_length",sxplayerdata_soede_old_sample_length_fields,SX_COUNT(sxplayerdata_soede_old_sample_length_fields),SX_BE};
static const sx_layout *const sx_playerdata_layouts[]={
 &sxplayerdata_rho_subsong,
 &sxplayerdata_rho_sequence_pointer,
 &sxplayerdata_rho_sequence_end,
 &sxplayerdata_jason_page_header,
 &sxplayerdata_jason_sample_length,
 &sxplayerdata_jason_old_sample_offset,
 &sxplayerdata_jason_old_music_end,
 &sxplayerdata_soede_sample_directory,
 &sxplayerdata_soede_new_sample,
 &sxplayerdata_soede_old_table_base,
 &sxplayerdata_soede_old_sample_offset,
 &sxplayerdata_soede_old_sample_length,
};
#define SXPLAYERDATA_LIMIT 24000U
static bool sxplayerdata_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p&&!xx_pd_is_stopped(c->pd);}
static bool sxplayerdata_read(sx_context*c,uint64_t p,void*b,size_t n){return sxplayerdata_span(c,p,n)&&sx_read(c,(int64_t)p,b,n);}
static uint16_t sxplayerdata16(const uint8_t*b){return (uint16_t)b[0]|(uint16_t)((uint16_t)b[1]<<8);}
static bool sxplayerdata_eq(sx_context*c,uint64_t p,const char*s,size_t n){uint8_t b[64];return n<=sizeof(b)&&sxplayerdata_read(c,p,b,n)&&!xx_rt_memcmp(b,s,n);}
static uint64_t sxplayerdata_width(const sx_layout*l){const xx_data_struct_field_desc*f=l->fields+l->field_count-1;return (uint64_t)f->rel_offset+(uint64_t)f->size;}
/* Insert only disjoint physical records. Shared pointers are decoded once;
 * conflicting interpretations remain covered by the first authenticated row.
 * Display-budget exhaustion retains earlier metadata and raw remainder. */
static bool sxplayerdata_put(sx_context*c,const sx_layout*l,uint64_t p,uint64_t count){
 uint64_t z=sxplayerdata_width(l);if(!count)return true;if(!z||count>UINT64_MAX/z||!sxplayerdata_span(c,p,count*z))return false;
 if(c->count>=SXPLAYERDATA_LIMIT)return true;
 uint64_t end=p+count*z;
 for(size_t i=0;i<c->count;i++){int64_t rel=c->items[i].offset-c->f->base_address;uint64_t n=(uint64_t)c->items[i].total_size;
  if(rel>=0&&p<(uint64_t)rel+n&&(uint64_t)rel<end)return true;}
 return count==1?sx_add(c,l,(int64_t)p,(int64_t)z):sx_table(c,l,(int64_t)p,(int64_t)z,(int64_t)count);
}
#define SXPLAYERDATA_ADD(c,n,p) sxplayerdata_put(c,&sxplayerdata_##n,p,1)
static bool sxplayerdata_opt(sx_context*c,const sx_layout*l,uint64_t p,uint64_t count){uint64_t z=sxplayerdata_width(l);return !count||!z||count>UINT64_MAX/z||!sxplayerdata_span(c,p,count*z)?true:sxplayerdata_put(c,l,p,count);}
static int sxplayerdata_order(const void*a,const void*b){const xx_data_struct*x=a,*y=b;return x->offset<y->offset?-1:x->offset>y->offset;}
static bool sxplayerdata_finish(sx_context*c){size_t n=c->count;int64_t end=0;xx_rt_qsort(c->items,n,sizeof(*c->items),sxplayerdata_order);for(size_t i=0;i<n;i++){int64_t p=c->items[i].offset-c->f->base_address,z=c->items[i].total_size;if(p<end||z<=0||p>c->size||z>c->size-p)return false;if(p>end&&!sx_raw(c,end,p-end))return false;end=p+z;}if(end<c->size&&!sx_raw(c,end,c->size-end))return false;xx_rt_qsort(c->items,c->count,sizeof(*c->items),sxplayerdata_order);return !c->failed&&!xx_pd_is_stopped(c->pd);}

static uint16_t sxplayerdata_be16(const uint8_t*b){return (uint16_t)((uint16_t)b[0]<<8)|b[1];}
static uint32_t sxplayerdata_be32(const uint8_t*b){return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
static bool sxplayerdata_word(sx_context*c,uint64_t p,uint16_t*v){uint8_t b[2];if(!sxplayerdata_read(c,p,b,2))return false;*v=sxplayerdata_be16(b);return true;}
/* Only original-loader operands locate data. Replayer code is never emitted as
 * fixed metadata and no external or transformed sample buffer is traversed. */
static bool sxplayerdata_signed(uint64_t p,int64_t d,uint64_t limit,uint64_t*out){
 if(d<0){uint64_t n=(uint64_t)(-(d+1))+1;if(n>p)return false;*out=p-n;}else{if((uint64_t)d>limit||p>limit-(uint64_t)d)return false;*out=p+(uint64_t)d;}return *out<=limit;
}
static bool sxplayerdata_find(sx_context*c,uint64_t start,uint64_t end,uint16_t value,uint64_t*after){
 if(end>(uint64_t)c->size)end=(uint64_t)c->size;
 for(uint64_t p=start;p<=end&&end-p>=2;p+=2){uint16_t v;if(!sxplayerdata_word(c,p,&v))return false;if(v==value){*after=p+2;return true;}}return false;
}
static bool sxplayerdata_rho(sx_context*c){
 uint64_t p,end=(uint64_t)c->size<65536?(uint64_t)c->size:65536,base;uint8_t b[4];uint16_t w;
 if(!sxplayerdata_eq(c,0,"\x00\x40\x7f\x40\x00\xc0\x81\xc0",8)||!sxplayerdata_eq(c,56,"\x41\xfa\xff\xee",4))return false;
 if(!sxplayerdata_find(c,180,end,0x4e75,&p)||!sxplayerdata_find(c,p,end,0xdffc,&p)||p>end||end-p<4)return false;p+=4;
 if(!sxplayerdata_find(c,p,end,0x4e75,&p)||!sxplayerdata_find(c,p,end,0x7e02,&p)||!sxplayerdata_word(c,p+2,&w)||!sxplayerdata_signed(p+2,(int16_t)w,end,&base)||!sxplayerdata_read(c,base,b,4))return false;
 uint64_t bytes=sxplayerdata_be32(b);if(!bytes||bytes%12||!sxplayerdata_span(c,base,bytes)||base+bytes>end)return false;
 if(!sxplayerdata_put(c,&sxplayerdata_rho_subsong,base,bytes/12))return false;
 uint64_t visited=0;
 for(uint64_t i=0;i<bytes/4&&visited<65536;i++){
  if(!sxplayerdata_read(c,base+i*4,b,4))return false;uint64_t rel=sxplayerdata_be32(b),q;if(rel<bytes||!sxplayerdata_signed(base,(int64_t)rel,end,&q))continue;
  uint64_t n=0;bool terminated=false;
  while(q<=end&&n<65536-visited&&end-q>=4&&n<=(end-q-4)/4){if(!sxplayerdata_read(c,q+n*4,b,4))return false;uint32_t v=sxplayerdata_be32(b);if(!v){terminated=true;break;}n++;}
  visited+=n+1;if(n&&!sxplayerdata_put(c,&sxplayerdata_rho_sequence_pointer,q,n))return false;
  if(terminated&&!SXPLAYERDATA_ADD(c,rho_sequence_end,q+n*4))return false;
 }
 return true;
}
static bool sxplayerdata_jason(sx_context*c){
 uint8_t b[50];if(!sxplayerdata_read(c,0,b,sizeof(b)))return false;
 if(sxplayerdata_be16(b)==2){
  uint16_t end=sxplayerdata_be16(b+48);if(end<50||!sxplayerdata_span(c,0,end))return false;
  for(unsigned i=2;i<48;i+=2){int16_t v=(int16_t)sxplayerdata_be16(b+i);if(v<50||(v&1)||(uint16_t)v>=end)return false;}
  if(!SXPLAYERDATA_ADD(c,jason_page_header,0))return false;
  uint16_t start=sxplayerdata_be16(b+2);if(end>=start+4U&&(end-start-4U)%4U==0){uint64_t n=(end-start-4U)/4U;if(n>32)n=32;if(n&&!sxplayerdata_put(c,&sxplayerdata_jason_sample_length,start,n))return false;}
  return true;
 }
 /* Original old player has a fixed32-long external sample-offset plane. */
 if(sxplayerdata_be16(b)||!sxplayerdata_eq(c,128,"\0\0\0\0\0\0\x0c\xbe",8)||!sxplayerdata_eq(c,3254,"\0\x03\x08\xbe\0\x03\x09\xbe",8))return false;
 uint8_t e[4];if(!sxplayerdata_read(c,2234,e,4))return false;uint64_t end=(uint64_t)sxplayerdata_be32(e)+2;if(end<2236||end>(uint64_t)c->size)return false;
 return sxplayerdata_put(c,&sxplayerdata_jason_old_sample_offset,0,32)&&SXPLAYERDATA_ADD(c,jason_old_music_end,2234);
}
static bool sxplayerdata_soede(sx_context*c){
 uint8_t h[12],b[6];uint64_t size=(uint64_t)c->size,init,global,p,lea,stop,limit=size<131072?size:131072;uint16_t v;
 if(!sxplayerdata_read(c,0,h,12)||sxplayerdata_be16(h)!=0x6000||sxplayerdata_be16(h+4)!=0x6000||sxplayerdata_be16(h+8)!=0x6000||!sxplayerdata_signed(2,(int16_t)sxplayerdata_be16(h+2),size,&init)||!init||(init&1))return false;
 if(!sxplayerdata_find(c,init,init+30,0x47fa,&lea)||!sxplayerdata_word(c,lea,&v)||!sxplayerdata_signed(lea,(int16_t)v,size,&global))return false;
 if(!sxplayerdata_find(c,lea,limit,0x4e75,&stop)||stop<8)return false;
 bool newer=sxplayerdata_eq(c,stop-8,"\x17\x7c\0\0",4);uint64_t check=stop;if(newer){if(check<6)return false;check-=6;}if(check<6||!sxplayerdata_eq(c,check-6,"\0\xbf\xe0\x01",4))return false;
 /* The retained sample locator is only established for patchable players. */
 if(!sxplayerdata_find(c,10,limit,0x5203,&p)||!sxplayerdata_eq(c,p+10,"\x17\x7c",2))return false;
 if(newer){
  uint64_t meta,table,first;
  if(!sxplayerdata_find(c,init,limit,0x41eb,&p)||!sxplayerdata_read(c,p,b,6)||!sxplayerdata_signed(global,(int16_t)sxplayerdata_be16(b+4),size,&meta)||meta<2||!sxplayerdata_read(c,meta-2,b,6))return false;
  int16_t bytes=(int16_t)sxplayerdata_be16(b);int32_t delta=(int32_t)sxplayerdata_be32(b+2);uint16_t add;if(!sxplayerdata_word(c,p,&add)||!sxplayerdata_signed(global,(int16_t)add,size,&first)||!sxplayerdata_signed(first,delta,size,&table))return false;
  if(bytes<=0||bytes%10||!sxplayerdata_span(c,table,(uint64_t)bytes))return false;
  if(!SXPLAYERDATA_ADD(c,soede_sample_directory,meta-2))return false;
  return sxplayerdata_put(c,&sxplayerdata_soede_new_sample,table,(uint64_t)bytes/10);
 }
 uint64_t meta,table,base;
 if(!sxplayerdata_find(c,init,limit,0x3d70,&p)||!sxplayerdata_find(c,p,limit,0xd5f0,&p)||p<18||!sxplayerdata_word(c,p-4,&v)||!sxplayerdata_signed(global,(int16_t)v,size,&meta)||!sxplayerdata_read(c,meta,b,4)||!sxplayerdata_signed(global,(int32_t)sxplayerdata_be32(b),size,&base)||!sxplayerdata_word(c,p-18,&v)||!sxplayerdata_signed(base,(int16_t)v,size,&table)||!sxplayerdata_span(c,table,192))return false;
 return SXPLAYERDATA_ADD(c,soede_old_table_base,meta)&&sxplayerdata_put(c,&sxplayerdata_soede_old_sample_offset,table,32)&&sxplayerdata_put(c,&sxplayerdata_soede_old_sample_length,table+128,32);
}
static bool sx_playerdata_supports(xx_file_type_t t){switch(t){
case XX_FILE_TYPE_DIE_MUSIC_ROBHUBBARD_ST_RHO:
case XX_FILE_TYPE_DIE_MUSIC_JASONPAGE:
case XX_FILE_TYPE_DIE_MUSIC_SOEDE_SOUNDMASTER:return true;default:return false;}}
static bool sx_playerdata_build(sx_context*c){bool ok=false;switch(c->f->file_type){
case XX_FILE_TYPE_DIE_MUSIC_ROBHUBBARD_ST_RHO:ok=sxplayerdata_rho(c);break;
case XX_FILE_TYPE_DIE_MUSIC_JASONPAGE:ok=sxplayerdata_jason(c);break;
case XX_FILE_TYPE_DIE_MUSIC_SOEDE_SOUNDMASTER:ok=sxplayerdata_soede(c);break;
default:return false;}(void)ok;
 if(c->failed||xx_pd_is_stopped(c->pd))return false;
 if(!c->count)return c->f->base_info_handled&&c->f->is_valid&&c->size>0&&sx_raw(c,0,c->size);
 return sxplayerdata_finish(c);}
static bool sx_playerdata_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60a30000U||id>=0x60a40000U)return true;
 if(id==sxplayerdata_rho_subsong.id&&!xx_rt_wcscmp(r->name,L"channel_0_sequence_offset"))return xx_data_struct_record_set_comment(r,L"Byte displacement relative to the anchored SongPtr table start. The first stored displacement divided by12 determines the three-channel subsong table size; no derived count is stored here.");
 if(id==sxplayerdata_rho_subsong.id&&!xx_rt_wcscmp(r->name,L"channel_1_sequence_offset"))return xx_data_struct_record_set_comment(r,L"Byte displacement relative to the anchored SongPtr table start. The first stored displacement divided by12 determines the three-channel subsong table size; no derived count is stored here.");
 if(id==sxplayerdata_rho_subsong.id&&!xx_rt_wcscmp(r->name,L"channel_2_sequence_offset"))return xx_data_struct_record_set_comment(r,L"Byte displacement relative to the anchored SongPtr table start. The first stored displacement divided by12 determines the three-channel subsong table size; no derived count is stored here.");
 if(id==sxplayerdata_rho_sequence_pointer.id&&!xx_rt_wcscmp(r->name,L"track_offset"))return xx_data_struct_record_set_comment(r,L"Byte displacement relative to the anchored SongPtr table start; referenced track bytecode remains raw.");
 if(id==sxplayerdata_rho_sequence_end.id&&!xx_rt_wcscmp(r->name,L"end_marker"))return xx_data_struct_record_set_comment(r,L"Zero longword terminates this channel sequence-pointer list.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"format_word"))return xx_data_struct_record_set_comment(r,L"On-disk0002 identity word. The original player modifies a high bit after relocating sample data; that runtime value is not substituted here.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"sample_lengths_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"sound_index_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"sound_data_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_timing_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"sound_priority_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_index_0_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_index_1_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_index_2_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_index_3_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_index_4_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_index_5_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_index_6_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_index_7_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_data_0_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_data_1_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_data_2_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_data_3_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_data_4_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_data_5_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_data_6_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"song_data_7_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"track_index_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"track_data_offset"))return xx_data_struct_record_set_comment(r,L"Signed16-bit byte displacement from the module start, as used by original ADDA.W instructions. Sound/song/track program bodies and external samples remain raw.");
 if(id==sxplayerdata_jason_page_header.id&&!xx_rt_wcscmp(r->name,L"music_end_offset"))return xx_data_struct_record_set_comment(r,L"Byte offset immediately after the stored music region, relative to the module start. The final four-byte trailer is not interpreted as a sample length.");
 if(id==sxplayerdata_jason_sample_length.id&&!xx_rt_wcscmp(r->name,L"byte_length"))return xx_data_struct_record_set_comment(r,L"Stored external sample byte length. The exporter rounds odd lengths for audio loading; the stored number is shown unchanged.");
 if(id==sxplayerdata_jason_old_sample_offset.id&&!xx_rt_wcscmp(r->name,L"sample_offset"))return xx_data_struct_record_set_comment(r,L"Byte offset into the separate sample buffer. Adjacent offsets determine lengths; the final sample has an implicit128-byte length, not a stored length field.");
 if(id==sxplayerdata_jason_old_music_end.id&&!xx_rt_wcscmp(r->name,L"final_word_offset"))return xx_data_struct_record_set_comment(r,L"Module-relative byte offset of the final two-byte music word; original loader adds2 to obtain the music byte length.");
 if(id==sxplayerdata_soede_sample_directory.id&&!xx_rt_wcscmp(r->name,L"table_byte_length"))return xx_data_struct_record_set_comment(r,L"Stored byte extent of the new-format ten-byte sample records. Original pointer arithmetic sign-extends this word; only positive complete extents are followed.");
 if(id==sxplayerdata_soede_sample_directory.id&&!xx_rt_wcscmp(r->name,L"table_displacement"))return xx_data_struct_record_set_comment(r,L"Signed byte displacement added to the globals base plus the separately anchored LEA displacement. The physical sample payload starts after table_byte_length bytes of records.");
 if(id==sxplayerdata_soede_new_sample.id&&!xx_rt_wcscmp(r->name,L"sample_displacement"))return xx_data_struct_record_set_comment(r,L"Signed byte displacement relative to the sample-data start after the sample-info table. Negative values mark unused entries; no sample payload is decoded.");
 if(id==sxplayerdata_soede_new_sample.id&&!xx_rt_wcscmp(r->name,L"length_words"))return xx_data_struct_record_set_comment(r,L"Stored length in two-byte words; zero marks an unused entry. No byte conversion is applied to the displayed value.");
 if(id==sxplayerdata_soede_new_sample.id&&!xx_rt_wcscmp(r->name,L"opaque_tail"))return xx_data_struct_record_set_comment(r,L"Four preserved bytes skipped by the retained sample-info loader; no unsupported parameter names are inferred.");
 if(id==sxplayerdata_soede_old_table_base.id&&!xx_rt_wcscmp(r->name,L"table_displacement"))return xx_data_struct_record_set_comment(r,L"Signed byte displacement from the anchored globals base to the old-format table region. Original code supplies the separate sample-info and sample-data displacements.");
 if(id==sxplayerdata_soede_old_sample_offset.id&&!xx_rt_wcscmp(r->name,L"sample_displacement"))return xx_data_struct_record_set_comment(r,L"Signed byte displacement relative to the separately anchored sample-data base. The corresponding length-word plane controls whether an entry is used.");
 if(id==sxplayerdata_soede_old_sample_length.id&&!xx_rt_wcscmp(r->name,L"length_words"))return xx_data_struct_record_set_comment(r,L"Stored length in two-byte words. Original old-format scanner treats values0 and1 as unused; no byte conversion is applied.");
 if(r->property&SX_OFFSET)return xx_data_struct_record_set_comment(r,L"Stored byte offset relative to the module start, unless the enclosing structure specifies another origin.");
 if(r->property&SX_ADDRESS)return xx_data_struct_record_set_comment(r,L"Encoded target-machine memory address; the field source-file offset is shown separately.");
 return true;
}
const sx_family sx_playerdata_family={sx_playerdata_supports,sx_playerdata_build,sx_playerdata_layouts,SX_COUNT(sx_playerdata_layouts),sx_playerdata_comment};
