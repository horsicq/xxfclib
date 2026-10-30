/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * IS_STORED data structures, moved out of the archivelater schema family. */
#include "../../xx_format_schema_archivelater.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"

static const xx_data_struct_field_desc sxal_is_stored_signature_fields[] = {
 SX_FIELD("signature","char[4]",0,4,SX_SIGNATURE),
};
const sx_layout sxal_is_stored_signature = {UINT32_C(0x6020004f),"is_stored_signature",sxal_is_stored_signature_fields,SX_COUNT(sxal_is_stored_signature_fields),SX_BE};
static const xx_data_struct_field_desc sxal_is_stored_marker_fields[] = {
 SX_FIELD("marker","char[3]",0,3,SX_SIGNATURE),
};
const sx_layout sxal_is_stored_marker = {UINT32_C(0x60200050),"is_stored_marker",sxal_is_stored_marker_fields,SX_COUNT(sxal_is_stored_marker_fields),SX_BE};
bool sx_is_stored_build(sx_context*c){uint8_t b[16];if(!sxal_read(c,0,b,sizeof(b)))return false;if(xx_rt_memcmp(b,"\xe7\x50\xa9\xae",4)||xx_rt_memcmp(b+6,"\xe7\x50\xa9\xae",4)||xx_rt_memcmp(b+12,"\xdd\xc1\xc0",3))return false;return sxal_add(c,&sxal_is_stored_signature,0)&&sxal_add(c,&sxal_is_stored_signature,6)&&sxal_add(c,&sxal_is_stored_marker,12);}
