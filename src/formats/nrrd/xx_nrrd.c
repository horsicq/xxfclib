/* SPDX-License-Identifier: MIT
 * Independently implemented from https://teem.sourceforge.net/nrrd/format.html */
#include "xxfclib/formats/nrrd/xx_nrrd.h"
#include "../xx_sixth_data.h"

static unsigned nrrd_width(const char *p) {
    if(!xx_rt_strcmp(p,"char") || !xx_rt_strcmp(p,"signed char") || !xx_rt_strcmp(p,"int8") || !xx_rt_strcmp(p,"int8_t") || !xx_rt_strcmp(p,"uchar") || !xx_rt_strcmp(p,"unsigned char") || !xx_rt_strcmp(p,"uint8") || !xx_rt_strcmp(p,"uint8_t")) return 1;
    if(!xx_rt_strcmp(p,"short") || !xx_rt_strcmp(p,"short int") || !xx_rt_strcmp(p,"signed short") || !xx_rt_strcmp(p,"signed short int") || !xx_rt_strcmp(p,"int16") || !xx_rt_strcmp(p,"int16_t") || !xx_rt_strcmp(p,"ushort") || !xx_rt_strcmp(p,"unsigned short") || !xx_rt_strcmp(p,"unsigned short int") || !xx_rt_strcmp(p,"uint16") || !xx_rt_strcmp(p,"uint16_t")) return 2;
    if(!xx_rt_strcmp(p,"int") || !xx_rt_strcmp(p,"signed int") || !xx_rt_strcmp(p,"int32") || !xx_rt_strcmp(p,"int32_t") || !xx_rt_strcmp(p,"uint") || !xx_rt_strcmp(p,"unsigned int") || !xx_rt_strcmp(p,"uint32") || !xx_rt_strcmp(p,"uint32_t") || !xx_rt_strcmp(p,"float")) return 4;
    if(!xx_rt_strcmp(p,"longlong") || !xx_rt_strcmp(p,"long long") || !xx_rt_strcmp(p,"long long int") || !xx_rt_strcmp(p,"signed long long") || !xx_rt_strcmp(p,"signed long long int") || !xx_rt_strcmp(p,"int64") || !xx_rt_strcmp(p,"int64_t") || !xx_rt_strcmp(p,"ulonglong") || !xx_rt_strcmp(p,"unsigned long long") || !xx_rt_strcmp(p,"unsigned long long int") || !xx_rt_strcmp(p,"uint64") || !xx_rt_strcmp(p,"uint64_t") || !xx_rt_strcmp(p,"double")) { return 8; } return 0;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    char line[4096],sizes[2048]={0},seen[64][96];unsigned lines=0,fields=0,width=0;uint64_t dim=0,n;bool raw=false,endian=false,ended=false;int64_t available=pm_available(f);
    fd_cursor c={f,0,available>65536?65536:(available>0?(uint64_t)available:0),pd,0};
    if(!sd_line(&c,line,sizeof(line)) || xx_rt_strlen(line)!=8 || xx_rt_memcmp(line,"NRRD000",7) || line[7]<'1' || line[7]>'5') return false;
    while(++lines<=256 && sd_line(&c,line,sizeof(line))) {char *key,*value,*colon;unsigned i;
        if(!line[0]) {ended=true;break;}if(line[0]=='#') continue;
        colon=xx_rt_strchr(line,':');if(!colon) return false;if(colon[1]=='=') continue;*colon=0;key=sd_trim(line);value=sd_trim(colon+1);
        for(i=0;key[i];++i) if(key[i]>='A' && key[i]<='Z') key[i]+=32;
        if(!*key || xx_rt_strlen(key)>=96 || fields>=64) return false;
        for(i=0;i<fields;++i) { if(!xx_rt_strcmp(seen[i],key)) return false; } xx_rt_memcpy(seen[fields++],key,xx_rt_strlen(key)+1);
        if(!xx_rt_strcmp(key,"dimension")) {if(!sd_uint(value,&dim) || !dim || dim>16) return false;}
        else if(!xx_rt_strcmp(key,"type")) {for(i=0;value[i];++i) if(value[i]>='A' && value[i]<='Z') value[i]+=32;width=nrrd_width(value);if(!width) return false;}
        else if(!xx_rt_strcmp(key,"sizes")) {if(xx_rt_strlen(value)>=sizeof(sizes)) return false;xx_rt_memcpy(sizes,value,xx_rt_strlen(value)+1);}
        else if(!xx_rt_strcmp(key,"encoding")) {for(i=0;value[i];++i) if(value[i]>='A' && value[i]<='Z') value[i]+=32;if(xx_rt_strcmp(value,"raw")) return false;raw=true;}
        else if(!xx_rt_strcmp(key,"endian")) {for(i=0;value[i];++i) if(value[i]>='A' && value[i]<='Z') value[i]+=32;if(xx_rt_strcmp(value,"little") && xx_rt_strcmp(value,"big")) return false;endian=true;}
        else if(!xx_rt_strcmp(key,"data file") || !xx_rt_strcmp(key,"datafile") || !xx_rt_strcmp(key,"line skip") || !xx_rt_strcmp(key,"lineskip") || !xx_rt_strcmp(key,"byte skip") || !xx_rt_strcmp(key,"byteskip") || !xx_rt_strcmp(key,"block size") || !xx_rt_strcmp(key,"blocksize")) return false;
    }
    if(!ended || !dim || !width || !raw || (width>1 && !endian) || !sd_dims(sizes,(unsigned)dim,&n) || !fd_mul(n,width,&n) || !fd_range(c.at,n,(uint64_t)available)) return false;
    if(!pm_add(f,s,"nrrd-header.txt",0,(int64_t)c.at) || !pm_add(f,s,"array-data.bin",(int64_t)c.at,(int64_t)n)) { return false; } s->size=(int64_t)(c.at+n);return true;
}

void xx_nrrd_init(xx_nrrd *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NRRD,"nrrd"); } }
xx_nrrd *xx_nrrd_create(xx_io_device *d,int64_t b) { xx_nrrd *r=(xx_nrrd *)xx_mem_alloc(sizeof(*r)); if(r) xx_nrrd_init(r,d,b); return r; }
void xx_nrrd_destroy(xx_nrrd *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nrrd_free(xx_nrrd *r) { if(r) { xx_nrrd_destroy(r); xx_mem_free(r); } }
bool xx_nrrd_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nrrd_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
