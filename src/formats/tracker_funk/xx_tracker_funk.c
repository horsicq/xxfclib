/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/tracker_funk/xx_tracker_funk.h"
#include "../common/xx_disk_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 disk_music_blob b={0};uint32_t ch,np=0,no=0,i,j,lens[64];uint64_t at=2449;bool ok=false;
 DISK_MUSIC_NEED(disk_music_load(f,&b,pd)&&disk_music_span(&b,0,2449)&&disk_music_tag(&b,0,"Funk",4)&&xx_data_get_u32(b.p+8, 4, 0, false)==b.n&&(b.p[5]>>1)>=10&&(b.p[6]>>4)<=7&&(b.p[6]&15)<=9);
 DISK_MUSIC_NEED(b.p[12]=='F'&&(b.p[13]=='k'||b.p[13]=='v'||(b.p[13]=='2'&&!(b.p[7]&1)))&&b.p[14]>='0'&&b.p[14]<='9'&&b.p[15]>='0'&&b.p[15]<='9');ch=(b.p[14]-'0')*10+b.p[15]-'0';DISK_MUSIC_NEED(ch&&ch<=32);
 while(no<256&&b.p[17+no]!=255){uint32_t v=b.p[17+no++];DISK_MUSIC_NEED(v<128);if(v>=np)np=v+1;}DISK_MUSIC_NEED(no&&no<256&&b.p[16]<no);
 for(i=0;i<128;++i) {DISK_MUSIC_NEED(b.p[273+i]<64); } for(i=0;i<64;++i){const uint8_t *q=b.p+401+i*32;uint32_t a=xx_data_get_u32(q+19, 4, 0, false);lens[i]=xx_data_get_u32(q+23, 4, 0, false);DISK_MUSIC_NEED(lens[i]<=16777216&&(a==UINT32_MAX||a<=lens[i]));}
 DISK_MUSIC_NEED(disk_music_emit(f,s,&b,"descriptor.fnk",0,17)&&disk_music_emit(f,s,&b,"orders.fnk",17,256)&&disk_music_emit(f,s,&b,"pattern-breaks.fnk",273,128)&&disk_music_emit(f,s,&b,"instruments.fnk",401,2048));
 for(i=0;i<np;++i){uint64_t size=(uint64_t)ch*64*3;DISK_MUSIC_NEED(disk_music_span(&b,at,size));for(j=0;j<ch*64;++j)DISK_MUSIC_NEED(disk_music_work(&b,1));DISK_MUSIC_NEED(disk_music_emit(f,s,&b,"pattern.fnk",at,size));at+=size;}
 for(i=0;i<64;++i)if(lens[i]>2){DISK_MUSIC_NEED(disk_music_emit(f,s,&b,"sample.pcm8",at,lens[i]));at+=lens[i];}
 DISK_MUSIC_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_tracker_funk_init(xx_tracker_funk *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_FUNK,"tracker_funk");}}
xx_tracker_funk *xx_tracker_funk_create(xx_io_device *d,int64_t b) {xx_tracker_funk *r=(xx_tracker_funk *)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_funk_init(r,d,b);return r;}
void xx_tracker_funk_destroy(xx_tracker_funk *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_funk_free(xx_tracker_funk *r) {if(r){xx_tracker_funk_destroy(r);xx_mem_free(r);}}
bool xx_tracker_funk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tracker_funk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
