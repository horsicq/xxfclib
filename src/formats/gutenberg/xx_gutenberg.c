/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native Gutenberg linked-sector filesystem from published layout facts.
 * Logical document bytes stop at zero; programs retain 250-byte sector data.
 * Backlink at the first sector is intentionally not required: the producer
 * leaves it stale. All other links, roots, loops/crosslinks and extents checked.
 */
#include "xxfclib/formats/gutenberg/xx_gutenberg.h"
#include "../apple_family/xx_apple_family_private.h"
#define GB_SECTORS 560U
static const uint8_t gb_po[16]={0,14,13,12,11,10,9,8,7,6,5,4,3,2,1,15};
static uint32_t gb_index(uint8_t t,uint8_t s) {t&=0x7fU;s&=0x7fU;if(t==0x40U)t=0U;if(s==0x40U)s=0U;return t<35U && s<16U?(uint32_t)t*16U+s:GB_SECTORS;}
static const uint8_t *gb_sector(const af_blob *b,uint32_t index,bool po) {
    uint32_t at=(index/16U)*4096U+(po?gb_po[index%16U]:index%16U)*256U;
    return index<GB_SECTORS && af_range(b,at,256U)?b->p+at:NULL;
}
static bool gb_chain(af_work *w,const af_blob *b,uint32_t first,bool po,uint8_t *claims,bool directory,uint16_t *list,uint32_t *count) {
    uint8_t seen[GB_SECTORS]={0};uint32_t current=first,previous=0U;*count=0U;
    for(;;) {
        const uint8_t *p;uint32_t next;
        if(current>=GB_SECTORS || seen[current] || *count>=GB_SECTORS || !(p=gb_sector(b,current,po)) || !af_poll(w))return false;
        if(gb_index(p[2],p[3])!=current || ((p[3]&0x80U)!=0U)!=(*count==0U) ||
           (*count && gb_index(p[0],p[1])!=previous))return false;
        if((directory && claims[current]>1U) || (!directory && claims[current]))return false;
        claims[current]=directory?1U:2U;seen[current]=1U;list[(*count)++]=(uint16_t)current;
        next=gb_index(p[4],p[5]);
        if(p[4]&0x80U)return next==first;
        previous=current;current=next;
    }
}
static bool gb_parse_order(af_work *w,const af_blob *b,bool po) {
    uint8_t claims[GB_SECTORS]={0};uint16_t directories[GB_SECTORS],sectors[GB_SECTORS];uint32_t nd,i,j;
    const uint8_t *root=gb_sector(b,17U*16U+7U,po);char volume[32];uint8_t vn[9];size_t length=9;
    if(b->n!=35U*4096U || !root)return false;
    for(i=0;i<9U;++i)vn[i]=root[6U+i]&0x7fU;while(length && vn[length-1U]==' ')--length;
    /* Volume names are descriptive, and the original producer allows slash.
     * They never become host paths; quote them as a single safe leaf. */
    if(root[15]!=0x8dU || !af_leaf(volume,sizeof(volume),vn,length) ||
       xx_rt_memcmp(root+16,"\xc4\xc9\xd2",3) || !gb_chain(w,b,17U*16U+7U,po,claims,true,directories,&nd))return false;
    for(i=0;i<nd;++i) {
        const uint8_t *d=gb_sector(b,directories[i],po);
        if(xx_rt_memcmp(d+6,root+6,9) || d[15]!=0x8dU)return false;
        for(j=0;j<15U;++j) {
            const uint8_t *e=d+16U+j*16U;char name[32];uint8_t type=e[14]&0x7fU;uint32_t first,ns,k,size;bool isdir;uint8_t *out;
            if(e[0]==0xa0U || !e[0])continue;
            if(e[15]!=0x8dU || !af_name(name,sizeof(name),e,12,true) ||
               (type!=' ' && type!='L' && type!='M' && type!='P'))return false;
            first=gb_index(e[12],e[13]);isdir=!xx_rt_strcmp(name,"DIR") && first==17U*16U+7U;
            if(!gb_chain(w,b,first,po,claims,isdir,sectors,&ns))return false;
            size=ns*250U;out=af_alloc(w,size,true);if(!out)return false;
            for(k=0;k<ns;++k)xx_rt_memcpy(out+k*250U,gb_sector(b,sectors[k],po)+6,250U);
            if(!isdir && (type==' ' || type=='L'))for(k=0;k<size;++k)if(!out[k]) { size=k;break; }
            if(!af_add(w,name,0,size,out)) {af_release(w,out,ns*250U);return false;}
        }
    }
    return w->s->count!=0U && af_poll(w);
}
static void gb_rollback(af_work *w) {
    size_t i;for(i=0;i<w->s->count;++i) {if(w->s->items[i].memory)xx_mem_free(w->s->items[i].memory);pm_free_password(w->s->items[i].password);}
    if(w->s->items)xx_mem_free(w->s->items);xx_mem_zero(w->s,sizeof(*w->s));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    af_work w;af_blob b;bool ok;xx_gutenberg *r=(xx_gutenberg *)f;
    if(!af_init(&w,f,s,pd) || !af_load(&w,&b))return false;
    if(r->profile>2U) {af_release(&w,b.p,b.n);return false;}
    ok=gb_parse_order(&w,&b,r->profile==2U);
    if(!ok && !r->profile && af_poll(&w)) {gb_rollback(&w);w.used=b.n;ok=gb_parse_order(&w,&b,true);r->detected_profile=2U;}
    else r->detected_profile=r->profile?r->profile:1U;
    if(ok) {s->size=b.n;r->number_of_records=s->count;r->note="Gutenberg files; validated sector chains; document terminator interpreted";}
    af_release(&w,b.p,b.n);return ok && af_poll(&w);
}
AF_DEFINE_READER(gutenberg,XX_FILE_TYPE_GUTENBERG,"dsk")
