/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://dicom.nema.org/medical/dicom/current/output/html/part10.html, https://dicom.nema.org/medical/dicom/current/output/html/part05.html
 * Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/dicom/xx_dicom.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

typedef struct dc_context { Abstractformat *f; pm_stream *s; xx_pd_struct *pd; bool be,implicit,encapsulated; uint32_t rows,columns,samples,bits,frames; bool pixels; unsigned dataset_ids; char sop[65],instance[65]; } dc_context;
static uint16_t dc16(dc_context *c,const uint8_t *p) { return c->be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static uint32_t dc32(dc_context *c,const uint8_t *p) { return c->be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static bool dc_uid(Abstractformat *f,int64_t at,uint32_t size,char *text) {
    unsigned i,n=size; uint8_t b[64]; if(!size || size>64 || !pm_read(f,at,b,size)) return false;
    if(!b[n-1]) { --n; } if(!n || b[0]=='.' || b[n-1]=='.') return false;
    for(i=0;i<n;++i) { if((b[i]<'0' || b[i]>'9') && b[i]!='.') return false; if(i && b[i]=='.' && b[i-1]=='.') return false; text[i]=(char)b[i]; } text[n]=0; return true;
}
static bool dc_vr(const uint8_t *p,bool *wide,unsigned *alignment) {
    static const char values[][3]={"AE","AS","AT","CS","DA","DS","DT","FD","FL","IS","LO","LT","OB","OD","OF","OL","OV","OW","PN","SH","SL","SQ","SS","ST","SV","TM","UC","UI","UL","UN","UR","US","UT","UV"}; unsigned i;
    for(i=0;i<sizeof(values)/sizeof(values[0]);++i) { if(p[0]==values[i][0] && p[1]==values[i][1]) break; } if(i==sizeof(values)/sizeof(values[0])) return false;
    *wide=(p[0]=='O') || (p[0]=='S' && p[1]=='Q') || (p[0]=='U' && (p[1]=='C' || p[1]=='N' || p[1]=='R' || p[1]=='T'));
    *alignment=1;
    if(((p[0]=='U' || p[0]=='S') && p[1]=='S') || (p[0]=='O' && p[1]=='W')) *alignment=2;
    if(((p[0]=='U' || p[0]=='S') && p[1]=='L') || (p[0]=='F' && p[1]=='L') || (p[0]=='O' && (p[1]=='F' || p[1]=='L')) || (p[0]=='A' && p[1]=='T')) *alignment=4;
    if((p[0]=='F' && p[1]=='D') || (p[0]=='O' && (p[1]=='D' || p[1]=='V')) || ((p[0]=='S' || p[0]=='U') && p[1]=='V')) *alignment=8;
    return true;
}
static bool dc_header(dc_context *c,int64_t at,int64_t end,uint32_t *tag,int64_t *body,uint32_t *length,bool *sequence,uint8_t vr[2]) {
    uint8_t h[12]; bool wide=false; unsigned alignment=1;
    if(end-at<8 || !pm_read(c->f,at,h,8)) { return false; } *tag=(uint32_t)dc16(c,h)<<16|dc16(c,h+2); *sequence=false; vr[0]=vr[1]=0;
    if((*tag>>16)==0xFFFE || c->implicit) { *body=at+8; *length=dc32(c,h+4); }
    else { if(!dc_vr(h+4,&wide,&alignment)) return false; vr[0]=h[4]; vr[1]=h[5]; *sequence=h[4]=='S' && h[5]=='Q';
        if(wide) { if(h[6] || h[7] || end-at<12 || !pm_read(c->f,at+8,h+8,4)) return false; *length=dc32(c,h+8); *body=at+12; }
        else { *length=dc16(c,h+6); *body=at+8; }
    }
    return *length==UINT32_MAX || (!(*length&1U) && *length%alignment==0 && *length<=(uint64_t)(end-*body));
}
static bool dc_data(dc_context *,int64_t *,int64_t,unsigned,bool,bool);
static bool dc_sequence(dc_context *c,int64_t *at,int64_t end,unsigned depth,bool undefined) {
    while(*at<end) { uint32_t tag,n; int64_t body; bool sequence; uint8_t vr[2];
        if((c->pd && xx_pd_is_stopped(c->pd)) || !dc_header(c,*at,end,&tag,&body,&n,&sequence,vr)) return false;
        if(tag==0xFFFEE0DDU) { if(!undefined || n) return false; *at=body; return true; }
        if(tag!=0xFFFEE000U) { return false; } *at=body;
        if(n==UINT32_MAX) { if(!dc_data(c,at,end,depth+1,true,false)) return false; }
        else { int64_t stop=body+n; if(!dc_data(c,at,stop,depth+1,false,false) || *at!=stop) return false; }
    } return !undefined && *at==end;
}
static bool dc_fragments(dc_context *c,int64_t *at,int64_t end) {
    uint8_t h[8]; uint32_t offsets[4096],count=0,seen=0,fragments=0; int64_t first; bool sequence; uint8_t vr[2]; uint32_t tag,n; int64_t body;
    if(!c->encapsulated || !dc_header(c,*at,end,&tag,&body,&n,&sequence,vr) || tag!=0xFFFEE000U || n==UINT32_MAX || n%4 || n/4>4096) { return false; } count=n/4;
    for(seen=0;seen<count;++seen) { if(!pm_read(c->f,body+(int64_t)seen*4,h,4)) return false; offsets[seen]=dc32(c,h); if((seen && offsets[seen]<=offsets[seen-1]) || (!seen && offsets[seen])) return false; }
    if(!pm_add(c->f,c->s,"pixel-offset-table.bin",body,n)) { return false; } *at=body+n; first=*at; seen=0;
    while(*at<end) { char name[48];
        if((c->pd && xx_pd_is_stopped(c->pd)) || !dc_header(c,*at,end,&tag,&body,&n,&sequence,vr)) return false;
        if(tag==0xFFFEE0DDU) { if(n || !fragments || seen!=count) return false; *at=body; return true; }
        if(tag!=0xFFFEE000U || n==UINT32_MAX || !n || fragments==4096) return false;
        if(seen<count) { uint64_t position=(uint64_t)(*at-first); if(offsets[seen]<position) return false; if(offsets[seen]==position) ++seen; }
        xx_rt_snprintf(name,sizeof(name),"pixel-fragment-%u.bin",fragments++); if(!pm_add(c->f,c->s,name,body,n)) return false; *at=body+n;
    } return false;
}
static bool dc_data(dc_context *c,int64_t *at,int64_t end,unsigned depth,bool item_undefined,bool top) {
    uint32_t previous=0; if(depth>32) return false;
    while(*at<end) { uint32_t tag,n; int64_t body,stop; bool sequence; uint8_t vr[2]; char name[48];
        if((c->pd && xx_pd_is_stopped(c->pd)) || !dc_header(c,*at,end,&tag,&body,&n,&sequence,vr)) return false;
        if(tag==0xFFFEE00DU) { if(!item_undefined || n) return false; *at=body; return true; }
        if((tag>>16)==0xFFFE || (tag>>16)==2 || tag<=previous) { return false; } previous=tag; stop=n==UINT32_MAX ? end : body+n;
        if(sequence) { *at=body; if(!dc_sequence(c,at,stop,depth,n==UINT32_MAX)) return false; continue; }
        if(tag==0x7FE00010U && n==UINT32_MAX) { if(!top || c->pixels) return false; c->pixels=true; *at=body; if(!dc_fragments(c,at,end)) return false; continue; }
        if(n==UINT32_MAX) return false;
        if(top && (tag==0x00080016U || tag==0x00080018U)) { char uid[65]; if(!dc_uid(c->f,body,n,uid) || xx_rt_strcmp(uid,tag==0x00080016U ? c->sop : c->instance)) return false; c->dataset_ids|=tag==0x00080016U ? 1U : 2U; }
        if(top && (tag==0x00280002U || tag==0x00280010U || tag==0x00280011U || tag==0x00280100U)) { uint8_t b[2]; if(n!=2 || !pm_read(c->f,body,b,2)) return false;
            if(tag==0x00280002U) c->samples=dc16(c,b); else if(tag==0x00280010U) c->rows=dc16(c,b); else if(tag==0x00280011U) c->columns=dc16(c,b); else c->bits=dc16(c,b); }
        if(top && tag==0x00280008U) { uint8_t b[16]; unsigned i; uint32_t frames=0; if(!n || n>16 || !pm_read(c->f,body,b,n)) return false;
            for(i=0;i<n && b[i]>='0' && b[i]<='9';++i) { if(frames>(UINT32_MAX-(b[i]-'0'))/10U) return false; frames=frames*10U+b[i]-'0'; }
            while(i<n && b[i]==' ') { ++i; } if(i!=n || !frames) return false; c->frames=frames; }
        if(top && tag==0x7FE00010U) { uint64_t pixels,bytes;
            if(c->pixels || c->encapsulated || !c->rows || !c->columns || !c->samples || (c->bits!=1 && c->bits!=8 && c->bits!=16)) return false;
            pixels=(uint64_t)c->rows*c->columns*c->samples; if(pixels>UINT64_MAX/c->frames || pixels*c->frames>UINT64_MAX/c->bits) return false; pixels*=c->frames; pixels*=c->bits; bytes=pixels/8+(pixels%8!=0);
            if(bytes+(bytes&1U)!=n) { return false; } c->pixels=true;
        }
        xx_rt_snprintf(name,sizeof(name),"tag-%08X.bin",tag); if(!pm_add(c->f,c->s,name,body,n)) return false; *at=stop;
    } return !item_undefined && *at==end;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    dc_context c; uint8_t h[16]; uint32_t meta_size,mask=0,previous=0; int64_t at=132,meta_end,limit=pm_available(f); char syntax[65];
    xx_mem_zero(&c,sizeof(c)); c.f=f; c.s=s; c.pd=pd; c.frames=1;
    if(!pm_read(f,128,h,16) || xx_rt_memcmp(h,"DICM",4) || xx_data_get_u16(h+4, 2, 0, false)!=2 || xx_data_get_u16(h+6, 2, 0, false) || h[8]!='U' || h[9]!='L' || xx_data_get_u16(h+10, 2, 0, false)!=4) return false;
    meta_size=xx_data_get_u32(h+12, 4, 0, false); at+=12; if(meta_size>(uint64_t)(limit-at)) return false; meta_end=at+meta_size;
    while(at<meta_end) { uint32_t tag,n; int64_t body; bool sequence; uint8_t vr[2]; char name[48];
        if((pd && xx_pd_is_stopped(pd)) || !dc_header(&c,at,meta_end,&tag,&body,&n,&sequence,vr) || (tag>>16)!=2 || tag<=previous || n==UINT32_MAX || sequence || (vr[0]=='U' && vr[1]=='N')) { return false; } previous=tag;
        if(tag==0x00020001U) { if(n!=2 || vr[0]!='O' || vr[1]!='B' || !pm_read(f,body,h,2) || !(h[1]&1)) return false; mask|=1; }
        if(tag==0x00020002U || tag==0x00020003U || tag==0x00020010U || tag==0x00020012U) { char value[65]; if(vr[0]!='U' || vr[1]!='I' || !dc_uid(f,body,n,value)) return false;
            if(tag==0x00020002U) { xx_rt_memcpy(c.sop,value,xx_rt_strlen(value)+1); mask|=2; }
            else if(tag==0x00020003U) { xx_rt_memcpy(c.instance,value,xx_rt_strlen(value)+1); mask|=4; }
            else if(tag==0x00020010U) { xx_rt_memcpy(syntax,value,xx_rt_strlen(value)+1); mask|=8; }
            else mask|=16;
        }
        xx_rt_snprintf(name,sizeof(name),"meta-%08X.bin",tag); if(!pm_add(f,s,name,body,n)) return false; at=body+n;
    }
    if(mask!=31) return false;
    if(!xx_rt_strcmp(syntax,"1.2.840.10008.1.2")) c.implicit=true;
    else if(!xx_rt_strcmp(syntax,"1.2.840.10008.1.2.1")) {}
    else if(!xx_rt_strcmp(syntax,"1.2.840.10008.1.2.2")) c.be=true;
    else { static const char *const enc[]={"1.2.840.10008.1.2.4.50","1.2.840.10008.1.2.4.51","1.2.840.10008.1.2.4.57","1.2.840.10008.1.2.4.70","1.2.840.10008.1.2.4.80","1.2.840.10008.1.2.4.81","1.2.840.10008.1.2.4.90","1.2.840.10008.1.2.4.91","1.2.840.10008.1.2.5"}; unsigned i;
        for(i=0;i<sizeof(enc)/sizeof(enc[0]);++i) { if(!xx_rt_strcmp(syntax,enc[i])) break; } if(i==sizeof(enc)/sizeof(enc[0])) return false; c.encapsulated=true; }
    if(at==limit || !dc_data(&c,&at,limit,0,false,true) || c.dataset_ids!=3) return false;
    if(c.pixels && (!c.rows || !c.columns || !c.samples || !c.bits)) { return false; } s->size=at; return true;
}

void xx_dicom_init(xx_dicom *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_DICOM,"dicom"); } }
xx_dicom *xx_dicom_create(xx_io_device *d,int64_t b) { xx_dicom *r=(xx_dicom *)xx_mem_alloc(sizeof(*r)); if(r) xx_dicom_init(r,d,b); return r; }
void xx_dicom_destroy(xx_dicom *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_dicom_free(xx_dicom *r) { if(r) { xx_dicom_destroy(r); xx_mem_free(r); } }
bool xx_dicom_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_dicom_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
