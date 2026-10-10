/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/tracker_coconizer/xx_tracker_coconizer.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 music_blob b={0};music_range ranges[1024];unsigned nr=0;uint64_t seq,pat,at,end=0;uint32_t ni,no,np,ch,i,j;bool cr=false,ok=false;
 MUSIC_NEED(music_load(f,&b,pd)&&music_span(&b,0,32)&&(b.p[0]==0x84||b.p[0]==0x88));ch=b.p[0]&63;ni=b.p[21];no=b.p[22];np=b.p[23];seq=xx_data_get_u32(b.p+24, 4, 0, false);pat=xx_data_get_u32(b.p+28, 4, 0, false);
 for(i=1;i<21;++i)if(b.p[i]==13)cr=true;
 MUSIC_NEED(cr&&ni&&ni<=100&&no&&np&&music_claim(&b,ranges,&nr,0,32+(uint64_t)ni*32,false)&&music_emit(f,s,&b,"descriptor.coco",0,32)&&music_emit(f,s,&b,"instruments.coco",32,(uint64_t)ni*32));
 MUSIC_NEED(music_claim(&b,ranges,&nr,seq,(uint64_t)no+1,false)&&b.p[(size_t)(seq+no)]==255);for(i=0;i<no;++i)MUSIC_NEED(b.p[(size_t)(seq+i)]<np);MUSIC_NEED(music_emit(f,s,&b,"orders.coco",seq,(uint64_t)no+1));
 for(i=0;i<np;++i){at=pat+(uint64_t)i*64*ch*4;MUSIC_NEED(music_claim(&b,ranges,&nr,at,(uint64_t)64*ch*4,false));for(j=0;j<64*ch;++j){const uint8_t *q=b.p+(size_t)at+j*4;MUSIC_NEED(music_work(&b,1)&&q[2]<=ni);}MUSIC_NEED(music_emit(f,s,&b,"pattern.coco",at,(uint64_t)64*ch*4));}
 for(i=0;i<ni;++i){const uint8_t *q=b.p+32+i*32;uint64_t a=xx_data_get_u32(q, 4, 0, false),n=xx_data_get_u32(q+4, 4, 0, false),lp=xx_data_get_u32(q+12, 4, 0, false),lz=xx_data_get_u32(q+16, 4, 0, false);MUSIC_NEED(xx_data_get_u32(q+8, 4, 0, false)<=255&&lp<=n&&(!lp||!lz||lz-1<=n-lp));if(n){MUSIC_NEED(music_claim(&b,ranges,&nr,a,n,false)&&music_emit(f,s,&b,"sample.vidc",a,n));}}
 for(i=0;i<nr;++i)if(ranges[i].at+ranges[i].n>end)end=ranges[i].at+ranges[i].n;
 if(end!=b.n){MUSIC_NEED(end+16==b.n);for(i=0;i<ni;++i){const uint8_t *q=b.p+32+i*32;uint64_t a=xx_data_get_u32(q, 4, 0, false),n=xx_data_get_u32(q+4, 4, 0, false);if(n){MUSIC_NEED(music_span(&b,a+n,16)&&music_zero_each(&b,a+n,15)&&b.p[(size_t)(a+n+15)]==13&&music_claim(&b,ranges,&nr,a+n,16,false));}}end+=16;}
 MUSIC_NEED(end==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_tracker_coconizer_init(xx_tracker_coconizer *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_COCONIZER,"tracker_coconizer");}}
xx_tracker_coconizer *xx_tracker_coconizer_create(xx_io_device *d,int64_t b) {xx_tracker_coconizer *r=(xx_tracker_coconizer *)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_coconizer_init(r,d,b);return r;}
void xx_tracker_coconizer_destroy(xx_tracker_coconizer *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_coconizer_free(xx_tracker_coconizer *r) {if(r){xx_tracker_coconizer_destroy(r);xx_mem_free(r);}}
bool xx_tracker_coconizer_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tracker_coconizer_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
