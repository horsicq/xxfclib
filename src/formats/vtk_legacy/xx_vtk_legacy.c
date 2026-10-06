/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.vtk.org/en/latest/vtk_file_formats/vtk_legacy_file_format.html */
#include "xxfclib/formats/vtk_legacy/xx_vtk_legacy.h"
#include "../xx_sixth_data.h"

static bool vtk_xyz(char *p,bool positive) {unsigned i;for(i=0;i<3;++i) {char *start;bool separated;p=sd_trim(p);start=p;while(*p && *p!=' ' && *p!='\t') ++p;separated=*p!=0;if(*p) *p++=0;if(!sd_float_token(start) || (positive && !sd_positive_float(start))) return false;if(!separated && i<2) return false;}return !*sd_trim(p);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    char line[1024],*p,*type;uint64_t points=1,declared,n,components=1;unsigned width,dim[3],i;bool cells=false;int64_t available=pm_available(f);
    fd_cursor c={f,0,available>16384?16384:(available>0?(uint64_t)available:0),pd,0};
    if(!sd_line(&c,line,sizeof(line)) || (xx_rt_strcmp(line,"# vtk DataFile Version 2.0") && xx_rt_strcmp(line,"# vtk DataFile Version 3.0")) || !sd_line(&c,line,257) || !sd_line(&c,line,sizeof(line)) || xx_rt_strcmp(line,"BINARY") || !sd_line(&c,line,sizeof(line)) || xx_rt_strcmp(line,"DATASET STRUCTURED_POINTS")) return false;
    if(!sd_line(&c,line,sizeof(line)) || !sd_prefix(line,"DIMENSIONS ",11)) { return false; } p=line+11;
    for(i=0;i<3;++i) {char token[32];size_t k=0;uint64_t v;while(*p==' ' || *p=='\t') ++p;while(*p && *p!=' ' && *p!='\t') {if(k+1>=sizeof(token)) return false;token[k++]=*p++;}token[k]=0;if(!sd_uint(token,&v) || !v || v>INT32_MAX) return false;dim[i]=(unsigned)v;}
    if(*sd_trim(p)) return false;
    if(!sd_line(&c,line,sizeof(line))) return false;
    if(sd_prefix(line,"ORIGIN ",7)) {if(!vtk_xyz(line+7,false) || !sd_line(&c,line,sizeof(line)) || !sd_prefix(line,"SPACING ",8) || !vtk_xyz(line+8,true)) return false;}
    else if(sd_prefix(line,"SPACING ",8)) {if(!vtk_xyz(line+8,true) || !sd_line(&c,line,sizeof(line)) || !sd_prefix(line,"ORIGIN ",7) || !vtk_xyz(line+7,false)) return false;}
    else return false;
    if(!sd_line(&c,line,sizeof(line))) return false;
    if(sd_prefix(line,"POINT_DATA ",11)) p=line+11;else if(sd_prefix(line,"CELL_DATA ",10)) {p=line+10;cells=true;}else return false;
    for(i=0;i<3;++i) {unsigned d=cells?(dim[i]>1?dim[i]-1:1):dim[i];if(!fd_mul(points,d,&points)) return false;}
    if(!sd_uint(p,&declared) || declared!=points || !sd_line(&c,line,sizeof(line)) || !sd_prefix(line,"SCALARS ",8)) return false;
    p=line+8;while(*p && *p!=' ' && *p!='\t') ++p;if(!*p) return false;type=sd_trim(p);p=type;while(*p && *p!=' ' && *p!='\t') ++p;
    if(*p) {*p++=0;if(!sd_uint(p,&components) || !components || components>4) return false;}
    if(!xx_rt_strcmp(type,"char") || !xx_rt_strcmp(type,"unsigned_char")) width=1;else if(!xx_rt_strcmp(type,"short") || !xx_rt_strcmp(type,"unsigned_short")) width=2;
    else if(!xx_rt_strcmp(type,"int") || !xx_rt_strcmp(type,"unsigned_int") || !xx_rt_strcmp(type,"float")) width=4;else if(!xx_rt_strcmp(type,"double")) width=8;else return false;
    if(!sd_line(&c,line,sizeof(line)) || xx_rt_strcmp(line,"LOOKUP_TABLE default") || !fd_mul(points,components,&n) || !fd_mul(n,width,&n) || !fd_range(c.at,n,(uint64_t)available)) return false;
    if(!pm_add(f,s,"vtk-header.txt",0,(int64_t)c.at) || !pm_add(f,s,"scalars.bin",(int64_t)c.at,(int64_t)n)) { return false; } s->size=(int64_t)(c.at+n);return true;
}

void xx_vtk_legacy_init(xx_vtk_legacy *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_VTK_LEGACY,"vtk_legacy"); } }
xx_vtk_legacy *xx_vtk_legacy_create(xx_io_device *d,int64_t b) { xx_vtk_legacy *r=(xx_vtk_legacy *)xx_mem_alloc(sizeof(*r)); if(r) xx_vtk_legacy_init(r,d,b); return r; }
void xx_vtk_legacy_destroy(xx_vtk_legacy *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_vtk_legacy_free(xx_vtk_legacy *r) { if(r) { xx_vtk_legacy_destroy(r); xx_mem_free(r); } }
bool xx_vtk_legacy_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_vtk_legacy_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
