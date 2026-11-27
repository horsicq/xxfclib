/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Registered single CAB resource lookup. */
#include "../../common/xx_pe_resource_archives.h"
static bool sfx_cab_resource_candidate(Abstractformat *f,pm_stream *s,int64_t at,uint32_t bytes,xx_pd_struct *pd)
{
    if(carrier_stop(pd) || bytes<36) return false;
    xx_io_device *view=xx_io_sub_open_ro(f->device,f->base_address+at,bytes); if(!view) return false;
    xx_cab child; xx_cab_init(&child,view,0);
    bool ok=xx_cab_handle_base_info(&child.format,pd) && child.number_of_records>0 && child.number_of_records<=65535 &&
            child.format.format_size>=36 && child.format.format_size<=bytes;
    int64_t size=child.format.format_size; xx_cab_destroy(&child); xx_io_close(view);
    return ok && !carrier_stop(pd) && pm_add(f,s,"payload.cab",at,size);
}
static bool sfx_cab_resources(xx_sfx_cab *a,xx_pd_struct *pd)
{
    static const uint8_t sig[]={'M','S','C','F'};
    pm_stream *s=(pm_stream *)xx_mem_calloc(1,sizeof(*s)); if(!s) return false;
    bool ok=xx_sfx_pe_resource_archives(&a->format,s,sig,sizeof(sig),sfx_cab_resource_candidate,pd) && s->count==1;
    if(ok) a->payload_offset=s->items[0].offset-a->format.base_address;
    pm_free_stream(s); return ok;
}
