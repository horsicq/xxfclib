/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.esri.com/library/whitepapers/pdfs/shapefile.pdf */
#include "xxfclib/formats/esri_shp/xx_esri_shp.h"
#include "../xx_seventh_data.h"

static uint64_t ordered(uint64_t x) {if(!(x&UINT64_C(0x7fffffffffffffff))) x=0;return x>>63 ? ~x : x^UINT64_C(0x8000000000000000);}
static bool box(const uint8_t *b) {unsigned i;for(i=0;i<4;++i) if(!sv_finite64(fd_le64(b+8*i))) return false;return ordered(fd_le64(b))<=ordered(fd_le64(b+16)) && ordered(fd_le64(b+8))<=ordered(fd_le64(b+24));}
static bool xy(Abstractformat *f,uint64_t at,uint64_t count,xx_pd_struct *pd) {uint8_t b[16];uint64_t i;for(i=0;i<count;++i) {if(fd_stop(pd) || !pm_read(f,(int64_t)(at+16*i),b,16) || !sv_finite64(fd_le64(b)) || !sv_finite64(fd_le64(b+8))) return false;}return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[100],b[44],q[4];uint64_t end,at=100,total_points=0;uint32_t declared,index=0;unsigned i;int64_t available=pm_available(f);
    if(fd_stop(pd) || available<108 || !pm_read(f,0,h,100) || pm_be32(h)!=9994 || pm_le32(h+28)!=1000 || !box(h+36)) return false;
    for(i=4;i<24;i+=4) if(pm_be32(h+i)) return false;end=(uint64_t)pm_be32(h+24)*2;declared=pm_le32(h+32);
    if(end<108 || end>67108864 || end>(uint64_t)available || (declared!=0 && declared!=1 && declared!=3 && declared!=5 && declared!=8) || !pm_add(f,s,"shp-header.bin",0,100)) return false;
    while(at<end) {uint64_t bytes,points,start;uint32_t kind,parts=0,j,last=0;char label[64];
        if(fd_stop(pd) || ++index>4095 || !fd_range(at,12,end) || !pm_read(f,(int64_t)at,b,12) || pm_be32(b)!=index) return false;
        bytes=(uint64_t)pm_be32(b+4)*2;kind=pm_le32(b+8);start=at+8;
        if(bytes<4 || !fd_range(start,bytes,end) || (kind && kind!=declared)) return false;
        if(!kind) {if(bytes!=4) return false;}
        else if(kind==1) {if(++total_points>1000000 || bytes!=20 || !xy(f,start+4,1,pd)) return false;}
        else {if(bytes<(kind==8?40U:44U) || !pm_read(f,(int64_t)start,b,kind==8?40:44) || !box(b+4)) return false;
            if(kind==8) {points=pm_le32(b+36);if(!points || points>1000000-total_points || bytes!=40+points*16 || !xy(f,start+40,points,pd)) return false;total_points+=points;}
            else {parts=pm_le32(b+36);points=pm_le32(b+40);if(!parts || parts>4096 || !points || points>1000000-total_points || bytes!=44+(uint64_t)parts*4+points*16) return false;total_points+=points;
                for(j=0;j<parts;++j) {uint32_t first;if(fd_stop(pd) || !pm_read(f,(int64_t)(start+44+4U*j),q,4)) return false;first=pm_le32(q);
                    if(first>=points || (!j && first) || (j && (first<=last || first-last<(kind==5?4U:2U)))) return false;last=first;
                }if(points-last<(kind==5?4U:2U) || !xy(f,start+44+(uint64_t)parts*4,points,pd)) return false;
                if(kind==5) for(j=0;j<parts;++j) {uint32_t first,next;uint8_t a[16],z[16];uint64_t data=start+44+(uint64_t)parts*4;
                    if(fd_stop(pd) || !pm_read(f,(int64_t)(start+44+4U*j),q,4)) return false;first=pm_le32(q);next=(uint32_t)points;
                    if(j+1<parts) {if(!pm_read(f,(int64_t)(start+48+4U*j),q,4)) return false;next=pm_le32(q);}
                    if(!pm_read(f,(int64_t)(data+16U*first),a,16) || !pm_read(f,(int64_t)(data+16U*(next-1)),z,16) || ordered(fd_le64(a))!=ordered(fd_le64(z)) || ordered(fd_le64(a+8))!=ordered(fd_le64(z+8))) return false;
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
