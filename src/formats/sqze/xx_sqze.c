/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/sqze/xx_sqze.h"
#include "../xx_memory_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
#include "xxfclib/algo/crc/xx_crc.h"

/* SQZE interleaves 16-bit LSB control words with an 8192-byte LZSS history.
 * The encoded end marker and the container CRC are both validated. */
typedef struct sqze_bits { const uint8_t *p; size_t n,pos; uint16_t bits; unsigned left; } sqze_bits;
static bool sqze_sq_bit(sqze_bits *s, unsigned *value) {
    if(!s->left) return false;
    *value=s->bits&1U; s->bits>>=1U;
    if(--s->left==0U) {
        if(s->n-s->pos<2U) return false;
        s->bits=mdm_u16(s->p+s->pos); s->pos+=2U; s->left=16U;
    }
    return true;
}

static bool sqze_sq_byte(sqze_bits *s, unsigned *value) {
    if(s->pos>=s->n) return false;
    *value=s->p[s->pos++]; return true;
}

static bool sqze_sq_decode(const uint8_t *p, size_t n, uint8_t *out, size_t size,
                          xx_pd_struct *pd) {
    sqze_bits stream; uint8_t history[8192]={0}; size_t pos=0U,ring=0U;
    unsigned b,c,d,length,delta,index;
    if(n<2U) return false;
    stream.p=p; stream.n=n; stream.pos=2U; stream.bits=mdm_u16(p); stream.left=16U;
    while(pos<size) {
        if(mdm_stopped(pd) || !sqze_sq_bit(&stream,&b)) return false;
        if(b) {
            if(!sqze_sq_byte(&stream,&c)) return false;
            history[ring]=(uint8_t)c; ring=(ring+1U)&8191U;
            out[pos++]=(uint8_t)(c^0xb3U);
        } else {
            if(!sqze_sq_bit(&stream,&b)) return false;
            if(!b) {
                if(!sqze_sq_bit(&stream,&b) || !sqze_sq_bit(&stream,&c) || !sqze_sq_byte(&stream,&d)) return false;
                length=b*2U+c+2U; delta=d+7936U;
            } else {
                if(!sqze_sq_byte(&stream,&b) || !sqze_sq_byte(&stream,&c)) return false;
                d=b+(c<<8U); length=(d>>8U)&7U;
                delta=(((d>>11U)|0xe0U)<<8U)+(d&255U);
                if(!length) {
                    if(!sqze_sq_byte(&stream,&b) || !b) return false;
                    length=b-1U;
                }
                length+=2U;
            }
            if(length>size-pos) return false;
            index=(unsigned)(ring+delta)&8191U;
            while(length--) {
                c=history[index]; index=(index+1U)&8191U;
                history[ring]=(uint8_t)c; ring=(ring+1U)&8191U;
                out[pos++]=(uint8_t)(c^0xb3U);
            }
        }
    }
    /* The actual encoded end marker is a long match with zero extended length.
     * Check it rather than accepting arbitrary bytes beyond the claimed size. */
    if(!sqze_sq_bit(&stream,&b) || b || !sqze_sq_bit(&stream,&b) || !b ||
       !sqze_sq_byte(&stream,&b) || !sqze_sq_byte(&stream,&c) || (c&7U) ||
       !sqze_sq_byte(&stream,&d) || d || stream.pos!=stream.n) return false;
    return !mdm_stopped(pd);
}

static bool sqze_sqze(Abstractformat *f, pm_stream *s, const uint8_t *p, size_t n,
                    xx_pd_struct *pd) {
    uint8_t *plain=NULL; size_t size,pos=0U; uint32_t version,packed; bool result=false;
    if(n<24U || memcmp(p,"SQZE",4U)!=0) return false;
    version=mdm_u32(p+4U); size=mdm_u32(p+12U); packed=mdm_u32(p+16U);
    if(version!=1U || mdm_u32(p+8U)!=24U || packed!=n-24U ||
       size>(MDM_MEMORY_LIMIT-n)/2U || xx_crc32_calc(0,p+24U,packed)!=mdm_u32(p+20U)) return false;
    plain=(uint8_t *)xx_mem_alloc(size?size:1U); if(!plain) return false;
    if(!sqze_sq_decode(p+24U,packed,plain,size,pd)) goto done;
    while(pos<size) {
        size_t payload,namesize; uint8_t *member; char *name=NULL;
        if(mdm_stopped(pd) || size-pos<8U) goto done;
        if(mdm_u32(plain+pos)==0x454c4946U) {
            if(size-pos<12U) goto done;
            payload=mdm_u32(plain+pos+4U); namesize=mdm_u32(plain+pos+8U); pos+=12U;
            if(namesize>1024U || namesize>size-pos) goto done;
            if(namesize) { name=mdm_name(plain+pos,namesize); if(!name) goto done; }
            pos+=namesize;
        } else if(mdm_u32(plain+pos+4U)==0xbfd95300U || mdm_u32(plain+pos+4U)==0xbfd95301U) {
            payload=mdm_u32(plain+pos); pos+=8U;
        } else goto done;
        if(payload>size-pos) { xx_mem_free(name); goto done; }
        member=(uint8_t *)xx_mem_alloc(payload?payload:1U);
        if(!member) { xx_mem_free(name); goto done; }
        if(payload) memcpy(member,plain+pos,payload);
        /* The source is one shared compressed stream; individual compressed
         * sizes cannot be inferred from logical record boundaries. */
        if(!mdm_memory_member(f,s,"content.bin",24,(int64_t)packed,member,payload,0x5351U,name)) {
            xx_mem_free(member); xx_mem_free(name); goto done;
        }
        pos+=payload;
    }
    s->size=(int64_t)n; result=true;
done: xx_mem_free(plain); return result;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd) {
    uint8_t *input; size_t size; bool result;
    if (format->file_type != XX_FILE_TYPE_SQZE) return false;
    input = mdm_input(format, &size, pd);
    if (!input) return false;
    result = sqze_sqze(format, members, input, size, pd);
    xx_mem_free(input);
    return result && !mdm_stopped(pd);
}

static void sqze_destroy(Abstractformat *format) {
    if (format) xx_format_cleanup_extra_parameters(format);
}
Abstractformat *xx_sqze_create(xx_io_device *device, int64_t base) {
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) { pm_init(format, device, base, XX_FILE_TYPE_SQZE, "sqze"); format->destroy=sqze_destroy; }
    return format;
}
void xx_sqze_free(Abstractformat *format) {
    if (format) { sqze_destroy(format); xx_mem_free(format); }
}

xx_file_type_t xx_sqze_detect(xx_io_device *device, int64_t base) {
    uint8_t h[16] = {0}; int64_t total, saved;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN; Abstractformat *format;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total-base < 10) return result;
    saved = xx_io_tell(device);
    if (!xx_io_read_at(device, base, h, (uint64_t)(total-base)<sizeof(h)?(size_t)(total-base):sizeof(h))) goto done;
    if (!(memcmp(h,"SQZE",4U)==0)) goto done;
    format = xx_sqze_create(device, base);
    if (format) {
        if (format->check_is_valid(format, NULL)) result = XX_FILE_TYPE_SQZE;
        xx_sqze_free(format);
    }
done:
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *sqze_open(xx_io_device *device) {
    return xx_sqze_create(device, 0);
}
static const xx_file_type_t sqze_types[] = {XX_FILE_TYPE_SQZE};
static const xx_format_search_desc sqze_descriptor = {
    sqze_types, 1, NULL, 0, sqze_open, xx_sqze_free, true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(sqze, sqze_descriptor)
