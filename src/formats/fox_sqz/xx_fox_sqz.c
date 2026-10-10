/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for Fox SQZ compressed archive.
 */
#include "xxfclib/formats/fox_sqz/xx_fox_sqz.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#define FOX_SQZ_MEMORY_CAP (256U*1024U*1024U)


static uint16_t fox_sqz_u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static uint32_t fox_sqz_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t fox_sqz_budget(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):FOX_SQZ_MEMORY_CAP;
    return n<FOX_SQZ_MEMORY_CAP?n:FOX_SQZ_MEMORY_CAP;
}
static bool fox_sqz_take(Abstractformat *f,int64_t *at,uint64_t n,void *p) {
    if(*at<0 || (uint64_t)*at>(uint64_t)pm_available(f) || n>(uint64_t)(pm_available(f)-*at)) return false;
    if(p && !pm_read(f,*at,p,(size_t)n)) return false;
    *at+=(int64_t)n; return true;
}
static bool fox_sqz_room(Abstractformat *f,pm_stream *s,uint64_t extra,bool adding) {
    size_t i,capacity=s->capacity;
    uint64_t used=extra,budget=fox_sqz_budget(f);
    if(adding && s->count==capacity) capacity=capacity?capacity*2U:8U;
    if(used>budget || (uint64_t)capacity*sizeof(pm_member)>budget-used) return false;
    used+=(uint64_t)capacity*sizeof(pm_member);
    for(i=0;i<s->count;++i) {
        uint64_t n=s->items[i].memory?(uint64_t)s->items[i].size:0;
        if(s->items[i].password) n+=xx_rt_strlen(s->items[i].password)+1U;
        if(n>budget-used) return false;
        used+=n;
    }
    return true;
}
static bool fox_sqz_add(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t n) {
    return fox_sqz_room(f,s,0,true) && pm_add(f,s,name,at,n);
}
static bool fox_sqz_owned(Abstractformat *f,pm_stream *s,const char *name,uint8_t *data,size_t plain,int64_t offset,int64_t packed) {
    if(!fox_sqz_room(f,s,plain,true) || !fox_sqz_add(f,s,name,offset,packed)) { xx_mem_free(data); return false; }
    s->items[s->count-1U].memory=data; s->items[s->count-1U].size=(int64_t)plain; return true;
}
static bool fox_sqz_rdc_block(const uint8_t *p,size_t size,uint8_t *dst,size_t cap,size_t *written,bool zero_history,xx_pd_struct *pd) {
    size_t at=0,out=0; uint16_t mask=0,flags=0;
    while(at<size) {
        unsigned token,kind,low,length,distance,i;
        if(pd && xx_pd_is_stopped(pd)) return false;
        mask>>=1;
        if(!mask) { if(size-at<2U) return false; flags=fox_sqz_u16(p+at); at+=2; mask=0x8000U; }
        if(at==size) return false;
        token=p[at++];
        if(!(mask&flags)) { if(out==cap) return false; dst[out++]=(uint8_t)token; continue; }
        kind=token>>4; low=token&15U;
        if(at==size) return false;
        if(kind<2U) {
            if(kind==0) length=low+3U;
            else { length=low+(unsigned)p[at++]*16U+19U; if(at==size) return false; }
            token=p[at++]; if(length>cap-out) return false;
            for(i=0;i<length;++i) dst[out++]=(uint8_t)token;
        } else {
            distance=low+3U+(unsigned)p[at++]*16U; length=kind;
            if(kind==2U) { if(at==size) return false; length=(unsigned)p[at++]+16U; }
            if((!zero_history && distance>out) || length>cap-out) return false;
            for(i=0;i<length;++i) { dst[out]=distance>out?0:dst[out-distance]; ++out; }
        }
    }
    *written=out; return true;
}
static void fox_sqz_fox_xor(uint8_t *p,size_t n,const uint8_t *key,size_t keysize) {
    size_t i; if(keysize) for(i=0;i<n;++i) p[i]^=key[i%keysize];
}
static bool fox_sqz_fox_keyed(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,uint8_t *key,char *credential) {
    uint8_t h[46]; size_t keysize=0,credential_size=0; unsigned method; int64_t at=46,size=pm_available(f);
    uint64_t retained=0;
    if(!pm_read(f,0,h,46) || xx_mem_compare(h,"FOXSQZ COMPRESSED FILE\0\x1a",24)) return false;
    method=fox_sqz_u16(h+32);
    if(h[24]) {
        unsigned extra=fox_sqz_u16(h+36),i; uint8_t step,acc=0;
        if(h[24]>4U || extra>26U || (extra && h[24]!=4U)) return false;
        xx_mem_copy(key,h+25,4); keysize=(size_t)h[24]+extra;
        if(!fox_sqz_take(f,&at,extra,key+4)) return false;
        step=key[0]; for(i=0;i<keysize;++i) { key[i]^=acc; acc=(uint8_t)(acc+step); }
        /* The archive stores a recovered binary XOR key, rather than the
         * original user password. Keep its public representation explicit and
         * reversible; decoding always uses the binary bytes above. */
        xx_mem_copy(credential,"embedded-key:",13U);
        for(i=0;i<keysize;++i) {
            static const char hex[]="0123456789ABCDEF";
            credential[13U+i*2U]=hex[key[i]>>4];
            credential[14U+i*2U]=hex[key[i]&15U];
        }
        credential_size=13U+keysize*2U+1U;
        credential[credential_size-1U]=0;
    }
    for(;;) {
        uint32_t packed_size,plain_size; uint16_t name_size,crc; uint8_t *packed=NULL,*plain=NULL;
        uint8_t rawname[1024]; char name[96]; size_t i,in=0,out=0; int64_t start;
        if((pd && xx_pd_is_stopped(pd)) || !fox_sqz_take(f,&at,23,h)) return false;
        fox_sqz_fox_xor(h,23,key,keysize);
        if(fox_sqz_u16(h)==0xfeU) { if(at!=size) return false; s->size=size; return true; }
        packed_size=fox_sqz_u32(h+3); plain_size=fox_sqz_u32(h+7); name_size=fox_sqz_u16(h+21); crc=fox_sqz_u16(h+11);
        if(fox_sqz_u16(h)!=0xebU || packed_size>INT32_MAX || plain_size>INT32_MAX ||
           !name_size || name_size>sizeof(rawname) || !fox_sqz_take(f,&at,name_size,rawname) ||
           retained+packed_size+plain_size+32768U+credential_size>fox_sqz_budget(f) ||
           !fox_sqz_room(f,s,(uint64_t)packed_size+plain_size+32768U+credential_size,true)) return false;
        fox_sqz_fox_xor(rawname,name_size,key,keysize);
        for(i=0;i<name_size && i<sizeof(name)-1U;++i) name[i]=rawname[i]>=32 && rawname[i]<127?(char)rawname[i]:'_';
        name[i]=0; start=at;
        packed=(uint8_t *)xx_mem_alloc(packed_size?packed_size:1U);
        plain=(uint8_t *)xx_mem_alloc(plain_size?plain_size:1U);
        if(!packed || !plain || !fox_sqz_take(f,&at,packed_size,packed)) { xx_mem_free(packed); xx_mem_free(plain); return false; }
        if(packed_size==plain_size) {
            while(in<packed_size) {
                size_t n=packed_size-in>8192U?8192U:packed_size-in;
                fox_sqz_fox_xor(packed+in,n,key,keysize); xx_mem_copy(plain+in,packed+in,n); in+=n;
            }
            out=plain_size;
        } else {
            bool valid=method==1U || method==4U,ended=false;
            while(valid && in<packed_size) {
                unsigned word; size_t n,wrote=0,expected,consumed=0;
                if((pd && xx_pd_is_stopped(pd)) || packed_size-in<2U) { valid=false; break; }
                word=fox_sqz_u16(packed+in); in+=2;
                if(!word) { ended=true; break; }
                n=(word&0x8000U)?0x10000U-word:word;
                if(n>16384U || n>packed_size-in) { valid=false; break; }
                fox_sqz_fox_xor(packed+in,n,key,keysize);
                expected=plain_size-out;
                if(word&0x8000U) { if(n>expected) valid=false; else { xx_mem_copy(plain+out,packed+in,n); wrote=n; } }
                else if(method==1U) {
                    if(expected>16384U) expected=16384U;
                    valid=fox_sqz_rdc_block(packed+in,n,plain+out,expected,&wrote,true,pd) && wrote==expected;
                } else {
                    size_t measured=0;
                    if(expected>8192U) expected=8192U;
                    valid=xx_dcl_scan_memory_zero_history(packed+in,n,expected,&consumed,&measured) &&
                          consumed==n && measured==expected &&
                          xx_dcl_decode_memory_zero_history(packed+in,n,plain+out,expected,&wrote) && wrote==expected;
                }
                in+=n; out+=wrote;
            }
            if(!valid || !ended || in!=packed_size || out!=plain_size) { xx_mem_free(packed); xx_mem_free(plain); return false; }
        }
        xx_mem_free(packed);
        if(out!=plain_size || xx_crc16(XX_CRC_TYPE_CRC16_ARC,plain,out)!=crc) { xx_mem_free(plain); return false; }
        if(!fox_sqz_owned(f,s,name,plain,plain_size,start,packed_size)) return false;
        s->items[s->count-1U].source_encrypted=keysize!=0;
        if(keysize && !(s->items[s->count-1U].password=xx_str_dup(credential))) return false;
        s->items[s->count-1U].compression_method=(uint16_t)method;
        retained+=plain_size+credential_size;
    }
}
static bool fox_sqz_fox(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t key[30]={0};char credential[74]={0};
    bool valid=fox_sqz_fox_keyed(f,s,pd,key,credential);
    xx_mem_zero(key,sizeof(key));xx_mem_zero(credential,sizeof(credential));return valid;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    if(pd&&xx_pd_is_stopped(pd))return false;
    return fox_sqz_fox(f,s,pd);
}

Abstractformat *xx_fox_sqz_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f)pm_init(f,d,base,XX_FILE_TYPE_FOX_SQZ,"bin");
    return f;
}
void xx_fox_sqz_free(Abstractformat *f) {
    if(f){xx_format_destroy(f);xx_mem_free(f);}
}

xx_file_type_t xx_fox_sqz_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t cursor,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    n=pm_available(&f);
    if(n>32)n=32;
    if(!pm_read(&f,0,h,(size_t)n))goto done;
    if(n>=24 && !xx_mem_compare(h,"FOXSQZ COMPRESSED FILE\0\x1a",24))type=XX_FILE_TYPE_FOX_SQZ;
done:
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_fox_sqz_open(xx_io_device *d) {return xx_fox_sqz_create(d,0);}
static const xx_file_type_t xx_fox_sqz_types[]={XX_FILE_TYPE_FOX_SQZ};
static const xx_format_search_desc xx_fox_sqz_desc={xx_fox_sqz_types,1,NULL,0,xx_fox_sqz_open,xx_fox_sqz_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(fox_sqz,xx_fox_sqz_desc)
