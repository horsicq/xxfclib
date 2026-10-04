/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native tagged DC42 microfloppy LisaFS reader. Format structures:
 * https://github.com/eschaton/lisafs (LisaOS page labels and catalog records)
 * https://github.com/aaru-dps/Aaru/tree/devel/Aaru.Filesystems/LisaFS
 * No implementation from either reference is incorporated. DC42 checksums,
 * catalog links, S-file maps, file IDs and page ordering are verified.
 */
#include "xxfclib/formats/lisa_fs/xx_lisa_fs.h"
#include "../apple_family/xx_apple_family_private.h"
typedef struct ls_entry {uint16_t parent,id;uint8_t type;uint32_t size;char leaf[96];} ls_entry;
typedef struct ls_ctx {af_work *w;af_blob *b;uint32_t data,tags,blocks,base,version,slist,packing,maxfiles,sblocks;
    uint16_t *claims;uint8_t *nodes;ls_entry *entries;uint32_t count,capacity;} ls_ctx;
static uint32_t ls_sum(af_work *w,const uint8_t *p,uint32_t n){uint32_t c=0,i;for(i=0;i<n;i+=2){if(!(i&4095U) && !af_poll(w))return 0;c+=pm_be16(p+i);c=(c>>1)|(c<<31);}return c;}
static const uint8_t *ls_data(ls_ctx *c,uint32_t page){return page<c->blocks-c->base?c->b->p+84U+(c->base+page)*512U:NULL;}
static const uint8_t *ls_tag(ls_ctx *c,uint32_t page){return page<c->blocks-c->base?c->b->p+84U+c->data+(c->base+page)*12U:NULL;}
static uint32_t ls_link(const uint8_t *t,bool forward){uint32_t n=pm_be16(t+(forward?8U:10U))&0x7ffU;return n==0x7ffU?UINT32_MAX:n;}
static const uint8_t *ls_srecord(ls_ctx *c,uint32_t id) {
    uint32_t page=c->slist+id/c->packing;const uint8_t *p=ls_data(c,page),*tag=ls_tag(c,page);
    if(id>=c->maxfiles || id/c->packing>=c->sblocks || !p || !tag || pm_be16(tag+4)!=3U)return NULL;
    return p+(id%c->packing)*14U;
}
static bool ls_file(ls_ctx *c,uint32_t id,uint32_t expected,uint8_t **result) {
    const uint8_t *sr=ls_srecord(c,id),*leader,*tag;uint32_t page,size,offset=0x88U,max=41U,i,j,logical=0,copied=0,blocks=0;uint8_t map[1024],*out=NULL;
    *result=NULL;if(!sr || id<4U || !af_poll(c->w))return false;page=pm_be32(sr);size=pm_be32(sr+8);
    if(size!=expected || page==UINT32_MAX || !(leader=ls_data(c,page)) || !(tag=ls_tag(c,page)) ||
       (int16_t)pm_be16(tag+4)!=-(int16_t)id || pm_be16(tag+6))return false;
    xx_rt_memcpy(map,leader,512);c->claims[page]=65535U;
    if(c->version==14U){uint32_t next=ls_link(tag,true);const uint8_t *more=ls_data(c,next),*mt=ls_tag(c,next);
        if(!more || !mt || (int16_t)pm_be16(mt+4)!=-(int16_t)id || ls_link(mt,false)!=page)return false;
        xx_rt_memcpy(map+512,more,512);offset=0x208U;max=84U;c->claims[next]=65535U;}
    /* Small maps hold 41 six-byte extents up to the user label. V1 maps occupy
     * their second leader page. A zero count terminates, without zeroing junk. */
    for(i=0;i<max;++i){const uint8_t *e=map+offset+i*6U;uint32_t first=pm_be32(e),count=pm_be16(e+4);
        if(!count)break;if(first>=c->blocks-c->base || count>c->blocks-c->base-first || count>UINT32_MAX-blocks)return false;
        blocks+=count;
        for(j=0;j<i;++j){const uint8_t *v=map+offset+j*6U;uint32_t va=pm_be32(v),vn=pm_be16(v+4);if(first<va+vn && va<first+count)return false;}}
    if((uint64_t)blocks*512U<size || (size && !blocks))return false;
    out=af_alloc(c->w,size,true);if(!out)return false;
    for(i=0;i<max;++i){const uint8_t *e=map+offset+i*6U;uint32_t first=pm_be32(e),count=pm_be16(e+4);if(!count)break;
        for(j=0;j<count;++j){const uint8_t *p=ls_data(c,first+j),*t=ls_tag(c,first+j);uint32_t n=size-copied;if(n>512U)n=512U;
            if(!af_poll(c->w) || !p || !t || pm_be16(t+4)!=id || pm_be16(t+6)!=logical++ || (c->claims[first+j] && c->claims[first+j]!=id)){af_release(c->w,out,size);return false;}
            c->claims[first+j]=(uint16_t)id;if(n)xx_rt_memcpy(out+copied,p,n);copied+=n;}}
    if(copied!=size || !af_poll(c->w)){af_release(c->w,out,size);return false;}
    *result=out;return true;
}
static bool ls_entry_add(ls_ctx *c,uint16_t parent,uint16_t id,uint8_t type,uint32_t size,const uint8_t *name,size_t n) {
    ls_entry *e;uint32_t i;size_t next,extra;
    if(c->count>=8192U || !id || (type!=1U && type!=3U && type!=2U))return false;
    for(i=0;i<c->count;++i)if(type==1U && c->entries[i].type==1U && c->entries[i].id==id)return false;
    if(c->count==c->capacity){next=c->capacity?c->capacity*2U:32U;extra=next*sizeof(ls_entry);
        if(c->w->used>c->w->limit || extra>c->w->limit-c->w->used)return false;
        e=(ls_entry *)xx_mem_realloc(c->entries,extra);if(!e)return false;c->w->used+=(next-c->capacity)*sizeof(ls_entry);c->entries=e;c->capacity=(uint32_t)next;}
    e=&c->entries[c->count];xx_mem_zero(e,sizeof(*e));e->parent=parent;e->id=id;e->type=type;e->size=size;
    if(!af_leaf(e->leaf,sizeof(e->leaf),name,n))return false;++c->count;return true;
}
static bool ls_node(ls_ctx *c,uint32_t start,unsigned depth,uint32_t *next_node) {
    uint8_t *node=NULL;uint32_t page=start,i,n,kind,child,prior=UINT32_MAX;bool ok=false;
    if(depth>16U || start>=c->blocks-c->base || c->nodes[start] || !af_poll(c->w))return false;c->nodes[start]=1;
    node=af_alloc(c->w,2048,false);if(!node)return false;
    for(i=0;i<4U;++i){const uint8_t *p=ls_data(c,page),*t=ls_tag(c,page);
        if(!p || !t || pm_be16(t+4)!=4U || c->claims[page] || (i && ls_link(t,false)!=prior))goto done;
        c->claims[page]=65535U;xx_rt_memcpy(node+i*512,p,512);prior=page;page=ls_link(t,true);}
    n=pm_be16(node+2036);kind=node[2046];*next_node=pm_be32(node+2042);
    if(n>128U || kind>1U || 2036U-n*2U<(kind?4U:0U))goto done;
    child=kind?pm_be32(node):UINT32_MAX;
    for(i=0;i<n;++i){uint32_t at=pm_be16(node+2034U-i*2U)+(kind?4U:0U),type,len,j;const uint8_t *e;size_t name_size;
        if(at>2036U-n*2U || 38U>2036U-n*2U-at)goto done;e=node+at;type=e[36];
        len=type==1U?48U:(type==2U || type==3U || type==4U || type==5U)?64U:type==8U?78U:38U;
        /* etype_pad is alignment storage, not a zero-valued format field.
         * Original LisaOS catalogs retain nonzero bytes here. */
        if(e[0]!=36U || len>2036U-n*2U-at)goto done;
        /* Distinct offset records may not alias or overlap one another. */
        for(j=0;j<i;++j){uint32_t old=pm_be16(node+2034U-j*2U)+(kind?4U:0U),ot=node[old+36],ol=ot==1?48:(ot>=2 && ot<=5)?64:ot==8?78:38;
            if(at<old+ol && old<at+len)goto done;}
        if(type==1U || type==2U || type==3U){name_size=0;while(name_size<33U && e[3+name_size])++name_size;
            if(!name_size || name_size>32U || !ls_entry_add(c,pm_be16(e+1),pm_be16(e+38),(uint8_t)type,type==1?0U:pm_be32(e+48),e+3,name_size))goto done;}
        else if(type==4U || type==5U || type>8U)goto done; /* Unsupported live pipe/event objects are not files. */
        if(kind){uint32_t next;if(child==UINT32_MAX || !ls_node(c,child,depth+1U,&next))goto done;child=next;}
    }
    ok=af_poll(c->w);
done:af_release(c->w,node,2048);return ok;
}
static bool ls_path(ls_ctx *c,const ls_entry *e,char out[96]) {
    const ls_entry *parts[33];uint32_t count=0,i,parent=e->parent;size_t at=0;parts[count++]=e;
    while(parent){const ls_entry *found=NULL;if(count>=33U)return false;
        for(i=0;i<c->count;++i)if(c->entries[i].type==1U && c->entries[i].id==parent){found=&c->entries[i];break;}
        if(!found)return false;parts[count++]=found;parent=found->parent;}
    for(i=count;i>0;--i){size_t n=xx_rt_strlen(parts[i-1U]->leaf);if(at+n+(at?1U:0U)>=96U)return false;if(at)out[at++]='/';xx_rt_memcpy(out+at,parts[i-1U]->leaf,n);at+=n;}out[at]=0;return af_safe(out);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    af_work w;af_blob b;ls_ctx c;const uint8_t *h,*m;uint32_t i,next,root,size;uint8_t *catalog=NULL;bool ok=false;
    if(!af_init(&w,f,s,pd) || !af_load(&w,&b))return false;xx_mem_zero(&c,sizeof(c));c.w=&w;c.b=&b;
    if(b.n<84U+512U || b.p[0]>63U || pm_be16(b.p+82)!=0x100U)goto done;c.data=pm_be32(b.p+64);c.tags=pm_be32(b.p+68);c.blocks=c.data/512U;
    if(!c.data || c.data%512U || c.tags!=c.blocks*12U || (uint64_t)84U+c.data+c.tags!=b.n ||
       ls_sum(&w,b.p+84,c.data)!=pm_be32(b.p+72) || ls_sum(&w,b.p+84+c.data+12,c.tags-12)!=pm_be32(b.p+76))goto done;
    h=b.p+84;if(pm_be16(h+4)!=0xaaaaU || pm_be16(h+6)!=0x850U || (c.base=pm_be16(h+14))>=c.blocks)goto done;
    m=ls_data(&c,0);if(!m || (c.version=pm_be16(m))!=14U && c.version!=15U && c.version!=17U || m[12]>32U ||
       pm_be16(m+126)!=512U || pm_be32(m+108)!=c.base || pm_be32(m+120)>c.blocks || pm_be32(m+120)<=c.base ||
       !(c.packing=pm_be16(m+152)) || c.packing>36U || !(c.sblocks=pm_be16(m+154)) ||
       !(c.maxfiles=pm_be16(m+160)) || c.maxfiles>32766U || c.maxfiles>c.packing*c.sblocks)goto done;
    c.slist=pm_be32(m+148);if(c.slist>=c.blocks-c.base || c.sblocks>c.blocks-c.base-c.slist)goto done;
    c.claims=(uint16_t *)af_alloc(&w,(uint64_t)c.blocks*sizeof(uint16_t),false);c.nodes=af_alloc(&w,c.blocks,false);if(!c.claims || !c.nodes)goto done;
    if(c.version==17U){root=pm_be32(m+302);if(!ls_node(&c,root,0,&next) || (next && next!=UINT32_MAX))goto done;}
    else {const uint8_t *sr=ls_srecord(&c,4);if(!sr)goto done;size=pm_be32(sr+8);
        if(!size || size%54U || size/54U>c.maxfiles || !ls_file(&c,4,size,&catalog))goto done;
        for(i=0;i<size;i+=54U){const uint8_t *e=catalog+i;uint8_t type=e[34];if(!e[0] || type==0 || type==6 || type==7)continue;
            if(e[0]>32U || (type!=2U && type!=3U) || !ls_srecord(&c,pm_be16(e+36)))goto done;
            if(!ls_entry_add(&c,0,pm_be16(e+36),type,pm_be32(ls_srecord(&c,pm_be16(e+36))+8),e+1,e[0]))goto done;}
        af_release(&w,catalog,size);catalog=NULL;}
    for(i=0;i<c.count;++i){ls_entry *e=&c.entries[i];char name[96];uint8_t *out=NULL;
        if(!ls_path(&c,e,name))goto done;if(e->type==1U){if(!af_add(&w,name,0,0,NULL))goto done;s->items[s->count-1U].compression_method=65535U;}
        else {if(!ls_file(&c,e->id,e->size,&out))goto done;if(!af_add(&w,name,0,e->size,out)){af_release(&w,out,e->size);goto done;}}}
    ok=af_poll(&w);if(ok){xx_lisa_fs *r=(xx_lisa_fs *)f;s->size=b.n;r->number_of_records=s->count;
        r->note="LisaOS1/2 flat and LisaOS3 B-tree catalogs from tagged DC42 microfloppies; original file bytes; DC42 checksums and page/extent identity verified";}
done:if(catalog)af_release(&w,catalog,size);af_release(&w,c.entries,(uint64_t)c.capacity*sizeof(ls_entry));af_release(&w,c.claims,(uint64_t)c.blocks*sizeof(uint16_t));af_release(&w,c.nodes,c.blocks);af_release(&w,b.p,b.n);return ok;
}
AF_DEFINE_READER(lisa_fs,XX_FILE_TYPE_LISA_FS,"dc42")
