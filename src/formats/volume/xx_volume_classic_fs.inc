/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original read-only classic filesystem walkers. On-disk facts: Linux
 * v6.12 sysv, qnx4, efs and bfs declarations; Linux v0.99-pl11 ext/xiafs.
 * No kernel implementation is copied or linked. Input remains borrowed;
 * logical file maps and directory workspaces are bounded RAM allocations. */
typedef struct vr_classic {
    Abstractformat *f; pm_stream *s; xx_pd_struct *pd;
    uint64_t limit,used,blocks,inodes,table,firstdata,ancestors[64];
    uint32_t block,cgfirst,cgsize,cginodes,cgcount; unsigned order,kind,depth;
} vr_classic;
enum { VC_UNIX,VC_EXT,VC_XIA,VC_QNX,VC_EFS,VC_BFS };
typedef struct vc_member_context { mm_map *map;uint64_t retained,limit; } vc_member_context;
static void vc_member_free(void *opaque) {vc_member_context *c=(vc_member_context *)opaque;if(c){mm_free(c->map);xx_mem_free(c);}}
static bool vc_member_range(Abstractformat *f,pm_member *m,uint64_t at,void *out,size_t n,xx_pd_struct *pd) {
    vc_member_context *c=(vc_member_context *)m->context;pm_member mapped;if(!c || !c->map)return false;xx_mem_zero(&mapped,sizeof(mapped));mapped.context=c->map;return mm_read(f,&mapped,at,out,n,pd);
}
static bool vc_member_unpack(Abstractformat *f,pm_member *m,xx_io_device *output,xx_pd_struct *pd) {
    vc_member_context *c=(vc_member_context *)m->context;const xx_var *v;uint64_t limit,at=0;size_t capacity=xx_get_file_buffer_size();uint8_t *b=NULL;
    if(!c || !vr_poll(pd))return false;limit=c->limit;
    v=xx_format_resolve_extra_parameter(f,((xx_volume *)f)->parse_options,XX_META_ID_OPT_MEMORY_LIMIT);if(v)limit=xx_var_get_u64(v);
    if(limit<c->retained || (c->map && m->size && capacity>limit-c->retained))return false;
    if(c->map && m->size) {b=(uint8_t *)xx_mem_alloc(capacity);if(!b)return false;}
    while(at<(uint64_t)m->size) {size_t n=(uint64_t)m->size-at<capacity?(size_t)((uint64_t)m->size-at):capacity,done=0;const uint8_t *data=m->memory?m->memory+(size_t)at:b;
        if(!vr_poll(pd) || (c->map && !vc_member_range(f,m,at,b,n,pd))) {xx_mem_free(b);return false;}
        while(output && done<n) {ssize_t bytes=xx_io_write(output,data+done,n-done);if(bytes<=0 || (size_t)bytes>n-done){xx_mem_free(b);return false;}done+=(size_t)bytes;}at+=n;
    }xx_mem_free(b);return vr_poll(pd);
}
static bool vr_classic_name(xx_file_type_t t) {
    return t==XX_FILE_TYPE_COHERENT_FS || t==XX_FILE_TYPE_LINUX_EXT || t==XX_FILE_TYPE_QNX4 ||
        t==XX_FILE_TYPE_SGI_EFS || t==XX_FILE_TYPE_SYSV_FS || t==XX_FILE_TYPE_UNIX_V7 ||
        t==XX_FILE_TYPE_UNIX_BFS || t==XX_FILE_TYPE_XENIX_FS || t==XX_FILE_TYPE_XIA_FS;
}
static uint16_t vc16(vr_classic *c,const uint8_t *p) { return c->order==1?pm_be16(p):pm_le16(p); }
static uint32_t vc32(vr_classic *c,const uint8_t *p) {
    if(c->order==2) return ((uint32_t)pm_le16(p)<<16)|pm_le16(p+2);
    return c->order==1?pm_be32(p):pm_le32(p);
}
static uint32_t vc24(vr_classic *c,const uint8_t *p) {
    if(c->order==2) return ((uint32_t)p[0]<<16)|p[1]|((uint32_t)p[2]<<8);
    return c->order==1?((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2]:p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);
}
static bool vr_classic_v7_identity(xx_volume *r,const uint8_t *super,int64_t source,unsigned *order) {
    vr_classic c;uint8_t root[64];unsigned i;xx_mem_zero(&c,sizeof(c));
    if(source<1536 || (source&511) || !pm_read(&r->format,1088,root,sizeof(root)))return false;
    for(i=0;i<3;++i) {uint32_t blocks,bytes;c.order=i;blocks=vc32(&c,super+2);bytes=vc32(&c,root+8);
        if(blocks && blocks<=UINT32_C(0xffffff) && (uint64_t)blocks<=(uint64_t)source/512 &&
           vc16(&c,super)>=3 && vc16(&c,super)<blocks && vc16(&c,super+6)<=50 && vc16(&c,super+208)<=100 &&
           (vc16(&c,root)&0xf000)==0x4000 && vc16(&c,root+2) && bytes>=32 && !(bytes&15) && bytes<=16384) {
            r->blocks=blocks;r->big_endian=i==1;if(order)*order=i;return true;
        }
    }return false;
}
static bool vc_take(vr_classic *c,uint64_t n) { if(c->used>c->limit || n>c->limit-c->used) return false; c->used+=n; return true; }
static bool vc_block(vr_classic *c,uint64_t block,void *b) {
    return vr_poll(c->pd) && block<c->blocks && block<=INT64_MAX/c->block && pm_read(c->f,(int64_t)(block*c->block),b,c->block);
}
static bool vc_extent(vr_classic *c,mm_map *m,int64_t source,uint64_t size) {
    size_t old=m->capacity; uint64_t allocation;
    if(!size) return true;
    if(m->count) { mm_extent *last=&m->extents[m->count-1];
        if((source==-1 && last->source==-1) || (source>=0 && last->source>=0 && (uint64_t)source==(uint64_t)last->source+last->length)) {
            if(size>INT64_MAX-m->size || (source>=0 && ((uint64_t)source>(uint64_t)pm_available(c->f) || size>(uint64_t)pm_available(c->f)-(uint64_t)source))) return false;
            last->length+=size; m->size+=size; return true;
        }
    }
    allocation=m->count==m->capacity?(m->capacity?m->capacity:8U)*sizeof(mm_extent):0;
    if(!vc_take(c,allocation)) return false;
    if(!mm_add(c->f,m,source,size)) { c->used-=allocation; return false; }
    return m->capacity>=old;
}
static void vc_drop_map(vr_classic *c,mm_map *m) { if(m) { c->used-=sizeof(*m)+m->capacity*sizeof(mm_extent); mm_free(m); } }
static mm_map *vc_new_map(vr_classic *c) { mm_map *m; if(!vc_take(c,sizeof(mm_map))) return NULL; m=mm_create(); if(!m)c->used-=sizeof(mm_map); return m; }
static bool vc_pointer(vr_classic *c,mm_map *m,uint32_t block,unsigned height,uint64_t *left,uint64_t *stack,unsigned depth) {
    uint64_t capacity=c->block,bytes; uint8_t *b=NULL; unsigned i; bool ok=false;
    if(!*left) return true;
    for(i=0;i<height;++i) { if(capacity>UINT64_MAX/(c->block/4)) return false; capacity*=c->block/4; }
    bytes=*left<capacity?*left:capacity;
    if(!block) { if(!vc_extent(c,m,-1,bytes)) return false; *left-=bytes; return true; }
    if(block<c->firstdata || block>=c->blocks || depth>=4) return false;
    for(i=0;i<depth;++i) if(stack[i]==block) return false;
    if(!height) { if(!vc_extent(c,m,(int64_t)((uint64_t)block*c->block),bytes)) return false; *left-=bytes; return true; }
    if(!vc_take(c,c->block)) return false; b=(uint8_t *)xx_mem_alloc(c->block);
    if(!b || !vc_block(c,block,b)) goto done; stack[depth]=block; ok=true;
    for(i=0;ok && i<c->block/4 && *left;++i) { uint32_t p=vc32(c,b+4*i); if(c->kind==VC_XIA)p&=0xffffff; ok=vr_poll(c->pd) && vc_pointer(c,m,p,height-1,left,stack,depth+1); }
done: xx_mem_free(b); c->used-=c->block; return ok;
}
static bool vc_efs_extent(vr_classic *c,mm_map *m,const uint8_t *e,uint64_t size,uint64_t *logical) {
    uint32_t block=((uint32_t)e[1]<<16)|((uint32_t)e[2]<<8)|e[3];
    uint64_t at=(((uint32_t)e[5]<<16)|((uint32_t)e[6]<<8)|e[7])*UINT64_C(512),bytes=(uint64_t)e[4]*512;
    if(e[0] || !e[4] || !block || block>=c->blocks || e[4]>c->blocks-block || at<*logical || at>=size) return false;
    if(at>*logical && !vc_extent(c,m,-1,at-*logical)) return false;
    if(bytes>size-at)bytes=size-at; if(!vc_extent(c,m,(int64_t)block*512,bytes)) return false; *logical=at+bytes; return true;
}
static mm_map *vc_map(vr_classic *c,const uint8_t *raw,uint64_t size) {
    mm_map *m=vc_new_map(c); uint64_t left=size,stack[4],logical=0; unsigned i; bool ok=true;
    if(!m) return NULL;
    if(c->kind==VC_UNIX || c->kind==VC_EXT || c->kind==VC_XIA) {
        unsigned direct=c->kind==VC_UNIX?10:c->kind==VC_EXT?9:8,indirect=c->kind==VC_XIA?2:3;
        unsigned start=c->kind==VC_UNIX?12:c->kind==VC_EXT?16:24;
        for(i=0;ok && i<direct+indirect && left;++i) { uint32_t p=c->kind==VC_UNIX?vc24(c,raw+start+3*i):vc32(c,raw+start+4*i); if(c->kind==VC_XIA)p&=0xffffff;
            ok=vr_poll(c->pd) && vc_pointer(c,m,p,i<direct?0:i-direct+1,&left,stack,0); }
        ok=ok && !left;
    } else if(c->kind==VC_BFS) {
        uint32_t first=pm_le32(raw+4),last=pm_le32(raw+8);
        if(size)ok=first>=c->firstdata && last>=first && last<c->blocks && size<=(uint64_t)(last-first+1)*512 && vc_extent(c,m,(int64_t)first*512,size);
    } else if(c->kind==VC_QNX) {
        uint16_t extents=pm_le16(raw+48); uint32_t next=pm_le32(raw+28),first=pm_le32(raw+20),n=pm_le32(raw+24),previous=0; uint8_t b[512]; unsigned consumed=0,chains=0;
        if(size && !extents)ok=false;
        if(extents) { if(!first || !n || first-1>=c->blocks || n>c->blocks-(first-1))ok=false;
            else { uint64_t bytes=(uint64_t)n*512;if(bytes>left)bytes=left;ok=vc_extent(c,m,(int64_t)(first-1)*512,bytes);left-=bytes; ++consumed; } }
        while(ok && consumed<extents) { unsigned j,count;
            if(!next || next==previous || ++chains>1093 || !vc_block(c,next-1,b) || xx_rt_memcmp(b+496,"IamXblk",7)) { ok=false;break; }
            count=b[8];if(!count || count>60 || count>extents-consumed || pm_le32(b+4)!=previous) {ok=false;break;}
            for(j=0;ok && j<count;++j) { uint64_t bytes; first=pm_le32(b+16+j*8);n=pm_le32(b+20+j*8);
                if(!first || !n || first-1>=c->blocks || n>c->blocks-(first-1)){ok=false;break;}
                bytes=(uint64_t)n*512; if(bytes>left)bytes=left;ok=vc_extent(c,m,(int64_t)(first-1)*512,bytes);left-=bytes; ++consumed;
            } previous=next;next=pm_le32(b);
        } ok=ok && !left && consumed==extents;
    } else if(c->kind==VC_EFS) {
        unsigned total=pm_be16(raw+28),containers=total<=12?0:((unsigned)raw[37]<<16)|((unsigned)raw[38]<<8)|raw[39],seen=0; uint8_t b[512];
        if(size && !total)ok=false;
        if(total<=12)for(i=0;ok && i<total;++i)ok=vc_efs_extent(c,m,raw+32+i*8,size,&logical);
        else { if(!containers || containers>12)ok=false;
            for(i=0;ok && i<containers && seen<total;++i) { const uint8_t *e=raw+32+i*8;uint32_t start=((uint32_t)e[1]<<16)|((uint32_t)e[2]<<8)|e[3];unsigned j,k;
                if(e[0] || !e[4] || !start || start>=c->blocks || e[4]>c->blocks-start){ok=false;break;}
                for(j=0;ok && j<e[4] && seen<total;++j) { if(!vc_block(c,start+j,b)){ok=false;break;}
                    for(k=0;ok && k<64 && seen<total;++k,++seen)ok=vr_poll(c->pd) && vc_efs_extent(c,m,b+k*8,size,&logical);
                }
            } ok=ok && seen==total;
        } if(ok && logical<size)ok=vc_extent(c,m,-1,size-logical);
    }
    if(!ok || m->size!=size) {vc_drop_map(c,m);return NULL;} return m;
}
static bool vc_name(const uint8_t *p,size_t n,char *out) {
    size_t i;if(!n || n>255)return false;
    for(i=0;i<n;++i) { if(!p[i] || p[i]<32 || p[i]=='/' || p[i]=='\\')return false;out[i]=(char)p[i]; }out[n]=0;return true;
}
static bool vc_emit(vr_classic *c,const char *path,mm_map *m,bool directory) {
    size_t n=xx_rt_strlen(path); pm_member *member;vc_member_context *context=NULL;
    if(!n || n>=4096 || !vc_take(c,(uint64_t)n+1+2*sizeof(pm_member))) {vc_drop_map(c,m);return false;}
    if(!vc_take(c,sizeof(*context)) || !(context=(vc_member_context *)xx_mem_alloc(sizeof(*context)))){vc_drop_map(c,m);return false;}context->map=m;context->limit=c->limit;context->retained=0;
    if(m) { if(!mm_emit(c->f,c->s,path,m)){xx_mem_free(context);return false;} }
    else if(!pm_add(c->f,c->s,path,0,0)){xx_mem_free(context);return false;}
    member=&c->s->items[c->s->count-1];
    member->context=context;member->free_context=vc_member_free;member->read_range=m?vc_member_range:NULL;member->read_all=vc_member_unpack;
    member->display_name=(char *)xx_mem_alloc(n+1);if(!member->display_name)return false;
    xx_rt_memcpy(member->display_name,path,n+1);member->directory=directory;
    return true;
}
static bool vc_walk(vr_classic *,uint64_t,const char *);
static bool vc_child(vr_classic *c,uint64_t key,const uint8_t *name,size_t n,const char *parent) {
    char leaf[256],path[4096];size_t len=xx_rt_strlen(parent);
    if(!vc_name(name,n,leaf))return false;if(!xx_rt_strcmp(leaf,".") || !xx_rt_strcmp(leaf,".."))return true;
    if(len+n+(len?1:0)>=sizeof(path))return false;xx_rt_snprintf(path,sizeof(path),"%s%s%s",parent,len?"/":"",leaf);return vc_walk(c,key,path);
}
static int64_t vc_source(mm_map *m,uint64_t logical) { size_t i;for(i=0;i<m->count;++i) {if(logical<m->extents[i].length)return m->extents[i].source<0?-1:m->extents[i].source+(int64_t)logical;logical-=m->extents[i].length;}return -1;}
static bool vc_directory(vr_classic *c,mm_map *m,uint64_t size,const char *path) {
    uint8_t *b=NULL;pm_member member;uint64_t at=0;bool ok=false;
    if(size>32U*1024U*1024U || size>SIZE_MAX || !vc_take(c,size?size:1))return false;
    b=(uint8_t *)xx_mem_alloc(size?(size_t)size:1);if(!b)goto done;xx_mem_zero(&member,sizeof(member));member.context=m;
    if(!mm_read(c->f,&member,0,b,(size_t)size,c->pd))goto done;ok=true;
    while(ok && at<size) {uint64_t key=0;size_t record=0,n=0;const uint8_t *name=NULL;unsigned i;
        if(!vr_poll(c->pd)){ok=false;break;}
        if(c->kind==VC_EXT || c->kind==VC_XIA) {size_t head=c->kind==VC_EXT?8:7;
            if(size-at<head){ok=false;break;}key=pm_le32(b+at);record=pm_le16(b+at+4);n=c->kind==VC_EXT?pm_le16(b+at+6):b[at+6];name=b+at+head;
            if(record<head || (record&3) || record>size-at || record>c->block-(at%c->block) || n>record-head || n>(c->kind==VC_EXT?255U:248U)){ok=false;break;}
        } else if(c->kind==VC_EFS) {
            size_t bytes=(size-at)<512?(size_t)(size-at):512;unsigned slots;
            if(bytes!=512 || pm_be16(b+at)!=0xbeef || (slots=b[at+3])>255 || 4U+slots>bytes){ok=false;break;}
            for(i=0;ok && i<slots;++i) {size_t offset=(size_t)b[at+4+i]*2;if(!offset)continue;
                if(offset<4U+slots || offset>bytes-5){ok=false;break;}key=pm_be32(b+at+offset);n=b[at+offset+4];
                if(n>bytes-offset-5){ok=false;break;}if(key)ok=vc_child(c,key,b+at+offset+5,n,path);
            }at+=512;continue;
        } else if(c->kind==VC_QNX) {
            uint8_t status;if(size-at<64){ok=false;break;}record=64;status=b[at+63];
            if(!b[at] || !(status&9)){at+=record;continue;}
            n=status&8?48:16;for(i=0;i<n && b[at+i];++i){}n=i;name=b+at;
            if(status&8) {uint32_t block=pm_le32(b+at+48);if(!block || block-1>=c->blocks || b[at+52]>=8){ok=false;break;}key=(uint64_t)(block-1)*512+b[at+52]*64;}
            else {int64_t source=vc_source(m,at);if(source<0){ok=false;break;}key=(uint64_t)source;}
        } else {if(size-at<16){ok=false;break;}record=16;key=vc16(c,b+at);name=b+at+2;n=14;for(i=0;i<n && name[i];++i){}n=i;}
        if(key)ok=vc_child(c,key,name,n,path);at+=record;
    }
done:xx_mem_free(b);c->used-=size?size:1;return ok;
}
static bool vc_inode(vr_classic *c,uint64_t key,uint8_t *raw,uint64_t *size,uint16_t *mode) {
    uint64_t at,n=64;
    if(c->kind==VC_QNX) {at=key;if(key&63)return false;}
    else if(c->kind==VC_EFS) {uint64_t index=key/4;if(!c->cginodes || index/c->cginodes>=c->cgcount)return false;at=((uint64_t)c->cgfirst+(index/c->cginodes)*c->cgsize+index%c->cginodes)*512+(key%4)*128;n=128;}
    else if(c->kind==VC_BFS) {if(key<2 || key>c->inodes+1)return false;at=512+(key-2)*64;}
    else {if(!key || key>c->inodes)return false;at=c->table+(key-1)*64;}
    if(at>INT64_MAX || !vr_poll(c->pd) || !pm_read(c->f,(int64_t)at,raw,(size_t)n))return false;
    if(c->kind==VC_QNX) {*size=pm_le32(raw+16);*mode=pm_le16(raw+50);if(!(raw[63]&1))return false;}
    else if(c->kind==VC_BFS) {uint32_t first=pm_le32(raw+4),end=pm_le32(raw+12),type=pm_le32(raw+16);
        if(pm_le16(raw)!=key || (type!=1 && type!=2) || (first && ((uint64_t)end+1<(uint64_t)first*512)))return false;
        *size=first?(uint64_t)end+1-(uint64_t)first*512:0;*mode=(uint16_t)(type==2?0x4000:0x8000);
    } else {*size=c->kind==VC_EXT?pm_le32(raw+4):vc32(c,raw+8);*mode=vc16(c,raw);}
    return true;
}
static bool vc_walk(vr_classic *c,uint64_t key,const char *path) {
    uint8_t raw[128];uint64_t size;uint16_t mode;mm_map *m=NULL;bool ok=false;unsigned i;const xx_var *limit;
    if(c->depth>=64 || c->s->count>=1000000 || !vc_inode(c,key,raw,&size,&mode))return false;
    mode&=0xf000;for(i=0;i<c->depth;++i)if(c->ancestors[i]==key)return false;
    if(mode!=0x4000 && mode!=0x8000 && mode!=0xa000)return mode==0x2000 || mode==0x6000 || mode==0x1000 || mode==0xc000;
    limit=xx_format_resolve_extra_parameter(c->f,((xx_volume *)c->f)->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(size>INT64_MAX || (mode!=0x4000 && limit && size>xx_var_get_u64(limit)))return false;
    m=vc_map(c,raw,size);if(!m)return false;
    if(mode==0x4000) {if(*path && !vc_emit(c,path,NULL,true))goto done;c->ancestors[c->depth++]=key;ok=vc_directory(c,m,size,path);--c->depth;}
    else {ok=vc_emit(c,path,m,false);m=NULL;}
done:vc_drop_map(c,m);return ok;
}
static bool vr_classic_files(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,const uint8_t *super,int64_t super_at) {
    vr_classic c;xx_volume *r=(xx_volume *)f;const xx_var *v;uint8_t sb[1024];uint64_t root=2;uint32_t first;bool ok=false;
    xx_mem_zero(&c,sizeof(c));c.f=f;c.s=s;c.pd=pd;c.limit=UINT64_C(256)*1024*1024;c.block=512;c.blocks=(uint64_t)pm_available(f)/512;c.kind=VC_UNIX;
    v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT);if(v)c.limit=xx_var_get_u64(v);if(!vc_take(&c,UINT64_C(65536)+xx_get_file_buffer_size()))return false;
    xx_rt_memcpy(sb,super,512);
    switch(f->file_type) {
    case XX_FILE_TYPE_LINUX_EXT:
        c.kind=VC_EXT;c.block=1024;c.blocks=pm_le32(sb+4);c.inodes=pm_le32(sb);c.table=2048;c.firstdata=pm_le32(sb+24);root=1;
        if(pm_le32(sb+28) || c.firstdata<2 || c.inodes>(c.firstdata*1024-2048)/64)return false;break;
    case XX_FILE_TYPE_XIA_FS:
        if(!pm_read(f,0,sb,1024))return false;c.kind=VC_XIA;c.block=pm_le32(sb+512);c.blocks=pm_le32(sb+516);c.inodes=pm_le32(sb+520);
        c.firstdata=pm_le32(sb+536);c.table=((uint64_t)1+pm_le32(sb+528)+pm_le32(sb+532))*c.block;root=1;
        if(c.block<1024 || c.block>4096 || pm_le32(sb+540)>2 || c.block!=(UINT64_C(1024)<<pm_le32(sb+540)) || c.table>c.firstdata*c.block || c.inodes>(c.firstdata*c.block-c.table)/64)return false;break;
    case XX_FILE_TYPE_QNX4:c.kind=VC_QNX;c.firstdata=0;root=512;break;
    case XX_FILE_TYPE_SGI_EFS:
        c.kind=VC_EFS;c.order=1;c.blocks=pm_be32(sb);c.cgfirst=pm_be32(sb+4);c.cgsize=pm_be32(sb+8);c.cginodes=pm_be16(sb+12);c.cgcount=pm_be16(sb+18);
        if(!c.cginodes || c.cginodes>=c.cgsize || !c.cgcount || c.cgfirst>=c.blocks || (uint64_t)c.cgcount*c.cgsize>c.blocks-c.cgfirst)return false;break;
    case XX_FILE_TYPE_UNIX_BFS:
        c.kind=VC_BFS;c.firstdata=pm_le32(sb+4)/512;c.blocks=((uint64_t)pm_le32(sb+8)+1)/512;c.inodes=(pm_le32(sb+4)-512)/64;
        if(pm_le32(sb+4)%512 || (pm_le32(sb+8)&511)!=511 || !c.inodes || c.inodes>512)return false;break;
    case XX_FILE_TYPE_COHERENT_FS:c.order=2;c.blocks=vc32(&c,sb+2);c.firstdata=pm_le16(sb);c.table=1024;c.inodes=(c.firstdata-2)*8;break;
    case XX_FILE_TYPE_UNIX_V7:
        if(!vr_classic_v7_identity(r,sb,pm_available(f),&c.order))return false;c.blocks=vc32(&c,sb+2);c.firstdata=vc16(&c,sb);c.table=1024;c.inodes=(c.firstdata-2)*8;break;
    case XX_FILE_TYPE_SYSV_FS:
    case XX_FILE_TYPE_XENIX_FS: {
        bool xenix=f->file_type==XX_FILE_TYPE_XENIX_FS;size_t magic_at=xenix?1016:504,base=0;uint32_t magic=xenix?0x002b5544:0xfd187e20,type;
        if(!pm_read(f,super_at,sb,1024))return false;
        if(!xenix && pm_le32(sb+504)!=magic && pm_be32(sb+504)!=magic && (pm_le32(sb+1016)==magic || pm_be32(sb+1016)==magic))base=512;
        if(pm_le32(sb+base+magic_at)!=magic && pm_be32(sb+base+magic_at)!=magic)return false;
        c.order=pm_be32(sb+base+magic_at)==magic?1:0;type=vc32(&c,sb+base+magic_at+4);if(type<1 || type>3)return false;c.block=256U<<type;
        first=vc16(&c,sb+base);c.firstdata=first;c.blocks=vc32(&c,sb+base+2);
        /* R4 aligns fsize at four bytes; R2/Xenix use packed two bytes. */
        if(!xenix && (!c.blocks || c.blocks>(uint64_t)pm_available(f)/c.block))c.blocks=vc32(&c,sb+base+4);
        if(!xenix) {
            uint32_t packed=vc32(&c,sb+base+2),aligned=vc32(&c,sb+base+4);
            bool pv=packed && packed<=(uint64_t)pm_available(f)/c.block,av=aligned && aligned<=(uint64_t)pm_available(f)/c.block;
            /* The kernel's primary R2/R4 discriminator is the timestamp at
             * R4's aligned offset. Bounds resolve a uniquely viable layout. */
            if(!pv && !av)return false;
            c.blocks=av && (!pv || vc32(&c,sb+base+420)>=UINT32_C(315532800))?aligned:packed;
        }
        c.table=2U*c.block;if(first<3)return false;c.inodes=(uint64_t)(first-2)*(c.block/64);break;
    }
    default:return false;
    }
    if(!c.blocks || c.blocks>(uint64_t)pm_available(f)/c.block || (c.kind==VC_UNIX && (c.firstdata<3 || c.blocks>UINT32_C(0xffffff))) || (c.kind!=VC_EFS && c.kind!=VC_QNX && (!c.inodes || c.firstdata>=c.blocks)))return false;
    ok=vc_walk(&c,root,"");if(!ok || !vr_poll(pd))return false;
    r->block_size=c.block;r->blocks=c.blocks;r->big_endian=c.order==1;
    r->capability="native directory and file extraction; sparse, direct, indirect or extent data decoded in RAM";
    if(!vr_info(f,s,c.order==2?
        "On-disk 32-bit integers use PDP word order. Symlink targets are exported as ordinary file bytes. Special device nodes are omitted. No mounting, writes or journal replay.\n":
        "Symlink targets are exported as ordinary file bytes. Special device nodes are omitted. No mounting, writes or journal replay.\n"))return false;
    {pm_member *info=&s->items[s->count-1];vc_member_context *context=NULL;size_t i;
        if(!vc_take(&c,sizeof(*context)+(uint64_t)info->size+2*sizeof(pm_member)) || !(context=(vc_member_context *)xx_mem_alloc(sizeof(*context))))return false;
        context->map=NULL;context->limit=c.limit;context->retained=0;info->context=context;info->free_context=vc_member_free;info->read_all=vc_member_unpack;
        for(i=0;i<s->count;++i)if(s->items[i].read_all==vc_member_unpack)((vc_member_context *)s->items[i].context)->retained=c.used-xx_get_file_buffer_size();
    }return true;
}
