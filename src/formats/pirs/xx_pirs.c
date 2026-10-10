/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Xbox STFS PIRS/LIVE/CON package.
 */
#include "xxfclib/formats/pirs/xx_pirs.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"
#include "xxfclib/algo/sha/xx_sha.h"

/* Xbox STFS mapping and hash-tree layout:
 * https://github.com/hetelek/Velocity/blob/master/XboxInternals/Stfs/StfsPackage.cpp
 * The RSA package signature is outside archive integrity testing; the SHA-1
 * metadata, hash-table hierarchy and every used data block are verified. */
static uint32_t pirs_le24(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16; }
static uint32_t pirs_be24(const uint8_t *p) { return (uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2]; }
typedef struct pirs_stfs_volume { uint64_t first;uint32_t allocated;unsigned sex,level;uint8_t *entries; } pirs_stfs_volume;
typedef struct pirs_stfs_block { uint64_t at;uint8_t hash[20]; } pirs_stfs_block;
typedef struct pirs_stfs_member { uint32_t count;pirs_stfs_block blocks[1]; } pirs_stfs_member;
static uint64_t pirs_stfs_data(const pirs_stfs_volume *v,uint32_t block) {
    uint64_t physical=block+(((uint64_t)block+170)/170<<v->sex);
    if(block>=170) physical+=((uint64_t)block+28900)/28900<<v->sex;
    if(block>=28900) physical+=UINT64_C(1)<<v->sex;
    return v->first+physical*4096;
}
static uint64_t pirs_stfs_table(const pirs_stfs_volume *v,uint32_t block,unsigned level) {
    uint64_t copies=UINT64_C(1)<<v->sex,step0=170+copies,step1=28900+171*copies,physical;
    if(level==2) physical=step1;
    else if(level==1) physical=block<28900?step0:copies+(uint64_t)(block/28900)*step1;
    else if(block<170) physical=0;
    else physical=(uint64_t)(block/170)*step0+(((uint64_t)block/28900+1)<<v->sex)+(block>=28900?copies:0);
    return v->first+physical*4096;
}
static bool pirs_stfs_hashed(Abstractformat *f,uint64_t at,const uint8_t *expected,uint8_t *block,xx_pd_struct *pd) {
    uint8_t hash[20];return !xgr_stop(pd) && xgr_range(at,4096,(uint64_t)pm_available(f)) && pm_read(f,(int64_t)at,block,4096) && xx_sha1_memory(block,4096,hash) && !memcmp(hash,expected,20);
}
static bool pirs_stfs_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    pirs_stfs_member *ctx=(pirs_stfs_member *)m->context;uint8_t block[4096];uint32_t i;uint64_t left=(uint64_t)m->size;
    for(i=0;i<ctx->count;++i) { size_t n=left<4096?(size_t)left:4096;if(!n || !pirs_stfs_hashed(f,ctx->blocks[i].at,ctx->blocks[i].hash,block,pd) || !xgr_write(out,block,n)) return false;left-=n; }
    return left==0 && !xgr_stop(pd);
}
static bool pirs_stfs(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[0x3ad],top[4096],middle[4096],bottom[4096],hash[20],*rows=NULL,*used=NULL;pirs_stfs_volume v;xx_sha1_context sha;
    uint64_t n=(uint64_t)pm_available(f),p;uint32_t table_blocks,table_start,capacity,i,j,block;bool ok=false;
    memset(&v,0,sizeof(v));
    if(n<sizeof(h) || !pm_read(f,0,h,sizeof(h)) || (memcmp(h,"PIRS",4) && memcmp(h,"LIVE",4) && memcmp(h,"CON ",4)) || h[0x379]!=0x24 || (h[0x37b]&~3U) || xgr_be32(h+0x3a9)!=0) return false;
    v.first=((uint64_t)xgr_be32(h+0x340)+4095)&~4095ULL;v.allocated=xgr_be32(h+0x395);v.sex=(~h[0x37b])&1;
    v.level=v.allocated<=170?0:v.allocated<=28900?1:2;table_blocks=xgr_le16(h+0x37c);table_start=pirs_le24(h+0x37e);capacity=table_blocks*64;
    if(v.first<sizeof(h) || v.first>16U*1024U*1024U || v.first>n || !v.allocated || v.allocated>1000000U || !table_blocks || capacity>XGR_MAX_RECORDS || table_blocks>v.allocated || table_start>=v.allocated || !xgr_range(pirs_stfs_data(&v,v.allocated-1),4096,n)) return false;
    xx_sha1_init(&sha);
    for(p=0x344;p<v.first;) { size_t amount=v.first-p<4096?(size_t)(v.first-p):4096;if(xgr_stop(pd) || !pm_read(f,(int64_t)p,bottom,amount)) goto done;xx_sha1_update(&sha,bottom,amount);p+=amount; }
    if(!xx_sha1_final(&sha,hash,20) || memcmp(hash,h+0x32c,20) || !pirs_stfs_hashed(f,pirs_stfs_table(&v,0,v.level)+(uint64_t)(h[0x37b]&2)*2048,h+0x381,top,pd)) goto done;
    v.entries=(uint8_t *)xx_mem_alloc((size_t)v.allocated*24);used=(uint8_t *)xx_mem_alloc(v.allocated);rows=(uint8_t *)xx_mem_alloc((size_t)table_blocks*4096);
    if(!v.entries || !used || !rows) goto done;memset(used,0,v.allocated);
    for(i=0;i<v.allocated;i+=170) {
        const uint8_t *parent;uint32_t available=v.allocated-i<170?v.allocated-i:170;
        if(!v.level) memcpy(bottom,top,4096);
        else {
            if(v.level==1) parent=top+(i/170)*24;
            else {
                const uint8_t *upper=top+(i/28900)*24;
                if(i%28900==0 && !pirs_stfs_hashed(f,pirs_stfs_table(&v,i,1)+(v.sex?(uint64_t)(upper[20]&0x40)*64:0),upper,middle,pd)) goto done;
                parent=middle+((i/170)%170)*24;
            }
            if(!pirs_stfs_hashed(f,pirs_stfs_table(&v,i,0)+(v.sex?(uint64_t)(parent[20]&0x40)*64:0),parent,bottom,pd)) goto done;
        }
        memcpy(v.entries+(size_t)i*24,bottom,(size_t)available*24);
    }
    block=table_start;
    for(i=0;i<table_blocks;++i) {
        const uint8_t *entry;
        if(block>=v.allocated || used[block]) goto done;entry=v.entries+(size_t)block*24;
        if(entry[20]<0x80 || !pirs_stfs_hashed(f,pirs_stfs_data(&v,block),entry,rows+(size_t)i*4096,pd)) goto done;
        used[block]=1;block=pirs_be24(entry+21);
    }
    if(block!=0xffffffU) goto done;
    for(i=0;i<capacity;++i) {
        const uint8_t *r=rows+(size_t)i*64;uint32_t len=r[40]&63,chain[256],depth=0,node=i;char name[XGR_MAX_NAME+1];size_t length=0;
        if(!len) continue;
        if(xgr_stop(pd) || len>40) goto done;
        for(;;) {
            const uint8_t *part=rows+(size_t)node*64;uint32_t parent=xgr_be16(part+50),k;
            if(!(part[40]&63) || (part[40]&63)>40 || depth==256) goto done;
            for(k=0;k<depth;++k) if(chain[k]==node) goto done;chain[depth++]=node;
            if(parent==0xffffU) break;
            if(parent>=capacity || !(rows[(size_t)parent*64+40]&0x80)) goto done;node=parent;
        }
        while(depth) {
            const uint8_t *part=rows+(size_t)chain[--depth]*64;size_t amount=part[40]&63;char component[41];
            memcpy(component,part,amount);component[amount]=0;
            if(memchr(component,0,amount) || !xgr_name(component) || strchr(component,'/') || length+amount+(length?1:0)>XGR_MAX_NAME) goto done;
            if(length) name[length++]='/';memcpy(name+length,component,amount);length+=amount;name[length]=0;
        }
        if(r[40]&0x80) { if(!xgr_add(f,s,name,0,0)) goto done;s->items[s->count-1].directory=true; }
        else {
            uint32_t size=xgr_be32(r+52),count=pirs_le24(r+41),start=pirs_le24(r+47);pirs_stfs_member *ctx;size_t allocation;
            if(count!=((uint64_t)size+4095)/4096 || pirs_le24(r+44)!=count || count>v.allocated) goto done;
            allocation=sizeof(*ctx)+(count?(size_t)(count-1)*sizeof(ctx->blocks[0]):0);ctx=(pirs_stfs_member *)xx_mem_alloc(allocation);if(!ctx) goto done;ctx->count=count;block=start;
            for(j=0;j<count;++j) {
                const uint8_t *entry;
                if(xgr_stop(pd) || block>=v.allocated || used[block]) { xx_mem_free(ctx);goto done; }
                entry=v.entries+(size_t)block*24;
                if(entry[20]<0x80) { xx_mem_free(ctx);goto done; }
                used[block]=2;ctx->blocks[j].at=pirs_stfs_data(&v,block);memcpy(ctx->blocks[j].hash,entry,20);
                block=(r[40]&0x40)?block+1:pirs_be24(entry+21);
            }
            if(count && !(r[40]&0x40) && block!=0xffffffU) { xx_mem_free(ctx);goto done; }
            /* Logical blocks can span hash tables, so compressed_size here
             * describes allocated content blocks rather than a linear slice. */
            if(!xgr_add(f,s,name,count?ctx->blocks[0].at:0,0)) { xx_mem_free(ctx);goto done; }
            {pm_member *m=&s->items[s->count-1];m->size=size;m->packed_size=(int64_t)count*4096;m->context=ctx;m->free_context=xgr_free;m->read_all=pirs_stfs_read;}
        }
    }
    s->size=(int64_t)n;ok=s->count!=0;
done:xx_mem_free(v.entries);xx_mem_free(used);xx_mem_free(rows);return ok;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    return pm_available(f)>=0 && !xgr_stop(pd) && pirs_stfs(f,s,pd);
}
Abstractformat *xx_pirs_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(!f) return NULL;
    xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_PIRS,"pak");return f;
}
void xx_pirs_free(Abstractformat *f) {
    if(f) { xx_format_cleanup_extra_parameters(f);xx_mem_free(f); }
}
xx_file_type_t xx_pirs_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[64];int64_t size,old=xx_io_tell(d);
    xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;size=pm_available(&f);
    if(size<8 || !pm_read(&f,0,h,(size_t)(size<64?size:64))) goto done;
    { uint32_t magic=xgr_le32(h);
    if(magic==0x53524950U || magic==0x4556494cU || magic==0x204e4f43U) type=XX_FILE_TYPE_PIRS;
    }
done:
    if(old>=0) xx_io_seek64(d,old,SEEK_SET);
    return type;
}
static Abstractformat *pirs_open(xx_io_device *d) { return xx_pirs_create(d,0); }
static const xx_file_type_t pirs_types[]={XX_FILE_TYPE_PIRS};
static const xx_format_search_desc pirs_desc={
    pirs_types,1,NULL,0,pirs_open,xx_pirs_free,true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(pirs,pirs_desc)
