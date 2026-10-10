/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.openfoam.com/documentation/user-guide/4-mesh-generation-and-conversion/4.1-mesh-description */
#include "xxfclib/formats/openfoam_points/xx_openfoam_points.h"
#include "../common/xx_scientific_structure.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};structure_lexer c={0};scientific_text_token key,value,point;unsigned keys=0,i;uint64_t count,j,head,body;bool format=false,cls=false,object=false,ok=false;
    BLOB_NEED(blob_load(f,&b,pd) && structure_ascii(&b));c.b=&b;c.work=b.n*3+256;
    BLOB_NEED(structure_expect(&c,"FoamFile") && structure_expect(&c,"{"));
    for(;;) {BLOB_NEED(structure_token(&c,&key));if(scientific_text_eq(&b,key,"}")) break;BLOB_NEED(structure_token(&c,&value) && structure_expect(&c,";"));
        if(scientific_text_eq(&b,key,"format")) {BLOB_NEED(!format && scientific_text_eq(&b,value,"ascii"));format=true;}
        else if(scientific_text_eq(&b,key,"class")) {BLOB_NEED(!cls && (scientific_text_eq(&b,value,"vectorField") || scientific_text_eq(&b,value,"pointField")));cls=true;}
        else if(scientific_text_eq(&b,key,"object")) {BLOB_NEED(!object && scientific_text_eq(&b,value,"points"));object=true;}
        else if(scientific_text_eq(&b,key,"version")) {BLOB_NEED(!(keys&1) && scientific_text_float(&b,value) && molecular_value(&b,value)==2);keys|=1;}
        else if(scientific_text_eq(&b,key,"location")) {BLOB_NEED(!(keys&2) && value.n>=2 && b.p[(size_t)value.at]=='"' && b.p[(size_t)(value.at+value.n-1)]=='"');keys|=2;}
        else if(scientific_text_eq(&b,key,"arch")) {BLOB_NEED(!(keys&4) && value.n>=2 && b.p[(size_t)value.at]=='"' && b.p[(size_t)(value.at+value.n-1)]=='"');keys|=4;}
        else BLOB_NEED(false);
    }head=c.at;
    BLOB_NEED(format && cls && object && structure_token(&c,&value) && scientific_text_uint(&b,value,&count) && count && count<=1000000 && structure_expect(&c,"("));body=c.at;
    for(j=0;j<count;++j) {BLOB_NEED(structure_expect(&c,"("));for(i=0;i<3;++i) BLOB_NEED(structure_token(&c,&point) && scientific_text_float(&b,point));BLOB_NEED(structure_expect(&c,")"));}
    BLOB_NEED(structure_expect(&c,")") && structure_finish(&c));BLOB_NEED(blob_add(f,s,&b,"mesh-header",0,head) && blob_add(f,s,&b,"point-count",head,body-head) && blob_add(f,s,&b,"point-vectors",body,b.n-body));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_openfoam_points_init(xx_openfoam_points *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_OPENFOAM_POINTS,"openfoam_points"); } }
xx_openfoam_points *xx_openfoam_points_create(xx_io_device *d,int64_t b) { xx_openfoam_points *r=(xx_openfoam_points *)xx_mem_alloc(sizeof(*r)); if(r) xx_openfoam_points_init(r,d,b); return r; }
void xx_openfoam_points_destroy(xx_openfoam_points *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_openfoam_points_free(xx_openfoam_points *r) { if(r) { xx_openfoam_points_destroy(r); xx_mem_free(r); } }
bool xx_openfoam_points_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_openfoam_points_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
