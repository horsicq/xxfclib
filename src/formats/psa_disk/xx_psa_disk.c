/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for PSA diagnostic FAT image.
 */
#include "xxfclib/formats/psa_disk/xx_psa_disk.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/formats/fat/xx_fat.h"
#include "xxfclib/data/xx_data.h"

static int64_t psa_disk_available(Abstractformat *f) {
    int64_t n;
    if (!f || !f->device || f->base_address<0 || (n=xx_io_size(f->device))<f->base_address) return -1;
    return n-f->base_address;
}
static bool psa_disk_read(Abstractformat *f, int64_t at, void *p, size_t n) {
    int64_t left=psa_disk_available(f);
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

typedef struct psa_disk_carrier {int64_t size;bool (*info)(Abstractformat *,xx_pd_struct *);void (*destroy)(Abstractformat *);} psa_disk_carrier;
static uint32_t psa_disk_u32(const uint8_t *p) {return xx_data_get_u32(p,4,0,false);}
static bool psa_disk_psa_header(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[84];int64_t n;xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;n=psa_disk_available(&f);
    return n>=596 && psa_disk_read(&f,0,h,84) && !xx_rt_memcmp(h,"                beer!",21) && h[21]<=3 && psa_disk_u32(h+30) && (uint64_t)psa_disk_u32(h+30)*512+84==(uint64_t)n;
}
static void psa_disk_carrier_destroy(Abstractformat *f) {
    psa_disk_carrier *c=f?f->priv:NULL;void (*destroy)(Abstractformat *);if(!c)return;
    destroy=c->destroy;f->priv=NULL;xx_mem_free(c);if(destroy)destroy(f);else xx_format_cleanup_extra_parameters(f);
}
static bool psa_disk_carrier_info(Abstractformat *f,xx_pd_struct *pd) {
    psa_disk_carrier *c=f->priv;if(!c||!c->info(f,pd))return false;f->format_size=c->size;return true;
}
static int64_t psa_disk_carrier_size(Abstractformat *f,xx_pd_struct *pd) {
    if(!f||!f->priv||(!f->base_info_handled&&!psa_disk_carrier_info(f,pd)))return -1;
    f->format_size=((psa_disk_carrier *)f->priv)->size;return f->format_size;
}
static uint64_t psa_disk_carrier_records(Abstractformat *f,xx_pd_struct *pd) {
    return psa_disk_carrier_size(f,pd)>=0?f->number_of_archive_records:0;
}
static bool psa_disk_set_carrier(Abstractformat *f,xx_file_type_t type,int64_t base) {
    psa_disk_carrier *c;if(!f)return false;c=xx_mem_alloc(sizeof(*c));if(!c)return false;
    c->size=xx_io_total_size(f->device)-base;c->info=f->handle_base_info;c->destroy=f->destroy;f->priv=c;f->file_type=type;f->handle_base_info=psa_disk_carrier_info;f->get_format_size=psa_disk_carrier_size;f->get_number_of_archive_records=psa_disk_carrier_records;f->destroy=psa_disk_carrier_destroy;return true;
}



Abstractformat *xx_psa_disk_create(xx_io_device *d,int64_t base) {
    bool valid=psa_disk_psa_header(d,base);xx_fat *r;
    if(!valid&&d&&xx_io_total_size(d)>base)return NULL;
    r=xx_fat_create(d,valid?base+84:base);
    if(r&&!psa_disk_set_carrier(&r->format,XX_FILE_TYPE_PSA_DISK,base)){xx_fat_free(r);r=NULL;}
    return (Abstractformat *)r;
}
void xx_psa_disk_free(Abstractformat *f) {
    if(f){psa_disk_carrier_destroy(f);xx_mem_free(f);}
}

xx_file_type_t xx_psa_disk_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t cursor,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    n=psa_disk_available(&f);
    if(n>=24 && psa_disk_read(&f,0,h,24) && !xx_rt_memcmp(h,"                beer!",21) && psa_disk_psa_header(d,base))type=XX_FILE_TYPE_PSA_DISK;
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_psa_disk_open(xx_io_device *d) {return xx_psa_disk_create(d,0);}
static const xx_file_type_t xx_psa_disk_types[]={XX_FILE_TYPE_PSA_DISK};
static const xx_format_search_desc xx_psa_disk_desc={xx_psa_disk_types,1,NULL,0,xx_psa_disk_open,xx_psa_disk_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(psa_disk,xx_psa_disk_desc)
