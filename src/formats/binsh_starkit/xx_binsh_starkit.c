/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for Tcl Starkit carried by a shell launcher.
 */
#include "xxfclib/formats/binsh_starkit/xx_binsh_starkit.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/formats/starkit/xx_starkit.h"
#include "xxfclib/data/xx_data.h"

static int64_t binsh_starkit_available(Abstractformat *f) {
    int64_t n;
    if (!f || !f->device || f->base_address<0 || (n=xx_io_size(f->device))<f->base_address) return -1;
    return n-f->base_address;
}
static bool binsh_starkit_read(Abstractformat *f, int64_t at, void *p, size_t n) {
    int64_t left=binsh_starkit_available(f);
    size_t capacity=xx_get_file_buffer_size(),done=0;
    if(at<0 || left<at || n>(uint64_t)(left-at) || (!p && n) ||
       xx_io_seek64(f->device,f->base_address+at,SEEK_SET)!=0) return false;
    while(done<n) {
        size_t request=n-done<capacity?n-done:capacity;
        ssize_t got=xx_io_read(f->device,(uint8_t *)p+done,request);
        if(got<=0 || (size_t)got>request) return false;
        done+=(size_t)got;
    }
    return true;
}

typedef struct binsh_starkit_carrier {int64_t size;bool (*info)(Abstractformat *,xx_pd_struct *);void (*destroy)(Abstractformat *);} binsh_starkit_carrier;
static int64_t binsh_starkit_starkit_offset(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[4096];int64_t n;size_t size,i;xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;n=binsh_starkit_available(&f);
    size=n<4096?(size_t)n:4096;if(n<8||!binsh_starkit_read(&f,0,h,size)||xx_rt_memcmp(h,"#!/bin/sh",9))return -1;
    for(i=9;i+8<=size;++i)if(!xx_rt_memcmp(h+i,"JL\x1a\0",4)&&xx_data_get_u32(h+i+4,4,0,true)==(uint64_t)n-i)return base+(int64_t)i;
    return -1;
}
static void binsh_starkit_carrier_destroy(Abstractformat *f) {
    binsh_starkit_carrier *c=f?f->priv:NULL;void (*destroy)(Abstractformat *);if(!c)return;
    destroy=c->destroy;f->priv=NULL;xx_mem_free(c);if(destroy)destroy(f);else xx_format_cleanup_extra_parameters(f);
}
static bool binsh_starkit_carrier_info(Abstractformat *f,xx_pd_struct *pd) {
    binsh_starkit_carrier *c=f->priv;if(!c||!c->info(f,pd))return false;f->format_size=c->size;return true;
}
static int64_t binsh_starkit_carrier_size(Abstractformat *f,xx_pd_struct *pd) {
    if(!f||!f->priv||(!f->base_info_handled&&!binsh_starkit_carrier_info(f,pd)))return -1;
    f->format_size=((binsh_starkit_carrier *)f->priv)->size;return f->format_size;
}
static uint64_t binsh_starkit_carrier_records(Abstractformat *f,xx_pd_struct *pd) {
    return binsh_starkit_carrier_size(f,pd)>=0?f->number_of_archive_records:0;
}
static bool binsh_starkit_set_carrier(Abstractformat *f,xx_file_type_t type,int64_t base) {
    binsh_starkit_carrier *c;if(!f)return false;c=xx_mem_alloc(sizeof(*c));if(!c)return false;
    c->size=xx_io_total_size(f->device)-base;c->info=f->handle_base_info;c->destroy=f->destroy;f->priv=c;f->file_type=type;f->handle_base_info=binsh_starkit_carrier_info;f->get_format_size=binsh_starkit_carrier_size;f->get_number_of_archive_records=binsh_starkit_carrier_records;f->destroy=binsh_starkit_carrier_destroy;return true;
}



Abstractformat *xx_binsh_starkit_create(xx_io_device *d,int64_t base) {
    int64_t at=binsh_starkit_starkit_offset(d,base);xx_starkit *r;
    if(at<0&&d&&xx_io_total_size(d)>base)return NULL;
    r=xx_starkit_create(d,at<0?base:at);
    if(r&&!binsh_starkit_set_carrier(&r->format,XX_FILE_TYPE_BINSH_STARKIT,base)){xx_starkit_free(r);r=NULL;}
    return (Abstractformat *)r;
}
void xx_binsh_starkit_free(Abstractformat *f) {
    if(f){binsh_starkit_carrier_destroy(f);xx_mem_free(f);}
}

xx_file_type_t xx_binsh_starkit_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t cursor,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    n=binsh_starkit_available(&f);
    if(n>=24 && binsh_starkit_read(&f,0,h,24) && !xx_rt_memcmp(h,"#!/bin/sh",9) && binsh_starkit_starkit_offset(d,base)>=0)type=XX_FILE_TYPE_BINSH_STARKIT;
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_binsh_starkit_open(xx_io_device *d) {return xx_binsh_starkit_create(d,0);}
static const xx_file_type_t xx_binsh_starkit_types[]={XX_FILE_TYPE_BINSH_STARKIT};
static const xx_format_search_desc xx_binsh_starkit_desc={xx_binsh_starkit_types,1,NULL,0,xx_binsh_starkit_open,xx_binsh_starkit_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(binsh_starkit,xx_binsh_starkit_desc)
