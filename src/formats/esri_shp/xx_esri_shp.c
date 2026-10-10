/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.esri.com/library/whitepapers/pdfs/shapefile.pdf */
#include "xxfclib/formats/esri_shp/xx_esri_shp.h"
#include "../common/xx_numeric_values.h"

static uint64_t ordered(uint64_t x) {if(!(x&UINT64_C(0x7fffffffffffffff))) x=0;return x>>63 ? ~x : x^UINT64_C(0x8000000000000000);}
static bool box(const uint8_t *b) {unsigned i;for(i=0;i<4;++i) if(!numeric_finite64(xx_data_get_u64(b+8*i, 8, 0, false))) return false;return ordered(xx_data_get_u64(b, 8, 0, false))<=ordered(xx_data_get_u64(b+16, 8, 0, false)) && ordered(xx_data_get_u64(b+8, 8, 0, false))<=ordered(xx_data_get_u64(b+24, 8, 0, false));}
static bool xy(Abstractformat *f,uint64_t at,uint64_t count,xx_pd_struct *pd) {uint8_t b[16];uint64_t i;for(i=0;i<count;++i) {if(binary_stop(pd) || !pm_read(f,(int64_t)(at+16*i),b,16) || !numeric_finite64(xx_data_get_u64(b, 8, 0, false)) || !numeric_finite64(xx_data_get_u64(b+8, 8, 0, false))) return false;}return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[100],b[44],q[4];uint64_t end,at=100,total_points=0;uint32_t declared,index=0;unsigned i;int64_t available=pm_available(f);
    if(binary_stop(pd) || available<108 || !pm_read(f,0,h,100) || xx_data_get_u32(h, 4, 0, true)!=9994 || xx_data_get_u32(h+28, 4, 0, false)!=1000 || !box(h+36)) return false;
    for(i=4;i<24;i+=4) { if(xx_data_get_u32(h+i, 4, 0, true)) return false; } end=(uint64_t)xx_data_get_u32(h+24, 4, 0, true)*2;declared=xx_data_get_u32(h+32, 4, 0, false);
    if(end<108 || end>67108864 || end>(uint64_t)available || (declared!=0 && declared!=1 && declared!=3 && declared!=5 && declared!=8) || !pm_add(f,s,"shp-header.bin",0,100)) return false;
    while(at<end) {uint64_t bytes,points,start;uint32_t kind,parts=0,j,last=0;char label[64];
        if(binary_stop(pd) || ++index>4095 || !binary_range(at,12,end) || !pm_read(f,(int64_t)at,b,12) || xx_data_get_u32(b, 4, 0, true)!=index) return false;
        bytes=(uint64_t)xx_data_get_u32(b+4, 4, 0, true)*2;kind=xx_data_get_u32(b+8, 4, 0, false);start=at+8;
        if(bytes<4 || !binary_range(start,bytes,end) || (kind && kind!=declared)) return false;
        if(!kind) {if(bytes!=4) return false;}
        else if(kind==1) {if(++total_points>1000000 || bytes!=20 || !xy(f,start+4,1,pd)) return false;}
        else {if(bytes<(kind==8?40U:44U) || !pm_read(f,(int64_t)start,b,kind==8?40:44) || !box(b+4)) return false;
            if(kind==8) {points=xx_data_get_u32(b+36, 4, 0, false);if(!points || points>1000000-total_points || bytes!=40+points*16 || !xy(f,start+40,points,pd)) return false;total_points+=points;}
            else {parts=xx_data_get_u32(b+36, 4, 0, false);points=xx_data_get_u32(b+40, 4, 0, false);if(!parts || parts>4096 || !points || points>1000000-total_points || bytes!=44+(uint64_t)parts*4+points*16) return false;total_points+=points;
                for(j=0;j<parts;++j) {uint32_t first;if(binary_stop(pd) || !pm_read(f,(int64_t)(start+44+4U*j),q,4)) return false;first=xx_data_get_u32(q, 4, 0, false);
                    if(first>=points || (!j && first) || (j && (first<=last || first-last<(kind==5?4U:2U)))) { return false; } last=first;
                }if(points-last<(kind==5?4U:2U) || !xy(f,start+44+(uint64_t)parts*4,points,pd)) return false;
                if(kind==5) for(j=0;j<parts;++j) {uint32_t first,next;uint8_t a[16],z[16];uint64_t data=start+44+(uint64_t)parts*4;
                    if(binary_stop(pd) || !pm_read(f,(int64_t)(start+44+4U*j),q,4)) { return false; } first=xx_data_get_u32(q, 4, 0, false);next=(uint32_t)points;
                    if(j+1<parts) {if(!pm_read(f,(int64_t)(start+48+4U*j),q,4)) return false;next=xx_data_get_u32(q, 4, 0, false);}
                    if(!pm_read(f,(int64_t)(data+16U*first),a,16) || !pm_read(f,(int64_t)(data+16U*(next-1)),z,16) || ordered(xx_data_get_u64(a, 8, 0, false))!=ordered(xx_data_get_u64(z, 8, 0, false)) || ordered(xx_data_get_u64(a+8, 8, 0, false))!=ordered(xx_data_get_u64(z+8, 8, 0, false))) return false;
                }
            }
        }
        xx_rt_snprintf(label,sizeof(label),"shape-%u.bin",index-1);if(!pm_add(f,s,label,(int64_t)start,(int64_t)bytes)) return false;at=start+bytes;
    }if(!index || at!=end) return false;s->size=(int64_t)end;return true;
}

void xx_esri_shp_init(xx_esri_shp *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ESRI_SHP,"esri_shp"); } }
xx_esri_shp *xx_esri_shp_create(xx_io_device *d,int64_t b) { xx_esri_shp *r=(xx_esri_shp *)xx_mem_alloc(sizeof(*r)); if(r) xx_esri_shp_init(r,d,b); return r; }
void xx_esri_shp_destroy(xx_esri_shp *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_esri_shp_free(xx_esri_shp *r) { if(r) { xx_esri_shp_destroy(r); xx_mem_free(r); } }
bool xx_esri_shp_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_esri_shp_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
