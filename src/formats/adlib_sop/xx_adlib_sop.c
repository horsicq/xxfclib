/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/adlib_sop/xx_adlib_sop.h"
#include "../xx_sixteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 m16_blob b={0};uint64_t at=76,start;uint32_t version,tr,ni,i;bool ok=false;
 M16_NEED(m16_load(f,&b,pd)&&m16_tag(&b,0,"sopepos",7)&&m16_span(&b,0,76));version=(uint32_t)b.p[7]|(uint32_t)b.p[8]<<8|(uint32_t)b.p[9]<<16;tr=b.p[73];ni=b.p[74];
 M16_NEED((version==0x100||version==0x200)&&b.p[54]<=1&&!b.p[55]&&b.p[56]&&!b.p[57]&&b.p[58]&&tr&&tr<=24&&ni&&ni<=128&&!b.p[75]&&m16_emit(f,s,&b,"descriptor.sop",0,76)&&m16_span(&b,at,tr));
 for(i=0;i<tr;++i)M16_NEED((b.p[(size_t)at+i]&127)<=2);M16_NEED(m16_emit(f,s,&b,"channel-modes.sop",at,tr));at+=tr;
 for(i=0;i<ni;++i){uint8_t type;uint64_t z;start=at;M16_NEED(m16_span(&b,at,28)&&m16_work(&b,1));type=b.p[(size_t)at];at+=28;M16_NEED(type==0||type==1||(type>=6&&type<=12));z=type==0?22:type==12?0:11;
  if(type==11){M16_NEED(version==0x200&&m16_span(&b,at,19));z=19+pm_le16(b.p+(size_t)at+4);}M16_NEED(m16_span(&b,at,z));at+=z;M16_NEED(m16_emit(f,s,&b,"instrument.sop",start,at-start));}
 for(i=0;i<=tr;++i){uint32_t count,k;uint64_t end;start=at;M16_NEED(m16_span(&b,at,6));count=pm_le16(b.p+(size_t)at);end=at+6+pm_le32(b.p+(size_t)at+2);at+=6;M16_NEED(end>=at&&end<=b.n);
  for(k=0;k<count;++k){uint8_t type,v;uint32_t z;M16_NEED(at<=end&&end-at>=4&&m16_work(&b,1));type=b.p[(size_t)at+2];v=b.p[(size_t)at+3];z=type==2?6:4;M16_NEED(type>=1&&type<=8&&end-at>=z);if(type==2||type==4||type==8)M16_NEED(v<=127);if(type==6)M16_NEED(v<ni);if(type==7)M16_NEED(version==0x100?v<=2:(v==0||v==64||v==128));at+=z;}
  M16_NEED(at==end&&m16_emit(f,s,&b,i==tr?"control-track.sop":"event-track.sop",start,at-start));}
 M16_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_sop_init(xx_adlib_sop *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_SOP,"adlib_sop");}}
xx_adlib_sop *xx_adlib_sop_create(xx_io_device *d,int64_t b) {xx_adlib_sop *r=(xx_adlib_sop *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_sop_init(r,d,b);return r;}
void xx_adlib_sop_destroy(xx_adlib_sop *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_sop_free(xx_adlib_sop *r) {if(r){xx_adlib_sop_destroy(r);xx_mem_free(r);}}
bool xx_adlib_sop_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_sop_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
