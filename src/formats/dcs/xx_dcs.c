/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/dcs/xx_dcs.h"
#include "../xx_bounded_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"

/* DCS v1 block stream (SXSEXP ProcessFileDCS layout); the Windows raw LZMS
 * decoder consumes each bounded block without disk staging or a subprocess. */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
static bool dcs_dcs_decode(Abstractformat *f, pm_member *m, xx_io_device *output, xx_pd_struct *pd) {
#ifdef _WIN32
    typedef BOOL (WINAPI *bdm_create_fn)(DWORD, void *, void **);
    typedef BOOL (WINAPI *bdm_decode_fn)(void *, const void *, SIZE_T, void *, SIZE_T, SIZE_T *);
    typedef BOOL (WINAPI *bdm_close_fn)(void *);
    HMODULE api=NULL;
    bdm_create_fn create=NULL; bdm_decode_fn decode=NULL; bdm_close_fn close=NULL;
    void *handle=NULL; FARPROC proc;
    uint8_t h[12],bh[8],*packed=NULL,*plain=NULL;
    uint64_t position=12,produced=0;
    uint32_t i,count,total;
    bool result=false;
    pm_stream *s=(pm_stream *)m->context;
    uint64_t budget=bdm_budget(f),reserve=s?(uint64_t)s->capacity*sizeof(pm_member):0;
    if(reserve>budget) return false;
    budget-=reserve;
    if(!pm_read(f,0,h,sizeof(h)) || xx_mem_compare(h,"DCS\1",4)) return false;
    count=bdm_u32(h+4); total=bdm_u32(h+8);
    if(!count || count>65536U || total!=(uint64_t)m->size) return false;
    /* Search only the system directory; never load an adjacent Cabinet.dll. */
    api=LoadLibraryExW(L"cabinet.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!api) goto done;
    proc=GetProcAddress(api,"CreateDecompressor");
    if(!proc) goto done;
    xx_mem_copy(&create,&proc,sizeof(create));
    proc=GetProcAddress(api,"Decompress");
    if(!proc) goto done;
    xx_mem_copy(&decode,&proc,sizeof(decode));
    proc=GetProcAddress(api,"CloseDecompressor");
    if(!proc) goto done;
    xx_mem_copy(&close,&proc,sizeof(close));
    if(!create((1U<<29)|5U,NULL,&handle)) goto done;
    for(i=0;i<count;++i) {
        uint32_t stored,expected,packed_size; SIZE_T actual=0;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)position,bh,sizeof(bh))) goto done;
        stored=bdm_u32(bh); expected=bdm_u32(bh+4);
        if(stored<5U || !expected || expected>BDM_BLOCK_LIMIT || stored-4U>BDM_BLOCK_LIMIT ||
           (uint64_t)expected+stored-4U>budget || produced+expected>total ||
           position+stored+4U>(uint64_t)pm_available(f)) goto done;
        packed_size=stored-4U;
        packed=(uint8_t *)xx_mem_alloc(packed_size); plain=(uint8_t *)xx_mem_alloc(expected);
        if(!packed || !plain || !pm_read(f,(int64_t)position+8,packed,packed_size) ||
           !decode(handle,packed,packed_size,plain,expected,&actual) || actual!=expected ||
           (pd && xx_pd_is_stopped(pd)) || !bdm_write(output,plain,expected,pd)) goto done;
        xx_mem_free(packed); packed=NULL; xx_mem_free(plain); plain=NULL;
        position+=(uint64_t)stored+4U; produced+=expected;
    }
    result=produced==total && position==(uint64_t)pm_available(f);
done:
    xx_mem_free(packed); xx_mem_free(plain);
    if(handle && close && !close(handle)) result=false;
    if(api) FreeLibrary(api);
    return result;
#else
    (void)f; (void)m; (void)output; (void)pd;
    return false;
#endif
}

static bool dcs_dcs_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    uint8_t h[12],bh[8];
    uint32_t count,total,i;
    uint64_t position=12,produced=0;
    int64_t size=pm_available(f);
    if(size<12 || !pm_read(f,0,h,sizeof(h)) || xx_mem_compare(h,"DCS\1",4)) return false;
    count=bdm_u32(h+4); total=bdm_u32(h+8);
    if(!count || count>65536U || !total || total>BDM_MEMORY_LIMIT) return false;
    for(i=0;i<count;++i) {
        uint32_t stored,expected;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)position,bh,sizeof(bh))) return false;
        stored=bdm_u32(bh); expected=bdm_u32(bh+4);
        if(stored<5U || !expected || expected>BDM_BLOCK_LIMIT || stored-4U>BDM_BLOCK_LIMIT ||
           position+stored+4U>(uint64_t)size || produced+expected>total) return false;
        produced+=expected; position+=(uint64_t)stored+4U;
    }
    if(produced!=total || position!=(uint64_t)size || !bdm_add(f,s,"payload.bin",12,size-12)) return false;
    s->items[0].size=total; s->items[0].read_all=dcs_dcs_decode;
    s->items[0].context=s;
    s->size=size;
    return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd) {
    if ((pd && xx_pd_is_stopped(pd)) || format->file_type != XX_FILE_TYPE_DCS) return false;
    return dcs_dcs_parse(format,members,pd);
}

Abstractformat *xx_dcs_create(xx_io_device *device, int64_t base) {
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) pm_init(format, device, base, XX_FILE_TYPE_DCS, "dcs");
    return format;
}
void xx_dcs_free(Abstractformat *format) {
    if (format) { xx_format_destroy(format); xx_mem_free(format); }
}

/* Detection keeps the inexpensive signature rule; the reader validates the grammar. */
xx_file_type_t xx_dcs_detect(xx_io_device *device, int64_t base) {
    uint8_t signature[4]; int64_t saved,total;
    xx_file_type_t result=XX_FILE_TYPE_UNKNOWN;
    if(!device || base<0 || (total=xx_io_size(device))<base || total-base<4) return result;
    saved=xx_io_tell(device);
    if(xx_io_read_at(device,base,signature,sizeof(signature)) &&
       !xx_mem_compare(signature,"DCS\1",sizeof(signature))) result=XX_FILE_TYPE_DCS;
    if(saved>=0 && xx_io_seek64(device,saved,SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *dcs_open(xx_io_device *device) {
    return xx_dcs_create(device, 0);
}
static const xx_file_type_t dcs_types[] = {XX_FILE_TYPE_DCS};
static const xx_format_search_desc dcs_descriptor = {
    dcs_types, 1, NULL, 0, dcs_open, xx_dcs_free, true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(dcs, dcs_descriptor)
