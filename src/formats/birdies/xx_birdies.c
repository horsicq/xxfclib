/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Birdies NUL-terminated resource table.
 */
#include "xxfclib/formats/birdies/xx_birdies.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"

static bool birdies_birdies(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[8];uint64_t n=(uint64_t)pm_available(f),p=8;uint32_t i,count,magic;
    if(n<8 || !pm_read(f,0,h,8)) return false;
    magic=xgr_le32(h);count=xgr_le32(h+4);
    if((magic!=0x0012ec9cU && magic!=0x773e8b56U && magic!=0x00ba09b8U) || !count || count>XGR_MAX_RECORDS || count>(n-8)/10) return false;
    for(i=0;i<count;++i) {
        char name[XGR_MAX_NAME+1];uint64_t at,size;
        if(xgr_stop(pd)) return false;
        if(!xgr_zstr(f,&p,n,name) || !name[0]) return false;
        if(!pm_read(f,(int64_t)p,h,8)) return false;p+=8;
        at=xgr_le32(h);size=xgr_le32(h+4);
        if(!xgr_range(at,size,n) || !xgr_add(f,s,name,at,size)) return false;
    }
    for(i=0;i<count;++i) if((uint64_t)(s->items[i].offset-f->base_address)<p && s->items[i].size) return false;
    s->size=(int64_t)n;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    return pm_available(f)>=0 && !xgr_stop(pd) && birdies_birdies(f,s,pd);
}
Abstractformat *xx_birdies_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(!f) return NULL;
    xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_BIRDIES,"pak");return f;
}
void xx_birdies_free(Abstractformat *f) {
    if(f) { xx_format_cleanup_extra_parameters(f);xx_mem_free(f); }
}
xx_file_type_t xx_birdies_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[64];int64_t size,old=xx_io_tell(d);
    xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;size=pm_available(&f);
    if(size<8 || !pm_read(&f,0,h,(size_t)(size<64?size:64))) goto done;
    { uint32_t magic=xgr_le32(h);
    if(magic==0x0012ec9cU || magic==0x773e8b56U || magic==0x00ba09b8U) type=XX_FILE_TYPE_BIRDIES;
    }
done:
    if(old>=0) xx_io_seek64(d,old,SEEK_SET);
    return type;
}
static Abstractformat *birdies_open(xx_io_device *d) { return xx_birdies_create(d,0); }
static const xx_file_type_t birdies_types[]={XX_FILE_TYPE_BIRDIES};
static const xx_format_search_desc birdies_desc={
    birdies_types,1,NULL,0,birdies_open,xx_birdies_free,true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(birdies,birdies_desc)
