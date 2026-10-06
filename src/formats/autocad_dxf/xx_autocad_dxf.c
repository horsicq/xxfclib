/* SPDX-License-Identifier: MIT
 * Independently implemented from https://images.autodesk.com/adsk/files/autocad_2014_pdf_dxf_reference_enu.pdf */
#include "xxfclib/formats/autocad_dxf/xx_autocad_dxf.h"
#include "../xx_ninth_data.h"

static unsigned group_kind(uint16_t c) {
    if((c>=10 && c<=59) || (c>=110 && c<=149) || (c>=210 && c<=239) || (c>=460 && c<=469) || (c>=1010 && c<=1059)) return 8;
    if((c>=60 && c<=79) || (c>=170 && c<=179) || (c>=270 && c<=279) || (c>=370 && c<=389) || (c>=400 && c<=409) || c==1060 || c==1070) return 2;
    if((c>=90 && c<=99) || (c>=420 && c<=429) || (c>=440 && c<=459) || c==1071) return 4;
    if(c>=160 && c<=169) return 9;
    if(c>=280 && c<=299) return 1;
    if((c>=310 && c<=319) || c==1004) return 3;
    if(c<=9 || c==100 || c==102 || c==105 || (c>=300 && c<=309) || (c>=320 && c<=369) || (c>=390 && c<=399) || (c>=410 && c<=419) || (c>=430 && c<=439) || (c>=470 && c<=481) || c==999 || (c>=1000 && c<=1009)) return 0;
    return 255;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[22];nh_blob b={0};bool ok=false,in_section=false,want_name=false,eof=false;uint64_t at=22,start=0;unsigned tags=0,sections=0;char section[256];
    if(!pm_read(f,0,h,22) || xx_rt_memcmp(h,"AutoCAD Binary DXF\r\n\x1a\0",22)) { return false; } NH_NEED(nh_load(f,&b,pd) && nh_add(f,s,&b,"sentinel",0,22));
    while(at<b.n) {uint16_t code;unsigned kind;uint64_t value;char text[1024]={0};NH_NEED(++tags<=1000000 && nh_span(&b,at,2) && !eof);code=pm_le16(b.p+(size_t)at);kind=group_kind(code);at+=2;value=at;NH_NEED(kind!=255);
        if(!kind) {size_t n=0;while(at<b.n && b.p[(size_t)at]) {NH_NEED(n+1<sizeof(text));text[n++]=(char)b.p[(size_t)at++];}NH_NEED(at<b.n && nh_ascii((const uint8_t *)text,n,false));++at;}
        else if(kind==3) {uint8_t n;NH_NEED(nh_span(&b,at,1));n=b.p[(size_t)at++];NH_NEED(nh_span(&b,at,n));at+=n;}
        else {unsigned width=kind==9 ? 8:kind;NH_NEED(nh_span(&b,at,width));if(kind==8) NH_NEED(nh_floats(&b,at,8,8,false));if(code>=290 && code<=299) NH_NEED(b.p[(size_t)at]<=1);at+=width;}
        if(want_name) {NH_NEED(code==2 && text[0] && xx_rt_strlen(text)<sizeof(section));xx_rt_memcpy(section,text,xx_rt_strlen(text)+1);want_name=false;}
        else if(!code && !xx_rt_strcmp(text,"SECTION")) {NH_NEED(!in_section && ++sections<=128);in_section=true;want_name=true;start=value-2;}
        else if(!code && !xx_rt_strcmp(text,"ENDSEC")) {NH_NEED(in_section && !want_name && nh_add(f,s,&b,section,start,at-start));in_section=false;}
        else if(!code && !xx_rt_strcmp(text,"EOF")) {NH_NEED(!in_section && sections && at==b.n && nh_add(f,s,&b,"eof",value-2,at-value+2));eof=true;}
        else NH_NEED(in_section);
    }NH_NEED(eof);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_autocad_dxf_init(xx_autocad_dxf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_AUTOCAD_DXF,"autocad_dxf"); } }
xx_autocad_dxf *xx_autocad_dxf_create(xx_io_device *d,int64_t b) { xx_autocad_dxf *r=(xx_autocad_dxf *)xx_mem_alloc(sizeof(*r)); if(r) xx_autocad_dxf_init(r,d,b); return r; }
void xx_autocad_dxf_destroy(xx_autocad_dxf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_autocad_dxf_free(xx_autocad_dxf *r) { if(r) { xx_autocad_dxf_destroy(r); xx_mem_free(r); } }
bool xx_autocad_dxf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_autocad_dxf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
