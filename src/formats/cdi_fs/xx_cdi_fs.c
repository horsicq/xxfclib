/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original Green Book Chapter III parser. BE path table, independent directory
 * attributes, fixed-ratio file interleave, and Mode2 Form1 EDC when available.
 * Form2 files retain complete 2336-byte sector records (subheader + payload +
 * reserved tail), since their conventional 2048-byte sizes lose media data.
 * https://icdia.co.uk/docs/funcspec.html
 */
#include "xxfclib/formats/cdi_fs/xx_cdi_fs.h"
#include "../apple_family/xx_apple_family_private.h"
#include "xxfclib/data/xx_data.h"
typedef struct ci_ctx {af_work *w;uint32_t stride,blocks;uint32_t *seen;uint32_t directories;bool form2;} ci_ctx;
static uint32_t ci_edc(const uint8_t *p,size_t n) {uint32_t c=0;size_t i;unsigned j;for(i=0;i<n;++i){c^=p[i];for(j=0;j<8;++j)c=(c>>1)^((c&1U)?0xd8018001U:0U);}return c;}
static bool ci_sector(ci_ctx *c,uint32_t block,uint8_t out[2352],bool metadata,bool *form2) {
    static const uint8_t sync[12]={0,255,255,255,255,255,255,255,255,255,255,0};
    if(block>=c->blocks || !af_read(c->w,(int64_t)block*c->stride,out,c->stride)) {return false; } *form2=false;
    if(c->stride==2048U)return true;
    if(xx_rt_memcmp(out,sync,12) || out[15]!=2 || xx_rt_memcmp(out+16,out+20,4))return false;
    *form2=(out[18]&0x20U)!=0;
    if(metadata && (*form2 || out[16] || out[17] || out[19] || (out[18]&~0x89U)))return false;
    if(!*form2 && ci_edc(out+16,2056)!=xx_data_get_u32(out+2072, 4, 0, false))return false;
    return af_poll(c->w);
}
static const uint8_t *ci_data(const ci_ctx *c,const uint8_t *p){return p+(c->stride==2352U?24U:0U);}
static bool ci_file(ci_ctx *c,uint32_t start,uint32_t size,uint8_t unit,uint8_t gap,uint8_t number,const char *name) {
    uint64_t sectors=((uint64_t)size+2047U)/2048U,i,last;uint8_t raw[2352],*out=NULL;bool form2=false,any=false,ok=false;uint64_t output=0,at=0;
    if((!unit && gap) || (unit && !gap) || (unit && !number) || sectors>UINT32_MAX)return false;
    last=sectors?sectors-1U:0;if(unit)last=(last/unit)*(unit+(uint64_t)gap)+last%unit;
    if(start>c->blocks || (sectors && last>=c->blocks-start))return false;
    /* First pass determines whether preserving full media sector records is
     * necessary. No unverified or zero-filled missing sectors are emitted. */
    for(i=0;i<sectors;++i){uint64_t index=unit?(i/unit)*(unit+(uint64_t)gap)+i%unit:i;
        if(!ci_sector(c,start+(uint32_t)index,raw,false,&form2) || (c->stride==2352U && raw[16]!=number)) {return false; } any|=form2;}
    if(any && size%2048U) {return false; } output=any?sectors*2336U:size;
    out=af_alloc(c->w,output,true);if(!out)return false;
    for(i=0;i<sectors;++i){uint64_t index=unit?(i/unit)*(unit+(uint64_t)gap)+i%unit:i;size_t z;
        if(!ci_sector(c,start+(uint32_t)index,raw,false,&form2))goto done;
        z=any?2336U:(size_t)(output-at>2048U?2048U:output-at);
        xx_rt_memcpy(out+(size_t)at,any?raw+16:ci_data(c,raw),z);at+=z;}
    if(any){char media[96];if(xx_rt_strlen(name)+12U>=sizeof(media))goto done;xx_rt_snprintf(media,sizeof(media),"%s.cdi-sectors",name);ok=af_add(c->w,media,0,output,out);c->form2=true;}
    else ok=af_add(c->w,name,0,output,out);
done:if(!ok)af_release(c->w,out,output);return ok;
}
static bool ci_walk(ci_ctx *c,uint32_t start,uint32_t bytes,const char *parent,uint32_t parent_start,unsigned depth) {
    uint8_t raw[2352],first[2352];uint32_t blocks=(uint32_t)(((uint64_t)bytes+2047U)/2048U),i,entries=0;bool form2;const uint8_t *p;uint32_t j;
    if(depth>32U || c->directories>=8192U || !bytes || bytes%2048U || start>=c->blocks || blocks>c->blocks-start)return false;
    for(i=0;i<c->directories;++i) {if(c->seen[i]==start)return false; } c->seen[c->directories++]=start;
    if(!ci_sector(c,start,first,true,&form2))return false;
    for(i=0;i<blocks;++i){uint32_t at=0;if(!ci_sector(c,start+i,raw,true,&form2))return false;p=ci_data(c,raw);
        while(at<2048U){const uint8_t *e=p+at;uint32_t n=e[0],names,tail,extent,size;uint16_t attrs;bool directory;char leaf[96],name[96];
            if(!n){if(!af_zero(e,2048U-at))return false;break;}
            if(n<44U || n>2048U-at || !af_poll(c->w)) {return false; } names=e[32];tail=33U+names+((names&1U)?0U:1U);
            if(!names || tail+10U!=n || e[1] || !af_zero(e+2,4) || !af_zero(e+10,4) || e[24] || (e[25]&~1U) || !af_zero(e+28,2) || xx_data_get_u16(e+30, 2, 0, true) ||
               (names%2U==0 && e[33+names]) || !af_zero(e+tail+6,2) || e[tail+9])return false;
            extent=xx_data_get_u32(e+6, 4, 0, true);size=xx_data_get_u32(e+14, 4, 0, true);attrs=xx_data_get_u16(e+tail+4, 2, 0, true);directory=(attrs&0x8000U)!=0;
            if(attrs&~0xc555U)return false;
            if(entries<2U){if(names!=1U || e[33]!=entries || !directory || e[26] || e[27] || e[tail+8] ||
                extent!=(entries?parent_start:start) || (!entries && size!=bytes))return false;}
            else{if(names>28U || !af_leaf(leaf,sizeof(leaf),e+33,names))return false;
                if(parent && *parent){if(xx_rt_strlen(parent)+xx_rt_strlen(leaf)+2U>=sizeof(name))return false;xx_rt_snprintf(name,sizeof(name),"%s/%s",parent,leaf);}else xx_rt_strncpy(name,leaf,sizeof(name));
                if(attrs&0x4000U)return false; /* CD-DA needs the disc TOC, absent from a data-track image. */
                if(directory){if(e[26] || e[27] || e[tail+8] || !af_add(c->w,name,0,0,NULL))return false;c->w->s->items[c->w->s->count-1U].compression_method=65535U;
                    if(!ci_walk(c,extent,size,name,start,depth+1U))return false;}
                else if(!ci_file(c,extent,size,e[26],e[27],e[tail+8],name))return false;
            }
            ++entries;at+=n;
        }
    }
    /* Every path-table directory must be reachable through this tree. */
    j=entries;return j>=2U && af_poll(c->w);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    af_work w;ci_ctx c;uint8_t raw[2352],descriptor[2048];const uint8_t *h;uint32_t path,size,start,at=0,count=0,root=0,rootbytes=0,record;uint8_t *table=NULL;bool form2,ended=false,ok=false;int64_t n;
    if(!af_init(&w,f,s,pd) || (n=pm_available(f))<17*2048) {return false; } xx_mem_zero(&c,sizeof(c));c.w=&w;
    c.stride=2048;if((uint64_t)n/2048U>UINT32_MAX)return false;c.blocks=(uint32_t)((uint64_t)n/2048U);
    if(!ci_sector(&c,16,raw,true,&form2) || raw[0]!=1 || xx_rt_memcmp(raw+1,"CD-I ",5)){
        if(n%2352U) {return false; } c.stride=2352;c.blocks=(uint32_t)((uint64_t)n/2352U);
        if(!ci_sector(&c,16,raw,true,&form2))return false;}
    h=ci_data(&c,raw);xx_rt_memcpy(descriptor,h,2048);h=descriptor;
    if(h[0]!=1 || xx_rt_memcmp(h+1,"CD-I ",5) || h[6]!=1 || h[7] || xx_rt_memcmp(h+8,"CD-RTOS",7) ||
       xx_data_get_u16(h+130, 2, 0, true)!=2048 || !xx_data_get_u32(h+84, 4, 0, true) || xx_data_get_u32(h+84, 4, 0, true)>c.blocks || h[881]!=1)return false;
    c.blocks=xx_data_get_u32(h+84, 4, 0, true);path=xx_data_get_u32(h+148, 4, 0, true);size=xx_data_get_u32(h+136, 4, 0, true);
    if(!size || size>1048576U || path>=c.blocks || ((uint64_t)size+2047)/2048>c.blocks-path)return false;
    for(record=17;record<80 && record<c.blocks;++record){if(!ci_sector(&c,record,raw,true,&form2))return false;h=ci_data(&c,raw);
        if(xx_rt_memcmp(h+1,"CD-I ",5) || h[6]!=1) {return false; } if(h[0]==255){ended=true;break;}if(h[0]!=1 && h[0]!=2)return false;}
    if(!ended) {return false; } table=af_alloc(&w,size,false);c.seen=(uint32_t *)af_alloc(&w,8192U*sizeof(uint32_t),false);if(!table || !c.seen)goto done;
    for(record=0;record<(size+2047U)/2048U;++record){uint32_t z=size-record*2048U;if(z>2048U)z=2048U;
        if(!ci_sector(&c,path+record,raw,true,&form2)) {goto done; } xx_rt_memcpy(table+record*2048U,ci_data(&c,raw),z);}
    while(at<size){uint32_t z,names,parent;
        if(size-at<8U || !(names=table[at]) || table[at+1]) {goto done; } z=8U+names+(names&1U);if(z>size-at || (names&1U && table[at+8+names]))goto done;
        start=xx_data_get_u32(table+at+2, 4, 0, true);parent=xx_data_get_u16(table+at+6, 2, 0, true);if(start>=c.blocks || ++count>8192U || !parent || parent>count)goto done;
        if(count==1U){if(names!=1U || table[at+8] || parent!=1U)goto done;root=start;}
        else if(parent==count || names>28U)goto done;
        at+=z;}
    if(!ci_sector(&c,root,raw,true,&form2)) {goto done; } h=ci_data(&c,raw);if(h[0]<44 || h[32]!=1 || h[33] || xx_data_get_u32(h+6, 4, 0, true)!=root)goto done;rootbytes=xx_data_get_u32(h+14, 4, 0, true);
    if(!ci_walk(&c,root,rootbytes,"",root,0) || c.directories!=count)goto done;
    /* Cross-check path-table extent identities against every reached directory. */
    at=0;while(at<size){uint32_t names=table[at],i;start=xx_data_get_u32(table+at+2, 4, 0, true);for(i=0;i<c.directories;++i)if(c.seen[i]==start)break;
        if(i==c.directories) {goto done; } c.seen[i]=UINT32_MAX; /* Every reached directory matches exactly one path entry. */
        at+=8U+names+(names&1U);}
    ok=af_poll(&w);if(ok){xx_cdi_fs *r=(xx_cdi_fs *)f;s->size=(int64_t)c.blocks*c.stride;r->number_of_records=s->count;
        r->note=c.form2?"CD-i files and complete Form2 media sector records (.cdi-sectors); Form1 EDC checked; no Form2 EDC is defined":c.stride==2352U?"CD-i native file extents/interleave and Mode2 Form1 EDC verified":"CD-i cooked Form1 files; directory/path-table extents verified; cooked images carry no sector EDC";}
done:af_release(&w,table,size);af_release(&w,c.seen,8192U*sizeof(uint32_t));return ok;
}
AF_DEFINE_READER(cdi_fs,XX_FILE_TYPE_CDI_FS,"iso")
