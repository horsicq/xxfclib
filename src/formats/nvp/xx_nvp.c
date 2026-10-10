/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/nvp/xx_nvp.h"
#include "../xx_memory_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
static bool nvp_skip_nvp_strings(const uint8_t *p, size_t end, size_t *at) {
    unsigned count; size_t i;
    if(*at>=end) return false;
    count=p[(*at)++];
    for(i=0U;i<count;++i) {
        size_t left=end-*at; const uint8_t *zero=(const uint8_t *)memchr(p+*at,0,left);
        if(!zero || (size_t)(zero-(p+*at))>1024U) return false;
        *at=(size_t)(zero-p)+1U;
    }
    return true;
}

static bool nvp_nvp(Abstractformat *f, pm_stream *s, const uint8_t *p, size_t n,
                   xx_pd_struct *pd) {
    size_t pos,i; uint16_t count; const uint8_t *zero;
    if(n<10U || memcmp(p,"NVP\1",4U)!=0) return false;
    zero=(const uint8_t *)memchr(p+4U,0,n-4U);
    if(!zero || zero-p>1028) return false;
    pos=(size_t)(zero-p)+1U;
    if(n-pos<5U) return false;
    count=mdm_u16(p+pos+3U); pos+=5U;
    for(i=0U;i<count;++i) {
        uint32_t type; size_t length,end,jpegat=0U,jpegsize=0U,at; char label[80];
        bool has_jpeg=false;
        if(mdm_stopped(pd) || n-pos<8U) return false;
        type=mdm_u32(p+pos); length=mdm_u32(p+pos+4U)&0x7fffffffU; pos+=8U;
        if(length>n-pos) return false;
        end=pos+length; at=pos;
        if((type==4005U || type==2203U) && length>=4U) {
            jpegsize=mdm_u32(p+at); jpegat=at+4U; has_jpeg=true;
        } else if(type==1500U && length>=21U) {
            jpegsize=mdm_u32(p+at+17U); jpegat=at+21U; has_jpeg=true;
        } else if(type==1502U && length>=17U) {
            jpegsize=mdm_u32(p+at+13U); jpegat=at+17U; has_jpeg=true;
        } else if(type==1503U) {
            if(!nvp_skip_nvp_strings(p,end,&at) || !nvp_skip_nvp_strings(p,end,&at) || end-at<3U) return false;
            at+=3U;
            if(!nvp_skip_nvp_strings(p,end,&at) || !nvp_skip_nvp_strings(p,end,&at) || end-at<6U) return false;
            at+=2U; jpegsize=mdm_u32(p+at); jpegat=at+4U; has_jpeg=true;
        }
        if(has_jpeg && jpegsize) {
            if(jpegat>end || jpegsize>end-jpegat) return false;
            (void)xx_rt_snprintf(label,sizeof(label),"record-%04u-type-%u.jpg",(unsigned)i,(unsigned)type);
            if(!pm_add(f,s,label,(int64_t)jpegat,(int64_t)jpegsize)) return false;
            /* Configuration preceding/following the JPEG remains a proper
             * separately bounded record payload, never the encoded container. */
            if(jpegat>pos) {
                (void)xx_rt_snprintf(label,sizeof(label),"record-%04u-type-%u-config.bin",(unsigned)i,(unsigned)type);
                if(!pm_add(f,s,label,(int64_t)pos,(int64_t)(jpegat-pos))) return false;
            }
            if(jpegsize<end-jpegat) {
                (void)xx_rt_snprintf(label,sizeof(label),"record-%04u-type-%u-tail.bin",(unsigned)i,(unsigned)type);
                if(!pm_add(f,s,label,(int64_t)(jpegat+jpegsize),(int64_t)(end-jpegat-jpegsize))) return false;
            }
        } else {
            (void)xx_rt_snprintf(label,sizeof(label),"record-%04u-type-%u.bin",(unsigned)i,(unsigned)type);
            if(!pm_add(f,s,label,(int64_t)pos,(int64_t)length)) return false;
        }
        pos=end;
    }
    if(pos!=n) return false;
    s->size=(int64_t)n; return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd) {
    uint8_t *input; size_t size; bool result;
    if (format->file_type != XX_FILE_TYPE_NVP) return false;
    input = mdm_input(format, &size, pd);
    if (!input) return false;
    result = nvp_nvp(format, members, input, size, pd);
    xx_mem_free(input);
    return result && !mdm_stopped(pd);
}

static void nvp_destroy(Abstractformat *format) {
    if (format) xx_format_cleanup_extra_parameters(format);
}
Abstractformat *xx_nvp_create(xx_io_device *device, int64_t base) {
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) { pm_init(format, device, base, XX_FILE_TYPE_NVP, "nvp"); format->destroy=nvp_destroy; }
    return format;
}
void xx_nvp_free(Abstractformat *format) {
    if (format) { nvp_destroy(format); xx_mem_free(format); }
}

xx_file_type_t xx_nvp_detect(xx_io_device *device, int64_t base) {
    uint8_t h[16] = {0}; int64_t total, saved;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN; Abstractformat *format;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total-base < 10) return result;
    saved = xx_io_tell(device);
    if (!xx_io_read_at(device, base, h, (uint64_t)(total-base)<sizeof(h)?(size_t)(total-base):sizeof(h))) goto done;
    if (!(memcmp(h,"NVP\1",4U)==0)) goto done;
    format = xx_nvp_create(device, base);
    if (format) {
        if (format->check_is_valid(format, NULL)) result = XX_FILE_TYPE_NVP;
        xx_nvp_free(format);
    }
done:
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *nvp_open(xx_io_device *device) {
    return xx_nvp_create(device, 0);
}
static const xx_file_type_t nvp_types[] = {XX_FILE_TYPE_NVP};
static const xx_format_search_desc nvp_descriptor = {
    nvp_types, 1, NULL, 0, nvp_open, xx_nvp_free, true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(nvp, nvp_descriptor)
