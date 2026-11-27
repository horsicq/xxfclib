/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* Private PE resource discovery for native RAR. */
#ifndef XX_RAR_PE_RESOURCE_INTERNAL_H
#define XX_RAR_PE_RESOURCE_INTERNAL_H
#include "../../executable/pe/xx_pe_resources_internal.h"

/* Authenticate one complete named resource using a bounded ordinary RAR
 * reader. Its source starts with RAR, so this probe cannot recurse into PE.
 * Reject multiple selected archives or a later malformed resource directory. */
static int xx_rar_pe_resource_find(Abstractformat *self, xx_rar_version_t *version,
                                    int64_t *signature, int64_t *end,
                                    xx_pd_struct *pd)
{
    uint8_t h[64], magic[8]; uint32_t pe_at;
    int64_t total=xx_io_total_size(self->device),selected=-1,stop=-1;
    xx_rar_version_t selected_version=XX_RAR_VERSION_UNKNOWN;
    xx_pe pe; xx_resource_state *state=NULL;
    bool recognized=false,ok=false; unsigned rows=0;
    if (self->base_address<0 || total-self->base_address<64 ||
        !xx_rar_read_exact_at(self->device,self->base_address,h,sizeof(h)) ||
        h[0]!='M' || h[1]!='Z') return 0;
    pe_at=xx_data_get_u32(h+60,4,0,false);
    if (pe_at<64U || pe_at>(uint64_t)(total-self->base_address)-4U ||
        !xx_rar_read_exact_at(self->device,self->base_address+pe_at,magic,4) ||
        xx_rt_memcmp(magic,"PE\0\0",4)) return 0;
    xx_pe_init(&pe,self->device,self->base_address);
    state=pe_resource_stream_create(&pe.format,pd);
    if (!state) goto done;
    for (;;) {
        const xx_resource_record *record=pe_resource_stream_current(&pe.format,state);
        if (!record) break;
        if (++rows>4096U || xx_pd_is_stopped(pd)) goto done;
        if ((record->type_name && (!xx_rt_strcmp(record->type_name,"THEMES") ||
                                  !xx_rt_strcmp(record->type_name,"RARFILE"))) ||
            (record->type_id==10U && record->name && !xx_rt_strcmp(record->name,"CABFILE")) ||
            (record->type_name && !xx_rt_strcmp(record->type_name,"PACK") &&
             record->name && !xx_rt_strcmp(record->name,"PACKEDFILE"))) {
            xx_io_device *view; xx_rar probe; xx_rar_version_t candidate;
            if (record->size<20 || record->offset<self->base_address ||
                record->offset>total || record->size>total-record->offset ||
                !xx_rar_read_exact_at(self->device,record->offset,magic,sizeof(magic))) goto done;
            if (!xx_rt_memcmp(magic,g_xx_rar5_signature,8)) candidate=XX_RAR_VERSION_5;
            else if (!xx_rt_memcmp(magic,g_xx_rar4_signature,7)) candidate=XX_RAR_VERSION_4;
            else goto next;
            recognized=true;
            if (selected>=0 || record->size>256*1024*1024) goto done;
            view=xx_io_sub_open_ro(self->device,record->offset,record->size);
            if (!view) goto done;
            xx_rar_init(&probe,view,0);
            ok=xx_rar_handle_base_info(&probe.format,pd) &&
                (probe.format.number_of_archive_records>0 || !probe.format.is_crypted) &&
                probe.format.format_size==record->size;
            xx_rar_destroy(&probe); xx_io_close(view);
            if (!ok) goto done;
            selected=record->offset; stop=record->offset+record->size;
            selected_version=candidate; ok=false;
        }
next:
        if (!pe_resource_stream_next(&pe.format,state,pd)) break;
    }
    if (state->failed || selected<0 || xx_pd_is_stopped(pd)) goto done;
    *version=selected_version; *signature=selected; if(end)*end=stop; ok=true;
done:
    if (state) pe_resource_stream_free(&pe.format,state);
    xx_pe_destroy(&pe);
    return ok?1:(recognized || xx_pd_is_stopped(pd)?-1:0);
}
#endif
