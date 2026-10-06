/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/firmware-image-format.html
 * ESP32 chip ID 0 with no appended SHA digest; verifies segment XOR checksum. No flash or firmware execution.
 */
#include "xxfclib/formats/espressif_image/xx_espressif_image.h"
#include "../xx_payload_members.h"


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[24],seg[8],buf[8192],checksum=0xef,stored; int64_t at=24,end,available=pm_available(f); unsigned i,j;
    if(!pm_read(f,0,h,24) || h[0]!=0xe9 || !h[1] || h[1]>16 || h[2]>3 || ((h[3]&15)>2 && (h[3]&15)!=15) || pm_le16(h+12)!=0 || h[23]!=0) return false;
    for(i=19;i<23;++i) if(h[i]) return false;
    for(i=0;i<h[1];++i) {
        uint32_t size,addr; int64_t pos; uint64_t left; char name[64];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,seg,8)) return false;
        addr=pm_le32(seg); size=pm_le32(seg+4); at+=8;
        if(!size || addr>UINT32_MAX-size || at>available || size>(uint64_t)(available-at)) return false;
        xx_rt_snprintf(name,sizeof(name),"segment-%u-address-%08x.bin",i,addr);
        if(!pm_add(f,s,name,at,size)) return false;
        pos=at; left=size;
        while(left) {
            size_t n=left>sizeof(buf) ? sizeof(buf) : (size_t)left;
            if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,pos,buf,n)) return false;
            for(j=0;j<n;++j) { checksum^=buf[j]; } pos+=n; left-=n;
        }
        at+=size;
    }
    end=(at+16)&~INT64_C(15); if(end>available) return false;
    while(at<end-1) { if(!pm_read(f,at++,&stored,1) || stored) return false; }
    if(!pm_read(f,end-1,&stored,1) || stored!=checksum) return false;
    s->size=end; return true;
}

void xx_espressif_image_init(xx_espressif_image *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ESPRESSIF_IMAGE,"espressif_image"); } }
xx_espressif_image *xx_espressif_image_create(xx_io_device *d,int64_t b) { xx_espressif_image *r=(xx_espressif_image *)xx_mem_alloc(sizeof(*r)); if(r) xx_espressif_image_init(r,d,b); return r; }
void xx_espressif_image_destroy(xx_espressif_image *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_espressif_image_free(xx_espressif_image *r) { if(r) { xx_espressif_image_destroy(r); xx_mem_free(r); } }
bool xx_espressif_image_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_espressif_image_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
