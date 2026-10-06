/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.itk.org/en/latest/learn/metaio.html */
#include "xxfclib/formats/metaimage/xx_metaimage.h"
#include "../xx_sixth_data.h"

static unsigned meta_width(const char *p) {
    if(!xx_rt_strcmp(p,"MET_CHAR") || !xx_rt_strcmp(p,"MET_UCHAR")) return 1;
    if(!xx_rt_strcmp(p,"MET_SHORT") || !xx_rt_strcmp(p,"MET_USHORT")) return 2;
    if(!xx_rt_strcmp(p,"MET_INT") || !xx_rt_strcmp(p,"MET_UINT") || !xx_rt_strcmp(p,"MET_FLOAT")) return 4;
    if(!xx_rt_strcmp(p,"MET_LONG_LONG") || !xx_rt_strcmp(p,"MET_ULONG_LONG") || !xx_rt_strcmp(p,"MET_DOUBLE")) { return 8; } return 0;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    char line[4096],sizes[2048]={0},seen[64][96];unsigned lines=0,fields=0,width=0;uint64_t dim=0,channels=1,n;bool binary=true,object=false,ended=false;int64_t available=pm_available(f);
    fd_cursor c={f,0,available>65536?65536:(available>0?(uint64_t)available:0),pd,0};
    while(++lines<=128 && sd_line(&c,line,sizeof(line))) {char *eq,*key,*value;unsigned i;if(!line[0] || line[0]=='#') continue;
        eq=xx_rt_strchr(line,'=');if(!eq) return false;*eq=0;key=sd_trim(line);value=sd_trim(eq+1);
        if(!*key || xx_rt_strlen(key)>=96 || fields>=64) return false;
        for(i=0;i<fields;++i) { if(!xx_rt_strcmp(seen[i],key)) return false; } xx_rt_memcpy(seen[fields++],key,xx_rt_strlen(key)+1);
        if(!object) {if(xx_rt_strcmp(key,"ObjectType") || xx_rt_strcmp(value,"Image")) return false;object=true;continue;}
        if(!xx_rt_strcmp(key,"NDims")) {if(!sd_uint(value,&dim) || !dim || dim>16) return false;}
        else if(!xx_rt_strcmp(key,"DimSize")) {if(xx_rt_strlen(value)>=sizeof(sizes)) return false;xx_rt_memcpy(sizes,value,xx_rt_strlen(value)+1);}
        else if(!xx_rt_strcmp(key,"ElementType")) {width=meta_width(value);if(!width) return false;}
        else if(!xx_rt_strcmp(key,"ElementNumberOfChannels")) {if(!sd_uint(value,&channels) || !channels || channels>4096) return false;}
        else if(!xx_rt_strcmp(key,"BinaryData")) {if(xx_rt_strcmp(value,"True") && xx_rt_strcmp(value,"true")) return false;binary=true;}
        else if(!xx_rt_strcmp(key,"CompressedData")) {if(xx_rt_strcmp(value,"False") && xx_rt_strcmp(value,"false")) return false;}
        else if(!xx_rt_strcmp(key,"BinaryDataByteOrderMSB") || !xx_rt_strcmp(key,"ElementByteOrderMSB")) {if(xx_rt_strcmp(value,"True") && xx_rt_strcmp(value,"False") && xx_rt_strcmp(value,"true") && xx_rt_strcmp(value,"false")) return false;}
        else if(!xx_rt_strcmp(key,"HeaderSize")) {uint64_t z;if(!sd_uint(value,&z) || z) return false;}
        else if(!xx_rt_strcmp(key,"ElementDataFile")) {if(xx_rt_strcmp(value,"LOCAL")) return false;ended=true;break;}
    }
    if(!ended || !object || !dim || !width || !binary || !sd_dims(sizes,(unsigned)dim,&n) || !fd_mul(n,width,&n) || !fd_mul(n,channels,&n) || !fd_range(c.at,n,(uint64_t)available)) return false;
    if(!pm_add(f,s,"metaimage-header.txt",0,(int64_t)c.at) || !pm_add(f,s,"image-data.bin",(int64_t)c.at,(int64_t)n)) { return false; } s->size=(int64_t)(c.at+n);return true;
}

void xx_metaimage_init(xx_metaimage *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_METAIMAGE,"metaimage"); } }
xx_metaimage *xx_metaimage_create(xx_io_device *d,int64_t b) { xx_metaimage *r=(xx_metaimage *)xx_mem_alloc(sizeof(*r)); if(r) xx_metaimage_init(r,d,b); return r; }
void xx_metaimage_destroy(xx_metaimage *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_metaimage_free(xx_metaimage *r) { if(r) { xx_metaimage_destroy(r); xx_mem_free(r); } }
bool xx_metaimage_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_metaimage_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
