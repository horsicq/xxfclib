/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/Danny02/OpenCTM/master/lib/compressRAW.c
 * OpenCTM version5 RAW meshes with bounded nondegenerate triangle index triplets, finite vertex/normal/UV/attribute arrays, length-framed map names and exact complete section ordering. MG1/MG2 and geometry rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/openctm_mesh/xx_openctm_mesh.h"
#include "../astc_texture/xx_tenth_media.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[36];return tg_probe(f,n,b,36)&&pm_tag(b,"OCTM",4)&&xx_data_get_u32(b+4, 4, 0, false)==5&&pm_tag(b+8,"RAW\0",4);}
static bool ct_string(const uint8_t *b,uint64_t *p,uint64_t n) {uint32_t size;if(!tg_span(*p,4,n))return false;size=xx_data_get_u32(b+*p, 4, 0, false);*p+=4;if(size>4096||!tg_span(*p,size,n))return false;*p+=size;return true;}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t v=xx_data_get_u32(b+12, 4, 0, false),t=xx_data_get_u32(b+16, 4, 0, false),uv=xx_data_get_u32(b+20, 4, 0, false),a=xx_data_get_u32(b+24, 4, 0, false),flags=xx_data_get_u32(b+28, 4, 0, false),i;uint64_t p=32,start;char label[64];
 if(!v||v>1000000||!t||t>1000000||uv>16||a>16||flags>1||!ct_string(b,&p,n)||!tg_emit(f,s,"ctm-header.bin",0,p,n))return false;
 start=p;if(!tg_span(p,4+(uint64_t)t*12,n)||!pm_tag(b+p,"INDX",4))return false;p+=4;
 for(i=0;i<t;++i){uint32_t x=xx_data_get_u32(b+p, 4, 0, false),y=xx_data_get_u32(b+p+4, 4, 0, false),z=xx_data_get_u32(b+p+8, 4, 0, false);if(tg_stop(pd)||x>=v||y>=v||z>=v||x==y||x==z||y==z)return false;p+=12;}
 if(!tg_emit(f,s,"triangle-indices.bin",start,p-start,n))return false;
 start=p;if(!tg_span(p,4,n)||!pm_tag(b+p,"VERT",4)||!tg_float_array(b,p+4,(uint64_t)v*3,n,pd))return false;p+=4+(uint64_t)v*12;if(!tg_emit(f,s,"vertices.bin",start,p-start,n))return false;
 if(flags){start=p;if(!tg_span(p,4,n)||!pm_tag(b+p,"NORM",4)||!tg_float_array(b,p+4,(uint64_t)v*3,n,pd))return false;p+=4+(uint64_t)v*12;if(!tg_emit(f,s,"normals.bin",start,p-start,n))return false;}
 for(i=0;i<uv+a;++i){uint64_t values=(uint64_t)v*(i<uv?2U:4U);start=p;if(!tg_span(p,4,n)||!pm_tag(b+p,i<uv?"TEXC":"ATTR",4))return false;p+=4;if(!ct_string(b,&p,n)||(i<uv&&!ct_string(b,&p,n))||!tg_float_array(b,p,values,n,pd))return false;p+=values*4;xx_rt_snprintf(label,sizeof(label),"%s-%u.bin",i<uv?"uv-map":"attribute",i<uv?i:i-uv);if(!tg_emit(f,s,label,start,p-start,n))return false;}
 if(p!=n) {return false; } s->size=(int64_t)n;return true;
}

void xx_openctm_mesh_init(xx_openctm_mesh *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_OPENCTM_MESH,"ctm");}}
xx_openctm_mesh *xx_openctm_mesh_create(xx_io_device *d,int64_t at) {xx_openctm_mesh *r=(xx_openctm_mesh *)xx_mem_alloc(sizeof(*r));if(r)xx_openctm_mesh_init(r,d,at);return r;}
void xx_openctm_mesh_destroy(xx_openctm_mesh *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_openctm_mesh_free(xx_openctm_mesh *r) {if(r){xx_openctm_mesh_destroy(r);xx_mem_free(r);}}
bool xx_openctm_mesh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_openctm_mesh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
