/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/svi-opensource/libics */
#include "xxfclib/formats/microscopy_ics/xx_microscopy_ics.h"
#include "../xx_eighth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0};char line[2048],*v[16],order[8][32];unsigned n=0,params=0,bits=0,seen=0,lines=0;uint64_t pixels=1,bytes;bool real=false;
    if(c.end>67108864 || !sd_line(&c,line,sizeof(line)) || xx_rt_strcmp(line,"\t") || !sd_line(&c,line,sizeof(line)) || xx_rt_strcmp(line,"ics_version\t2.0")) return false;
    while(c.at<65536 && ++lines<=512) {uint64_t x;unsigned i,flag=0;if(!sd_line(&c,line,sizeof(line))) return false;n=sv_tokens(line,v,16);if(!n || n>16) return false;
        if(n==1 && !xx_rt_strcmp(v[0],"end")) break;
        if(!xx_rt_strcmp(v[0],"filename")) {if(n!=2 || xx_rt_strlen(v[1])>255) return false;flag=1;}
        else if(!xx_rt_strcmp(v[0],"layout")) {
            if(n<3) return false;
            if(!xx_rt_strcmp(v[1],"parameters")) {if(n!=3 || !sd_uint(v[2],&x) || x<2 || x>8) return false;params=(unsigned)x;flag=2;}
            else if(!xx_rt_strcmp(v[1],"order")) {if(!params || n!=params+2 || xx_rt_strcmp(v[2],"bits")) return false;for(i=0;i<params;++i) {unsigned j;if(xx_rt_strlen(v[i+2])>=32) return false;for(j=0;j<i;++j) if(!xx_rt_strcmp(order[j],v[i+2])) return false;xx_rt_memcpy(order[i],v[i+2],xx_rt_strlen(v[i+2])+1);}flag=4;}
            else if(!xx_rt_strcmp(v[1],"sizes")) {if(!params || n!=params+2 || !sd_uint(v[2],&x) || !(x==8 || x==16 || x==32 || x==64)) return false;bits=(unsigned)x;for(i=3;i<n;++i) if(!sd_uint(v[i],&x) || !x || !fd_mul(pixels,x,&pixels) || pixels>16777216) return false;flag=8;}
            else if(!xx_rt_strcmp(v[1],"coordinates")) {if(n!=3 || (xx_rt_strcmp(v[2],"video") && xx_rt_strcmp(v[2],"cartesian"))) return false;flag=16;}
            else if(!xx_rt_strcmp(v[1],"significant_bits")) {if(n!=3 || !bits || !sd_uint(v[2],&x) || !x || x>bits) return false;flag=32;}
            else return false;
        }else if(!xx_rt_strcmp(v[0],"representation")) {
            if(n<3) return false;
            if(!xx_rt_strcmp(v[1],"format")) {if(n!=3 || (xx_rt_strcmp(v[2],"integer") && xx_rt_strcmp(v[2],"real"))) return false;real=!xx_rt_strcmp(v[2],"real");flag=64;}
            else if(!xx_rt_strcmp(v[1],"sign")) {if(n!=3 || (xx_rt_strcmp(v[2],"signed") && xx_rt_strcmp(v[2],"unsigned"))) return false;flag=128;}
            else if(!xx_rt_strcmp(v[1],"compression")) {if(n!=3 || xx_rt_strcmp(v[2],"uncompressed")) return false;flag=256;}
            else if(!xx_rt_strcmp(v[1],"byte_order")) {unsigned used=0;if(!bits || n!=2+bits/8) return false;for(i=2;i<n;++i) {if(!sd_uint(v[i],&x) || !x || x>bits/8 || (used&(1U<<(unsigned)x))) return false;used|=1U<<(unsigned)x;}flag=512;}
            else if(!xx_rt_strcmp(v[1],"SCIL_TYPE")) {if(n!=3) return false;flag=1024;}else return false;
        }else if(!xx_rt_strcmp(v[0],"parameter")) {if(n<3 || !(seen&8)) return false;if(!xx_rt_strcmp(v[1],"origin") || !xx_rt_strcmp(v[1],"scale")) {if(n!=params+2) return false;for(i=2;i<n;++i) if(!sd_float_token(v[i])) return false;}
            else if(!xx_rt_strcmp(v[1],"units") || !xx_rt_strcmp(v[1],"labels")) {if(n!=params+2) return false;}else return false;
        }else if(xx_rt_strcmp(v[0],"history")) return false;
        if(flag && (seen&flag)) return false;seen|=flag;
    }
    if(lines>512 || c.at>65536 || n!=1 || xx_rt_strcmp(v[0],"end") || (seen&974)!=974 || (real && bits!=32 && bits!=64) || !fd_mul(pixels,bits/8,&bytes) || !eh_span(c.at,bytes,c.end) || c.at+bytes!=c.end || !pm_add(f,s,"ics-header.txt",0,(int64_t)c.at) || !pm_add(f,s,"pixels.bin",(int64_t)c.at,(int64_t)bytes)) return false;
    s->size=(int64_t)c.end;return true;
}

void xx_microscopy_ics_init(xx_microscopy_ics *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MICROSCOPY_ICS,"microscopy_ics"); } }
xx_microscopy_ics *xx_microscopy_ics_create(xx_io_device *d,int64_t b) { xx_microscopy_ics *r=(xx_microscopy_ics *)xx_mem_alloc(sizeof(*r)); if(r) xx_microscopy_ics_init(r,d,b); return r; }
void xx_microscopy_ics_destroy(xx_microscopy_ics *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_microscopy_ics_free(xx_microscopy_ics *r) { if(r) { xx_microscopy_ics_destroy(r); xx_mem_free(r); } }
bool xx_microscopy_ics_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_microscopy_ics_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
