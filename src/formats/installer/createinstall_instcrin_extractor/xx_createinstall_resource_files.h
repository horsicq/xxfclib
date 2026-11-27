/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Private codec streams in registered FILE_RESOURCE leaves. */
#include "../../executable/pe/xx_pe_resources_internal.h"
static bool ci_resource_files(Abstractformat *f,ci_codec *codec,ci_walk **out,xx_pd_struct *pd)
{
    xx_pe pe; xx_resource_state *state=NULL; ci_walk *walk=NULL;
    bool ok=false,signature=false; uint64_t packed_total=0,raw_total=0; size_t visits=0;
    if(!f || !f->device || f->base_address<0 || (pd && xx_pd_is_stopped(pd))) return false;
    int64_t total=xx_io_total_size(f->device); if(total<f->base_address) return false;
    xx_pe_init(&pe,f->device,f->base_address);
    if(!xx_pe_handle_base_info(&pe.format,pd) || pe.format.format_size<=0 || pe.format.format_size>total-f->base_address) goto done;
    state=pe_resource_stream_create(&pe.format,pd); if(!state) goto done;
    walk=(ci_walk *)xx_mem_calloc(1,sizeof(*walk)); if(!walk) goto done;
    walk->archive_size=pe.format.format_size; walk->records_offset=-1;
    for(;;) {
        const xx_resource_record *r=pe_resource_stream_current(&pe.format,state);
        if(!r) break;
        if(++visits>4096 || (pd && xx_pd_is_stopped(pd))) goto done;
        if(r->type_name && ci_same(r->type_name,"FILE_RESOURCE")) {
            uint64_t raw=0,used=0; uint8_t head[8]; char name[80]; ci_member member;
            if(r->name_id==UINT32_MAX || r->name_id>65535 || r->language_id>65535 || r->offset<f->base_address ||
               r->size<=0 || r->size>64*1024*1024 || r->offset>total || r->size>total-r->offset || walk->count>=128 ||
               (uint64_t)r->size>UINT64_C(64)*1024*1024-packed_total) goto done;
            packed_total+=(uint64_t)r->size;
            if(r->size>=8 && ci_read_at(f->device,r->offset,head,8) && xx_rt_memcmp(head,ci_signature,8)==0) signature=true;
            ci_input_open(&codec->in,f->device,NULL,r->offset,(uint64_t)r->size); ci_sink_none(codec);
            if(!ci_decode_stream(codec,UINT64_C(32)*1024*1024,&raw,&used,pd) || !raw || used!=(uint64_t)r->size ||
               raw>UINT64_C(512)*1024*1024-raw_total) goto done;
            raw_total+=raw;
            xx_rt_snprintf(name,sizeof(name),"FILE_RESOURCE/%u/%u.bin",(unsigned)r->language_id,(unsigned)r->name_id);
            xx_mem_zero(&member,sizeof(member)); member.name=xx_str_dup(name); member.key=xx_str_dup(name);
            member.header_offset=member.data_offset=r->offset-f->base_address; member.packed_size=r->size; member.unpacked_size=(int64_t)raw;
            if(!member.name || !member.key || !ci_publish(walk,&member)) {
                xx_mem_free(member.name); xx_mem_free(member.key); goto done;
            }
        }
        if(!pe_resource_stream_next(&pe.format,state,pd)) break;
    }
    if(state->failed || !walk->count || !signature || (pd && xx_pd_is_stopped(pd))) goto done;
    xx_mem_free(walk->slots); walk->slots=NULL; walk->slot_count=0;
    *out=walk; walk=NULL; ok=true;
done:
    ci_walk_free(walk);
    if(state) pe_resource_stream_free(&pe.format,state);
    xx_pe_destroy(&pe); return ok;
}
