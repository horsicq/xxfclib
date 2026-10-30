/* SPDX-License-Identifier: MIT
 * Partial physical metadata from xxfclib's native audio.1.sg detector.
 * Aero's original demo songs corroborate its 12-byte envelope and LE size.
 * The unverified Aero chunk bodies and ACE synthesis/event values remain raw. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxa37_aero_header_fields[]={
 SX_FIELD("signature","bytes",0,8,SX_SIGNATURE),
 SX_FIELD("payload_size","uint32",8,4,SX_SIZE),
};
static const sx_layout sxa37_aero_header={0x60ab0001U,"aero_header",sxa37_aero_header_fields,SX_COUNT(sxa37_aero_header_fields),SX_LE};
static const xx_data_struct_field_desc sxa37_tag_fields[]={SX_FIELD("signature","bytes",0,4,SX_SIGNATURE)};
static const sx_layout sxa37_am_tag={0x60ab0002U,"ace_module_tag",sxa37_tag_fields,SX_COUNT(sxa37_tag_fields),SX_BE};
static const sx_layout sxa37_sample_tag={0x60ab0003U,"ace_sample_data_tag",sxa37_tag_fields,SX_COUNT(sxa37_tag_fields),SX_BE};
static const xx_data_struct_field_desc sxa37_patch_header_fields[]={
 SX_FIELD("signature","bytes",0,4,SX_SIGNATURE),
 SX_FIELD("instrument_count","uint32",4,4,SX_COUNT_PROP),
 SX_FIELD("instrument_data_tag","bytes",8,4,SX_SIGNATURE),
};
static const sx_layout sxa37_patch_header={0x60ab0005U,"ace_patch_header",sxa37_patch_header_fields,SX_COUNT(sxa37_patch_header_fields),SX_BE};
static const xx_data_struct_field_desc sxa37_instrument_name_fields[]={SX_FIELD("name","char[20]",0,20,SX_STRING)};
static const sx_layout sxa37_instrument_name={0x60ab0006U,"ace_instrument_name",sxa37_instrument_name_fields,SX_COUNT(sxa37_instrument_name_fields),SX_BE};
static const xx_data_struct_field_desc sxa37_song_header_fields[]={
 SX_FIELD("signature","bytes",0,4,SX_SIGNATURE),
 SX_FIELD("title","char[20]",4,20,SX_STRING),
 SX_FIELD("author","char[20]",24,20,SX_STRING),
 SX_FIELD("orders","bytes",44,256,SX_NONE),
};
static const sx_layout sxa37_song_header={0x60ab0007U,"ace_song_header",sxa37_song_header_fields,SX_COUNT(sxa37_song_header_fields),SX_BE};
static const xx_data_struct_field_desc sxa37_pattern_list_fields[]={
 SX_FIELD("signature","bytes",0,4,SX_SIGNATURE),
 SX_FIELD("pattern_count","uint32",4,4,SX_COUNT_PROP),
};
static const sx_layout sxa37_pattern_list={0x60ab0008U,"ace_pattern_list_header",sxa37_pattern_list_fields,SX_COUNT(sxa37_pattern_list_fields),SX_BE};
static const xx_data_struct_field_desc sxa37_pattern_fields[]={
 SX_FIELD("channel_count","uint16",0,2,SX_COUNT_PROP),
 SX_FIELD("row_count","uint16",2,2,SX_COUNT_PROP),
 SX_FIELD("name","char[12]",4,12,SX_STRING),
};
static const sx_layout sxa37_pattern={0x60ab0009U,"ace_pattern_header",sxa37_pattern_fields,SX_COUNT(sxa37_pattern_fields),SX_BE};
static const sx_layout sxa37_empty_pattern={0x60ab000aU,"ace_empty_pattern_header",sxa37_pattern_fields,2,SX_BE};
static const sx_layout*const sx_aeroace_layouts[]={&sxa37_aero_header,&sxa37_am_tag,&sxa37_sample_tag,&sxa37_patch_header,&sxa37_instrument_name,&sxa37_song_header,&sxa37_pattern_list,&sxa37_pattern,&sxa37_empty_pattern};
static bool sx_aeroace_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_IONEO_AEROSTUDIO||t==XX_FILE_TYPE_DIE_MUSIC_NEWBEAT_ACETRACKER_AM01||t==XX_FILE_TYPE_DIE_MUSIC_NEWBEAT_ACETRACKER_ASG1;}
static bool sxa37_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p;}
static bool sxa37_read(sx_context*c,uint64_t p,void*out,size_t n){return sxa37_span(c,p,n)&&sx_read(c,(int64_t)p,out,n);}
static uint16_t sxa37_be16(const uint8_t*b){return(uint16_t)((uint16_t)b[0]<<8|b[1]);}
static uint32_t sxa37_be32(const uint8_t*b){return(uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
static bool sxa37_raw_all(sx_context*c){uint8_t sentinel;if(!c||c->count||c->failed||c->size<=0||xx_pd_is_stopped(c->pd)||!sxa37_read(c,0,&sentinel,1))return false;return sx_raw(c,0,c->size);}
static bool sxa37_raw_tail(sx_context*c,uint64_t p){if(c&&!p&&!c->count)return sxa37_raw_all(c);uint8_t b;if(c->failed||p>(uint64_t)c->size||xx_pd_is_stopped(c->pd))return false;if(p==(uint64_t)c->size)return c->count>0;if(!sxa37_read(c,p,&b,1))return false;return sx_raw(c,(int64_t)p,c->size-(int64_t)p);}
static bool sxa37_emit(sx_context*c,const sx_layout*l,uint64_t p,uint64_t n,uint64_t*cursor){if(p<*cursor||!sxa37_span(c,p,n))return false;if(p>*cursor&&!sx_raw(c,(int64_t)*cursor,(int64_t)(p-*cursor)))return false;if(!sx_add(c,l,(int64_t)p,(int64_t)n))return false;*cursor=p+n;return true;}
static bool sxa37_aero(sx_context*c){uint8_t h[48];if(!sxa37_span(c,0,48))return sxa37_raw_all(c);if(!sxa37_read(c,0,h,sizeof(h)))return false;if(memcmp(h,"AERO\0\0\0\1",8)||h[15]!=0xca||h[31]!=0xca||h[47]!=0xca)return sxa37_raw_all(c);uint64_t cursor=0;return sxa37_emit(c,&sxa37_aero_header,0,12,&cursor)&&sxa37_raw_tail(c,cursor);}
static bool sxa37_song(sx_context*c,uint64_t p,bool standalone,uint64_t*cursor){
 uint8_t h[8];if(!sxa37_span(c,p,308))return sxa37_raw_tail(c,*cursor);if(!sxa37_read(c,p,h,4))return false;if(memcmp(h,"ASG1",4))return sxa37_raw_tail(c,*cursor);if(!sxa37_read(c,p+300,h,8))return false;if(memcmp(h,"APN1",4))return sxa37_raw_tail(c,*cursor);
 uint64_t count=standalone?255:sxa37_be32(h+4);
 if(!sxa37_emit(c,&sxa37_song_header,p,300,cursor)||!sxa37_emit(c,&sxa37_pattern_list,p+300,8,cursor))return false;
 p+=308;for(uint64_t i=0;i<count;i++){
  if(!sxa37_span(c,p,4))break;if(!sxa37_read(c,p,h,4))return false;uint64_t ch=sxa37_be16(h),rows=sxa37_be16(h+2),events=ch*rows;
  if(ch>16)break;uint64_t width=standalone&&!events?4:16;
  if(!sxa37_span(c,p,width+events*4))break;
  if(!sxa37_emit(c,width==4?&sxa37_empty_pattern:&sxa37_pattern,p,width,cursor))return false;
  p+=width+events*4;
 }
 return sxa37_raw_tail(c,*cursor);
}
static bool sxa37_ace(sx_context*c){
 uint8_t h[12];uint64_t cursor=0;
 if(c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_NEWBEAT_ACETRACKER_ASG1)return sxa37_song(c,0,true,&cursor);
 if(!sxa37_span(c,0,64))return sxa37_raw_all(c);if(!sxa37_read(c,0,h,7))return false;if(memcmp(h,"AM01\0\0\0",7))return sxa37_raw_all(c);
 if(!sxa37_read(c,56,h,4))return false;if(memcmp(h,"ASD1",4))return sxa37_raw_all(c);
 uint64_t amp=0,limit=(uint64_t)c->size<0x1000000U?(uint64_t)c->size:0x1000000U;uint8_t scan[4096];
 for(uint64_t p=64;p<limit&&!amp;){uint64_t n=limit-p;if(n>sizeof(scan))n=sizeof(scan);if(n<12)break;if(!sxa37_read(c,p,scan,(size_t)n))return false;for(uint64_t i=0;i+12<=n;i++)if(!memcmp(scan+i,"AMP",3)&&!memcmp(scan+i+8,"ASSH",4)){amp=p+i;break;}p+=n-11;}
 if(!amp)return sxa37_raw_all(c);
 if(!sxa37_emit(c,&sxa37_am_tag,0,4,&cursor)||!sxa37_emit(c,&sxa37_sample_tag,56,4,&cursor))return false;
 if(!sxa37_read(c,amp,h,12))return false;uint64_t instruments=sxa37_be32(h+4);unsigned revision=h[3];uint64_t stride=revision=='1'?0xcd:revision=='2'?0xd1:revision=='3'?0xd4:0;
 if(!sxa37_emit(c,&sxa37_patch_header,amp,12,&cursor))return false;
 uint64_t p=amp+12;if(!stride||instruments>((uint64_t)c->size-p)/stride)return sxa37_raw_tail(c,cursor);
 for(uint64_t i=0;i<instruments;i++){if(!sxa37_emit(c,&sxa37_instrument_name,p+4,20,&cursor))return false;p+=stride;}
 return sxa37_song(c,p,false,&cursor);
}
static bool sx_aeroace_build(sx_context*c){return c&&c->f&&sx_aeroace_supports(c->f->file_type)&&(c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_IONEO_AEROSTUDIO?sxa37_aero(c):sxa37_ace(c))&&!c->failed&&!xx_pd_is_stopped(c->pd);}
static bool sx_aeroace_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60ab0001U||id>0x60ab000aU||r->value.type==XX_VAR_TYPE_FLOAT||r->value.type==XX_VAR_TYPE_DOUBLE)return true;
 const wchar_t*s=NULL;
 if(id==0x60ab0001U)s=!xx_rt_wcscmp(r->name,L"payload_size")?L"Little-endian payload byte count; native detection reports total size as this value plus 12.":L"Eight-byte native signature: AERO 00 00 00 01.";
 else if(id==0x60ab0002U)s=L"AM01 module tag; the remaining module controls are raw.";
 else if(id==0x60ab0003U)s=L"ASD1 sample-data section tag at byte offset 0x38.";
 else if(id==0x60ab0005U){if(!xx_rt_wcscmp(r->name,L"instrument_count"))s=L"Stored number of synthesis instrument records.";else if(!xx_rt_wcscmp(r->name,L"instrument_data_tag"))s=L"ASSH tag before the instrument records.";else s=L"AMP1, AMP2 or AMP3 tag selects instrument record strides 0xCD, 0xD1 or 0xD4; other revisions remain raw.";}
 else if(id==0x60ab0006U)s=L"Twenty stored instrument-name bytes; synthesis parameters remain raw.";
 else if(id==0x60ab0007U){if(!xx_rt_wcscmp(r->name,L"signature"))s=L"ASG1 song metadata tag.";else if(!xx_rt_wcscmp(r->name,L"orders"))s=L"256 stored one-byte pattern order entries; native detection uses nonzero entries to derive order and pattern maxima.";else s=L"Twenty stored CP437 text bytes; raw bytes preserve the original encoding.";}
 else if(id==0x60ab0008U)s=!xx_rt_wcscmp(r->name,L"pattern_count")?L"Stored big-endian pattern count. Standalone ASG files save 255 pattern slots.":L"APN1 pattern-list tag.";
 else if(id==0x60ab0009U||id==0x60ab000aU){if(!xx_rt_wcscmp(r->name,L"channel_count"))s=L"Stored channel count; event data has four bytes per channel and row.";else if(!xx_rt_wcscmp(r->name,L"row_count"))s=L"Stored row count; event data has four bytes per channel and row.";else s=L"Twelve stored pattern-name bytes.";}
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_aeroace_family={sx_aeroace_supports,sx_aeroace_build,sx_aeroace_layouts,SX_COUNT(sx_aeroace_layouts),sx_aeroace_comment};
