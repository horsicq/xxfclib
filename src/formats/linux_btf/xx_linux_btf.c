/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://docs.kernel.org/bpf/btf.html */
#include "xxfclib/formats/linux_btf/xx_linux_btf.h"
#include "../microsoft_msf/xx_tenth_containers.h"

static bool bt_name(nh_blob *b,uint64_t strings,uint64_t length,uint32_t offset,bool empty) {
    uint64_t at=strings+offset;if(offset>=length) return false;return th_z(b,&at,strings+length,empty);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b;uint64_t h,types,tn,strings,sn,at,end,i,j,tail,count=0;uint32_t *offsets=NULL;bool ok=false;
    if(!nh_load(f,&b,pd)) return false;NH_NEED(nh_span(&b,0,24) && pm_le16(b.p)==0xeb9f && b.p[2]==1 && !b.p[3]);h=pm_le32(b.p+4);types=pm_le32(b.p+8);tn=pm_le32(b.p+12);strings=pm_le32(b.p+16);sn=pm_le32(b.p+20);
    NH_NEED(h>=24 && h<=65536 && !types && strings==tn && sn && nh_span(&b,h,tn) && nh_span(&b,h+strings,sn) && h+tn+sn==b.n);types+=h;strings+=h;NH_NEED(!b.p[(size_t)strings] && !b.p[(size_t)(strings+sn-1)]);at=strings;
    while(at<strings+sn) NH_NEED(th_z(&b,&at,strings+sn,true));
    offsets=(uint32_t *)xx_mem_alloc((size_t)(tn/12+1)*sizeof(*offsets));NH_NEED(offsets);offsets[0]=0;end=types+tn;
    for(at=types;at<end;at+=12+tail) {uint32_t info,kind,vlen,name;NH_NEED(nh_span(&b,at,12) && eh_span(at,12,end) && count<1000000);info=pm_le32(b.p+(size_t)at+4);kind=(info>>24)&31;vlen=info&65535;name=pm_le32(b.p+(size_t)at);NH_NEED(!(info&0x60ff0000U) && kind>=1 && kind<=19 && bt_name(&b,strings,sn,name,true));
        offsets[++count]=(uint32_t)at;tail=0;
        if(kind==1 || kind==14 || kind==17) tail=4;else if(kind==3) tail=12;else if(kind==4 || kind==5 || kind==15 || kind==19) tail=(uint64_t)vlen*12;else if(kind==6 || kind==13) tail=(uint64_t)vlen*8;
        NH_NEED(eh_span(at+12,tail,end));
        if(kind!=4 && kind!=5 && kind!=6 && kind!=13 && kind!=15 && kind!=19 && kind!=12) NH_NEED(!vlen);
        if(kind==12) NH_NEED(vlen<=2);
        if(kind!=4 && kind!=5 && kind!=6 && kind!=7 && kind!=17 && kind!=18 && kind!=19) NH_NEED(!(info&0x80000000U));
    }NH_NEED(at==end);
    for(i=1;i<=count;++i) {uint32_t info,kind,vlen,x,name;at=offsets[i];name=pm_le32(b.p+(size_t)at);info=pm_le32(b.p+(size_t)at+4);kind=(info>>24)&31;vlen=info&65535;x=pm_le32(b.p+(size_t)at+8);NH_NEED(!fd_stop(pd));
        if(kind==2 || (kind>=8 && kind<=14) || kind==17 || kind==18) NH_NEED(x<=count);
        if(kind==1) {uint32_t enc=pm_le32(b.p+(size_t)at+12);NH_NEED(x && x<=16 && !(enc&0xf800ff00U) && (enc&255) && (uint64_t)((enc>>16)&255)+(enc&255)<=(uint64_t)x*8);}
        if(kind==3) NH_NEED(pm_le32(b.p+(size_t)at+12)>0 && pm_le32(b.p+(size_t)at+12)<=count && pm_le32(b.p+(size_t)at+16)>0 && pm_le32(b.p+(size_t)at+16)<=count);
        if(kind==6 || kind==19) NH_NEED(x==1 || x==2 || x==4 || x==8);
        if(kind==7) NH_NEED(!x && name);
        if(kind==14) NH_NEED(x && pm_le32(b.p+(size_t)at+12)<=2);
        if(kind==16) NH_NEED(x==2 || x==4 || x==8 || x==12 || x==16);
        for(j=0;j<vlen;++j) {uint64_t p=at+12+j*(kind==6 || kind==13 ? 8:12);if(kind==4 || kind==5 || kind==6 || kind==13 || kind==19) NH_NEED(bt_name(&b,strings,sn,pm_le32(b.p+(size_t)p),kind==4 || kind==5 || kind==13));
            if(kind==4 || kind==5 || kind==13) NH_NEED(pm_le32(b.p+(size_t)p+4)<=count);
            if(kind==4 || kind==5) {uint32_t bits=pm_le32(b.p+(size_t)p+8);uint64_t bitoff=info&0x80000000U ? bits&0xffffffU:bits;NH_NEED(bitoff<=(uint64_t)x*8 && (!(info&0x80000000U) || bitoff+(bits>>24)<=(uint64_t)x*8));}
            if(kind==15) {uint32_t ref=pm_le32(b.p+(size_t)p);uint64_t pos=pm_le32(b.p+(size_t)p+4),len=pm_le32(b.p+(size_t)p+8);NH_NEED(ref && ref<=count && ((pm_le32(b.p+offsets[ref]+4)>>24)&31)==14 && (!x || eh_span(pos,len,x)));}
        }
        if(kind==17) {uint32_t component=pm_le32(b.p+(size_t)at+12),k;NH_NEED(x && x<=count);k=(pm_le32(b.p+offsets[x]+4)>>24)&31;NH_NEED(k==4 || k==5 || k==8 || k==12 || k==14);if(component!=0xffffffffU) {uint32_t target=x;if(k==12) target=pm_le32(b.p+offsets[x]+8);NH_NEED(target && target<=count && component<(pm_le32(b.p+offsets[target]+4)&65535));}}
    }
    NH_NEED(nh_add(f,s,&b,"btf-header",0,h) && nh_add(f,s,&b,"types",types,tn) && nh_add(f,s,&b,"strings",strings,sn));s->size=(int64_t)b.n;ok=true;
done:if(offsets) xx_mem_free(offsets);xx_mem_free(b.p);return ok;
}
void xx_linux_btf_init(xx_linux_btf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LINUX_BTF,"btf"); } }
xx_linux_btf *xx_linux_btf_create(xx_io_device *d,int64_t b) { xx_linux_btf *r=(xx_linux_btf *)xx_mem_alloc(sizeof(*r)); if(r) xx_linux_btf_init(r,d,b); return r; }
void xx_linux_btf_destroy(xx_linux_btf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_linux_btf_free(xx_linux_btf *r) { if(r) { xx_linux_btf_destroy(r); xx_mem_free(r); } }
bool xx_linux_btf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_linux_btf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
