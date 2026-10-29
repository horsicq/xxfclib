/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Zoom disk image data structures, moved out of the systemtail schema family. */
#include "../../xx_format_schema_systemtail.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"

static const xx_data_struct_field_desc sxst_zoom_header_fields[] = {
    SX_FIELD("signature","char[4]",0,4,SX_SIGNATURE),
    SX_FIELD("first_cylinder","uint8",4,1,SX_NONE),
    SX_FIELD("last_cylinder","uint8",5,1,SX_NONE),
    SX_FIELD("version","uint8",6,1,SX_ID),
    SX_FIELD("unknown_07","uint8",7,1,SX_NONE),
    SX_FIELD("flags","uint32",8,4,SX_FLAGS),
    SX_FIELD("unknown_0c","byte[12]",12,12,SX_NONE),
    SX_FIELD("attached_text_raw_size","uint32",24,4,SX_SIZE),
    SX_FIELD("attached_text_packed_size","uint32",28,4,SX_SIZE),
    SX_FIELD("note_offset","uint32",32,4,SX_OFFSET),
    SX_FIELD("password_protected","uint8",36,1,SX_FLAGS),
    SX_FIELD("unknown_25","byte[35]",37,35,SX_NONE),
    SX_FIELD("header_checksum","uint32",72,4,SX_CHECKSUM),
};
const sx_layout sxst_zoom_header = {0x60370005U,"zoom_header",sxst_zoom_header_fields,SX_COUNT(sxst_zoom_header_fields),SX_BE};
static const xx_data_struct_field_desc sxst_zoom_note_checksum_fields[] = {
    SX_FIELD("note_checksum","uint32",0,4,SX_CHECKSUM),
};
const sx_layout sxst_zoom_note_checksum = {0x60370006U,"zoom_note_checksum",sxst_zoom_note_checksum_fields,SX_COUNT(sxst_zoom_note_checksum_fields),SX_BE};
static const xx_data_struct_field_desc sxst_zoom_chunk_fields[] = {
    SX_FIELD("cylinders","uint8[5]",0,5,SX_NONE),
    SX_FIELD("unknown_05","uint8",5,1,SX_NONE),
    SX_FIELD("sector_masks","uint32[5]",6,20,SX_FLAGS),
    SX_FIELD("packed_size","uint16",26,2,SX_SIZE),
    SX_FIELD("middle_size","uint16",28,2,SX_SIZE),
    SX_FIELD("final_size","uint16",30,2,SX_SIZE),
    SX_FIELD("lzhuf_flag","uint16",32,2,SX_FLAGS),
    SX_FIELD("packed_checksum","uint32",34,4,SX_CHECKSUM),
    SX_FIELD("record_checksum","uint32",38,4,SX_CHECKSUM),
};
const sx_layout sxst_zoom_chunk = {0x60370007U,"zoom_chunk",sxst_zoom_chunk_fields,SX_COUNT(sxst_zoom_chunk_fields),SX_BE};
#define SXST_LIMIT 24000U
#define SXST_ADD(c,l,p) sxst_put(c,&sxst_##l,p,1)
static bool sxst_zoom(sx_context*c){uint8_t h[76],b[42];if(!sxst_read(c,0,h,76)||!SXST_ADD(c,zoom_header,0))return false;uint64_t p=76,n=sxst_uint(h+28,4,true);if(n){if(!sxst_span(c,p,n+4))return true;if(!SXST_ADD(c,zoom_note_checksum,p+n))return false;p+=n+4;}if(h[36])return true;for(unsigned i=0;i<SXST_LIMIT&&c->count<SXST_LIMIT;i++){if(!sxst_read(c,p,b,42))break;n=sxst_uint(b+26,2,true);if(!sxst_span(c,p+42,n)||!SXST_ADD(c,zoom_chunk,p))break;p+=42+n;}return true;}
bool sx_zoom_build(sx_context*c)
{
 return sxst_zoom(c);
}
#undef SXST_LIMIT
#undef SXST_ADD
