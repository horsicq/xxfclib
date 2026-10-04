/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original bounded Opera read-only filesystem parser, from the published
 * Portfolio DiscLabel / DirectoryHeader / DirectoryRecord structures.
 * https://github.com/trapexit/portfolio_os/tree/master/src/filesystem/includes
 */
#include "xxfclib/formats/opera_fs/xx_opera_fs.h"
#include "../apple_family/xx_apple_family_private.h"
typedef struct op_file {uint32_t block,blocks,bytes,burst,gap,avatar;} op_file;
typedef struct op_ctx {af_work *w;uint32_t volume_block,volume_count;uint64_t *seen;uint32_t directories;} op_ctx;
static bool op_bounds(op_ctx *c,const op_file *f,uint32_t avatar) {
    uint64_t last=f->blocks?(uint64_t)(f->blocks-1U):0U,end;
    if(f->block<c->volume_block || f->block>65536U || f->block%c->volume_block ||
       f->bytes>(uint64_t)f->blocks*f->block || (!f->burst && f->gap))return false;
    if(!f->blocks)return !f->bytes && avatar<=c->volume_count;
    if(f->burst)last=(last/f->burst)*(f->burst+(uint64_t)f->gap)+last%f->burst;
    end=(uint64_t)avatar+(last+1U)*(f->block/c->volume_block);
    return end<=c->volume_count;
}
static bool op_block(op_ctx *c,const op_file *f,uint32_t i,uint8_t *out) {
    uint64_t index=i;if(i>=f->blocks || !af_poll(c->w))return false;
    if(f->burst)index=(index/f->burst)*(f->burst+(uint64_t)f->gap)+index%f->burst;
    index=(uint64_t)f->avatar*c->volume_block+index*f->block;
    return index<=INT64_MAX && af_read(c->w,(int64_t)index,out,f->block);
}
static bool op_walk(op_ctx *c,const op_file *dir,const char *parent,unsigned depth) {
    uint8_t *block=NULL;uint32_t current=0,previous=UINT32_MAX,walked=0,i;bool ok=false;uint64_t id=(uint64_t)dir->avatar*c->volume_block;
    if(depth>32U || c->directories>=8192U || !dir->blocks || !op_bounds(c,dir,dir->avatar))return false;
    for(i=0;i<c->directories;++i)if(c->seen[i]==id)return false;c->seen[c->directories++]=id;
    block=af_alloc(c->w,dir->block,false);if(!block)return false;
    while(current!=UINT32_MAX){uint32_t next,first,free_at,at;bool end=false;
        if(++walked>dir->blocks || !op_block(c,dir,current,block))goto done;
        next=pm_be32(block);first=pm_be32(block+16);free_at=pm_be32(block+12);
        if(pm_be32(block+4)!=previous || first<20U || first>free_at || free_at>dir->block ||
           (next!=UINT32_MAX && next>=dir->blocks))goto done;
        at=first;
        while(at<free_at){const uint8_t *e=block+at;op_file file;uint32_t flags,avatars,j,n;char leaf[96],name[96];size_t len;uint8_t *out=NULL,*buf=NULL;
            if(free_at-at<72U || !af_poll(c->w))goto done;
            flags=pm_be32(e);avatars=pm_be32(e+64);if(avatars>=256U || 72U+avatars*4U>free_at-at)goto done;n=72U+avatars*4U;
            file.block=pm_be32(e+12);file.bytes=pm_be32(e+16);file.blocks=pm_be32(e+20);file.burst=pm_be32(e+24);file.gap=pm_be32(e+28);file.avatar=pm_be32(e+68);
            len=0;while(len<32U && e[32+len])++len;if(!af_leaf(leaf,sizeof(leaf),e+32,len))goto done;
            if(parent && *parent){if(xx_rt_strlen(parent)+xx_rt_strlen(leaf)+2U>=sizeof(name))goto done;xx_rt_snprintf(name,sizeof(name),"%s/%s",parent,leaf);}else xx_rt_strncpy(name,leaf,sizeof(name));
            for(j=0;j<=avatars;++j)if(!op_bounds(c,&file,pm_be32(e+68+j*4U)))goto done;
            if(flags&1U){if(file.bytes && file.bytes<(uint64_t)file.blocks*file.block)goto done;
                if(!af_add(c->w,name,0,0,NULL))goto done;c->w->s->items[c->w->s->count-1U].compression_method=65535U;
                if(!op_walk(c,&file,name,depth+1U))goto done;
            }else{uint32_t copied=0;
                out=af_alloc(c->w,file.bytes,true);buf=af_alloc(c->w,file.block,false);
                if(!out || !buf){af_release(c->w,out,file.bytes);af_release(c->w,buf,file.block);goto done;}
                for(j=0;j<file.blocks;++j){uint32_t z=file.bytes-copied;if(z>file.block)z=file.block;
                    if(!op_block(c,&file,j,buf)){af_release(c->w,out,file.bytes);af_release(c->w,buf,file.block);goto done;}
                    if(z)xx_rt_memcpy(out+copied,buf,z);copied+=z;}
                af_release(c->w,buf,file.block);
                if(!af_add(c->w,name,0,file.bytes,out)){af_release(c->w,out,file.bytes);goto done;}
            }
            at+=n;
            if(flags&0xc0000000U){if(!(flags&0x40000000U) || at>free_at)goto done;end=(flags&0x80000000U)!=0;break;}
        }
        if(end){ok=af_poll(c->w);goto done;}
        if(at!=free_at || (first!=free_at && next==UINT32_MAX))goto done;
        previous=current;current=next;
    }
    ok=af_poll(c->w);
done:af_release(c->w,block,dir->block);return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    af_work w;op_ctx c;op_file root;uint8_t h[132];uint32_t avatars,i;int64_t n;bool ok=false;
    if(!af_init(&w,f,s,pd) || (n=pm_available(f))<2048 || !af_read(&w,0,h,sizeof(h)))return false;
    if(h[0]!=1 || xx_rt_memcmp(h+1,"ZZZZZ",5) || h[6]!=1)return false;
    xx_mem_zero(&c,sizeof(c));c.w=&w;c.volume_block=pm_be32(h+76);c.volume_count=pm_be32(h+80);avatars=pm_be32(h+96);
    if(c.volume_block<512U || c.volume_block>65536U || (c.volume_block&(c.volume_block-1U)) ||
       !c.volume_count || (uint64_t)c.volume_count*c.volume_block>(uint64_t)n || avatars>7U)return false;
    root.block=pm_be32(h+92);root.blocks=pm_be32(h+88);root.bytes=0;root.burst=root.gap=0;root.avatar=pm_be32(h+100);
    for(i=0;i<=avatars;++i)if(!op_bounds(&c,&root,pm_be32(h+100+i*4U)))return false;
    c.seen=(uint64_t *)af_alloc(&w,8192U*sizeof(uint64_t),false);if(!c.seen)return false;
    ok=op_walk(&c,&root,"",0);af_release(&w,c.seen,8192U*sizeof(uint64_t));
    if(ok){xx_opera_fs *r=(xx_opera_fs *)f;s->size=(int64_t)c.volume_count*c.volume_block;r->number_of_records=s->count;
        r->note="Opera directory/extent and burst-gap validation; primary avatar file bytes; no per-file checksum exists";}
    return ok;
}
AF_DEFINE_READER(opera_fs,XX_FILE_TYPE_OPERA_FS,"iso")
