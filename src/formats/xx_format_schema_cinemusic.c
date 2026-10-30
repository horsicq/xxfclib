/* SPDX-License-Identifier: MIT
 * Physical player metadata; original source-backed implementation. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxcine_robotham_header_fields[]={
 SX_FIELD("channel_count","uint16",0,2,SX_COUNT_PROP),
 SX_FIELD("table_1_count","uint16",2,2,SX_COUNT_PROP),
 SX_FIELD("table_2_count","uint16",4,2,SX_COUNT_PROP),
 SX_FIELD("sample_count","uint16",6,2,SX_COUNT_PROP),
};
static const sx_layout sxcine_robotham_header={0x60aa0001U,"robotham_header",sxcine_robotham_header_fields,SX_COUNT(sxcine_robotham_header_fields),SX_BE};
static const xx_data_struct_field_desc sxcine_robotham_channel_start_fields[]={
 SX_FIELD("sequence_offset","uint32",0,4,SX_OFFSET),
};
static const sx_layout sxcine_robotham_channel_start={0x60aa0002U,"robotham_channel_start",sxcine_robotham_channel_start_fields,SX_COUNT(sxcine_robotham_channel_start_fields),SX_BE};
static const xx_data_struct_field_desc sxcine_robotham_table_1_offset_fields[]={
 SX_FIELD("data_offset","uint32",0,4,SX_OFFSET),
};
static const sx_layout sxcine_robotham_table_1_offset={0x60aa0003U,"robotham_table_1_offset",sxcine_robotham_table_1_offset_fields,SX_COUNT(sxcine_robotham_table_1_offset_fields),SX_BE};
static const xx_data_struct_field_desc sxcine_robotham_table_2_offset_fields[]={
 SX_FIELD("data_offset","uint32",0,4,SX_OFFSET),
};
static const sx_layout sxcine_robotham_table_2_offset={0x60aa0004U,"robotham_table_2_offset",sxcine_robotham_table_2_offset_fields,SX_COUNT(sxcine_robotham_table_2_offset_fields),SX_BE};
static const xx_data_struct_field_desc sxcine_robotham_sample_fields[]={
 SX_FIELD("sample_offset","uint32",0,4,SX_OFFSET),
 SX_FIELD("sample_length_words","uint16",4,2,SX_NONE),
 SX_FIELD("repeat_offset","uint32",6,4,SX_OFFSET),
 SX_FIELD("repeat_length_words","uint16",10,2,SX_NONE),
};
static const sx_layout sxcine_robotham_sample={0x60aa0005U,"robotham_sample",sxcine_robotham_sample_fields,SX_COUNT(sxcine_robotham_sample_fields),SX_BE};
static const xx_data_struct_field_desc sxcine_cinemaware_header_fields[]={
 SX_FIELD("signature","uint32",0,4,SX_SIGNATURE),
 SX_FIELD("sample_count","uint8",4,1,SX_COUNT_PROP),
 SX_FIELD("sample_info_count","uint8",5,1,SX_COUNT_PROP),
};
static const sx_layout sxcine_cinemaware_header={0x60aa0006U,"cinemaware_header",sxcine_cinemaware_header_fields,SX_COUNT(sxcine_cinemaware_header_fields),SX_BE};
static const sx_layout*const sx_cinemusic_layouts[]={&sxcine_robotham_header,&sxcine_robotham_channel_start,&sxcine_robotham_table_1_offset,&sxcine_robotham_table_2_offset,&sxcine_robotham_sample,&sxcine_cinemaware_header};
static bool sx_cinemusic_supports(xx_file_type_t t){return t==XX_FILE_TYPE_DIE_MUSIC_PAULROBOTHAM_DAT||t==XX_FILE_TYPE_DIE_MUSIC_CINEMAWARE_CIN;}
static bool sxcine_span(sx_context*c,uint64_t p,uint64_t n){return c&&c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p;}
static bool sxcine_read(sx_context*c,uint64_t p,void*out,size_t n){return sxcine_span(c,p,n)&&sx_read(c,(int64_t)p,out,n);}
static uint16_t sxcine16(const uint8_t*b){return(uint16_t)(((uint16_t)b[0]<<8)|b[1]);}
static uint32_t sxcine32(const uint8_t*b){return((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];}
static bool sxcine_table(sx_context*c,const sx_layout*l,uint64_t p,uint64_t width,uint64_t count){return count&&sxcine_span(c,p,width*count)&&sx_table(c,l,(int64_t)p,(int64_t)width,(int64_t)count);}
/* Native-valid variants without complete authenticated metadata retain raw
 * bytes. A failed device read or cancellation must never become raw success. */
static bool sxcine_raw(sx_context*c){uint8_t b;if(c->count||c->failed||c->size<=0||!sxcine_read(c,0,&b,1)||xx_pd_is_stopped(c->pd))return false;return sx_raw(c,0,c->size);}
static bool sxcine_robotham(sx_context*c){
 uint8_t h[8],b[4];if(!sxcine_span(c,0,8))return sxcine_raw(c);if(!sxcine_read(c,0,h,8))return false;
 uint64_t ch=sxcine16(h),n1=sxcine16(h+2),n2=sxcine16(h+4),ns=sxcine16(h+6);
 /* The original Check2 rejects nonzero high bytes for all three counts.
  * Counts/pointers are physical metadata. External SSD sample offsets are
  * displayed but never followed in this DAT source device. */
 if(!ch||ch>4||!n1||n1>255||!n2||n2>255||!ns||ns>255)return sxcine_raw(c);
 uint64_t p1=8+ch*4,p2=p1+n1*4,sp=p2+n2*4,end=sp+ns*12;
 if(!sxcine_span(c,0,end)||!sxcine_span(c,end,254))return sxcine_raw(c);
 if(!sxcine_read(c,p2,b,4))return false;if(sxcine32(b)!=end)return sxcine_raw(c);
 for(uint64_t p=8;p<sp;p+=4){if(!sxcine_read(c,p,b,4))return false;uint32_t v=sxcine32(b);if(!v||(p<p1?(v>65535):(v>INT32_MAX||(v&1))))return sxcine_raw(c);}
 for(uint64_t p=end;p<end+254;p++){uint8_t v;if(!sxcine_read(c,p,&v,1))return false;if(v!=0x3f)return sxcine_raw(c);}
 return sx_add(c,&sxcine_robotham_header,0,8)&&sxcine_table(c,&sxcine_robotham_channel_start,8,4,ch)&&sxcine_table(c,&sxcine_robotham_table_1_offset,p1,4,n1)&&sxcine_table(c,&sxcine_robotham_table_2_offset,p2,4,n2)&&sxcine_table(c,&sxcine_robotham_sample,sp,12,ns)&&sx_raw(c,(int64_t)end,c->size-(int64_t)end);
}
static bool sxcine_cinemaware(sx_context*c){uint8_t h[6];if(!sxcine_span(c,0,sizeof(h)))return sxcine_raw(c);if(!sxcine_read(c,0,h,sizeof(h)))return false;if(memcmp(h,"IBLK",4)||!h[4]||h[4]>0x80||!h[5]||h[5]>0x80)return sxcine_raw(c);return sx_add(c,&sxcine_cinemaware_header,0,sizeof(h))&&sx_raw(c,sizeof(h),c->size-sizeof(h));}
static bool sx_cinemusic_build(sx_context*c){return c&&c->f&&sx_cinemusic_supports(c->f->file_type)&&(c->f->file_type==XX_FILE_TYPE_DIE_MUSIC_CINEMAWARE_CIN?sxcine_cinemaware(c):sxcine_robotham(c))&&!c->failed&&!xx_pd_is_stopped(c->pd);}
static bool sx_cinemusic_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||id<0x60aa0001U||id>=0x60ab0000U||r->value.type==XX_VAR_TYPE_FLOAT||r->value.type==XX_VAR_TYPE_DOUBLE)return true;
 const wchar_t*s=NULL;
 if(id==0x60aa0001U){if(!xx_rt_wcscmp(r->name,L"channel_count"))s=L"Number of channel-start offsets (1 to 4).";else if(!xx_rt_wcscmp(r->name,L"table_1_count"))s=L"Number of offsets in the first replay data table.";else if(!xx_rt_wcscmp(r->name,L"table_2_count"))s=L"Number of offsets in the second replay data table; its first offset equals the end of sample metadata.";else if(!xx_rt_wcscmp(r->name,L"sample_count"))s=L"Number of 12-byte external SSD sample descriptors.";}
 else if(id==0x60aa0002U||id==0x60aa0003U||id==0x60aa0004U)s=L"Byte offset relative to the start of this DAT file; the replay relocates this value in memory.";
 else if(id==0x60aa0005U){if(!xx_rt_wcscmp(r->name,L"sample_offset")||!xx_rt_wcscmp(r->name,L"repeat_offset"))s=L"Byte offset in the separate SSD sample file; not an offset in this DAT file.";else s=L"Length in 16-bit audio words; byte length is twice the stored value.";}
 else if(id==0x60aa0006U){if(!xx_rt_wcscmp(r->name,L"signature"))s=L"Physical IBLK tag checked by the native Cinemaware detector.";else if(!xx_rt_wcscmp(r->name,L"sample_count"))s=L"Stored sample count used by the detector to locate ASEQ at 0x16 + 0x8A times this value.";else s=L"Sample-information entry count read by the original player; 0x8A-byte entries remain raw.";}
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_cinemusic_family={sx_cinemusic_supports,sx_cinemusic_build,sx_cinemusic_layouts,SX_COUNT(sx_cinemusic_layouts),sx_cinemusic_comment};
