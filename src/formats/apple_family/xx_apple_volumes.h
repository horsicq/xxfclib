/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original composition of existing native filesystem readers. No temporary
 * files or external converters: validated forks are copied into bounded RAM.
 */
#ifndef XX_APPLE_VOLUMES_PRIVATE_H
#define XX_APPLE_VOLUMES_PRIVATE_H
#include "xx_apple_family_private.h"
#include "xxfclib/formats/prodos/xx_prodos.h"
#include "xxfclib/formats/apple_pascal/xx_apple_pascal.h"
#include "xxfclib/formats/apple_dos33/xx_apple_dos33.h"
#include "xxfclib/formats/apple_dos33_32/xx_apple_dos33_32.h"
#include "xxfclib/formats/apple_dos32/xx_apple_dos32.h"
#include "xxfclib/formats/hfs/xx_hfs.h"
#include "xxfclib/formats/cpm/xx_cpm_presets.h"
typedef bool (*av_extract_fn)(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
typedef void (*av_free_fn)(void *);
static bool av_budget(Abstractformat *f,uint32_t key,uint64_t n) {
    xx_var v;bool ok;xx_var_init(&v);xx_var_set_u64(&v,n);ok=xx_format_set_extra_parameter(f,key,&v);xx_var_cleanup(&v);return ok;
}
static bool av_name(char *out,size_t cap,const char *prefix,const char *name) {
    static const char hex[]="0123456789ABCDEF";size_t at=0,i,n;
    if(!name || !*name)return false;
    n=prefix?xx_rt_strlen(prefix):0U;if(n+1U>=cap)return false;
    if(n){xx_rt_memcpy(out,prefix,n);at=n;out[at++]='/';}
    for(i=0;name[i];++i){unsigned char c=(unsigned char)name[i];
        if(c>=33U && c<=126U && c!='~' && c!='\\' && c!=':' && c!='<' && c!='>' && c!='"' && c!='|' && c!='?' && c!='*') {
            if(at+1U>=cap) {return false; } out[at++]=(char)c;
        } else {if(at+3U>=cap)return false;out[at++]='~';out[at++]=hex[c>>4U];out[at++]=hex[c&15U];}}
    out[at]=0;return af_safe(out);
}
/* kind: 0 conservative automatic ProDOS/Pascal/HFS, 1 ProDOS, 2 Pascal,
 * 3 HFS, 4 DOS16, 5 DOS32, 6 DOS13, 7 Apple-DO CP/M. A rejected hinted filesystem is not replaced by
 * another guess. A valid empty volume is accepted without inventing a file. */
static bool av_volume(af_work *w,xx_io_device *device,const char *prefix,unsigned kind) {
    Abstractformat *f=NULL;av_extract_fn extract=NULL;av_free_fn destroy=NULL;
    xx_archive_record_state *st=NULL;uint8_t h[512];int64_t saved;bool ok=false;
    uint64_t remaining,original_limit=w->limit;size_t expected,seen=0;
    if(!device || !af_poll(w) || w->used>w->limit)return false;
    remaining=(w->limit-w->used)/2U;if(remaining<4096U)return false;
    if(!kind){saved=xx_io_tell(device);if(saved<0 || xx_io_seek64(device,1024,SEEK_SET)!=0 || xx_io_read(device,h,sizeof(h))!=(ssize_t)sizeof(h) || xx_io_seek64(device,saved,SEEK_SET)!=0)return false;
        if((h[4]&0xf0U)==0xf0U && (h[4]&15U) && h[35]==39U && h[36]==13U)kind=1;
        else if(pm_le16(h)==0U && pm_le16(h+2)==6U && pm_le16(h+4)==0U && h[6]>=1U && h[6]<=7U)kind=2;
        else if(h[0]=='B' && h[1]=='D')kind=3;
        else {if(xx_io_size(device)!=143360 || xx_io_seek64(device,2816,SEEK_SET)!=0 || xx_io_read(device,h,sizeof(h))!=(ssize_t)sizeof(h) || xx_io_seek64(device,saved,SEEK_SET)!=0)return false;
            if((h[4]&0xf0U)==0xf0U && (h[4]&15U) && h[35]==39U && h[36]==13U)kind=1;
            else if(pm_le16(h)==0U && pm_le16(h+2)==6U && pm_le16(h+4)==0U && h[6]>=1U && h[6]<=7U)kind=2;else return false;}}
    switch(kind){
    case 1:f=(Abstractformat *)xx_prodos_create(device,0);extract=xx_prodos_extract_record_to_device;destroy=(av_free_fn)xx_prodos_free;break;
    case 2:f=(Abstractformat *)xx_apple_pascal_create(device,0);extract=xx_apple_pascal_extract_record_to_device;destroy=(av_free_fn)xx_apple_pascal_free;break;
    case 3:f=(Abstractformat *)xx_hfs_create(device,0);extract=xx_hfs_extract_record_to_device;destroy=(av_free_fn)xx_hfs_free;break;
    /* These callers already provide DOS-order sector images: normalized GCR,
     * DOS.MASTER slots and the explicit DOS hybrid carrier. The unadorned
     * reader's AUTO ambiguity refusal should not undo this known ordering. */
    case 4:f=(Abstractformat *)xx_apple_dos33_create(device,0);if(f)((xx_apple_dos33 *)f)->requested_order=XX_APPLE_DOS33_ORDER_DOS;extract=xx_apple_dos33_extract_record_to_device;destroy=(av_free_fn)xx_apple_dos33_free;break;
    case 5:f=(Abstractformat *)xx_apple_dos33_32_create(device,0);extract=xx_apple_dos33_32_extract_record_to_device;destroy=(av_free_fn)xx_apple_dos33_32_free;break;
    case 6:f=(Abstractformat *)xx_apple_dos32_create(device,0);extract=xx_apple_dos32_extract_record_to_device;destroy=(av_free_fn)xx_apple_dos32_free;break;
    case 7:f=(Abstractformat *)xx_cpm_create_preset(device,0,XX_CPM_PRESET_APPLE_DO);extract=xx_cpm_extract_record_to_device;destroy=(av_free_fn)xx_cpm_free;break;
    default:return false;}
    if(!f || !av_budget(f,XX_META_ID_OPT_MEMORY_LIMIT,remaining) || !av_budget(f,XX_META_ID_OPT_MAX_MEMBER_SIZE,w->member_limit))goto done;
    st=xx_format_create_archive_records_reading(f,NULL,w->pd);if(!st)goto done;
    /* The native child retains its own bounded view while output forks are
     * collected. Reserve half of the remaining ceiling for each lifetime. */
    w->limit=w->used+remaining;
    if(kind==1 && (((xx_prodos *)f)->damaged || ((xx_prodos *)f)->truncated))goto done;
    if(st->total_records<0) {goto done; } expected=(size_t)st->total_records;
    /* CP/M has no on-disk geometry signature. The explicit DOS hybrid view
     * supplies its Apple-DO preset and requires a live, validated directory. */
    if(kind==7 && !expected)goto done;
    if(expected>AF_COUNT_MAX || expected>AF_COUNT_MAX-w->s->count)goto done;
    while(st->has_record){const xx_archive_record *r;const char *name;char safe[96];uint64_t size;bool folder;uint8_t *out=NULL;xx_io_device *dest=NULL;
        if(!af_poll(w) || ++seen>expected || !(r=xx_format_get_current_archive_record(f,st)))goto done;
        name=xx_archive_record_get_original_name(r);size=xx_archive_record_get_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,UINT64_MAX);
        folder=xx_archive_record_get_meta_bool(r,XX_META_ID_IS_FOLDER,false);
        if(!av_name(safe,sizeof(safe),prefix,name) || size==UINT64_MAX || (folder && size))goto done;
        if(folder){if(!af_add(w,safe,0,0,NULL))goto done;w->s->items[w->s->count-1U].compression_method=65535U;}
        else {out=af_alloc(w,size,true);if(!out || !(dest=xx_io_mem_open(out,(size_t)size)) || !extract(f,st,dest,w->pd) || xx_io_tell(dest)!=(int64_t)size){if(dest)xx_io_close(dest);af_release(w,out,size);goto done;}
            xx_io_close(dest);if(!af_add(w,safe,0,size,out)){af_release(w,out,size);goto done;}}
        if(!xx_format_archive_record_move_to_next(f,st,w->pd) && st->has_record)goto done;
    }
    ok=seen==expected && af_poll(w);
done:if(st)xx_format_free_archive_records_reading(f,st);if(f)destroy(f);w->limit=original_limit;return ok;
}
static XXFC_MAYBE_UNUSED bool av_extent(af_work *w,uint64_t at,uint64_t size,const char *prefix,unsigned kind) {
    xx_io_device *sub;int64_t available=pm_available(w->f);bool ok;
    if(at>INT64_MAX || size>INT64_MAX || available<0 || at>(uint64_t)available || size>(uint64_t)available-at)return false;
    sub=xx_io_sub_open_ro(w->f->device,w->f->base_address+(int64_t)at,(int64_t)size);if(!sub)return false;
    ok=av_volume(w,sub,prefix,kind);xx_io_close(sub);return ok;
}
#endif
