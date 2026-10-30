/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * UHARC data structures, moved out of the archivelater schema family. */
#include "../../xx_format_schema_archivelater.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"

static const xx_data_struct_field_desc sxal_uharc_header_fields[] = {
 SX_FIELD("signature","char[3]",0,3,SX_SIGNATURE),
 SX_FIELD("version","uint8",3,1,SX_ID),
};
const sx_layout sxal_uharc_header = {UINT32_C(0x6020004e),"uharc_header",sxal_uharc_header_fields,SX_COUNT(sxal_uharc_header_fields),SX_BE};
bool sx_uharc_build(sx_context*c)
{
 return sxal_add(c,&sxal_uharc_header,0);
}
