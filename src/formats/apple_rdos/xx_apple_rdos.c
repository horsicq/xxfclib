/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * SSI RDOS32 (13-sector), RDOS33 (16-sector ProDOS order), and RDOS3
 * (13 logical sectors on physical 16-sector tracks), from original layout
 * notes. Canonical catalog identity, sorted disjoint extents and lengths.
 * profile 1/2/3 explicitly selects RDOS32/33/3; 0 infers canonical headers.
 */
#include "xxfclib/formats/apple_rdos/xx_apple_rdos.h"
#include "../apple_family/xx_apple_family_private.h"
#include "xxfclib/data/xx_data.h"
static bool rd_label(char *out,size_t cap,const uint8_t *p,bool leaf) {
    static const char hex[]="0123456789ABCDEF";size_t n=24,i,at=0;
    while(n && (p[n-1U]&0x7fU)==' ') {--n; } if(!n)return false;
    for(i=0;i<n;++i){uint8_t c=p[i]&0x7fU;if(c<32U || c>126U)return false;
        if(leaf && (c=='~' || c=='/' || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*' || c==' ' || c=='.')){
            if(at+3U>=cap) {return false; } out[at++]='~';out[at++]=hex[c>>4];out[at++]=hex[c&15];
        }else{if(at+1U>=cap)return false;out[at++]=(char)c;}}
    out[at]=0;return !leaf || af_safe(out);
}
static bool rd_copy(af_work *w,const af_blob *b,uint32_t variant,uint32_t first,uint32_t count,uint8_t *out) {
    uint32_t i,logical=variant==2U?16U:13U,physical=variant==1U?13U:16U;
    for(i=0;i<count;++i) { uint32_t index=first+i,at=(index/logical*physical+index%logical)*256U;
        if(!af_range(b,at,256U) || !af_poll(w)) {return false; } xx_rt_memcpy(out+i*256U,b->p+at,256U); }return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    af_work w;af_blob b;xx_apple_rdos *r=(xx_apple_rdos *)f;uint32_t variant,logical,catalog,i,last=0U,entries;char header[32];bool ok=false;
    if(!af_init(&w,f,s,pd) || !af_load(&w,&b))return false;
    if(r->profile>3U || (b.n!=35U*13U*256U && b.n!=35U*16U*256U))goto done;
    catalog=(b.n==35U*13U*256U?13U:16U)*256U;
    if(!af_range(&b,catalog,32U) || !rd_label(header,sizeof(header),b.p+catalog,false))goto done;
    if(!xx_rt_strcmp(header,"RDOS 2.1 COPYRIGHT 1981"))variant=b.n==35U*13U*256U?1U:3U;
    else if(!xx_rt_strcmp(header,"RDOS 3.3 COPYRIGHT 1986") || !xx_rt_strcmp(header,"SSI SAVE GAME DISK RDOS") || !xx_rt_strcmp(header,">-SSI GAME SAVE DISK-<"))variant=2U;
    else goto done;
    if(r->profile)variant=r->profile;
    if((variant==1U)!=(b.n==35U*13U*256U))goto done;
    logical=variant==2U?16U:13U;entries=(variant==2U?16U:11U)*8U;
    for(i=0;i<entries;++i) {
        const uint8_t *e;char name[80];uint32_t count,size,first;uint8_t type;uint8_t *out;
        if(!af_range(&b,catalog+i*32U,32U)) {goto done; } e=b.p+catalog+i*32U;
        if(e[0]==0x80U || !e[0])continue;
        type=e[24]&0x7fU;if(type==' ')continue;
        if(!rd_label(name,sizeof(name),e,true) || (type!='A' && type!='B' && type!='T' && type!='S'))goto done;
        count=e[25];size=xx_data_get_u16(e+28, 2, 0, false);first=xx_data_get_u16(e+30, 2, 0, false);
        if(!count || size>count*256U || first>=35U*logical || count>35U*logical-first || (i && first<last))goto done;
        last=first+count;out=af_alloc(&w,count*256U,true);if(!out)goto done;
        if(!rd_copy(&w,&b,variant,first,count,out)) {af_release(&w,out,count*256U);goto done;}
        if(type=='T') {uint32_t z;for(z=0;z<size;++z)if(!out[z]) {size=z;break;} }
        if(!af_add(&w,name,0,size,out)) {af_release(&w,out,count*256U);goto done;}
    }
    if(!s->count) {goto done; } s->size=b.n;r->number_of_records=s->count;r->detected_profile=variant;
    r->note=variant==1U?"SSI RDOS32 files; 13-sector physical layout":variant==2U?"SSI RDOS33 files; 16-sector ProDOS order":"SSI RDOS3 files; 13-sector layout in 16-sector image";ok=af_poll(&w);
done:af_release(&w,b.p,b.n);return ok;
}
AF_DEFINE_READER(apple_rdos,XX_FILE_TYPE_APPLE_RDOS,"dsk")
