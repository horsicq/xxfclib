/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://www.lmd.de/products/vcl/lmdtools/
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/lmd_container/xx_lmd_container.h"
#include "../makeself/xx_fourth_wrapper_table.h"

/* A compressed bitmap is a sequence of length-prefixed blocks.  The 0x40
 * block stores a big-endian 16-bit control word, with one MSB-first control
 * bit for each literal or copy.  The 0x80 block stores literal bytes.  A
 * compressed block can expand to at most 0x800e bytes. */
#define LMD_BLOCK_LIMIT 0x800eU
#define LMD_MEMBER_LIMIT (64U * 1024U * 1024U)

static bool lmd_block(const uint8_t *input,size_t size,uint8_t *output,size_t *written) {
    size_t at=0,produced=0; uint32_t flags=0; unsigned remaining=0;
    if(!size) return false;
    if(input[0]==0x80) {
        if(size-1>LMD_BLOCK_LIMIT) return false;
        xx_rt_memcpy(output,input+1,size-1); *written=size-1; return true;
    }
    if(input[0]!=0x40 || size<3) return false;
    flags=((uint32_t)input[1]<<8)|input[2]; at=3; remaining=16;
    while(at<size) {
        if(!remaining) {
            if(size-at<2) return false;
            flags=((uint32_t)input[at]<<8)|input[at+1]; at+=2; remaining=16;
            if(at==size) return false;
        }
        if(flags&0x8000U) {
            uint8_t first,second; size_t distance,count,k;
            if(size-at<2) return false;
            first=input[at++]; second=input[at++];
            distance=((size_t)first<<4)|(second>>4);
            if(!distance) {
                if(size-at<2) return false;
                count=((size_t)second<<8)+input[at++]+16U;
                if(count>LMD_BLOCK_LIMIT-produced) return false;
                xx_rt_memset(output+produced,input[at++],count); produced+=count;
            } else {
                count=(second&15U)+3U;
                if(distance>produced || count>LMD_BLOCK_LIMIT-produced) return false;
                for(k=0;k<count;++k) { output[produced]=output[produced-distance]; ++produced; }
            }
        } else {
            if(produced==LMD_BLOCK_LIMIT) return false;
            output[produced++]=input[at++];
        }
        flags=(flags<<1)&0xffffU; --remaining;
    }
    *written=produced; return true;
}

static bool lmd_bitmap(Abstractformat *f,pm_member *member,xx_pd_struct *pd) {
    uint8_t *packed=NULL,*block=NULL,*plain=NULL; size_t at=0,used=0,capacity=0,size;
    bool ok=false;
    if(member->size<5 || (uint64_t)member->size>LMD_MEMBER_LIMIT) return false;
    size=(size_t)member->size;
    packed=(uint8_t *)xx_mem_alloc(size);
    block=(uint8_t *)xx_mem_alloc(LMD_BLOCK_LIMIT);
    if(!packed || !block || !pm_read(f,member->offset-f->base_address,packed,size)) goto done;
    while(at<size) {
        uint32_t length; size_t count;
        if(wg_stop(pd) || size-at<4) goto done;
        length=pm_le32(packed+at); at+=4;
        if(!length || length>LMD_BLOCK_LIMIT || length>size-at ||
           !lmd_block(packed+at,length,block,&count) ||
           count>LMD_MEMBER_LIMIT-used) goto done;
        at+=length;
        if(count) {
            if(used+count>capacity) {
                size_t wanted=capacity ? capacity*2U : LMD_BLOCK_LIMIT;
                uint8_t *next;
                if(wanted<used+count) wanted=used+count;
                if(wanted>LMD_MEMBER_LIMIT) wanted=LMD_MEMBER_LIMIT;
                next=(uint8_t *)xx_mem_realloc(plain,wanted);
                if(!next) goto done;
                plain=next; capacity=wanted;
            }
            xx_rt_memcpy(plain+used,block,count); used+=count;
        }
    }
    if(used<26 || plain[0]!='B' || plain[1]!='M' ||
       pm_le32(plain+2)!=(uint32_t)used || pm_le32(plain+10)>=used) goto done;
    member->memory=plain; member->size=(int64_t)used; plain=NULL;
    { size_t n=xx_rt_strlen(member->name);
      if(n<12 || xx_rt_memcmp(member->name+n-12,".lmd-encoded",12)) goto done;
      xx_rt_snprintf(member->name+n-12,13,".bmp");
    }
    ok=true;
done:
    if(plain) xx_mem_free(plain);
    if(block) xx_mem_free(block);
    if(packed) xx_mem_free(packed);
    return ok;
}

static bool wg_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16]; uint32_t count,i; int64_t limit=pm_available(f),table=13; bool bitmap;
    if(!pm_read(f,0,h,13) || h[0]!=8 || (xx_rt_memcmp(h+1,"LMDSLL30",8) && xx_rt_memcmp(h+1,"LMDBML30",8)) || !(count=pm_le32(h+9)) || count>4096 || (uint64_t)count*4>(uint64_t)(limit-table)) { return false; } bitmap=!xx_rt_memcmp(h+1,"LMDBML30",8);
    for(i=0;i<count;++i) { uint32_t begin,stop; int64_t data; char name[48];
        if(wg_stop(pd) || !pm_read(f,table+(int64_t)i*4,h,4)) { return false; } begin=pm_le32(h);
        if(i+1<count) { if(!pm_read(f,table+(int64_t)(i+1)*4,h,4)) return false; stop=pm_le32(h); } else { if(limit>UINT32_MAX) return false; stop=(uint32_t)limit; }
        if(begin<table+(uint64_t)count*4 || stop<=begin || !wg_range(limit,begin,stop-begin)) { return false; } data=begin;
        { uint8_t flags,n; uint32_t bytes; if(!pm_read(f,data++,&flags,1) || (flags&~15U) || !!(flags&8)!=bitmap || (!bitmap && !(flags&2))) return false;
          if(flags&2) { if(data>=stop || !pm_read(f,data++,&n,1) || !n || !wg_range(stop,data,n)) return false; data+=n; }
          if(flags&4) { if(!wg_range(stop,data,4)) return false; data+=4; }
          if(bitmap) { uint8_t b[18]; if(stop-data<4 || !pm_read(f,data,b,4) || (bytes=pm_le32(b))!=(uint64_t)(stop-data-4) || !bytes) return false; data+=4;
            if(flags&1) { if(bytes<5 || !pm_read(f,data,b,4) || pm_le32(b)!=bytes-4) return false; xx_rt_snprintf(name,sizeof(name),"bitmap-%u.lmd-encoded",i); }
            else { if(bytes<26 || !pm_read(f,data,b,18) || b[0]!='B' || b[1]!='M' || pm_le32(b+2)!=bytes || pm_le32(b+10)>=bytes) return false; xx_rt_snprintf(name,sizeof(name),"bitmap-%u.bmp",i); }
            if(!pm_add(f,s,name,data,bytes)) return false;
            xx_rt_snprintf(s->items[s->count-1].name,sizeof(s->items[s->count-1].name),
                           (flags&1) ? "%u.lmd-encoded" : "%u.bmp",i);
          } else {
            /* SLL's string and optional four-byte value describe the item;
             * neither is a file payload. U3 materializes an empty .txt. */
            if(data!=stop || !pm_add(f,s,"text",data,0)) return false;
            xx_rt_snprintf(s->items[s->count-1].name,sizeof(s->items[s->count-1].name),"%u.txt",i);
          }
        }
    } s->size=limit; return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    size_t i;
    if(!wg_parse(f,s,pd) || !wg_members(s,pd)) return false;
    for(i=0;i<s->count;++i) {
        pm_member *m=&s->items[i]; size_t n=xx_rt_strlen(m->name);
        if(n>=12 && !xx_rt_memcmp(m->name+n-12,".lmd-encoded",12) &&
           !lmd_bitmap(f,m,pd)) return false;
    }
    return true;
}
void xx_lmd_container_init(xx_lmd_container *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LMD_CONTAINER,"dat"); } }
xx_lmd_container *xx_lmd_container_create(xx_io_device *d,int64_t b) { xx_lmd_container *r=(xx_lmd_container *)xx_mem_alloc(sizeof(*r)); if(r) xx_lmd_container_init(r,d,b); return r; }
void xx_lmd_container_destroy(xx_lmd_container *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_lmd_container_free(xx_lmd_container *r) { if(r) { xx_lmd_container_destroy(r); xx_mem_free(r); } }
bool xx_lmd_container_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_lmd_container_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
