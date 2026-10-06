/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.sas.com/content/dam/sasdam/documents/20260302/record-layout-of-a-sas-version-5-or-6-data-set-in-sas-transport-xport-format.pdf */
#include "xxfclib/formats/sas_xport/xx_sas_xport.h"
#include "../xx_fifth_data.h"

static bool xp_header(Abstractformat *f,int64_t at,const char *name,uint8_t *h) {
    size_t n=xx_rt_strlen(name); return n<=16 && pm_read(f,at,h,80) && !xx_rt_memcmp(h,"HEADER RECORD*******",20) && !xx_rt_memcmp(h+20,name,n) && !xx_rt_memcmp(h+28,"HEADER RECORD!!!!!!!",20);
}
static bool xp_digits(const uint8_t *p,unsigned n,uint32_t *value) { unsigned i; *value=0; for(i=0;i<n;++i) { if(p[i]<'0' || p[i]>'9') return false; *value=*value*10+(p[i]-'0'); } return true; }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[140],names[4096][8]; uint32_t count,descriptor,i,width=0; uint64_t dictend,obs,available=(uint64_t)pm_available(f);
    if(available<720 || available%80 || !xp_header(f,0,"LIBRARY ",h) || !fd_equal(f,80,"SAS     SAS     SASLIB  ",24) || !xp_header(f,240,"MEMBER  ",h) || !xp_digits(h+75,3,&descriptor) || descriptor!=140 || !xp_header(f,320,"DSCRPTR ",h) || !fd_equal(f,400,"SAS     ",8) || !fd_equal(f,416,"SASDATA ",8) || !xp_header(f,560,"NAMESTR ",h) || !xp_digits(h+54,4,&count) || !count || count>4096) return false;
    dictend=640+(uint64_t)count*140; obs=((dictend+79)/80)*80;
    if(!fd_range(640,(uint64_t)count*140,available) || !xp_header(f,(int64_t)obs,"OBS     ",h)) return false;
    for(i=0;i<count;++i) { uint32_t j; uint16_t type,len; if(fd_stop(pd) || !pm_read(f,640+(int64_t)i*140,h,140) || ((type=pm_be16(h))!=1 && type!=2) || pm_be16(h+2) || !(len=pm_be16(h+4)) || pm_be16(h+6)!=i+1 || pm_be32(h+84)!=width || !h[8] || h[8]==' ') return false;
        if(type==1 && (len<2 || len>8)) return false;
        for(j=0;j<8;++j) if(h[8+j]<32 || h[8+j]>126) return false;
        for(j=0;j<i;++j) { if(!xx_rt_memcmp(names[j],h+8,8)) return false; } xx_rt_memcpy(names[i],h+8,8); width+=len;
    }
    if(!width || obs+80>=available) return false;
    /* XPORT lacks an explicit observation count. Preserve its padded region,
       and reject another member rather than guessing an ambiguous boundary. */
    for(dictend=obs+80;dictend<available;dictend+=80) { if(fd_stop(pd)) return false; if(fd_equal(f,(int64_t)dictend,"HEADER RECORD*******MEMBER  ",28)) return false; }
    if(!pm_add(f,s,"xport-dictionary.bin",0,(int64_t)obs+80) || !pm_add(f,s,"observations-padded.bin",(int64_t)obs+80,(int64_t)(available-obs-80))) return false;
    s->size=(int64_t)available; return true;
}

void xx_sas_xport_init(xx_sas_xport *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SAS_XPORT,"sas_xport"); } }
xx_sas_xport *xx_sas_xport_create(xx_io_device *d,int64_t b) { xx_sas_xport *r=(xx_sas_xport *)xx_mem_alloc(sizeof(*r)); if(r) xx_sas_xport_init(r,d,b); return r; }
void xx_sas_xport_destroy(xx_sas_xport *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sas_xport_free(xx_sas_xport *r) { if(r) { xx_sas_xport_destroy(r); xx_mem_free(r); } }
bool xx_sas_xport_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sas_xport_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

