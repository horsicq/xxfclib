/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
 * GLB2 chunks; JSON syntax/asset version verified, no rendering or URI access.
 */
#include "xxfclib/formats/glb/xx_glb.h"
#include "xxfclib/json/xx_json.h"
#include "../xx_payload_members.h"
#include "../xx_fourth_utf8.h"
#include "xxfclib/data/xx_data.h"

/* The shared JSON API returns C strings without their decoded byte length.
 * Check consumed escapes so an embedded U+0000 cannot alias a required key
 * or version through strcmp; escaped backslashes are skipped as a pair. */
static bool glb_matches(xx_json *j,size_t begin,const char *decoded,const char *expected) {
    size_t i;
    if(xx_rt_strcmp(decoded,expected)) return false;
    for(i=begin;i<j->position;++i) if(j->data[i]=='\\') {
        ++i;
        if(i+4<j->position && j->data[i]=='u' && j->data[i+1]=='0' && j->data[i+2]=='0' &&
           j->data[i+3]=='0' && j->data[i+4]=='0') return false;
    }
    return true;
}
static bool glb_asset(xx_json *j) {
    bool version=false; if(!xx_json_object_begin(j) || xx_json_object_empty(j)) return false;
    for(;;) {
        char *key=NULL,*value=NULL; bool ok; size_t begin=j->position;
        if(!xx_json_object_key(j,&key)) return false;
        if(glb_matches(j,begin,key,"version")) { begin=j->position; ok=!version && xx_json_string(j,&value) && glb_matches(j,begin,value,"2.0"); version=true; }
        else ok=xx_json_skip(j);
        if(value) { xx_str_free(value); } xx_str_free(key); if(!ok) return false;
        if(!xx_json_more(j)) break;
    }
    return xx_json_object_end(j) && version;
}
static bool glb_json(Abstractformat *f,int64_t at,uint32_t size,xx_pd_struct *pd) {
    uint8_t *data; xx_json j; bool ok=false,asset=false;
    const xx_var *budget=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    if(!size || size>4U*1024U*1024U || (budget && (uint64_t)size*3U+256U>xx_var_get_u64(budget))) return false;
    data=(uint8_t *)xx_mem_alloc(size); if(!data) return false;
    if(!pm_read(f,at,data,size) || !fourth_utf8(data,size,pd)) goto done;
    xx_json_init(&j,data,size); if(!xx_json_object_begin(&j) || xx_json_object_empty(&j)) goto done;
    for(;;) {
        char *key=NULL; bool valid; size_t begin=j.position;
        if((pd && xx_pd_is_stopped(pd)) || !xx_json_object_key(&j,&key)) goto done;
        if(glb_matches(&j,begin,key,"asset")) { valid=!asset && glb_asset(&j); asset=true; }
        else valid=xx_json_skip(&j);
        xx_str_free(key); if(!valid) goto done;
        if(!xx_json_more(&j)) break;
    }
    if(!xx_json_object_end(&j) || !asset) goto done;
    while(j.position<j.size && (data[j.position]==' ' || data[j.position]=='\r' || data[j.position]=='\n' || data[j.position]=='\t')) ++j.position;
    ok=j.position==j.size;
done: xx_mem_free(data); return ok && (!pd || !xx_pd_is_stopped(pd));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[12],e[8]; uint32_t length; int64_t at=12; bool bin=false;
    if(!pm_read(f,0,h,12) || xx_rt_memcmp(h,"glTF",4) || xx_data_get_u32(h+4, 4, 0, false)!=2) return false;
    length=xx_data_get_u32(h+8, 4, 0, false); if(length<24 || length%4 || length>pm_available(f)) return false;
    while(at<length) {
        uint32_t n,type; char name[48];
        if((pd && xx_pd_is_stopped(pd)) || length-at<8 || !pm_read(f,at,e,8)) return false;
        n=xx_data_get_u32(e, 4, 0, false); type=xx_data_get_u32(e+4, 4, 0, false); if(n%4 || n>(uint64_t)(length-at-8)) return false;
        if(!s->count) { if(type!=0x4e4f534aU || !glb_json(f,at+8,n,pd)) return false; xx_rt_snprintf(name,sizeof(name),"scene.json"); }
        else if(type==0x4e4f534aU) return false;
        else if(type==0x004e4942U) { if(bin || s->count!=1) return false; bin=true; xx_rt_snprintf(name,sizeof(name),"buffer.bin"); }
        else xx_rt_snprintf(name,sizeof(name),"chunk-%08x.bin",type);
        if(!pm_add(f,s,name,at+8,n)) return false;
        at+=8+(int64_t)n;
    }
    s->size=length; return at==length && s->count!=0;
}

void xx_glb_init(xx_glb *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GLB,"glb"); } }
xx_glb *xx_glb_create(xx_io_device *d,int64_t b) { xx_glb *r=(xx_glb *)xx_mem_alloc(sizeof(*r)); if(r) xx_glb_init(r,d,b); return r; }
void xx_glb_destroy(xx_glb *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_glb_free(xx_glb *r) { if(r) { xx_glb_destroy(r); xx_mem_free(r); } }
bool xx_glb_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_glb_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
