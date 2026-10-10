/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * PAM PAK hierarchical resource archive.
 */
#include "xxfclib/formats/pam_pak/xx_pam_pak.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"

static int pam_pak_pam_find(const uint8_t *rows,uint32_t count,uint32_t id) {
    uint32_t left=0,right=count;
    while(left<right) { uint32_t middle=left+(right-left)/2,key=xgr_le32(rows+(size_t)middle*24);if(key<id) left=middle+1;else if(key>id) right=middle;else return (int)middle; }
    return -1;
}
static bool pam_pak_pam(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[36],*rows=NULL;uint64_t n=(uint64_t)pm_available(f),names,end;uint32_t count,i;bool ok=false;
    if(n<36 || !pm_read(f,0,h,36) || memcmp(h,"PAM_PAK\0",8)) return false;
    count=xgr_le16(h+8);names=36ULL+32ULL*count;end=names+xgr_le32(h+12);
    if(!count || end>n || names>=end) return false;
    rows=(uint8_t *)xx_mem_alloc((size_t)count*24);
    if(!rows || !pm_read(f,36+(int64_t)count*8,rows,(size_t)count*24)) goto done;
    for(i=1;i<count;++i) if(xgr_le32(rows+(size_t)(i-1)*24)>=xgr_le32(rows+(size_t)i*24)) goto done;
    for(i=0;i<count;++i) {
        uint32_t chain[256],depth=0,j=i,number;uint8_t item[8];uint64_t at,size;char name[XGR_MAX_NAME+1];size_t used=0;
        if(xgr_stop(pd)) goto done;
        for(;;) {
            const uint8_t *r=rows+(size_t)j*24;uint32_t parent=xgr_le32(r+16),k;
            if(depth==256) goto done;
            for(k=0;k<depth;++k) if(chain[k]==j) goto done;
            chain[depth++]=j;
            if(!parent) break;
            { int found=pam_pak_pam_find(rows,count,parent);if(found<0) goto done;j=(uint32_t)found; }
        }
        while(depth) {
            char part[XGR_MAX_NAME+1];const uint8_t *r=rows+(size_t)chain[--depth]*24;uint64_t pos=names+xgr_le32(r+8);size_t len;
            if(pos<names || pos>=end || !xgr_zstr(f,&pos,end,part) || !xgr_name(part) || strchr(part,'/')) goto done;
            len=xx_rt_strlen(part);if(used+len+(used?1:0)>XGR_MAX_NAME) goto done;
            if(used) name[used++]='/';memcpy(name+used,part,len);used+=len;name[used]=0;
        }
        number=xgr_le16(rows+(size_t)i*24+4);
        if(number>=count || !pm_read(f,36+(int64_t)number*8,item,8)) goto done;
        size=xgr_le32(item);at=xgr_le32(item+4);
        if(at==0xffffffffU) {
            if(size!=0xc0000000U || !xgr_add(f,s,name,end,0)) goto done;
            s->items[s->count-1].directory=true;
        } else if(at<end || !xgr_range(at,size,n) || !xgr_add(f,s,name,at,size)) goto done;
    }
    s->size=(int64_t)n;ok=true;
done:xx_mem_free(rows);return ok;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    return pm_available(f)>=0 && !xgr_stop(pd) && pam_pak_pam(f,s,pd);
}
Abstractformat *xx_pam_pak_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(!f) return NULL;
    xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_PAM_PAK,"pak");return f;
}
void xx_pam_pak_free(Abstractformat *f) {
    if(f) { xx_format_cleanup_extra_parameters(f);xx_mem_free(f); }
}
xx_file_type_t xx_pam_pak_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[64];int64_t size,old=xx_io_tell(d);
    xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;size=pm_available(&f);
    if(size<8 || !pm_read(&f,0,h,(size_t)(size<64?size:64))) goto done;
    { uint32_t magic=xgr_le32(h);
    if(magic==0x5f4d4150U && size>=36 && !memcmp(h,"PAM_PAK\0",8)) type=XX_FILE_TYPE_PAM_PAK;
    }
done:
    if(old>=0) xx_io_seek64(d,old,SEEK_SET);
    return type;
}
static Abstractformat *pam_pak_open(xx_io_device *d) { return xx_pam_pak_create(d,0); }
static const xx_file_type_t pam_pak_types[]={XX_FILE_TYPE_PAM_PAK};
static const xx_format_search_desc pam_pak_desc={
    pam_pak_types,1,NULL,0,pam_pak_open,xx_pam_pak_free,true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(pam_pak,pam_pak_desc)
