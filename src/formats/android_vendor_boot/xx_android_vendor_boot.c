/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/include/bootimg/bootimg.h
 * Versions 3/4; exports ramdisk fragments, DTB and bootconfig without decompressing ramdisks.
 */
#include "xxfclib/formats/android_vendor_boot/xx_android_vendor_boot.h"
#include "../xx_payload_members.h"


static int64_t page_end(int64_t at,uint32_t page) { return (at+page-1)&~((int64_t)page-1); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[2128],e[108]; uint32_t v,page,header,ram,dtb,table=0,count=0,entry=0,config=0,i;
    int64_t available=pm_available(f),ra,da,ta,ca,end;
    if(!pm_read(f,0,h,2112) || xx_rt_memcmp(h,"VNDRBOOT",8)) return false;
    v=pm_le32(h+8); page=pm_le32(h+12); ram=pm_le32(h+24); header=pm_le32(h+2096); dtb=pm_le32(h+2100);
    if((v!=3 && v!=4) || page<512 || page>65536 || (page&(page-1)) || header!=(v==3 ? 2112U : 2128U)) return false;
    if(v==4) {
        if(!pm_read(f,2112,h+2112,16)) return false;
        table=pm_le32(h+2112); count=pm_le32(h+2116); entry=pm_le32(h+2120); config=pm_le32(h+2124);
        if(count>65533 || (count && entry!=108) || (!count && entry!=0 && entry!=108) || (uint64_t)count*entry!=table) return false;
    }
    if((v==4 && ram && !count) || (!ram && !dtb && !config)) return false;
    ra=page_end(header,page); da=ra+page_end(ram,page); ta=da+page_end(dtb,page); ca=ta+page_end(table,page);
    end=ca+page_end(config,page); if(end>available) return false;
    if(v==3 && ram) { if(!pm_add(f,s,"vendor-ramdisk.bin",ra,ram)) return false; }
    else for(i=0;i<count;++i) {
        uint32_t size,off,type; char name[64];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,ta+(int64_t)i*entry,e,108)) return false;
        size=pm_le32(e); off=pm_le32(e+4); type=pm_le32(e+8);
        if(!size || off>ram || size>ram-off || type>3) return false;
        xx_rt_snprintf(name,sizeof(name),"ramdisk-%u-type-%u.bin",i,type);
        if(!pm_add(f,s,name,ra+off,size)) return false;
    }
    if((dtb && !pm_add(f,s,"device-tree.dtb",da,dtb)) || (config && !pm_add(f,s,"bootconfig.txt",ca,config))) return false;
    s->size=end; return true;
}

void xx_android_vendor_boot_init(xx_android_vendor_boot *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ANDROID_VENDOR_BOOT,"android_vendor_boot"); } }
xx_android_vendor_boot *xx_android_vendor_boot_create(xx_io_device *d,int64_t b) { xx_android_vendor_boot *r=(xx_android_vendor_boot *)xx_mem_alloc(sizeof(*r)); if(r) xx_android_vendor_boot_init(r,d,b); return r; }
void xx_android_vendor_boot_destroy(xx_android_vendor_boot *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_android_vendor_boot_free(xx_android_vendor_boot *r) { if(r) { xx_android_vendor_boot_destroy(r); xx_mem_free(r); } }
bool xx_android_vendor_boot_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_android_vendor_boot_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
