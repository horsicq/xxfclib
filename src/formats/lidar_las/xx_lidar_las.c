/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/laspy/laspy/master/laspy/header.py */
#include "xxfclib/formats/lidar_las/xx_lidar_las.h"
#include "../xx_seventh_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    static const unsigned widths[]={20,28,26,34};uint8_t h[227],b[54];uint64_t at,data,n,points,header;unsigned vlrs,fmt,i;int64_t available=pm_available(f);
    if(fd_stop(pd) || available<227 || !pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"LASF",4) || h[24]!=1 || h[25]!=2 || (pm_le16(h+6)&~1U)) return false;
    header=pm_le16(h+94);data=pm_le32(h+96);vlrs=pm_le32(h+100);fmt=h[104];points=pm_le32(h+107);
    if(header<227 || header>65535 || data<header || vlrs>1024 || fmt>3 || pm_le16(h+105)<widths[fmt] || !points || points>1000000 || !fd_mul(points,pm_le16(h+105),&n) || !fd_range(data,n,(uint64_t)available)) return false;
    for(i=0;i<3;++i) if(!sv_positive64(fd_le64(h+131+8*i))) return false;
    for(i=155;i<227;i+=8) if(!sv_finite64(fd_le64(h+i))) return false;
    for(i=0;i<3;++i) if(sv_ordered64(fd_le64(h+187+16*i))>sv_ordered64(fd_le64(h+179+16*i))) return false;
    if(!pm_add(f,s,"las-header.bin",0,(int64_t)header)) { return false; } at=header;
    for(i=0;i<vlrs;++i) {uint64_t z;char label[64];if(fd_stop(pd) || !fd_range(at,54,data) || !pm_read(f,(int64_t)at,b,54)) return false;
        z=54U+pm_le16(b+20);if(!fd_range(at,z,data)) return false;
        xx_rt_snprintf(label,sizeof(label),"vlr-%u.bin",i);if(!pm_add(f,s,label,(int64_t)at,(int64_t)z)) return false;at+=z;
    }
    if(at<data && !pm_add(f,s,"point-padding.bin",(int64_t)at,(int64_t)(data-at))) return false;
    if(!pm_add(f,s,"points.bin",(int64_t)data,(int64_t)n)) { return false; } s->size=(int64_t)(data+n);return true;
}

void xx_lidar_las_init(xx_lidar_las *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LIDAR_LAS,"lidar_las"); } }
xx_lidar_las *xx_lidar_las_create(xx_io_device *d,int64_t b) { xx_lidar_las *r=(xx_lidar_las *)xx_mem_alloc(sizeof(*r)); if(r) xx_lidar_las_init(r,d,b); return r; }
void xx_lidar_las_destroy(xx_lidar_las *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_lidar_las_free(xx_lidar_las *r) { if(r) { xx_lidar_las_destroy(r); xx_mem_free(r); } }
bool xx_lidar_las_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_lidar_las_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
