/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/adlib_rad/xx_adlib_rad.h"
#include "../xx_fourteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 fm_blob b={0};fm_range ranges[1024];unsigned nr=0;uint64_t at=18,start,end=0;uint32_t last=0,i,no;bool ok=false;
 FM_NEED(fm_load(f,&b,pd)&&fm_tag(&b,0,"RAD by REALiTY!!",16)&&fm_span(&b,0,18)&&b.p[16]==0x10&&!(b.p[17]&0x20));if(b.p[17]&128)FM_NEED(fm_zstring(&b,&at,b.n,65536));FM_NEED(fm_emit(f,s,&b,"descriptor.rad",0,at));
 for(;;){uint8_t id;FM_NEED(fm_span(&b,at,1));start=at;id=b.p[(size_t)at++];if(!id)break;FM_NEED(id<=31&&id>last&&fm_span(&b,at,11)&&fm_emit(f,s,&b,"instrument.rad",start,12));last=id;at+=11;}
 start=at;FM_NEED(fm_span(&b,at,1));no=b.p[(size_t)at++];FM_NEED(no&&no<=128&&fm_span(&b,at,(uint64_t)no+64));for(i=0;i<no;++i){uint8_t v=b.p[(size_t)(at+i)];FM_NEED((v&128)?(v&127U)<no:v<32);}at+=no;FM_NEED(fm_emit(f,s,&b,"orders.rad",start,(uint64_t)no+1));start=at;at+=64;FM_NEED(fm_claim(&b,ranges,&nr,0,at,false)&&fm_emit(f,s,&b,"pattern-offsets.rad",start,64));
 for(i=0;i<32;++i){uint64_t p=pm_le16(b.p+(size_t)start+i*2),a=p;uint32_t line=0,lastline=0;bool first=true;if(!p)continue;FM_NEED(p>=at);for(;;){uint8_t l,c;FM_NEED(fm_span(&b,p,1)&&fm_work(&b,1));l=b.p[(size_t)p++];line=l&127;FM_NEED(line<64&&(first||line>lastline));lastline=line;first=false;for(;;){uint8_t inst;FM_NEED(fm_span(&b,p,3)&&fm_work(&b,1));c=b.p[(size_t)p++];FM_NEED((c&15)<9&&!(c&0x70));++p;inst=b.p[(size_t)p++];FM_NEED((inst>>4)<=31);if(inst&15){FM_NEED(fm_span(&b,p,1));++p;}if(c&128)break;}if(l&128)break;}FM_NEED(fm_claim(&b,ranges,&nr,a,p-a,false)&&fm_emit(f,s,&b,"pattern.rad",a,p-a));if(p>end)end=p;}
 FM_NEED(end==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_rad_init(xx_adlib_rad *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_RAD,"adlib_rad");}}
xx_adlib_rad *xx_adlib_rad_create(xx_io_device *d,int64_t b) {xx_adlib_rad *r=(xx_adlib_rad *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_rad_init(r,d,b);return r;}
void xx_adlib_rad_destroy(xx_adlib_rad *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_rad_free(xx_adlib_rad *r) {if(r){xx_adlib_rad_destroy(r);xx_mem_free(r);}}
bool xx_adlib_rad_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_rad_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
