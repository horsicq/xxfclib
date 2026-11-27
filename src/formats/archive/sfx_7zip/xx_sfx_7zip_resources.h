/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Private 7z resource adapter. */
#include "../../common/xx_pe_resource_archives.h"
static bool sfx7_resource_candidate(Abstractformat *f,pm_stream *s,int64_t at,uint32_t bytes,xx_pd_struct *pd)
{
    uint8_t h[32]; uint64_t offset,length;
    if(bytes<32 || !pm_read(f,at,h,sizeof(h))) return false;
    offset=xx_data_get_u64(h+12,8,0,false); length=xx_data_get_u64(h+20,8,0,false);
    if(offset>bytes-32U || length>bytes-32U-offset) return false;
    return sfx7_resource_at_parse(f,s,at,pd);
}
static bool sfx7_resources(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    static const uint8_t signature[]={0x37,0x7a,0xbc,0xaf,0x27,0x1c};
    return xx_sfx_pe_resource_archives(f,s,signature,sizeof(signature),sfx7_resource_candidate,pd);
}
