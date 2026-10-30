/* SPDX-License-Identifier: MIT
 * Physical original-player song and sample metadata. Playback bytecode and sample bodies remain raw.
 * See docs/data_struct_companiondata_scope.txt for format-specific limits. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxcdata_hippel_sample_directory_fields[]={
 SX_FIELD("data_offset","uint16",0,2,SX_OFFSET),
 SX_FIELD("span_control","uint16",2,2,SX_NONE),
 SX_FIELD("opaque_control","bytes",4,4,SX_NONE),
};
static const sx_layout sxcdata_hippel_sample_directory={UINT32_C(0x60A90001),"hippel_sample_directory",sxcdata_hippel_sample_directory_fields,SX_COUNT(sxcdata_hippel_sample_directory_fields),SX_BE};
static const xx_data_struct_field_desc sxcdata_zound_header_fields[]={
 SX_FIELD("last_saved_position","uint8",0,1,SX_NONE),
 SX_FIELD("last_saved_pattern","uint8",1,1,SX_NONE),
 SX_FIELD("restart_position","uint8",2,1,SX_NONE),
 SX_FIELD("end_position","uint8",3,1,SX_NONE),
 SX_FIELD("speed","uint8",4,1,SX_NONE),
};
static const sx_layout sxcdata_zound_header={UINT32_C(0x60A90002),"zound_header",sxcdata_zound_header_fields,SX_COUNT(sxcdata_zound_header_fields),SX_LE};
static const xx_data_struct_field_desc sxcdata_zound_sample_slot_fields[]={
 SX_FIELD("saved_state","bytes",0,4,SX_NONE),
 SX_FIELD("name_prefix","char[32]",4,32,SX_NONE),
 SX_FIELD("opaque_name_tail","bytes",36,8,SX_NONE),
 SX_FIELD("transpose","int8",44,1,SX_NONE),
 SX_FIELD("opaque_byte","bytes",45,1,SX_NONE),
 SX_FIELD("length_words","uint16",46,2,SX_NONE),
 SX_FIELD("repeat_words","uint16",48,2,SX_NONE),
 SX_FIELD("loop_start_words","uint16",50,2,SX_NONE),
 SX_FIELD("opaque_tail","bytes",52,2,SX_NONE),
};
static const sx_layout sxcdata_zound_sample_slot={UINT32_C(0x60A90003),"zound_sample_slot",sxcdata_zound_sample_slot_fields,SX_COUNT(sxcdata_zound_sample_slot_fields),SX_BE};
static const xx_data_struct_field_desc sxcdata_zound_position_channel_fields[]={
 SX_FIELD("pattern_index","uint8",0,1,SX_NONE),
 SX_FIELD("volume_adjustment","int8",1,1,SX_NONE),
 SX_FIELD("sample_index_addend","uint8",2,1,SX_NONE),
 SX_FIELD("note_addend","uint8",3,1,SX_NONE),
};
static const sx_layout sxcdata_zound_position_channel={UINT32_C(0x60A90004),"zound_position_channel",sxcdata_zound_position_channel_fields,SX_COUNT(sxcdata_zound_position_channel_fields),SX_LE};
static const xx_data_struct_field_desc sxcdata_zound_external_path_fields[]={
 SX_FIELD("sample_path","char[101]",0,101,SX_NONE),
};
static const sx_layout sxcdata_zound_external_path={UINT32_C(0x60A90005),"zound_external_path",sxcdata_zound_external_path_fields,SX_COUNT(sxcdata_zound_external_path_fields),SX_LE};
static const sx_layout *const sx_companiondata_layouts[]={
 &sxcdata_hippel_sample_directory,
 &sxcdata_zound_header,
 &sxcdata_zound_sample_slot,
 &sxcdata_zound_position_channel,
 &sxcdata_zound_external_path,
};
#define SXCDATA_LIMIT 24000U
static bool sxcdata_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p&&!xx_pd_is_stopped(c->pd);}
static bool sxcdata_read(sx_context*c,uint64_t p,void*b,size_t n){return sxcdata_span(c,p,n)&&sx_read(c,(int64_t)p,b,n);}
static uint16_t sxcdata16(const uint8_t*b){return (uint16_t)b[0]|(uint16_t)((uint16_t)b[1]<<8);}
static bool sxcdata_eq(sx_context*c,uint64_t p,const char*s,size_t n){uint8_t b[64];return n<=sizeof(b)&&sxcdata_read(c,p,b,n)&&!xx_rt_memcmp(b,s,n);}
static uint64_t sxcdata_width(const sx_layout*l){const xx_data_struct_field_desc*f=l->fields+l->field_count-1;return (uint64_t)f->rel_offset+(uint64_t)f->size;}
/* Insert only disjoint physical records. Shared pointers are decoded once;
 * conflicting interpretations remain covered by the first authenticated row.
 * Display-budget exhaustion retains earlier metadata and raw remainder. */
static bool sxcdata_put(sx_context*c,const sx_layout*l,uint64_t p,uint64_t count){
 uint64_t z=sxcdata_width(l);if(!count)return true;if(!z||count>UINT64_MAX/z||!sxcdata_span(c,p,count*z))return false;
 if(c->count>=SXCDATA_LIMIT)return true;
 uint64_t end=p+count*z;
 for(size_t i=0;i<c->count;i++){int64_t rel=c->items[i].offset-c->f->base_address;uint64_t n=(uint64_t)c->items[i].total_size;
  if(rel>=0&&p<(uint64_t)rel+n&&(uint64_t)rel<end)return true;}
 return count==1?sx_add(c,l,(int64_t)p,(int64_t)z):sx_table(c,l,(int64_t)p,(int64_t)z,(int64_t)count);
}
#define SXCDATA_ADD(c,n,p) sxcdata_put(c,&sxcdata_##n,p,1)
static bool sxcdata_opt(sx_context*c,const sx_layout*l,uint64_t p,uint64_t count){uint64_t z=sxcdata_width(l);return !count||!z||count>UINT64_MAX/z||!sxcdata_span(c,p,count*z)?true:sxcdata_put(c,l,p,count);}
static int sxcdata_order(const void*a,const void*b){const xx_data_struct*x=a,*y=b;return x->offset<y->offset?-1:x->offset>y->offset;}
static bool sxcdata_finish(sx_context*c){size_t n=c->count;int64_t end=0;xx_rt_qsort(c->items,n,sizeof(*c->items),sxcdata_order);for(size_t i=0;i<n;i++){int64_t p=c->items[i].offset-c->f->base_address,z=c->items[i].total_size;if(p<end||z<=0||p>c->size||z>c->size-p)return false;if(p>end&&!sx_raw(c,end,p-end))return false;end=p+z;}if(end<c->size&&!sx_raw(c,end,c->size-end))return false;xx_rt_qsort(c->items,c->count,sizeof(*c->items),sxcdata_order);return !c->failed&&!xx_pd_is_stopped(c->pd);}

static uint16_t sxcdata_be16(const uint8_t*b){return (uint16_t)((uint16_t)b[0]<<8)|b[1];}
static uint32_t sxcdata_be32(const uint8_t*b){return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
static bool sxcdata_word(sx_context*c,uint64_t p,uint16_t*v){uint8_t b[2];if(!sxcdata_read(c,p,b,2))return false;*v=sxcdata_be16(b);return true;}
static bool sxcdata_hippel(sx_context*c){
 uint8_t b[8];uint16_t first,previous=0;uint64_t count=0;
 if(!sxcdata_span(c,0,128)||!sxcdata_read(c,0,b,8))return false;
 first=sxcdata_be16(b);if(first<128||first%8||!sxcdata_span(c,0,first))return false;
 /* Authenticate the zero-ended directory before exposing fields. A terminal
  * boundary may equal EOF; bytes after the terminator remain ordinary raw. */
 for(uint64_t p=0;p+8<=first;p+=8){
  if(!sxcdata_read(c,p,b,8))return false;
  uint16_t v=sxcdata_be16(b);
  if(!v){count=p/8+1;break;}
  if(v<first||v<previous||v>(uint64_t)c->size)return false;
  previous=v;
 }
 if(count<9)return false;
 return sxcdata_put(c,&sxcdata_hippel_sample_directory,0,count);
}
static bool sxcdata_zound(sx_context*c){
 uint8_t b[5];if(!sxcdata_read(c,0,b,5))return false;
 uint64_t positions=(uint64_t)b[0]+1,patterns=(uint64_t)b[1]+1;
 uint64_t path=869+positions*16+patterns*128;
 /* Native recognition permits a short path tail; retain complete preceding
  * metadata and leave an incomplete optional path as raw bytes. */
 if(!sxcdata_span(c,0,path)||
    !(sxcdata_eq(c,path,"Samples",7)||sxcdata_eq(c,path,"samples",7)||
      (sxcdata_span(c,path,4)&&sxcdata_read(c,path,b,4)&&b[0]=='d'&&b[1]=='f'&&b[2]>='0'&&b[2]<='9'&&b[3]==':')))return false;
 if(!SXCDATA_ADD(c,zound_header,0)||!sxcdata_put(c,&sxcdata_zound_sample_slot,5,16)||
    !sxcdata_put(c,&sxcdata_zound_position_channel,869,positions*4))return false;
 return sxcdata_opt(c,&sxcdata_zound_external_path,path,1);
}
static bool sx_companiondata_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_HIPPEL_ATARIST_SMPSET||t==XX_FILE_TYPE_DIE_MUSIC_AJ_ZOUNDMONITOR;}
static bool sx_companiondata_build(sx_context*c){
 bool ok=false;switch(c->f->file_type){
 case XX_FILE_TYPE_DIE_MUSIC_HIPPEL_ATARIST_SMPSET:ok=sxcdata_hippel(c);break;
 case XX_FILE_TYPE_DIE_MUSIC_AJ_ZOUNDMONITOR:ok=sxcdata_zound(c);break;
 default:return false;}(void)ok;
 if(c->failed||xx_pd_is_stopped(c->pd))return false;
 if(!c->count)return c->f->base_info_handled&&c->f->is_valid&&c->size>0&&sx_raw(c,0,c->size);
 return sxcdata_finish(c);
}
static bool sx_companiondata_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60a90000U||id>=0x60aa0000U)return true;
 if(id==sxcdata_hippel_sample_directory.id&&!xx_rt_wcscmp(r->name,L"data_offset"))return xx_data_struct_record_set_comment(r,L"Byte offset from the beginning of the separate sample-set file. A zero offset ends the directory; the last nonzero boundary can mark the end of the sample data. No external song pointer is substituted.");
 if(id==sxcdata_hippel_sample_directory.id&&!xx_rt_wcscmp(r->name,L"span_control"))return xx_data_struct_record_set_comment(r,L"Stored control word: zero selects a span ending at the next directory offset; 0x0010 selects a sixteen-byte sample and self-loop. Other original-player values act as an end offset, but are outside the native-recognized first sixteen rows. No derived byte length is displayed.");
 if(id==sxcdata_hippel_sample_directory.id&&!xx_rt_wcscmp(r->name,L"opaque_control"))return xx_data_struct_record_set_comment(r,L"Four preserved control bytes whose meaning is not established by the retained loader. Native recognition checks either 01000000 or 00000000 with the corresponding preceding word; no invented fields are assigned.");
 if(id==sxcdata_zound_header.id&&!xx_rt_wcscmp(r->name,L"last_saved_position"))return xx_data_struct_record_set_comment(r,L"Stored count minus one for the physical position table. Each position contains four four-byte channel records; inactive saved positions remain visible.");
 if(id==sxcdata_zound_header.id&&!xx_rt_wcscmp(r->name,L"last_saved_pattern"))return xx_data_struct_record_set_comment(r,L"Stored count minus one for the physical pattern region. Each pattern has thirty-two four-byte packed rows; pattern bodies remain raw.");
 if(id==sxcdata_zound_header.id&&!xx_rt_wcscmp(r->name,L"restart_position"))return xx_data_struct_record_set_comment(r,L"Zero-based position selected when the replay position reaches end_position.");
 if(id==sxcdata_zound_header.id&&!xx_rt_wcscmp(r->name,L"end_position"))return xx_data_struct_record_set_comment(r,L"Stored end-position index compared after incrementing the replay position; it is an exclusive bound in the retained player, not a converted count.");
 if(id==sxcdata_zound_header.id&&!xx_rt_wcscmp(r->name,L"speed"))return xx_data_struct_record_set_comment(r,L"Stored player tick delay used with the original 50 Hz playback loop. The value is displayed unchanged; no inferred beats-per-minute conversion.");
 if(id==sxcdata_zound_sample_slot.id&&!xx_rt_wcscmp(r->name,L"saved_state"))return xx_data_struct_record_set_comment(r,L"Four saved bytes replaced in memory by the loaded external sample address. Their original serialized meaning is unproven, so they remain bytes.");
 if(id==sxcdata_zound_sample_slot.id&&!xx_rt_wcscmp(r->name,L"name_prefix"))return xx_data_struct_record_set_comment(r,L"First thirty-two bytes of the external sample name, matching the retained sample-information export width. The loader reads a NUL-terminated name beginning here; later bytes are preserved separately.");
 if(id==sxcdata_zound_sample_slot.id&&!xx_rt_wcscmp(r->name,L"opaque_name_tail"))return xx_data_struct_record_set_comment(r,L"Eight preserved bytes before the transpose parameter; no assumption is made that they extend the exported thirty-two-byte name.");
 if(id==sxcdata_zound_sample_slot.id&&!xx_rt_wcscmp(r->name,L"transpose"))return xx_data_struct_record_set_comment(r,L"Signed eight-bit sample pitch adjustment, sign-extended by the original player.");
 if(id==sxcdata_zound_sample_slot.id&&!xx_rt_wcscmp(r->name,L"length_words"))return xx_data_struct_record_set_comment(r,L"Stored external sample length in two-byte words. No byte conversion or replacement from an external FORM file is applied.");
 if(id==sxcdata_zound_sample_slot.id&&!xx_rt_wcscmp(r->name,L"repeat_words"))return xx_data_struct_record_set_comment(r,L"Stored repeat length in two-byte words, copied to the audio length register for looping.");
 if(id==sxcdata_zound_sample_slot.id&&!xx_rt_wcscmp(r->name,L"loop_start_words"))return xx_data_struct_record_set_comment(r,L"Stored loop-start displacement in two-byte words from the external sample beginning. The player doubles it only at playback; this is not a file-relative offset.");
 if(id==sxcdata_zound_position_channel.id&&!xx_rt_wcscmp(r->name,L"pattern_index"))return xx_data_struct_record_set_comment(r,L"Zero-based index of a saved 128-byte pattern; the packed pattern body is not interpreted here.");
 if(id==sxcdata_zound_position_channel.id&&!xx_rt_wcscmp(r->name,L"volume_adjustment"))return xx_data_struct_record_set_comment(r,L"Signed eight-bit adjustment added to volume and clamped by the player to 0 through 64.");
 if(id==sxcdata_zound_position_channel.id&&!xx_rt_wcscmp(r->name,L"sample_index_addend"))return xx_data_struct_record_set_comment(r,L"Stored byte added to the pattern sample index. The original ADD.B operation is preserved without assigning a signed interpretation.");
 if(id==sxcdata_zound_position_channel.id&&!xx_rt_wcscmp(r->name,L"note_addend"))return xx_data_struct_record_set_comment(r,L"Stored byte added to the pattern note. The original ADD.B operation is preserved without assigning a signed interpretation.");
 if(id==sxcdata_zound_external_path.id&&!xx_rt_wcscmp(r->name,L"sample_path"))return xx_data_struct_record_set_comment(r,L"Physical 101-byte path region following the complete saved order and pattern regions. External sample files are not reconstructed or given offsets in this module.");
 if(r->property&SX_OFFSET)return xx_data_struct_record_set_comment(r,L"Stored byte offset relative to the module start, unless the enclosing structure specifies another origin.");
 if(r->property&SX_ADDRESS)return xx_data_struct_record_set_comment(r,L"Encoded target-machine memory address; the field source-file offset is shown separately.");
 return true;
}
const sx_family sx_companiondata_family={sx_companiondata_supports,sx_companiondata_build,sx_companiondata_layouts,SX_COUNT(sx_companiondata_layouts),sx_companiondata_comment};
