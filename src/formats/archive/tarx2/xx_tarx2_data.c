/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * TARX2 data structures, moved out of the archivelater schema family. */
#include "../../xx_format_schema_archivelater.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"

static const xx_data_struct_field_desc sxal_tarx2_signature_fields[] = {
 SX_FIELD("signature","char[4]",0,4,SX_SIGNATURE),
};
const sx_layout sxal_tarx2_signature = {UINT32_C(0x60200051),"tarx2_signature",sxal_tarx2_signature_fields,SX_COUNT(sxal_tarx2_signature_fields),SX_BE};
static const xx_data_struct_field_desc sxal_tarx2_marker_fields[] = {
 SX_FIELD("marker","char[4]",0,4,SX_SIGNATURE),
};
const sx_layout sxal_tarx2_marker = {UINT32_C(0x60200052),"tarx2_marker",sxal_tarx2_marker_fields,SX_COUNT(sxal_tarx2_marker_fields),SX_BE};
bool sx_tarx2_build(sx_context*c){uint8_t b[16];static const uint8_t a[4]={0x78,0x76,0x95,0x7d},z[4]={0x9c,0x3b,0xad,0x45};if(!sxal_read(c,0,b,sizeof(b))||xx_rt_memcmp(b,a,4)||xx_rt_memcmp(b+8,z,4))return false;return sxal_add(c,&sxal_tarx2_signature,0)&&sxal_add(c,&sxal_tarx2_marker,8);}
