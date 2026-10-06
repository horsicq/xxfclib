/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/dranjan/python-plyfile/master/plyfile.py */
#include "xxfclib/formats/polygon_ply/xx_polygon_ply.h"
#include "../xx_seventh_data.h"

typedef struct ply_prop {unsigned width,count_width;bool is_list;char name[64];} ply_prop;
typedef struct ply_element {uint64_t rows;unsigned properties;ply_prop props[64];char name[64];} ply_element;
static unsigned ply_width(const char *p) {
    if(!xx_rt_strcmp(p,"char") || !xx_rt_strcmp(p,"uchar") || !xx_rt_strcmp(p,"int8") || !xx_rt_strcmp(p,"uint8")) return 1;
    if(!xx_rt_strcmp(p,"short") || !xx_rt_strcmp(p,"ushort") || !xx_rt_strcmp(p,"int16") || !xx_rt_strcmp(p,"uint16")) return 2;
    if(!xx_rt_strcmp(p,"int") || !xx_rt_strcmp(p,"uint") || !xx_rt_strcmp(p,"float") || !xx_rt_strcmp(p,"int32") || !xx_rt_strcmp(p,"uint32") || !xx_rt_strcmp(p,"float32")) return 4;
    if(!xx_rt_strcmp(p,"double") || !xx_rt_strcmp(p,"float64")) { return 8; } return 0;
}
static unsigned ply_count_width(const char *p) {if(!xx_rt_strcmp(p,"uchar") || !xx_rt_strcmp(p,"uint8")) return 1;if(!xx_rt_strcmp(p,"ushort") || !xx_rt_strcmp(p,"uint16")) return 2;if(!xx_rt_strcmp(p,"uint") || !xx_rt_strcmp(p,"uint32")) return 4;return 0;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    ply_element *elements=NULL;char line[4096],*v[5];uint64_t total=0;unsigned ne=0,lines=0,i,j,k,t;bool be=false,ended=false,ok=false;int64_t available=pm_available(f);fd_cursor c={f,0,0,pd,0};c.end=(uint64_t)available;
    if(fd_stop(pd) || available<40 || !sd_line(&c,line,sizeof(line)) || xx_rt_strcmp(line,"ply") || !sd_line(&c,line,sizeof(line))) return false;
    if(!xx_rt_strcmp(line,"format binary_big_endian 1.0")) be=true;else if(xx_rt_strcmp(line,"format binary_little_endian 1.0")) return false;
    elements=(ply_element *)xx_mem_alloc(32*sizeof(*elements));if(!elements) return false;xx_mem_zero(elements,32*sizeof(*elements));
    while(c.at<65536 && ++lines<=2048 && sd_line(&c,line,sizeof(line))) {
        char *p=sd_trim(line);if(sd_prefix(p,"comment ",8) || sd_prefix(p,"obj_info ",9)) continue;t=sv_tokens(p,v,5);
        if(t==1 && !xx_rt_strcmp(v[0],"end_header")) {ended=true;break;}
        if(t==3 && !xx_rt_strcmp(v[0],"element")) {uint64_t rows;size_t z=xx_rt_strlen(v[1]);if(ne==32 || !z || z>=64 || !sd_uint(v[2],&rows) || !rows || rows>1000000-total) goto done;
            for(i=0;i<ne;++i) { if(!xx_rt_strcmp(elements[i].name,v[1])) goto done; } xx_rt_memcpy(elements[ne].name,v[1],z+1);elements[ne++].rows=rows;total+=rows;}
        else if(ne && t>=3 && !xx_rt_strcmp(v[0],"property")) {ply_element *e=elements+ne-1;ply_prop *prop;size_t z;char *name;if(e->properties==64 || (t!=3 && t!=5)) goto done;name=v[t-1];z=xx_rt_strlen(name);if(!z || z>=64) goto done;for(i=0;i<e->properties;++i) if(!xx_rt_strcmp(e->props[i].name,name)) goto done;prop=e->props+e->properties++;xx_rt_memcpy(prop->name,name,z+1);
            if(t==3) {prop->width=ply_width(v[1]);if(!prop->width) goto done;}
            else if(t==5 && !xx_rt_strcmp(v[1],"list")) {prop->width=ply_width(v[3]);prop->count_width=ply_count_width(v[2]);prop->is_list=true;if(!prop->width || !prop->count_width) goto done;}
            else goto done;
        }else goto done;
    }
    if(!ended || !ne || c.at>65536 || !pm_add(f,s,"ply-header.txt",0,(int64_t)c.at)) goto done;
    for(i=0;i<ne;++i) {ply_element *e=elements+i;uint64_t begin=c.at;char label[96];if(!e->properties || e->rows*e->properties>2000000) goto done;
        for(j=0;j<e->rows;++j) {if(fd_stop(pd)) goto done;for(k=0;k<e->properties;++k) {ply_prop *p=e->props+k;uint64_t n=p->width;
                if(p->is_list) {uint8_t b[4];uint64_t count;if(!fd_get(&c,b,p->count_width)) goto done;count=p->count_width==1?b[0]:(p->count_width==2?fd_u16(b,be):fd_u32(b,be));if(count>65536 || !fd_mul(count,p->width,&n)) goto done;}
                if(!fd_skip(&c,n)) goto done;
            }}
        xx_rt_snprintf(label,sizeof(label),"element-%u.bin",i);if(!pm_add(f,s,label,(int64_t)begin,(int64_t)(c.at-begin))) goto done;
    }s->size=(int64_t)c.at;ok=true;
done:xx_mem_free(elements);return ok;
}

void xx_polygon_ply_init(xx_polygon_ply *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_POLYGON_PLY,"polygon_ply"); } }
xx_polygon_ply *xx_polygon_ply_create(xx_io_device *d,int64_t b) { xx_polygon_ply *r=(xx_polygon_ply *)xx_mem_alloc(sizeof(*r)); if(r) xx_polygon_ply_init(r,d,b); return r; }
void xx_polygon_ply_destroy(xx_polygon_ply *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_polygon_ply_free(xx_polygon_ply *r) { if(r) { xx_polygon_ply_destroy(r); xx_mem_free(r); } }
bool xx_polygon_ply_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_polygon_ply_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
