/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.biosemi.com/faq/file_format.htm */
#include "xxfclib/formats/biomedical_bdf/xx_biomedical_bdf.h"
#include "../xx_seventh_data.h"
static bool bdf_float(const uint8_t *p,size_t n,bool positive) {char b[32],*v;if(n>=sizeof(b)) return false;xx_rt_memcpy(b,p,n);b[n]=0;v=sd_trim(b);return sd_float_token(v) && (!positive || sd_positive_float(v));}
static bool bdf_signed(const uint8_t *p,size_t n,int64_t *v) {char b[32],*t;uint64_t z;bool neg;if(n>=sizeof(b)) return false;xx_rt_memcpy(b,p,n);b[n]=0;t=sd_trim(b);neg=*t=='-';if(*t=='-' || *t=='+') ++t;if(!sd_uint(t,&z) || z>8388608 || (!neg && z>8388607)) return false;*v=neg?-(int64_t)z:(int64_t)z;return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[256],b[80];uint64_t ns,records,header,samples[512],row=0,n;unsigned i,j;int64_t available=pm_available(f);
    if(fd_stop(pd) || available<256 || !pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"\xff" "BIOSEMI",8) || !sd_fixed_uint(h+252,4,&ns) || !ns || ns>512 || !sd_fixed_uint(h+236,8,&records) || !records || records>4096 || !sd_fixed_uint(h+184,8,&header) || header!=256*(ns+1) || !bdf_float(h+244,8,true)) return false;
    for(i=1;i<sizeof(h);++i) if(h[i]<32 || h[i]>126) return false;
    if(xx_rt_memcmp(h+192,"24BIT",5)) return false;for(i=197;i<236;++i) if(h[i]!=' ') return false;
    for(i=0;i<16;++i) {uint8_t ch=h[168+i];if(i==2 || i==5 || i==10 || i==13) {if(ch!='.') return false;} else if(ch<'0' || ch>'9') return false;}
    if(header>(uint64_t)available || records*ns>4096) return false;
    for(i=0;i<ns;++i) {int64_t lo,hi;uint64_t at;
        if(fd_stop(pd) || !pm_read(f,(int64_t)(256+216*ns+8*i),b,8) || !sd_fixed_uint(b,8,&samples[i]) || !samples[i] || samples[i]>1000000 || !fd_mul(samples[i],3,&n) || row>(uint64_t)INT64_MAX-n) return false;row+=n;
        at=256+104*ns+8*i;if(!pm_read(f,(int64_t)at,b,8) || !bdf_float(b,8,false) || !pm_read(f,(int64_t)(at+8*ns),b,8) || !bdf_float(b,8,false)) return false;
        at=256+120*ns+8*i;if(!pm_read(f,(int64_t)at,b,8) || !bdf_signed(b,8,&lo) || !pm_read(f,(int64_t)(at+8*ns),b,8) || !bdf_signed(b,8,&hi) || hi<=lo) return false;
    }
    /* Every signal-header byte must be printable ASCII. */
    for(n=256;n<header;) {size_t z=header-n>sizeof(b)?sizeof(b):(size_t)(header-n);if(fd_stop(pd) || !pm_read(f,(int64_t)n,b,z)) return false;for(i=0;i<z;++i) if(b[i]<32 || b[i]>126) return false;n+=z;}
    if(!fd_mul(row,records,&n) || !fd_range(header,n,(uint64_t)available) || !pm_add(f,s,"bdf-header.txt",0,(int64_t)header)) return false;
    n=header;
    for(j=0;j<records;++j) for(i=0;i<ns;++i) {char label[64];xx_rt_snprintf(label,sizeof(label),"record-%u-signal-%u.bin",j,i);if(fd_stop(pd) || !pm_add(f,s,label,(int64_t)n,(int64_t)(samples[i]*3))) return false;n+=samples[i]*3;}
    s->size=(int64_t)n;return true;
}

void xx_biomedical_bdf_init(xx_biomedical_bdf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_BIOMEDICAL_BDF,"biomedical_bdf"); } }
xx_biomedical_bdf *xx_biomedical_bdf_create(xx_io_device *d,int64_t b) { xx_biomedical_bdf *r=(xx_biomedical_bdf *)xx_mem_alloc(sizeof(*r)); if(r) xx_biomedical_bdf_init(r,d,b); return r; }
void xx_biomedical_bdf_destroy(xx_biomedical_bdf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_biomedical_bdf_free(xx_biomedical_bdf *r) { if(r) { xx_biomedical_bdf_destroy(r); xx_mem_free(r); } }
bool xx_biomedical_bdf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_biomedical_bdf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
