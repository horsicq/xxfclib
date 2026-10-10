/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Private seekable borrowed ZIP view. The owner chooses envelope offsets
 * and an optional byte-complement boundary; no container grammar lives here. */
#ifndef XX_ZIP_BORROWED_VIEW_H
#define XX_ZIP_BORROWED_VIEW_H
#include "xxfclib/formats/zip/xx_zip.h"
#include "xxfclib/memory/xx_memory.h"
#include <stdio.h>
#include <limits.h>
#include <string.h>
typedef struct zview {
    xx_zip zip; xx_io_device view; xx_io_device *source; int64_t base,size,pos,skip;
    xx_file_type_t type;bool complement,header_valid; int64_t complement_offset;
} zview;
static ssize_t zview_read(xx_io_device *d, void *buffer, size_t n) {
    zview *p=(zview *)d->priv; size_t i,first;
    if(!buffer && n) return -1;
    if((uint64_t)n>(uint64_t)(p->size-p->pos)) n=(size_t)(p->size-p->pos);
    if(!n) return 0;
    if(!xx_io_read_at(p->source,p->base+p->pos,buffer,n)) return -1;
    first=p->pos>=p->complement_offset?0U:
        (uint64_t)(p->complement_offset-p->pos)>n?n:(size_t)(p->complement_offset-p->pos);
    if(p->complement) for(i=first;i<n;++i) ((uint8_t *)buffer)[i]^=0xffU;
    p->pos+=(int64_t)n; return (ssize_t)n;
}
static int zview_seek64(xx_io_device *d,int64_t off,int whence) {
    zview *p=(zview *)d->priv; int64_t base;
    if(whence==SEEK_SET) base=0;
    else if(whence==SEEK_CUR) base=p->pos;
    else if(whence==SEEK_END) base=p->size;
    else return -1;
    if(off < -base || off > p->size-base) return -1;
    p->pos=base+off; return 0;
}
static int zview_seek(xx_io_device *d,long off,int whence) { return zview_seek64(d,off,whence); }
static int64_t zview_tell(xx_io_device *d) { return ((zview *)d->priv)->pos; }
static int64_t zview_size(xx_io_device *d) { return ((zview *)d->priv)->size; }


static xx_file_type_t zview_type(Abstractformat *f){return ((zview *)f)->type;}

static bool zview_valid(Abstractformat *f,xx_pd_struct *pd){
    zview *p=(zview *)f;bool ok=p->header_valid&&xx_zip_check_is_valid(f,pd);f->file_type=p->type;return ok;
}

static bool zview_info(Abstractformat *f,xx_pd_struct *pd){
    zview *p=(zview *)f;bool ok=p->header_valid&&xx_zip_handle_base_info(f,pd);f->file_type=p->type;return ok;
}

static int64_t zview_size_callback(Abstractformat *f,xx_pd_struct *pd){
    zview *p=(zview *)f;
    if(!p->header_valid||!xx_format_handle_base_info(f,pd)||f->format_size<0||f->format_size>INT64_MAX-p->skip)return -1;
    return f->format_size+p->skip;
}

static void zview_destroy(Abstractformat *format) {
    if (format) xx_zip_destroy((xx_zip *)format);
}
static Abstractformat *zview_create(xx_io_device *device, int64_t base, int64_t skip,
        xx_file_type_t type, const char *extension, bool valid, int64_t complement_offset) {
    zview *view = (zview *)xx_mem_alloc(sizeof(*view)); Abstractformat *format;
    if (!view) return NULL;
    xx_mem_zero(view, sizeof(*view));
    view->source=device; view->base=base+skip; view->size=valid?xx_io_size(device)-base-skip:0;
    view->skip=skip; view->type=type; view->header_valid=valid;
    view->complement=complement_offset>=0; view->complement_offset=complement_offset;
    view->view.priv=view; view->view.read=zview_read; view->view.seek=zview_seek;
    view->view.seek64=zview_seek64; view->view.tell=zview_tell; view->view.total_size=zview_size;
    xx_zip_init(&view->zip,&view->view,0); format=&view->zip.format; format->file_type=type;
    xx_format_set_extension(format,extension); format->destroy=zview_destroy;
    format->get_file_type=zview_type; format->check_is_valid=zview_valid;
    format->handle_base_info=zview_info; format->get_format_size=zview_size_callback;
    format->create_archive_records_writing=NULL; format->pack_archive_record=NULL;
    format->finalize_archive_records_writing=NULL; format->free_archive_records_writing=NULL;
    return format;
}
#endif
