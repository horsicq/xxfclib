/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/zxml/xx_zxml.h"
#include "../xx_memory_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
static bool zxml_zxml(Abstractformat *f, pm_stream *s, const uint8_t *p, size_t n,
                     xx_pd_struct *pd) {
    uint32_t packed,size; uint8_t *plain;
    if(n<12U || memcmp(p,"ZXML",4U)!=0) return false;
    packed=mdm_u32(p+4U); size=mdm_u32(p+8U);
    if(packed!=n-12U || size>MDM_MEMORY_LIMIT-n || (packed>>31U) || (size>>31U)) return false;
    plain=(uint8_t *)xx_mem_alloc(size?size:1U); if(!plain) return false;
    if(!mdm_inflate(p+12U,packed,plain,size,true,pd) ||
       !mdm_memory_member(f,s,"content.xml",12,(int64_t)packed,plain,size,8U,NULL)) {
        xx_mem_free(plain); return false;
    }
    s->size=(int64_t)n; return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd) {
    uint8_t *input; size_t size; bool result;
    if (format->file_type != XX_FILE_TYPE_ZXML) return false;
    input = mdm_input(format, &size, pd);
    if (!input) return false;
    result = zxml_zxml(format, members, input, size, pd);
    xx_mem_free(input);
    return result && !mdm_stopped(pd);
}

static void zxml_destroy(Abstractformat *format) {
    if (format) xx_format_cleanup_extra_parameters(format);
}
Abstractformat *xx_zxml_create(xx_io_device *device, int64_t base) {
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) { pm_init(format, device, base, XX_FILE_TYPE_ZXML, "xml"); format->destroy=zxml_destroy; }
    return format;
}
void xx_zxml_free(Abstractformat *format) {
    if (format) { zxml_destroy(format); xx_mem_free(format); }
}

xx_file_type_t xx_zxml_detect(xx_io_device *device, int64_t base) {
    uint8_t h[16] = {0}; int64_t total, saved;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN; Abstractformat *format;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total-base < 10) return result;
    saved = xx_io_tell(device);
    if (!xx_io_read_at(device, base, h, (uint64_t)(total-base)<sizeof(h)?(size_t)(total-base):sizeof(h))) goto done;
    if (!(memcmp(h,"ZXML",4U)==0)) goto done;
    format = xx_zxml_create(device, base);
    if (format) {
        if (format->check_is_valid(format, NULL)) result = XX_FILE_TYPE_ZXML;
        xx_zxml_free(format);
    }
done:
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *zxml_open(xx_io_device *device) {
    return xx_zxml_create(device, 0);
}
static const xx_file_type_t zxml_types[] = {XX_FILE_TYPE_ZXML};
static const xx_format_search_desc zxml_descriptor = {
    zxml_types, 1, NULL, 0, zxml_open, xx_zxml_free, true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(zxml, zxml_descriptor)
