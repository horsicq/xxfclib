/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Only original BCM! initial flat-coded words, before adaptive symbols. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxbcm_bcm80_first_block_fields[]={
 SX_FIELD("signature","bytes",0,4,SX_SIGNATURE),
 SX_FIELD("first_block_length_code","uint32",4,4,SX_NONE),
 SX_FIELD("primary_index_code","uint32",8,4,SX_NONE),
};
static const sx_layout sxbcm_bcm80_first_block={UINT32_C(0x60a70001),"bcm80_first_block",sxbcm_bcm80_first_block_fields,SX_COUNT(sxbcm_bcm80_first_block_fields),SX_BE};
static const xx_data_struct_field_desc sxbcm_bcm80_empty_fields[]={
 SX_FIELD("signature","bytes",0,4,SX_SIGNATURE),
 SX_FIELD("end_marker_code","uint32",4,4,SX_NONE),
 SX_FIELD("empty_crc_code","uint32",8,4,SX_NONE),
};
static const sx_layout sxbcm_bcm80_empty={UINT32_C(0x60a70002),"bcm80_empty",sxbcm_bcm80_empty_fields,SX_COUNT(sxbcm_bcm80_empty_fields),SX_BE};
static const sx_layout*const sx_bcmrange_layouts[]={&sxbcm_bcm80_first_block,&sxbcm_bcm80_empty};
static bool sx_bcmrange_supports(xx_file_type_t type){return type==XX_FILE_TYPE_BCM;}
static bool sx_bcmrange_build(sx_context*c){
 if(!c||!c->f||!sx_bcmrange_supports(c->f->file_type)||c->size<=0||c->f->base_address<0||c->f->base_address>INT64_MAX-c->size||c->failed||xx_pd_is_stopped(c->pd))return false;
 if((!c->f->base_info_handled&&!xx_format_handle_base_info(c->f,c->pd))||!c->f->is_valid)return false;
 uint8_t b[12];size_t n=c->size<12?(size_t)c->size:12;if(!sx_read(c,0,b,n))return false;
 if(c->size>=16&&!xx_rt_memcmp(b,"BCM!",4)){
  uint32_t length=~xx_data_get_u32(b,sizeof b,4,true),second=~xx_data_get_u32(b,sizeof b,8,true);
  /* Decode only for variant/range validation, never into a displayed field.
   * The first two uniform words are byte-aligned complements. After any
   * adaptive symbol this physical equivalence no longer holds. */
  const sx_layout*l=NULL;
  if(length&&length<=UINT32_C(268435456)&&second>=1&&second<=length)l=&sxbcm_bcm80_first_block;
  else if(!length&&!second)l=&sxbcm_bcm80_empty;
  if(l)return sx_add(c,l,0,12)&&sx_raw(c,12,c->size-12)&&!c->failed&&!xx_pd_is_stopped(c->pd);
 }
 /* BCM1 uses model-coded words; unsupported/cached-damaged variants stay raw. */
 return sx_raw(c,0,c->size)&&!c->failed&&!xx_pd_is_stopped(c->pd);
}
static bool sx_bcmrange_comment(xx_data_struct_record*r,uint32_t id){
 if(!r||!r->name||(id!=sxbcm_bcm80_first_block.id&&id!=sxbcm_bcm80_empty.id)||r->value.type==XX_VAR_TYPE_FLOAT||r->value.type==XX_VAR_TYPE_DOUBLE)return true;
 const wchar_t*s=NULL;
 if(!xx_rt_wcscmp(r->name,L"signature"))s=L"BCM! variant (original v1.10 beta and later). BCM1 uses different model-coded headers and remains raw.";
 else if(!xx_rt_wcscmp(r->name,L"first_block_length_code"))s=L"Actual first flat-coded 32-bit word, preserved as a big-endian unsigned code value. Its ones-complement decodes to the first block length; this field is not a plain length or total decompressed size.";
 else if(!xx_rt_wcscmp(r->name,L"primary_index_code"))s=L"Actual second flat-coded 32-bit word before adaptive symbols, preserved unchanged. Its ones-complement decodes to the one-based BWT primary index; it is not a source byte offset.";
 else if(!xx_rt_wcscmp(r->name,L"end_marker_code"))s=L"Actual first flat-coded word in an empty stream: 0xFFFFFFFF encodes the zero end marker. The stored code is preserved, without conversion to a plain length.";
 else if(!xx_rt_wcscmp(r->name,L"empty_crc_code"))s=L"Actual second flat-coded word in an empty stream: 0xFFFFFFFF encodes the CRC of empty data (zero). It remains an encoded code value, not a displayed decoded checksum. Later adaptive CRC words have no fixed physical word extent.";
 return !s||xx_data_struct_record_set_comment(r,s);
}
const sx_family sx_bcmrange_family={sx_bcmrange_supports,sx_bcmrange_build,sx_bcmrange_layouts,SX_COUNT(sx_bcmrange_layouts),sx_bcmrange_comment};
