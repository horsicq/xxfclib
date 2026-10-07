/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos */
#include "xxfclib/formats/photontiming_phu/xx_photontiming_phu.h"
#include "../xx_eighth_data.h"

typedef struct pq_item {char name[32];int32_t index;uint32_t type;uint64_t value;} pq_item;
static bool pq_header(Abstractformat *f,const char *magic,pq_item *items,unsigned *count,uint64_t *end,xx_pd_struct *pd) {
    uint8_t h[48];uint64_t at=16,total=(uint64_t)pm_available(f);unsigned i,j;bool nul=false;
    if(total>67108864 || !pm_read(f,0,h,16) || xx_rt_memcmp(h,magic,8) || h[8]<'1' || h[8]>'3' || h[9]!='.') return false;
    for(i=8;i<16;++i) {if(!h[i]) nul=true;else if(nul || (h[i]!='.' && (h[i]<'0' || h[i]>'9'))) return false;}if(!nul) return false;
    for(i=0;i<1024;++i) {pq_item *v=items+i;uint64_t n=0;if(at>4194256 || !eh_take(f,&at,total,h,48,pd)) return false;
        for(j=0;j<32 && h[j];++j) { if(h[j]<32 || h[j]>126) return false; } if(!j || j==32) return false;
        xx_rt_memcpy(v->name,h,32);v->index=(int32_t)xx_data_get_u32(h+32, 4, 0, false);v->type=xx_data_get_u32(h+36, 4, 0, false);v->value=xx_data_get_u64(h+40, 8, 0, false);
        if(v->index < -1 || v->index>=4096) return false;
        for(j=0;j<i;++j) if(items[j].index==v->index && !xx_rt_strcmp(items[j].name,v->name)) return false;
        switch(v->type) {
        case 0xffff0008U:if(v->value) return false;break;
        case 0x00000008U:break;
        case 0x10000008U:case 0x11000008U:case 0x12000008U:break;
        case 0x20000008U:case 0x21000008U:if(!sv_finite64(v->value)) return false;break;
        case 0x2001ffffU:case 0x4001ffffU:case 0x4002ffffU:case 0xffffffffU:
            n=v->value;if(n>1048576 || !eh_span(at,n,total) || at+n>4194304) return false;
            if(v->type==0x2001ffffU && !eh_float_array(f,at,n,8,false,pd)) return false;
            if(v->type==0x4001ffffU && (!n || !pm_read(f,(int64_t)(at+n-1),h,1) || h[0])) return false;
            if(v->type==0x4002ffffU && (n<2 || (n&1) || !pm_read(f,(int64_t)(at+n-2),h,2) || h[0] || h[1])) return false;
            at+=n;break;
        default:return false;
        }
        if(!xx_rt_strcmp(v->name,"Header_End")) {if(v->index!=-1 || v->type!=0xffff0008U) return false;*count=i+1;*end=at;return true;}
    }return false;
}
static bool pq_int(const pq_item *items,unsigned n,const char *name,int32_t index,uint64_t *value) {
    unsigned i;for(i=0;i<n;++i) if(items[i].index==index && !xx_rt_strcmp(items[i].name,name)) {if(items[i].type!=0x10000008U || items[i].value>INT64_MAX) return false;*value=items[i].value;return true;}return false;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    pq_item *items=(pq_item *)xx_mem_alloc(1024*sizeof(*items));unsigned n=0,i;uint64_t at=0,curves=0,total=(uint64_t)pm_available(f);bool ok=false;char label[48];
    if(!items) return false;
    if(!pq_header(f,"PQHISTO\0",items,&n,&at,pd) || !pq_int(items,n,"HistoResult_NumberOfCurves",-1,&curves) || !curves || curves>128 || !pm_add(f,s,"phu-header.bin",0,(int64_t)at)) goto done;
    for(i=0;i<curves;++i) {uint64_t bins,index,bytes,offset;unsigned j;bool has_offset=false;
        if(fd_stop(pd) || !pq_int(items,n,"HistResDscr_CurveIndex",(int32_t)i,&index) || index!=i || !pq_int(items,n,"HistResDscr_HistogramBins",(int32_t)i,&bins) || !bins || bins>16777216 || !fd_mul(bins,4,&bytes) || !eh_span(at,bytes,total)) goto done;
        for(j=0;j<n;++j) if(items[j].index==(int32_t)i && !xx_rt_strcmp(items[j].name,"HistResDscr_DataOffset")) has_offset=true;
        if(has_offset && (!pq_int(items,n,"HistResDscr_DataOffset",(int32_t)i,&offset) || offset!=at)) goto done;
        xx_rt_snprintf(label,sizeof(label),"histogram-%u.bin",i);if(!pm_add(f,s,label,(int64_t)at,(int64_t)bytes)) goto done;at+=bytes;
    }if(at!=total) goto done;s->size=(int64_t)at;ok=true;
done:xx_mem_free(items);return ok;
}

void xx_photontiming_phu_init(xx_photontiming_phu *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PHOTONTIMING_PHU,"photontiming_phu"); } }
xx_photontiming_phu *xx_photontiming_phu_create(xx_io_device *d,int64_t b) { xx_photontiming_phu *r=(xx_photontiming_phu *)xx_mem_alloc(sizeof(*r)); if(r) xx_photontiming_phu_init(r,d,b); return r; }
void xx_photontiming_phu_destroy(xx_photontiming_phu *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_photontiming_phu_free(xx_photontiming_phu *r) { if(r) { xx_photontiming_phu_destroy(r); xx_mem_free(r); } }
bool xx_photontiming_phu_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_photontiming_phu_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
