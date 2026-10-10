/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/yaze_ydsk/xx_yaze_ydsk.h"
#include "../common/xx_disk_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 disk_music_blob b={0};uint32_t spt,psh,secsize,sectors,tracklen;uint64_t drive,i,count;bool ok=false;
 DISK_MUSIC_NEED(disk_music_load(f,&b,pd)&&disk_music_tag(&b,0,"<CPM_Disk>",10)&&disk_music_span(&b,0,128)&&b.p[16]<=1&&disk_music_zero(&b,10,6)&&disk_music_zero(&b,17,15)&&disk_music_zero(&b,49,79));spt=xx_data_get_u16(b.p+32, 2, 0, false);psh=b.p[47];DISK_MUSIC_NEED(spt&&psh<=7&&spt%(1U<<psh)==0);secsize=128U<<psh;sectors=spt>>psh;tracklen=spt*128U;drive=b.n-128;
 DISK_MUSIC_NEED(sectors&&sectors<=256&&drive&&drive%tracklen==0&&b.p[34]<=8&&b.p[48]==(1U<<psh)-1);
 if(b.p[34]){uint64_t declared=(128ULL<<b.p[34])*(1U+xx_data_get_u16(b.p+37, 2, 0, false))+(uint64_t)tracklen*xx_data_get_u16(b.p+45, 2, 0, false);DISK_MUSIC_NEED(b.p[35]==(1U<<b.p[34])-1&&declared==drive);}else DISK_MUSIC_NEED(disk_music_zero(&b,35,12));
 count=drive/secsize;DISK_MUSIC_NEED(count<4096&&disk_music_emit(f,s,&b,"descriptor.ydsk",0,128));for(i=0;i<count;++i)DISK_MUSIC_NEED(disk_music_work(&b,1)&&disk_music_emit(f,s,&b,"sector.raw",128+i*secsize,secsize));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_yaze_ydsk_init(xx_yaze_ydsk *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_YAZE_YDSK,"yaze_ydsk");}}
xx_yaze_ydsk *xx_yaze_ydsk_create(xx_io_device *d,int64_t b) {xx_yaze_ydsk *r=(xx_yaze_ydsk *)xx_mem_alloc(sizeof(*r));if(r)xx_yaze_ydsk_init(r,d,b);return r;}
void xx_yaze_ydsk_destroy(xx_yaze_ydsk *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_yaze_ydsk_free(xx_yaze_ydsk *r) {if(r){xx_yaze_ydsk_destroy(r);xx_mem_free(r);}}
bool xx_yaze_ydsk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_yaze_ydsk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
