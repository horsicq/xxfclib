/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Photodex named-resource package.
 */
#include "xxfclib/formats/px/xx_px.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"

/* Photodex named-resource packages (PXT/PXS): preserve every named value,
 * including icons, thumbnails, style definitions and resource envelopes.
 * Resource envelopes are exported intact, without pretending their internal
 * bitmap/video encodings are directly usable image files. */
static bool px_px(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[62];uint64_t n=(uint64_t)pm_available(f),p;uint32_t descriptors,outer=0;
    if(n<62 || !pm_read(f,0,h,62) || memcmp(h,"Photodex Presenter Stream\x1a\n\r\n\x1a",30) || xgr_le16(h+34)<1 || xgr_le16(h+34)>5) return false;
    descriptors=xgr_le32(h+58);p=62ULL+44ULL*descriptors;
    if(descriptors>XGR_MAX_RECORDS || p>n) return false;
    while(p<n) {
        uint8_t bh[12];uint32_t count,i;uint64_t end;
        if(xgr_stop(pd) || ++outer>XGR_MAX_RECORDS || !xgr_range(p,12,n) || !pm_read(f,(int64_t)p,bh,12)) return false;
        p+=12;end=p+xgr_le32(bh+4);
        if(end>n || xgr_le16(bh)!=0 || xgr_le16(bh+2)!=1 || !xgr_range(p,4,end) || !pm_read(f,(int64_t)p,bh,4)) return false;
        count=xgr_le32(bh);p+=4;
        if(!count || count>XGR_MAX_RECORDS-s->count) return false;
        for(i=0;i<count;++i) {
            uint8_t rh[37],magic[2];char category[33],name[80];size_t len;uint32_t size;const char *ext="bin";
            if(xgr_stop(pd) || !xgr_range(p,37,end) || !pm_read(f,(int64_t)p,rh,37)) return false;p+=37;
            for(len=0;len<32 && rh[len];++len) category[len]=(char)rh[len];category[len]=0;
            if(!len || len==32 || !xgr_name(category) || strchr(category,'/') || rh[36]!=1) return false;
            size=xgr_le32(rh+32);
            if(!xgr_range(p,size,end)) return false;
            if(size>=2 && !pm_read(f,(int64_t)p,magic,2)) return false;
            if((!strcmp(category,"thumbnail") || !strcmp(category,"icon")) && size>=2 && magic[0]==0xff && magic[1]==0xd8) ext="jpg";
            xx_rt_snprintf(name,sizeof(name),"%s/%06u.%s",category,(unsigned)s->count+1,ext);
            if(!xgr_add(f,s,name,p,size)) return false;p+=size;
        }
        if(p!=end) return false;
    }
    s->size=(int64_t)n;return s->count!=0;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    return pm_available(f)>=0 && !xgr_stop(pd) && px_px(f,s,pd);
}
Abstractformat *xx_px_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(!f) return NULL;
    xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_PX,"pak");return f;
}
void xx_px_free(Abstractformat *f) {
    if(f) { xx_format_cleanup_extra_parameters(f);xx_mem_free(f); }
}
xx_file_type_t xx_px_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[64];int64_t size,old=xx_io_tell(d);
    xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;size=pm_available(&f);
    if(size<8 || !pm_read(&f,0,h,(size_t)(size<64?size:64))) goto done;
    if(size>=62 && !memcmp(h,"Photodex Presenter Stream\x1a\n\r\n\x1a",30)) type=XX_FILE_TYPE_PX;
done:
    if(old>=0) xx_io_seek64(d,old,SEEK_SET);
    return type;
}
static Abstractformat *px_open(xx_io_device *d) { return xx_px_create(d,0); }
static const xx_file_type_t px_types[]={XX_FILE_TYPE_PX};
static const xx_format_search_desc px_desc={
    px_types,1,NULL,0,px_open,xx_px_free,true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(px,px_desc)
