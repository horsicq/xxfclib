/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native ZXML, standalone zisofs, SQZE, NVP, Planet Source Code ZIP and CRX.
 * Grammar evidence: U3 recovered parser functions 0064d770, 0060bfe0,
 * 0056b0f0, 0056b670, 0063ad80 and 0072b600; standard zisofs-tools.
 * Decoded members are bounded in RAM, including validation and TEST.
 */
#include "xx_arc9_streams.h"
#include "../xx_payload_members.h"
#include "xxfclib/formats/zip/xx_zip.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include <string.h>

#define ARC9_MEMORY_LIMIT ((size_t)256U * 1024U * 1024U)
#define ARC9_PSC_START INT64_C(12672)

static uint16_t a9_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
static uint32_t a9_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool a9_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }

/* Exact-capacity sink: a malformed stream cannot allocate or emit more than
 * the declared logical member size. The Deflate engine monitors cancellation. */
typedef struct a9_sink { uint8_t *data; size_t size, used; } a9_sink;
static ssize_t a9_write(xx_io_device *d, const void *p, size_t n) {
    a9_sink *s=(a9_sink *)d->priv;
    if(n>s->size-s->used) return -1;
    if(n) memcpy(s->data+s->used,p,n);
    s->used+=n; return (ssize_t)n;
}
static bool a9_inflate(const uint8_t *p, size_t n, uint8_t *out, size_t size,
                        bool wrapped, xx_pd_struct *pd) {
    xx_io_device device={0}; a9_sink sink; size_t consumed=0U;
    if(a9_stopped(pd)) return false;
    if(wrapped) {
        if(n<6U || !xx_zlib_stream_header_is_valid(p,n)) return false;
        p+=2U; n-=6U;
    }
    sink.data=out; sink.size=size; sink.used=0U;
    device.priv=&sink; device.write=a9_write;
    if(!xx_deflate_unpack_memory_to_device_ex(p,n,&device,&consumed,false,pd) ||
       consumed!=n || sink.used!=size || a9_stopped(pd)) return false;
    if(wrapped) {
        uint32_t expected=((uint32_t)p[n]<<24)|((uint32_t)p[n+1U]<<16)|
                          ((uint32_t)p[n+2U]<<8)|(uint32_t)p[n+3U];
        if(xx_zlib_stream_adler32(out,size)!=expected) return false;
    }
    return true;
}

/* Source filenames are not required for finite compressed streams. Where the
 * container supplies a filename, retain it after rejecting unsafe components. */
static char *a9_name(const uint8_t *p, size_t n) {
    char *name; size_t i,start=0U;
    if(!n || n>4096U || p[0]=='/' || p[0]=='\\') return NULL;
    name=(char *)xx_mem_alloc(n+1U); if(!name) return NULL;
    for(i=0;i<=n;++i) {
        unsigned char c=i<n?p[i]:0U;
        if(i<n && (c<32U || c==127U || c==':' || c=='<' || c=='>' ||
                     c=='"' || c=='|' || c=='?' || c=='*')) goto bad;
        if(c=='/' || c=='\\' || i==n) {
            size_t len=i-start;
            if((len==1U && p[start]=='.') ||
               (len==2U && p[start]=='.' && p[start+1U]=='.') ||
               (!len && i<n) ||
               (len && (p[i-1U]=='.' || p[i-1U]==' '))) goto bad;
            start=i+1U;
        }
        name[i]=c=='\\'?'/':(char)c;
    }
    return name;
bad: xx_mem_free(name); return NULL;
}

static uint8_t *a9_input(Abstractformat *f, size_t *n, xx_pd_struct *pd) {
    int64_t available=pm_available(f); uint8_t *p;
    if(available<0 || (uint64_t)available>ARC9_MEMORY_LIMIT || a9_stopped(pd)) return NULL;
    *n=(size_t)available; p=(uint8_t *)xx_mem_alloc(*n?*n:1U);
    if(!p || !pm_read(f,0,p,*n) || a9_stopped(pd)) { xx_mem_free(p); return NULL; }
    return p;
}
static bool a9_memory_member(Abstractformat *f, pm_stream *s, const char *label,
                              int64_t at, int64_t packed, uint8_t *plain,
                              size_t size, uint16_t method, char *name) {
    pm_member *m;
    if(!pm_add(f,s,label,at,packed)) return false;
    m=&s->items[s->count-1U]; m->memory=plain; m->size=(int64_t)size;
    m->packed_size=packed; m->compression_method=method; m->display_name=name;
    return true;
}

static bool a9_zxml(Abstractformat *f, pm_stream *s, const uint8_t *p, size_t n,
                     xx_pd_struct *pd) {
    uint32_t packed,size; uint8_t *plain;
    if(n<12U || memcmp(p,"ZXML",4U)!=0) return false;
    packed=a9_u32(p+4U); size=a9_u32(p+8U);
    if(packed!=n-12U || size>ARC9_MEMORY_LIMIT-n || (packed>>31U) || (size>>31U)) return false;
    plain=(uint8_t *)xx_mem_alloc(size?size:1U); if(!plain) return false;
    if(!a9_inflate(p+12U,packed,plain,size,true,pd) ||
       !a9_memory_member(f,s,"content.xml",12,(int64_t)packed,plain,size,8U,NULL)) {
        xx_mem_free(plain); return false;
    }
    s->size=(int64_t)n; return true;
}

static bool a9_zisofs(Abstractformat *f, pm_stream *s, const uint8_t *p, size_t n,
                      xx_pd_struct *pd) {
    static const uint8_t magic[8]={0x37,0xe4,0x53,0x96,0xc9,0xdb,0xd6,0x07};
    uint32_t size; size_t block,blocks,header,table,i,position=0U; uint8_t *plain;
    if(n<20U || memcmp(p,magic,8U)!=0 || p[12]<4U || p[13]>29U || p[14] || p[15]) return false;
    size=a9_u32(p+8U); block=(size_t)1U<<p[13]; header=(size_t)p[12]*4U;
    if(size>ARC9_MEMORY_LIMIT-n || !block || header>n) return false;
    blocks=size/block+(size%block!=0U); table=(blocks+1U)*4U;
    if(table>n-header || a9_u32(p+header)!=header+table ||
       a9_u32(p+header+blocks*4U)!=n) return false;
    for(i=0U;i<blocks;++i) {
        size_t a=a9_u32(p+header+i*4U),b=a9_u32(p+header+(i+1U)*4U);
        if(a>b || b>n || b-a>block+65536U) return false;
    }
    plain=(uint8_t *)xx_mem_alloc(size?size:1U); if(!plain) return false;
    for(i=0U;i<blocks;++i) {
        size_t a=a9_u32(p+header+i*4U),b=a9_u32(p+header+(i+1U)*4U);
        size_t want=size-position<block?size-position:block;
        if(a9_stopped(pd)) goto bad;
        if(a==b) memset(plain+position,0,want);
        else if(!a9_inflate(p+a,b-a,plain+position,want,true,pd)) goto bad;
        position+=want;
    }
    if(!a9_memory_member(f,s,"content.bin",(int64_t)(header+table),
                          (int64_t)(n-header-table),plain,size,8U,NULL)) goto bad;
    s->size=(int64_t)n; return true;
bad: xx_mem_free(plain); return false;
}

/* SQZE uses little-endian 16-bit control words interleaved with literals and
 * LZSS matches. Match history is 8192 bytes. All logical output is XOR B3. */
typedef struct a9_sq { const uint8_t *p; size_t n,pos; uint16_t bits; unsigned left; } a9_sq;
static bool a9_sq_bit(a9_sq *s, unsigned *value) {
    if(!s->left) return false;
    *value=s->bits&1U; s->bits>>=1U;
    if(--s->left==0U) {
        if(s->n-s->pos<2U) return false;
        s->bits=a9_u16(s->p+s->pos); s->pos+=2U; s->left=16U;
    }
    return true;
}
static bool a9_sq_byte(a9_sq *s, unsigned *value) {
    if(s->pos>=s->n) return false;
    *value=s->p[s->pos++]; return true;
}
static bool a9_sq_decode(const uint8_t *p, size_t n, uint8_t *out, size_t size,
                          xx_pd_struct *pd) {
    a9_sq stream; uint8_t history[8192]={0}; size_t pos=0U,ring=0U;
    unsigned b,c,d,length,delta,index;
    if(n<2U) return false;
    stream.p=p; stream.n=n; stream.pos=2U; stream.bits=a9_u16(p); stream.left=16U;
    while(pos<size) {
        if(a9_stopped(pd) || !a9_sq_bit(&stream,&b)) return false;
        if(b) {
            if(!a9_sq_byte(&stream,&c)) return false;
            history[ring]=(uint8_t)c; ring=(ring+1U)&8191U;
            out[pos++]=(uint8_t)(c^0xb3U);
        } else {
            if(!a9_sq_bit(&stream,&b)) return false;
            if(!b) {
                if(!a9_sq_bit(&stream,&b) || !a9_sq_bit(&stream,&c) || !a9_sq_byte(&stream,&d)) return false;
                length=b*2U+c+2U; delta=d+7936U;
            } else {
                if(!a9_sq_byte(&stream,&b) || !a9_sq_byte(&stream,&c)) return false;
                d=b+(c<<8U); length=(d>>8U)&7U;
                delta=(((d>>11U)|0xe0U)<<8U)+(d&255U);
                if(!length) {
                    if(!a9_sq_byte(&stream,&b) || !b) return false;
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
    if(!a9_sq_bit(&stream,&b) || b || !a9_sq_bit(&stream,&b) || !b ||
       !a9_sq_byte(&stream,&b) || !a9_sq_byte(&stream,&c) || (c&7U) ||
       !a9_sq_byte(&stream,&d) || d || stream.pos!=stream.n) return false;
    return !a9_stopped(pd);
}

static bool a9_sqze(Abstractformat *f, pm_stream *s, const uint8_t *p, size_t n,
                    xx_pd_struct *pd) {
    uint8_t *plain=NULL; size_t size,pos=0U; uint32_t version,packed; bool result=false;
    if(n<24U || memcmp(p,"SQZE",4U)!=0) return false;
    version=a9_u32(p+4U); size=a9_u32(p+12U); packed=a9_u32(p+16U);
    if(version!=1U || a9_u32(p+8U)!=24U || packed!=n-24U ||
       size>(ARC9_MEMORY_LIMIT-n)/2U || xx_crc32_calc(0,p+24U,packed)!=a9_u32(p+20U)) return false;
    plain=(uint8_t *)xx_mem_alloc(size?size:1U); if(!plain) return false;
    if(!a9_sq_decode(p+24U,packed,plain,size,pd)) goto done;
    while(pos<size) {
        size_t payload,namesize; uint8_t *member; char *name=NULL;
        if(a9_stopped(pd) || size-pos<8U) goto done;
        if(a9_u32(plain+pos)==0x454c4946U) {
            if(size-pos<12U) goto done;
            payload=a9_u32(plain+pos+4U); namesize=a9_u32(plain+pos+8U); pos+=12U;
            if(namesize>1024U || namesize>size-pos) goto done;
            if(namesize) { name=a9_name(plain+pos,namesize); if(!name) goto done; }
            pos+=namesize;
        } else if(a9_u32(plain+pos+4U)==0xbfd95300U || a9_u32(plain+pos+4U)==0xbfd95301U) {
            payload=a9_u32(plain+pos); pos+=8U;
        } else goto done;
        if(payload>size-pos) { xx_mem_free(name); goto done; }
        member=(uint8_t *)xx_mem_alloc(payload?payload:1U);
        if(!member) { xx_mem_free(name); goto done; }
        if(payload) memcpy(member,plain+pos,payload);
        /* The source is one shared compressed stream; individual compressed
         * sizes cannot be inferred from logical record boundaries. */
        if(!a9_memory_member(f,s,"content.bin",24,(int64_t)packed,member,payload,0x5351U,name)) {
            xx_mem_free(member); xx_mem_free(name); goto done;
        }
        pos+=payload;
    }
    s->size=(int64_t)n; result=true;
done: xx_mem_free(plain); return result;
}

static bool a9_skip_nvp_strings(const uint8_t *p, size_t end, size_t *at) {
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
static bool a9_nvp(Abstractformat *f, pm_stream *s, const uint8_t *p, size_t n,
                   xx_pd_struct *pd) {
    size_t pos,i; uint16_t count; const uint8_t *zero;
    if(n<10U || memcmp(p,"NVP\1",4U)!=0) return false;
    zero=(const uint8_t *)memchr(p+4U,0,n-4U);
    if(!zero || zero-p>1028) return false;
    pos=(size_t)(zero-p)+1U;
    if(n-pos<5U) return false;
    count=a9_u16(p+pos+3U); pos+=5U;
    for(i=0U;i<count;++i) {
        uint32_t type; size_t length,end,jpegat=0U,jpegsize=0U,at; char label[80];
        bool has_jpeg=false;
        if(a9_stopped(pd) || n-pos<8U) return false;
        type=a9_u32(p+pos); length=a9_u32(p+pos+4U)&0x7fffffffU; pos+=8U;
        if(length>n-pos) return false;
        end=pos+length; at=pos;
        if((type==4005U || type==2203U) && length>=4U) {
            jpegsize=a9_u32(p+at); jpegat=at+4U; has_jpeg=true;
        } else if(type==1500U && length>=21U) {
            jpegsize=a9_u32(p+at+17U); jpegat=at+21U; has_jpeg=true;
        } else if(type==1502U && length>=17U) {
            jpegsize=a9_u32(p+at+13U); jpegat=at+17U; has_jpeg=true;
        } else if(type==1503U) {
            if(!a9_skip_nvp_strings(p,end,&at) || !a9_skip_nvp_strings(p,end,&at) || end-at<3U) return false;
            at+=3U;
            if(!a9_skip_nvp_strings(p,end,&at) || !a9_skip_nvp_strings(p,end,&at) || end-at<6U) return false;
            at+=2U; jpegsize=a9_u32(p+at); jpegat=at+4U; has_jpeg=true;
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

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    uint8_t *p; size_t n; bool result=false;
    p=a9_input(f,&n,pd); if(!p) return false;
    switch(f->file_type) {
        case XX_FILE_TYPE_ZXML: result=a9_zxml(f,s,p,n,pd); break;
        case XX_FILE_TYPE_ZISOFS: result=a9_zisofs(f,s,p,n,pd); break;
        case XX_FILE_TYPE_SQZE: result=a9_sqze(f,s,p,n,pd); break;
        case XX_FILE_TYPE_NVP: result=a9_nvp(f,s,p,n,pd); break;
        default: break;
    }
    xx_mem_free(p); return result && !a9_stopped(pd);
}

/* Planet Source Code's .psc archives complement every byte starting at
 * absolute archive offset 12672. A seekable borrowed view feeds the established
 * ZIP reader, including all its codecs, ZIP64, passwords and checksum checks.
 * It owns no input and performs no temporary-file staging or writes. */
typedef struct a9_psc {
    xx_zip zip; xx_io_device view; xx_io_device *source; int64_t base,size,pos,skip;
    xx_file_type_t type;bool complement,header_valid;
} a9_psc;
static ssize_t a9_psc_read(xx_io_device *d, void *buffer, size_t n) {
    a9_psc *p=(a9_psc *)d->priv; size_t i,first;
    if(!buffer && n) return -1;
    if((uint64_t)n>(uint64_t)(p->size-p->pos)) n=(size_t)(p->size-p->pos);
    if(!n) return 0;
    if(!xx_io_read_at(p->source,p->base+p->pos,buffer,n)) return -1;
    first=p->pos>=ARC9_PSC_START?0U:
        (uint64_t)(ARC9_PSC_START-p->pos)>n?n:(size_t)(ARC9_PSC_START-p->pos);
    if(p->complement) for(i=first;i<n;++i) ((uint8_t *)buffer)[i]^=0xffU;
    p->pos+=(int64_t)n; return (ssize_t)n;
}
static int a9_psc_seek64(xx_io_device *d,int64_t off,int whence) {
    a9_psc *p=(a9_psc *)d->priv; int64_t base;
    if(whence==SEEK_SET) base=0;
    else if(whence==SEEK_CUR) base=p->pos;
    else if(whence==SEEK_END) base=p->size;
    else return -1;
    if(off < -base || off > p->size-base) return -1;
    p->pos=base+off; return 0;
}
static int a9_psc_seek(xx_io_device *d,long off,int whence) { return a9_psc_seek64(d,off,whence); }
static int64_t a9_psc_tell(xx_io_device *d) { return ((a9_psc *)d->priv)->pos; }
static int64_t a9_psc_size(xx_io_device *d) { return ((a9_psc *)d->priv)->size; }

/* Chromium CRX2 fixed header and CRX3 protobuf envelope. Signatures are
 * retained in the original envelope; extraction verifies the ZIP checksums,
 * and does not claim to authenticate the extension publisher. Layout source:
 * chromium/src/components/crx_file/crx3.proto (Chromium's BSD licensed spec). */
static bool a9_varint(const uint8_t *p,size_t n,size_t *at,uint64_t *value) {
    unsigned shift; uint64_t result=0U;
    for(shift=0U;shift<70U;shift+=7U) {
        unsigned b;
        if(*at>=n) return false;
        b=p[(*at)++];
        if(shift==63U && b>1U) return false;
        result|=(uint64_t)(b&127U)<<shift;
        if(!(b&128U)) { *value=result; return true; }
    }
    return false;
}
static bool a9_proto(const uint8_t *p,size_t n) {
    size_t at=0U; uint64_t tag,value;
    while(at<n) {
        if(!a9_varint(p,n,&at,&tag) || !(tag>>3U) || (tag>>3U)>0x1fffffffU) return false;
        switch(tag&7U) {
            case 0U: if(!a9_varint(p,n,&at,&value)) return false; break;
            case 1U: if(n-at<8U) return false; at+=8U; break;
            case 2U:
                if(!a9_varint(p,n,&at,&value) || value>n-at) return false;
                at+=(size_t)value; break;
            case 5U: if(n-at<4U) return false; at+=4U; break;
            default: return false;
        }
    }
    return true;
}
static bool a9_crx_offset(xx_io_device *d,int64_t base,int64_t *offset) {
    uint8_t h[16],magic[4],*header=NULL; uint32_t version; uint64_t at,available;
    int64_t total=xx_io_size(d); bool valid=false;
    if(base<0 || total<base || total-base<16 || !xx_io_read_at(d,base,h,sizeof(h)) || memcmp(h,"Cr24",4U)) return false;
    available=(uint64_t)(total-base); version=a9_u32(h+4U);
    if(version==2U) {
        uint32_t key=a9_u32(h+8U),signature=a9_u32(h+12U);
        if(!key || !signature || key>16U*1024U*1024U || signature>16U*1024U*1024U) return false;
        at=16U+(uint64_t)key+signature;
    } else if(version==3U) {
        uint32_t size=a9_u32(h+8U);
        if(!size || size>16U*1024U*1024U || (uint64_t)size>available-12U) return false;
        header=(uint8_t *)xx_mem_alloc(size); if(!header) return false;
        valid=xx_io_read_at(d,base+12,header,size) && a9_proto(header,size);
        xx_mem_free(header); if(!valid) return false;
        at=12U+(uint64_t)size;
    } else return false;
    if(at>available || available-at<22U ||
       !xx_io_read_at(d,base+(int64_t)at,magic,sizeof(magic)) ||
       (a9_u32(magic)!=0x04034b50U && a9_u32(magic)!=0x06054b50U)) return false;
    *offset=(int64_t)at; return true;
}
/* ZIP's base-info callback normally classifies the object as ZIP/ZIP64.
 * These borrowed views retain their outer container identity, including after
 * iterator creation. Keep native internal offsets/size relative to the view;
 * the public size callback adds the CRX envelope without moving ZIP offsets. */
static xx_file_type_t a9_wrapper_type(Abstractformat *f){return ((a9_psc *)f)->type;}
static bool a9_wrapper_valid(Abstractformat *f,xx_pd_struct *pd){
    a9_psc *p=(a9_psc *)f;bool ok=p->header_valid&&xx_zip_check_is_valid(f,pd);f->file_type=p->type;return ok;
}
static bool a9_wrapper_info(Abstractformat *f,xx_pd_struct *pd){
    a9_psc *p=(a9_psc *)f;bool ok=p->header_valid&&xx_zip_handle_base_info(f,pd);f->file_type=p->type;return ok;
}
static int64_t a9_wrapper_size(Abstractformat *f,xx_pd_struct *pd){
    a9_psc *p=(a9_psc *)f;
    if(!p->header_valid||!xx_format_handle_base_info(f,pd)||f->format_size<0||f->format_size>INT64_MAX-p->skip)return -1;
    return f->format_size+p->skip;
}
static void a9_destroy(Abstractformat *f) {
    if(!f) return;
    if(f->file_type==XX_FILE_TYPE_ZIP_PSC || f->file_type==XX_FILE_TYPE_CRX) xx_zip_destroy((xx_zip *)f);
    else xx_format_cleanup_extra_parameters(f);
}
Abstractformat *xx_arc9_streams_create(xx_io_device *d,int64_t b,xx_file_type_t type) {
    Abstractformat *f; const char *ext;
    if(!d || b<0 || xx_io_size(d)<b) return NULL;
    if(type==XX_FILE_TYPE_ZIP_PSC || type==XX_FILE_TYPE_CRX) {
        int64_t skip=0;bool header_valid=type!=XX_FILE_TYPE_CRX||a9_crx_offset(d,b,&skip);
        a9_psc *p=(a9_psc *)xx_mem_alloc(sizeof(*p)); if(!p) return NULL;
        xx_mem_zero(p,sizeof(*p)); p->source=d; p->base=b+skip; p->size=header_valid?xx_io_size(d)-b-skip:0;
        p->skip=skip;p->type=type;p->header_valid=header_valid;
        p->complement=type==XX_FILE_TYPE_ZIP_PSC;
        p->view.priv=p; p->view.read=a9_psc_read; p->view.seek=a9_psc_seek;
        p->view.seek64=a9_psc_seek64; p->view.tell=a9_psc_tell; p->view.total_size=a9_psc_size;
        xx_zip_init(&p->zip,&p->view,0); f=&p->zip.format; f->file_type=type;
        xx_format_set_extension(f,type==XX_FILE_TYPE_ZIP_PSC?"psc":"crx"); f->destroy=a9_destroy;
        f->get_file_type=a9_wrapper_type;f->check_is_valid=a9_wrapper_valid;
        f->handle_base_info=a9_wrapper_info;f->get_format_size=a9_wrapper_size;
        f->create_archive_records_writing=NULL; f->pack_archive_record=NULL;
        f->finalize_archive_records_writing=NULL; f->free_archive_records_writing=NULL;
        return f;
    }
    if(type==XX_FILE_TYPE_ZXML) ext="xml";
    else if(type==XX_FILE_TYPE_ZISOFS) ext="zisofs";
    else if(type==XX_FILE_TYPE_SQZE) ext="sqze";
    else if(type==XX_FILE_TYPE_NVP) ext="nvp";
    else return NULL;
    f=(Abstractformat *)xx_mem_alloc(sizeof(*f)); if(!f) return NULL;
    pm_init(f,d,b,type,ext); f->destroy=a9_destroy; return f;
}
void xx_arc9_streams_free(Abstractformat *f) { if(f) { a9_destroy(f); xx_mem_free(f); } }
static bool a9_psc_eocd(xx_io_device *d,int64_t b) {
    int64_t total=xx_io_size(d),available; size_t n,i; uint8_t *tail; bool result=false;
    if(b<0 || total<b || (available=total-b)<ARC9_PSC_START+22) return false;
    n=(uint64_t)available>65557U?65557U:(size_t)available;
    tail=(uint8_t *)xx_mem_alloc(n); if(!tail) return false;
    if(!xx_io_read_at(d,total-(int64_t)n,tail,n)) goto done;
    i=n-22U;
    for(;;) {
        if(a9_u32(tail+i)==0xf9fab4afU &&
           (size_t)(a9_u16(tail+i+20U)^0xffffU)==n-i-22U &&
           available-(int64_t)n+(int64_t)i>=ARC9_PSC_START) { result=true; break; }
        if(!i) break;
        --i;
    }
done: xx_mem_free(tail); return result;
}
bool xx_arc9_streams_probe(xx_io_device *d,int64_t b,xx_file_type_t type) {
    Abstractformat *f; bool result;
    if(type==XX_FILE_TYPE_ZIP_PSC && !a9_psc_eocd(d,b)) return false;
    f=xx_arc9_streams_create(d,b,type);
    if(!f) return false;
    result=f->check_is_valid(f,NULL); xx_arc9_streams_free(f); return result;
}
xx_file_type_t xx_arc9_streams_detect(xx_io_device *d,int64_t b) {
    uint8_t h[16]; int64_t size; xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d || b<0 || (size=xx_io_size(d))<b || size-b<10) return type;
    memset(h,0,sizeof(h));
    if(!xx_io_read_at(d,b,h,(uint64_t)(size-b)<sizeof(h)?(size_t)(size-b):sizeof(h))) return type;
    if(memcmp(h,"ZXML",4U)==0) type=XX_FILE_TYPE_ZXML;
    else if(a9_u32(h)==0x9653e437U && a9_u32(h+4U)==0x07d6dbc9U) type=XX_FILE_TYPE_ZISOFS;
    else if(memcmp(h,"NVP\1",4U)==0) type=XX_FILE_TYPE_NVP;
    else if(memcmp(h,"SQZE",4U)==0) type=XX_FILE_TYPE_SQZE;
    else if(memcmp(h,"Cr24",4U)==0) type=XX_FILE_TYPE_CRX;
    else if(a9_u32(h)==0x04034b50U && a9_psc_eocd(d,b)) type=XX_FILE_TYPE_ZIP_PSC;
    if(type!=XX_FILE_TYPE_UNKNOWN && !xx_arc9_streams_probe(d,b,type)) type=XX_FILE_TYPE_UNKNOWN;
    return type;
}
