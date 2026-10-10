/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Top-level definite-length TLV support adapted from Formats XDER.
 * Selected explicitly: its broad grammar is unsuitable for automatic detection. */
#include "xxfclib/formats/der/xx_der.h"
#include "../xx_payload_members.h"

static bool der_header(Abstractformat *f,xx_der_header *out,xx_pd_struct *pd) {
    uint8_t header[6];
    unsigned width,i;
    uint64_t length;
    int64_t available=pm_available(f);
    if (!out || available<2 || (pd && xx_pd_is_stopped(pd)) ||
        !xx_io_read_at(f->device,f->base_address,header,2)) return false;
    width=0;length=header[1];
    if (header[1]&128) {
        width=header[1]&127;
        if (!width || width>4 || (int64_t)(2+width)>available ||
            !xx_io_read_at(f->device,f->base_address+2,header+2,width)) return false;
        length=0;
        for (i=0;i<width;++i) length=(length<<8)|header[2+i];
    }
    if (length>(uint64_t)(available-2-width)) return false;
    out->tag=header[0];out->header_size=out->content_offset=2+width;
    out->content_size=(int64_t)length;
    return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    xx_der_header h;
    if (!der_header(f,&h,pd) || !pm_add(f,s,"tlv-header.bin",0,h.header_size) ||
        !pm_add(f,s,"value.bin",h.content_offset,h.content_size)) return false;
    s->size=h.header_size+h.content_size;
    return true;
}
void xx_der_init(xx_der *r,xx_io_device *d,int64_t base) {
    if (!r) return;
    xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,base,XX_FILE_TYPE_DER,"der");
    r->format.is_archive=false;r->format.format_type=XX_TYPE_RAW;
    xx_format_set_mime_type(&r->format,"application/octet-stream");
}
xx_der *xx_der_create(xx_io_device *d,int64_t base) {
    xx_der *r=(xx_der *)xx_mem_alloc(sizeof(*r));
    if (r) xx_der_init(r,d,base);
    return r;
}
void xx_der_destroy(xx_der *r) {
    if (r) { xx_format_cleanup_extra_parameters(&r->format);xx_format_invalidate_memory_map(&r->format); }
}
void xx_der_free(xx_der *r) { if (r) { xx_der_destroy(r);xx_mem_free(r); } }
bool xx_der_get_header(xx_der *r,xx_der_header *header,xx_pd_struct *pd) {
    return r && der_header(&r->format,header,pd);
}
