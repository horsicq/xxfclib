/* SPDX-License-Identifier: MIT */
#include "xxfclib/formats/upx_engine/xx_upx_engine.h"
#include "xxfclib/formats/sevenzip_engine/xx_sevenzip_engine.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
Abstractformat *xx_upx_create(xx_io_device *d,int64_t base) {
    return xx_sevenzip_engine_create_helper(d,base,"UPX","xfu_upx_helper.exe",XX_FILE_TYPE_UPX,"exe");
}
void xx_upx_free(Abstractformat *f) { xx_sevenzip_engine_free(f); }
static bool upx_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool upx_read_exact(xx_io_device*d,void*p,size_t n,xx_pd_struct*pd) {
    size_t at=0;while(at<n && !upx_stopped(pd)) { ssize_t got=xx_io_read(d,(unsigned char*)p+at,n-at);if(got<=0 || (size_t)got>n-at)return false;at+=(size_t)got; }return at==n;
}
static bool upx_scan(xx_io_device*d,int64_t start,int64_t count,unsigned required_format,xx_pd_struct*pd) {
    unsigned char buf[4103];size_t carried=0;
    if(xx_io_seek64(d,start,SEEK_SET)!=0)return false;
    while(count>0 && !upx_stopped(pd)) {
        size_t n=count>4096?4096:(size_t)count,i;
        if(!upx_read_exact(d,buf+carried,n,pd))return false;
        for(i=0;i+8<=carried+n;++i)if(!xx_rt_memcmp(buf+i,"UPX!",4) &&
            buf[i+4]>0 && buf[i+4]<255 && buf[i+5]>0 &&
            (!required_format || buf[i+5]==required_format) &&
            ((buf[i+6]>=2 && buf[i+6]<=10) || buf[i+6]==14) &&
            (buf[i+7]&15)>=1 && (buf[i+7]&15)<=10)return true;
        { size_t total=carried+n;carried=total<7?total:7;
          xx_rt_memmove(buf,buf+total-carried,carried); }count-=n;
    }
    return false;
}
bool xx_upx_has_marker_device(xx_io_device*d,xx_pd_struct*pd) {
    int64_t saved=xx_io_tell(d),size=xx_io_total_size(d),window;unsigned char h[4];unsigned legacy=0;bool found=false;
    if(saved<0 || size<4 || upx_stopped(pd) || xx_io_seek64(d,0,SEEK_SET)!=0 || !upx_read_exact(d,h,4,pd))goto done;
    /* COMMAIN1 starts CMP SP,imm16; SYSMAIN1 starts the DOS device sentinel.
     * PackCom::canUnpack reads its type1/type2 header within the first128 bytes.
     * Requiring those loader prefixes prevents archives/text containing a
     * child's pack header from being classified as a legacy executable. */
    if(size<=0xff00 && h[0]==0x81 && h[1]==0xfc)legacy=1;
    else if(size<=0x10000 && h[0]==0xff && h[1]==0xff && h[2]==0xff && h[3]==0xff)legacy=2;
    if(legacy){found=upx_scan(d,0,size<128?size:128,legacy,pd);goto done;}
    /* Official PE/DOS, ELF, Mach-O and Atari carriers. */
    if(!(h[0]=='M'&&h[1]=='Z') && xx_rt_memcmp(h,"\177ELF",4) &&
       xx_rt_memcmp(h,"\xcf\xfa\xed\xfe",4) && xx_rt_memcmp(h,"\xce\xfa\xed\xfe",4) &&
       xx_rt_memcmp(h,"\xfe\xed\xfa\xcf",4) && xx_rt_memcmp(h,"\xfe\xed\xfa\xce",4) &&
       xx_rt_memcmp(h,"\xca\xfe\xba\xbe",4) && !(h[0]==0x60&&h[1]==0x1a))goto done;
    window=size<1024*1024?size:1024*1024;
    found=upx_scan(d,0,window,0,pd);
    if(!found && size>window)found=upx_scan(d,size-window,window,0,pd);
 done:if(saved>=0)(void)xx_io_seek64(d,saved,SEEK_SET);return found&&!upx_stopped(pd);
}
xx_file_type_t xx_upx_detect_device(xx_io_device*d,xx_pd_struct*pd) {
    int64_t saved=xx_io_tell(d);xx_file_type_t result=XX_FILE_TYPE_UNKNOWN;Abstractformat*f=NULL;
    if(!xx_upx_has_marker_device(d,pd))goto done;
    f=xx_upx_create(d,0);
    if(f && xx_format_is_valid(f,pd) && xx_format_handle_base_info(f,pd))result=XX_FILE_TYPE_UPX;
 done:xx_upx_free(f);if(saved>=0)(void)xx_io_seek64(d,saved,SEEK_SET);return result;
}
