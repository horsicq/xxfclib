/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/BlackrockNeurotech/NPMK/blob/master/NPMK/openNEV.m */
#include "xxfclib/formats/blackrock_nev/xx_blackrock_nev.h"
#include "../xx_ninth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[336];nh_blob b={0};bool ok=false;uint32_t header,packet,ext,last=0;uint64_t at;unsigned records=0,i;
    if(!pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"NEURALEV",8) || h[8]!=2 || (h[9]!=2 && h[9]!=3)) return false;
    header=pm_le32(h+12);packet=pm_le32(h+16);ext=pm_le32(h+332);
    NH_NEED(!(pm_le16(h+10)&~1U) && packet>=8 && packet<=65536 && ext<=1024 && header==336+32*ext && pm_le32(h+20) && pm_le32(h+24) && nh_date(h+28));
    NH_NEED(nh_load(f,&b,pd) && nh_span(&b,0,header) && b.n>header && (b.n-header)%packet==0 && (b.n-header)/packet<=4095);
    for(i=0;i<ext;++i) NH_NEED(nh_ascii(b.p+336+i*32,8,false));
    NH_NEED(nh_add(f,s,&b,"header",0,header));at=header;
    while(at<b.n) {uint32_t time=pm_le32(b.p+(size_t)at);uint16_t id=pm_le16(b.p+(size_t)at+4);NH_NEED(!records || time>=last);NH_NEED(id==0 || (id<=2048 && id>=1));if(id) NH_NEED(b.p[(size_t)at+7]==0);else NH_NEED(packet>=10 && !(b.p[(size_t)at+6]&~0x81U));
        NH_NEED(nh_add(f,s,&b,id ? "spike":"digital",at,packet));last=time;at+=packet;++records;
    }s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_blackrock_nev_init(xx_blackrock_nev *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_BLACKROCK_NEV,"blackrock_nev"); } }
xx_blackrock_nev *xx_blackrock_nev_create(xx_io_device *d,int64_t b) { xx_blackrock_nev *r=(xx_blackrock_nev *)xx_mem_alloc(sizeof(*r)); if(r) xx_blackrock_nev_init(r,d,b); return r; }
void xx_blackrock_nev_destroy(xx_blackrock_nev *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_blackrock_nev_free(xx_blackrock_nev *r) { if(r) { xx_blackrock_nev_destroy(r); xx_mem_free(r); } }
bool xx_blackrock_nev_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_blackrock_nev_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
