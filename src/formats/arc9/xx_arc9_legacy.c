/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native readers for ASD, CopyFloppyDisk, RDC, OSL2000, LDS, SKF and
 * FoxSqz. Container grammars independently checked against original samples
 * and the recovered U3 archive readers; native codec primitives are reused.
 */
#include "xx_arc9_legacy.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#define AL_CAP (256U*1024U*1024U)
static uint16_t al_u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static uint32_t al_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t al_u64(const uint8_t *p) { return al_u32(p)|((uint64_t)al_u32(p+4)<<32); }
static uint64_t al_budget(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):AL_CAP;
    return n<AL_CAP?n:AL_CAP;
}
static bool al_name(Abstractformat *f,int64_t *at,char *name,size_t cap,bool long_size) {
    uint8_t h[4],raw[1024]; uint32_t n; size_t i,copy;
    if(!pm_read(f,*at,h,long_size?4:1)) return false;
    *at+=long_size?4:1; n=long_size?al_u32(h):h[0];
    if(n>1024U || !pm_read(f,*at,raw,n)) return false;
    *at+=n;
    if(name && cap) {
        copy=n<cap-1U?n:cap-1U;
        for(i=0;i<copy;++i) name[i]=raw[i]>=32 && raw[i]<127?(char)raw[i]:'_';
        name[copy]=0;
    }
    return true;
}
static bool al_take(Abstractformat *f,int64_t *at,uint64_t n,void *p) {
    if(*at<0 || (uint64_t)*at>(uint64_t)pm_available(f) || n>(uint64_t)(pm_available(f)-*at)) return false;
    if(p && !pm_read(f,*at,p,(size_t)n)) return false;
    *at+=(int64_t)n; return true;
}
static bool al_word(Abstractformat *f,int64_t *at,uint32_t *v) {
    uint8_t h[4]; if(!al_take(f,at,4,h)) return false; *v=al_u32(h); return true;
}
static bool al_room(Abstractformat *f,pm_stream *s,uint64_t extra,bool adding) {
    size_t i,capacity=s->capacity;
    uint64_t used=extra,budget=al_budget(f);
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
static bool al_add(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t n) {
    return al_room(f,s,0,true) && pm_add(f,s,name,at,n);
}
static bool al_owned(Abstractformat *f,pm_stream *s,const char *name,uint8_t *data,size_t plain,int64_t offset,int64_t packed) {
    if(!al_room(f,s,plain,true) || !al_add(f,s,name,offset,packed)) { xx_mem_free(data); return false; }
    s->items[s->count-1U].memory=data; s->items[s->count-1U].size=(int64_t)plain; return true;
}

static bool al_lds(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[20]; int64_t at; uint32_t count,i;
    if(!pm_read(f,0,h,14) || xx_mem_compare(h,"STOR\1\1",6) || al_u64(h+6)<14U ||
       al_u64(h+6)>(uint64_t)pm_available(f)) return false;
    at=(int64_t)al_u64(h+6);
    if(!al_word(f,&at,&count) || count>1000000U) return false;
    for(i=0;i<count;++i) {
        uint64_t offset,size; uint32_t n; uint8_t raw[1024]; char name[96]; size_t j,copy;
        if((pd && xx_pd_is_stopped(pd)) || !al_take(f,&at,20,h)) return false;
        offset=al_u64(h); size=al_u64(h+8); n=al_u32(h+16);
        if(n>1024U || !al_take(f,&at,n,raw) || offset>INT64_MAX || size>INT64_MAX) return false;
        copy=n<95U?n:95U;
        for(j=0;j<copy;++j) name[j]=raw[j]>=32 && raw[j]<127?(char)raw[j]:'_';
        name[copy]=0;
        if(!al_add(f,s,name,(int64_t)offset,(int64_t)size)) return false;
    }
    s->size=pm_available(f); return true;
}
static uint8_t al_rol(uint8_t v) { return (uint8_t)((v<<1)|(v>>7)); }
static uint8_t al_ror(uint8_t v) { return (uint8_t)((v>>1)|(v<<7)); }
static void al_osl_decode(uint8_t *p,size_t n) {
    size_t i=n;
    while(i) { uint8_t v; --i; v=p[i]; if(i) v=p[i-1U]^(uint8_t)(al_rol(v)+i); p[i]=(uint8_t)(al_ror(v)-i)^90U; }
}
static uint16_t al_osl_sum(const uint8_t *p,size_t n) {
    uint16_t sum=0; size_t i;
    for(i=0;i<n;++i) { sum=(uint16_t)(sum+p[i]); sum=(uint16_t)((sum<<1)|(sum>>15)); }
    return sum;
}
static bool al_osl(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16],tail[2]; int64_t at=3,size=pm_available(f); uint64_t retained=0;
    if(!pm_read(f,0,h,3) || xx_mem_compare(h,"CAB",3)) return false;
    while(at<size) {
        uint32_t n; uint8_t *plain; char name[11]; size_t i;
        if((pd && xx_pd_is_stopped(pd)) || !al_take(f,&at,16,h)) return false;
        al_osl_decode(h,16); n=al_u32(h+2);
        if(al_u16(h)!=0x4948U || n>al_budget(f)-retained || !al_room(f,s,(uint64_t)n+1024U,true) || n>(uint64_t)(size-at) ||
           (uint64_t)at+n+2U>(uint64_t)size) return false;
        for(i=0;i<10;++i) name[i]=h[i+6]>=32 && h[i+6]<127?(char)h[i+6]:0;
        name[10]=0; plain=(uint8_t *)xx_mem_alloc(n?n:1U);
        if(!plain || !pm_read(f,at,plain,n) || !pm_read(f,at+n,tail,2)) { xx_mem_free(plain); return false; }
        al_osl_decode(plain,n);
        if(al_osl_sum(plain,n)!=al_u16(tail)) { xx_mem_free(plain); return false; }
        if(!xx_rt_strcmp(name,"MENU")) {
            uint8_t key=90;
            for(i=0;i<n;++i) { key=(uint8_t)((key>>4)+(key<<4)+1U); plain[i]^=key; }
        }
        if(!al_owned(f,s,name,plain,n,at,n)) return false;
        retained+=n; at+=(int64_t)n+2;
    }
    if(!s->count) return false;
    s->size=size; return true;
}

static bool al_rdc_block(const uint8_t *p,size_t size,uint8_t *dst,size_t cap,size_t *written,bool zero_history,xx_pd_struct *pd) {
    size_t at=0,out=0; uint16_t mask=0,flags=0;
    while(at<size) {
        unsigned token,kind,low,length,distance,i;
        if(pd && xx_pd_is_stopped(pd)) return false;
        mask>>=1;
        if(!mask) { if(size-at<2U) return false; flags=al_u16(p+at); at+=2; mask=0x8000U; }
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
static bool al_rdc(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    int64_t at=0,size=pm_available(f); uint8_t h[2],*image=NULL; size_t used=0,capacity=0;
    bool result=false;
    while(at<size) {
        uint16_t word; size_t n,wrote=0; uint8_t *packed,block[16384];
        if((pd && xx_pd_is_stopped(pd)) || !al_take(f,&at,2,h)) goto done;
        word=al_u16(h);
        if(!word) { if(at!=size) goto done; break; }
        n=(word&0x8000U)?(size_t)(0x10000U-word):word;
        if(n>(uint64_t)(size-at)) goto done;
        packed=(uint8_t *)xx_mem_alloc(n);
        if(!packed || !pm_read(f,at,packed,n)) { xx_mem_free(packed); goto done; }
        if(word&0x8000U) wrote=n;
        else if(!al_rdc_block(packed,n,block,sizeof(block),&wrote,false,pd)) { xx_mem_free(packed); goto done; }
        if(used+wrote>al_budget(f)) { xx_mem_free(packed); goto done; }
        if(capacity<used+wrote) {
            uint8_t *next; size_t cap=capacity?capacity:16384U;
            while(cap<used+wrote) { if(cap>al_budget(f)/2U) {cap=(size_t)al_budget(f);break;} cap*=2U; }
            if(!al_room(f,s,(uint64_t)cap+n+sizeof(block),true)) { xx_mem_free(packed); goto done; }
            next=(uint8_t *)xx_mem_realloc(image,cap);
            if(!next) { xx_mem_free(packed); goto done; } image=next; capacity=cap;
        }
        xx_mem_copy(image+used,(word&0x8000U)?packed:block,wrote); used+=wrote; at+=(int64_t)n;
        xx_mem_free(packed);
    }
    if(!used) goto done;
    result=al_owned(f,s,"decompressed.bin",image,used,0,size); image=NULL; s->size=size;
done:
    xx_mem_free(image); return result;
}

static bool al_cfd(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[32],small[2],*image=NULL; int64_t at=32,size=pm_available(f); size_t out=0,expected;
    bool result=false;
    if(!pm_read(f,0,h,32) || al_u32(h)!=0 || al_u16(h+4)!=0 || h[6] || al_u16(h+7)!=512U ||
       h[12]!=2U || (al_u16(h+22)!=1U && al_u16(h+22)!=2U) || al_u16(h+24)!=0U ||
       (al_u16(h+26)!=0U && al_u16(h+26)!=0x800U)) return false;
    if(al_u16(h+26)&0x800U) { if(!al_add(f,s,"floppy.ima",32,size-32)) return false; s->size=size; return true; }
    expected=(size_t)al_u16(h+15)*512U;
    if(!expected || !al_room(f,s,(uint64_t)expected+65536U,true)) return false;
    image=(uint8_t *)xx_mem_alloc(expected); if(!image) return false;
    while(at<size) {
        uint16_t packed,index_size; uint8_t *p; size_t n,table,position,data_left;
        if((pd && xx_pd_is_stopped(pd)) || !al_take(f,&at,2,small)) goto done;
        packed=al_u16(small);
        if(packed<4U || packed-2U>(uint64_t)(size-at)) goto done;
        n=packed-2U; p=(uint8_t *)xx_mem_alloc(n);
        if(!p || !pm_read(f,at,p,n)) { xx_mem_free(p); goto done; }
        index_size=al_u16(p);
        if(index_size<2U || (index_size-2U)%4U || index_size>n) { xx_mem_free(p); goto done; }
        position=index_size; data_left=n-index_size;
        for(table=2;table<index_size;table+=4) {
            size_t last=al_u16(p+table),run=al_u16(p+table+2),copy;
            if(last<position || (copy=last-position+1U)>data_left || copy>expected-out || run>expected-out-copy) { xx_mem_free(p); goto done; }
            xx_mem_copy(image+out,p+position,copy); out+=copy; position+=copy; data_left-=copy;
            while(run--) { image[out]=image[out-1U]; ++out; }
        }
        if(data_left>expected-out) { xx_mem_free(p); goto done; }
        xx_mem_copy(image+out,p+position,data_left); out+=data_left; at+=(int64_t)n; xx_mem_free(p);
    }
    if(out!=expected) goto done;
    result=al_owned(f,s,"floppy.ima",image,out,32,size-32); image=NULL; s->size=size;
done:
    xx_mem_free(image); return result;
}

static bool al_asd(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[14],*packed=NULL,*plain=NULL,history[4096]; int64_t at=8,size=pm_available(f),start;
    uint32_t count,i; uint64_t total=0; uint32_t *crcs=NULL; size_t in=0,out=0,pos=0; unsigned flags=0,mask=0,extra;
    bool result=false;
    if(!pm_read(f,0,h,8) || xx_mem_compare(h,"ASD01\x1a",6) || !(count=al_u16(h+6))) return false;
    crcs=(uint32_t *)xx_mem_alloc(count*sizeof(*crcs)); if(!crcs) return false;
    for(i=0;i<count;++i) {
        char name[96]; uint32_t n;
        if((pd && xx_pd_is_stopped(pd)) || !al_name(f,&at,name,sizeof(name),false) || !al_take(f,&at,14,h)) goto done;
        n=al_u32(h); crcs[i]=al_u32(h+4);
        if(total+n>al_budget(f)/2U || !al_add(f,s,name,0,0)) goto done;
        s->items[s->count-1U].size=n; total+=n;
    }
    start=at;
    if(size-at<1 || !al_room(f,s,(uint64_t)(size-at)+2U*total+count*sizeof(*crcs)+sizeof(history),false)) goto done;
    packed=(uint8_t *)xx_mem_alloc((size_t)(size-at)); plain=(uint8_t *)xx_mem_alloc(total?(size_t)total:1U);
    if(!packed || !plain || !pm_read(f,at,packed,(size_t)(size-at))) goto done;
    xx_rt_memset(history,'0',sizeof(history)); extra=packed[in++];
    while(out<total) {
        if(pd && xx_pd_is_stopped(pd)) goto done;
        mask>>=1;
        if(!mask) { if(in>=(uint64_t)(size-start)) goto done; flags=packed[in++]; mask=128U; }
        if(flags&mask) {
            unsigned token,length,distance,j;
            if(in+2U>(uint64_t)(size-start)) goto done;
            token=((unsigned)packed[in]<<8)|packed[in+1U]; in+=2;
            length=(token>>12); if(length==15U) length+=extra; length+=3U;
            distance=(token&4095U)+1U;
            if(length>total-out) goto done;
            for(j=0;j<length;++j) { uint8_t v=history[(pos-distance)&4095U]; plain[out++]=history[pos]=v;pos=(pos+1U)&4095U; }
        } else {
            uint8_t v; if(in>=(uint64_t)(size-start)) goto done; v=packed[in++]; plain[out++]=history[pos]=v; pos=(pos+1U)&4095U;
        }
    }
    /* ASD writers finish their last buffered group with ASCII '0' literals.
     * The archive CRCs and declared sizes cover only the real plaintext. */
    if((uint64_t)(size-start)-in>64U) goto done;
    while(in<(uint64_t)(size-start)) { if(packed[in]!=0 && packed[in]!='0') goto done; ++in; }
    out=0;
    for(i=0;i<count;++i) {
        size_t n=(size_t)s->items[i].size; uint8_t *copy;
        if(xx_crc32(XX_CRC_TYPE_CRC32,plain+out,n)!=crcs[i]) goto done;
        copy=(uint8_t *)xx_mem_alloc(n?n:1U); if(!copy) goto done;
        xx_mem_copy(copy,plain+out,n); s->items[i].memory=copy; s->items[i].compression_method=1; out+=n;
    }
    if(count) s->items[0].packed_size=size-start;
    s->size=size; result=true;
done:
    xx_mem_free(packed); xx_mem_free(plain); xx_mem_free(crcs); return result;
}

static bool al_skf_group(Abstractformat *f,pm_stream *s,int64_t *at,
                         unsigned version,const char *object,xx_pd_struct *pd) {
    uint32_t count,i,n; uint8_t h[2];
    if(!al_word(f,at,&count) || count>1000000U) return false;
    for(i=0;i<count;++i) {
        char name[128]; const char *ext="bin"; int64_t start;
        if((pd && xx_pd_is_stopped(pd)) || (version>218U && !al_take(f,at,4,NULL)) ||
           !al_word(f,at,&n) || n>INT32_MAX || !al_take(f,at,2,h)) return false;
        start=*at;
        if(n>2U) {
            if(!pm_read(f,start,h,2)) return false;
            if(h[0]=='B' && h[1]=='M') ext="bmp";
            else if(h[0]==137U && h[1]=='P') ext="png";
        }
        xx_rt_snprintf(name,sizeof(name),"%s_%u.%s",object,(unsigned)s->count+1U,ext);
        if(!al_take(f,at,n,NULL) || !al_add(f,s,name,start,n) ||
           !al_take(f,at,version>205U?24U:20U,NULL)) return false;
    }
    if(!al_word(f,at,&n) || n>INT32_MAX/6U || !al_take(f,at,(uint64_t)n*6U,NULL)) return false;
    if(version>201U) {
        if(!al_word(f,at,&n) || n>INT32_MAX/6U || !al_take(f,at,(uint64_t)n*6U,NULL)) return false;
        if(version>206U) {
            if(!al_word(f,at,&n)) return false;
            if(n && (!al_take(f,at,27,NULL) || !al_name(f,at,NULL,0,false))) return false;
        }
        if(version>218U && !al_take(f,at,4,NULL)) return false;
    }
    return al_take(f,at,4,NULL);
}
static bool al_skf(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    char version_name[16]; int64_t at=0; unsigned version; uint32_t objects,i,j;
    if(!al_name(f,&at,version_name,sizeof(version_name),false) ||
       xx_rt_strlen(version_name)<6U || xx_mem_compare(version_name,"SKF",3) ||
       version_name[3]<'0' || version_name[3]>'9' || version_name[4]!='.' ||
       version_name[5]<'0' || version_name[5]>'9') return false;
    version=(unsigned)(version_name[3]-'0')*100U+(unsigned)(version_name[5]-'0');
    if(version_name[6]) {
        if(version_name[6]<'0' || version_name[6]>'9' || version_name[7]) return false;
        version=version/100U*100U+(unsigned)(version_name[5]-'0')*10U+(unsigned)(version_name[6]-'0');
    }
    if(version<200U || version>299U) return false;
    for(i=0;i<(version>218U?10U:4U);++i) if(!al_name(f,&at,NULL,0,true)) return false;
    if(!al_word(f,&at,&objects) || objects>1000000U) return false;
    for(i=0;i<objects;++i) {
        char object[96]; uint32_t properties;
        if((pd && xx_pd_is_stopped(pd)) || !al_name(f,&at,object,sizeof(object),false) || !object[0] ||
           !al_word(f,&at,&properties) || properties>65535U) return false;
        for(j=0;j<properties;++j) {
            uint8_t h[2]; unsigned prop,k; uint32_t n,m;
            if(!al_take(f,&at,2,h) || !(prop=al_u16(h)) || !al_skf_group(f,s,&at,version,object,pd)) return false;
            if(prop==13U) { if(!al_take(f,&at,4,NULL)) return false; }
            else if(prop==44U) {
                if(!al_word(f,&at,&n) || n>INT32_MAX || !al_take(f,&at,n,NULL)) return false;
            } else if(prop==2U) {
                for(k=0;k<4U;++k) {
                    if(!al_skf_group(f,s,&at,version,object,pd) || !al_word(f,&at,&n) ||
                       n>INT32_MAX/14U || !al_take(f,&at,(uint64_t)n*14U,NULL) ||
                       !al_word(f,&at,&m) || m>1000000U) return false;
                    while(m--) {
                        if(!al_take(f,&at,14,NULL) || !al_word(f,&at,&n) ||
                           n>INT32_MAX || !al_take(f,&at,n,NULL)) return false;
                    }
                    if(version<209U && !al_take(f,&at,4,NULL)) return false;
                }
                if(!al_skf_group(f,s,&at,version,object,pd)) return false;
                if(version<=208U && !al_take(f,&at,version==200U?100U:version==201U?148U:76U,NULL)) return false;
            }
        }
    }
    /* SkinCrafter appends a version-specific global settings block. Preserve
     * it alongside the resource payloads rather than discarding unknown fields. */
    if(at<pm_available(f) && !al_add(f,s,"skin-metadata.bin",at,pm_available(f)-at)) return false;
    s->size=pm_available(f); return true;
}

static void al_fox_xor(uint8_t *p,size_t n,const uint8_t *key,size_t keysize) {
    size_t i; if(keysize) for(i=0;i<n;++i) p[i]^=key[i%keysize];
}
static bool al_fox_keyed(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,uint8_t *key,char *credential) {
    uint8_t h[46]; size_t keysize=0,credential_size=0; unsigned method; int64_t at=46,size=pm_available(f);
    uint64_t retained=0;
    if(!pm_read(f,0,h,46) || xx_mem_compare(h,"FOXSQZ COMPRESSED FILE\0\x1a",24)) return false;
    method=al_u16(h+32);
    if(h[24]) {
        unsigned extra=al_u16(h+36),i; uint8_t step,acc=0;
        if(h[24]>4U || extra>26U || (extra && h[24]!=4U)) return false;
        xx_mem_copy(key,h+25,4); keysize=(size_t)h[24]+extra;
        if(!al_take(f,&at,extra,key+4)) return false;
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
        if((pd && xx_pd_is_stopped(pd)) || !al_take(f,&at,23,h)) return false;
        al_fox_xor(h,23,key,keysize);
        if(al_u16(h)==0xfeU) { if(at!=size) return false; s->size=size; return true; }
        packed_size=al_u32(h+3); plain_size=al_u32(h+7); name_size=al_u16(h+21); crc=al_u16(h+11);
        if(al_u16(h)!=0xebU || packed_size>INT32_MAX || plain_size>INT32_MAX ||
           !name_size || name_size>sizeof(rawname) || !al_take(f,&at,name_size,rawname) ||
           retained+packed_size+plain_size+32768U+credential_size>al_budget(f) ||
           !al_room(f,s,(uint64_t)packed_size+plain_size+32768U+credential_size,true)) return false;
        al_fox_xor(rawname,name_size,key,keysize);
        for(i=0;i<name_size && i<sizeof(name)-1U;++i) name[i]=rawname[i]>=32 && rawname[i]<127?(char)rawname[i]:'_';
        name[i]=0; start=at;
        packed=(uint8_t *)xx_mem_alloc(packed_size?packed_size:1U);
        plain=(uint8_t *)xx_mem_alloc(plain_size?plain_size:1U);
        if(!packed || !plain || !al_take(f,&at,packed_size,packed)) { xx_mem_free(packed); xx_mem_free(plain); return false; }
        if(packed_size==plain_size) {
            while(in<packed_size) {
                size_t n=packed_size-in>8192U?8192U:packed_size-in;
                al_fox_xor(packed+in,n,key,keysize); xx_mem_copy(plain+in,packed+in,n); in+=n;
            }
            out=plain_size;
        } else {
            bool valid=method==1U || method==4U,ended=false;
            while(valid && in<packed_size) {
                unsigned word; size_t n,wrote=0,expected,consumed=0;
                if((pd && xx_pd_is_stopped(pd)) || packed_size-in<2U) { valid=false; break; }
                word=al_u16(packed+in); in+=2;
                if(!word) { ended=true; break; }
                n=(word&0x8000U)?0x10000U-word:word;
                if(n>16384U || n>packed_size-in) { valid=false; break; }
                al_fox_xor(packed+in,n,key,keysize);
                expected=plain_size-out;
                if(word&0x8000U) { if(n>expected) valid=false; else { xx_mem_copy(plain+out,packed+in,n); wrote=n; } }
                else if(method==1U) {
                    if(expected>16384U) expected=16384U;
                    valid=al_rdc_block(packed+in,n,plain+out,expected,&wrote,true,pd) && wrote==expected;
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
        if(!al_owned(f,s,name,plain,plain_size,start,packed_size)) return false;
        s->items[s->count-1U].source_encrypted=keysize!=0;
        if(keysize && !(s->items[s->count-1U].password=xx_str_dup(credential))) return false;
        s->items[s->count-1U].compression_method=(uint16_t)method;
        retained+=plain_size+credential_size;
    }
}
static bool al_fox(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t key[30]={0};char credential[74]={0};
    bool valid=al_fox_keyed(f,s,pd,key,credential);
    xx_mem_zero(key,sizeof(key));xx_mem_zero(credential,sizeof(credential));return valid;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    if(pd && xx_pd_is_stopped(pd)) return false;
    switch(f->file_type) {
    case XX_FILE_TYPE_LDS:return al_lds(f,s,pd);
    case XX_FILE_TYPE_OSL2000:return al_osl(f,s,pd);
    case XX_FILE_TYPE_RDC:return al_rdc(f,s,pd);
    case XX_FILE_TYPE_CFD:return al_cfd(f,s,pd);
    case XX_FILE_TYPE_ASD:return al_asd(f,s,pd);
    case XX_FILE_TYPE_SKF:return al_skf(f,s,pd);
    case XX_FILE_TYPE_FOX_SQZ:return al_fox(f,s,pd);
    default:return false;
    }
}
Abstractformat *xx_arc9_legacy_create(xx_io_device *d,int64_t b,xx_file_type_t type) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f)); if(f) pm_init(f,d,b,type,"bin"); return f;
}
void xx_arc9_legacy_free(Abstractformat *f) { if(f) { xx_format_destroy(f); xx_mem_free(f); } }
bool xx_arc9_legacy_probe(xx_io_device *d,int64_t b,xx_file_type_t type) {
    Abstractformat f; bool valid; pm_init(&f,d,b,type,"bin"); valid=pm_valid(&f,NULL); xx_format_destroy(&f); return valid;
}
xx_file_type_t xx_arc9_legacy_detect(xx_io_device *d,int64_t base) {
    Abstractformat f; uint8_t h[32]; int64_t cursor; size_t n; xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d || base<0 || xx_io_size(d)<base) return type;
    cursor=xx_io_tell(d); xx_mem_zero(&f,sizeof(f)); f.device=d; f.base_address=base;
    n=(uint64_t)pm_available(&f)<32U?(size_t)pm_available(&f):32U;
    if(!pm_read(&f,0,h,n)) goto done;
    if(n>=8 && !xx_mem_compare(h,"ASD01\x1a",6)) type=XX_FILE_TYPE_ASD;
    else if(n>=7 && (h[0]==6 || h[0]==7) && !xx_mem_compare(h+1,"SKF2.",5)) type=XX_FILE_TYPE_SKF;
    else if(n>=24 && !xx_mem_compare(h,"FOXSQZ COMPRESSED FILE\0\x1a",24)) type=XX_FILE_TYPE_FOX_SQZ;
    else if(n>=14 && !xx_mem_compare(h,"STOR\1\1",6)) type=XX_FILE_TYPE_LDS;
    else if(n>=19 && !xx_mem_compare(h,"CAB",3)) { uint8_t header[16];xx_mem_copy(header,h+3,16);al_osl_decode(header,16);if(al_u16(header)==0x4948U) type=XX_FILE_TYPE_OSL2000; }
    else if(n>=32 && al_u32(h)==0 && al_u16(h+4)==0 && !h[6] && al_u16(h+7)==512U && h[12]==2 &&
       (al_u16(h+22)==1 || al_u16(h+22)==2) && al_u16(h+24)==0 && (al_u16(h+26)==0 || al_u16(h+26)==2048U)) type=XX_FILE_TYPE_CFD;
    else {
        const char *path=xx_io_source_path(d); size_t len=path?xx_rt_strlen(path):0;
        if(len>=4U && path[len-1U]=='_' && path[len-4U]=='.' && xx_arc9_legacy_probe(d,base,XX_FILE_TYPE_RDC)) type=XX_FILE_TYPE_RDC;
    }
done:
    (void)xx_io_seek64(d,cursor,SEEK_SET); return type;
}
