/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://ecma-international.org/publications-and-standards/standards/ecma-335/
 * Standalone CLI metadata root 1.1, including portable PDB streams; exports streams without decoding rows or executing code.
 */
#include "xxfclib/formats/dotnet_metadata/xx_dotnet_metadata.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16],d[8],v[256],b; uint32_t len; uint16_t count; uint64_t total; int64_t at; unsigned i,j;
    char names[64][33]; uint32_t offsets[64],sizes[64];
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"BSJB",4) || xx_data_get_u16(h+4, 2, 0, false)!=1 || xx_data_get_u16(h+6, 2, 0, false)!=1 || xx_data_get_u32(h+8, 4, 0, false)) return false;
    len=xx_data_get_u32(h+12, 4, 0, false); if(!len || len>256 || len%4 || !pm_read(f,16,v,len)) return false;
    for(i=0;i<len && v[i];++i) {} if(i==len) return false;
    at=16+len; if(!pm_read(f,at,h,4) || xx_data_get_u16(h, 2, 0, false)) return false;
    count=xx_data_get_u16(h+2, 2, 0, false); at+=4; if(!count || count>64) return false;
    for(i=0;i<count;++i) {
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,d,8)) return false;
        offsets[i]=xx_data_get_u32(d, 4, 0, false); sizes[i]=xx_data_get_u32(d+4, 4, 0, false); at+=8;
        for(j=0;j<33;++j) { if(j==32 || !pm_read(f,at++,&b,1)) return false; names[i][j]=(char)b; if(!b) break; if(b<32 || b>126 || b=='/' || b=='\\' || b==':') return false; }
        if(!j) { return false; } at=(at+3)&~INT64_C(3);
        for(j=0;j<i;++j) if(!xx_rt_strcmp(names[j],names[i])) return false;
    }
    total=(uint64_t)at;
    for(i=0;i<count;++i) {
        uint64_t end=(uint64_t)offsets[i]+sizes[i]; char name[64];
        if(offsets[i]<(uint64_t)at || offsets[i]%4 || end>(uint64_t)pm_available(f)) return false;
        for(j=0;j<i;++j) if(sizes[i] && sizes[j] && offsets[i]<(uint64_t)offsets[j]+sizes[j] && offsets[j]<end) return false;
        xx_rt_snprintf(name,sizeof(name),"stream-%s.bin",names[i]);
        if(!pm_add(f,s,name,offsets[i],sizes[i])) return false;
        if(end>total) total=end;
    }
    s->size=(int64_t)total; return true;
}

void xx_dotnet_metadata_init(xx_dotnet_metadata *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_DOTNET_METADATA,"dotnet_metadata"); } }
xx_dotnet_metadata *xx_dotnet_metadata_create(xx_io_device *d,int64_t b) { xx_dotnet_metadata *r=(xx_dotnet_metadata *)xx_mem_alloc(sizeof(*r)); if(r) xx_dotnet_metadata_init(r,d,b); return r; }
void xx_dotnet_metadata_destroy(xx_dotnet_metadata *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_dotnet_metadata_free(xx_dotnet_metadata *r) { if(r) { xx_dotnet_metadata_destroy(r); xx_mem_free(r); } }
bool xx_dotnet_metadata_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_dotnet_metadata_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
