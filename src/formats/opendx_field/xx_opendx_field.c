/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.ks.uiuc.edu/Research/vmd/plugins/molfile/dxplugin.html */
#include "xxfclib/formats/opendx_field/xx_opendx_field.h"
#include "../common/xx_scientific_text.h"
static bool dx_next(scientific_text_lines *c,scientific_text_token *line) {while(c->at<c->b->n) {if(!scientific_text_line(c,line)) return false;*line=scientific_text_trim(c->b,*line);if(line->n && c->b->p[(size_t)line->at]!='#') return true;}return false;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[512];uint64_t dims[3],total=1,v,head,end;unsigned i,j,nt;bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;
    BLOB_NEED(dx_next(&c,&line) && scientific_text_split(&b,line,t,512,&nt,false) && nt==8 && scientific_text_eq(&b,t[0],"object") && scientific_text_eq(&b,t[1],"1") && scientific_text_eq(&b,t[2],"class") && scientific_text_eq(&b,t[3],"gridpositions") && scientific_text_eq(&b,t[4],"counts"));
    for(i=0;i<3;++i) BLOB_NEED(scientific_text_uint(&b,t[5+i],&dims[i]) && dims[i] && binary_mul(total,dims[i],&total) && total<=1000000);
    BLOB_NEED(dx_next(&c,&line) && scientific_text_split(&b,line,t,512,&nt,false) && nt==4 && scientific_text_eq(&b,t[0],"origin"));for(i=1;i<4;++i) BLOB_NEED(scientific_text_float(&b,t[i]));
    for(i=0;i<3;++i) {
        BLOB_NEED(dx_next(&c,&line) && scientific_text_split(&b,line,t,512,&nt,false) && nt==4 && scientific_text_eq(&b,t[0],"delta"));
        for(j=0;j<3;++j) BLOB_NEED(scientific_text_float(&b,t[1+j]) && (i==j ? !scientific_text_zero(&b,t[1+j]):scientific_text_zero(&b,t[1+j])));
    }
    BLOB_NEED(dx_next(&c,&line) && scientific_text_split(&b,line,t,512,&nt,false) && nt==8 && scientific_text_eq(&b,t[0],"object") && scientific_text_eq(&b,t[1],"2") && scientific_text_eq(&b,t[2],"class") && scientific_text_eq(&b,t[3],"gridconnections") && scientific_text_eq(&b,t[4],"counts"));
    for(i=0;i<3;++i) BLOB_NEED(scientific_text_uint(&b,t[5+i],&v) && v==dims[i]);
    BLOB_NEED(dx_next(&c,&line) && scientific_text_split(&b,line,t,512,&nt,false) && nt==12 && scientific_text_eq(&b,t[0],"object") && scientific_text_eq(&b,t[1],"3") && scientific_text_eq(&b,t[2],"class") && scientific_text_eq(&b,t[3],"array") && scientific_text_eq(&b,t[4],"type") && (scientific_text_eq(&b,t[5],"float") || scientific_text_eq(&b,t[5],"double") || scientific_text_eq(&b,t[5],"\"float\"") || scientific_text_eq(&b,t[5],"\"double\"")) && scientific_text_eq(&b,t[6],"rank") && scientific_text_eq(&b,t[7],"0") && scientific_text_eq(&b,t[8],"items") && scientific_text_uint(&b,t[9],&v) && v==total && scientific_text_eq(&b,t[10],"data") && scientific_text_eq(&b,t[11],"follows"));head=c.at;
    v=0;while(v<total) {BLOB_NEED(dx_next(&c,&line) && scientific_text_split(&b,line,t,512,&nt,false) && nt && nt<=total-v);for(i=0;i<nt;++i) BLOB_NEED(scientific_text_float(&b,t[i]));v+=nt;}
    end=c.at;BLOB_NEED(dx_next(&c,&line));
    if(scientific_text_prefix(&b,line,"attribute ")) {BLOB_NEED(scientific_text_eq(&b,line,"attribute \"dep\" string \"positions\"") && dx_next(&c,&line));}
    BLOB_NEED(scientific_text_prefix(&b,line,"object ") && line.n>=20);
    {scientific_text_token tail=scientific_text_slice(line,line.n-12,12),name=scientific_text_trim(&b,scientific_text_slice(line,7,line.n-19));BLOB_NEED(scientific_text_eq(&b,tail," class field") && name.n && name.n<=255);
     if(b.p[(size_t)name.at]=='"') {BLOB_NEED(name.n>=3 && b.p[(size_t)(name.at+name.n-1)]=='"');for(j=1;j+1<name.n;++j) BLOB_NEED(b.p[(size_t)(name.at+j)]>=32 && b.p[(size_t)(name.at+j)]!='"' && b.p[(size_t)(name.at+j)]!='\\');}
     else BLOB_NEED(scientific_text_uint(&b,name,&v) && v>3);}
    for(i=0;i<3;++i) {static const char *parts[]={"component \"positions\" value 1","component \"connections\" value 2","component \"data\" value 3"};BLOB_NEED(dx_next(&c,&line) && scientific_text_eq(&b,line,parts[i]));}
    while(c.at<b.n) BLOB_NEED(scientific_text_line(&c,&line) && (!line.n || b.p[(size_t)line.at]=='#'));
    BLOB_NEED(blob_add(f,s,&b,"metadata",0,head) && blob_add(f,s,&b,"values",head,end-head) && blob_add(f,s,&b,"field",end,b.n-end));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_opendx_field_init(xx_opendx_field *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_OPENDX_FIELD,"opendx_field"); } }
xx_opendx_field *xx_opendx_field_create(xx_io_device *d,int64_t b) { xx_opendx_field *r=(xx_opendx_field *)xx_mem_alloc(sizeof(*r)); if(r) xx_opendx_field_init(r,d,b); return r; }
void xx_opendx_field_destroy(xx_opendx_field *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_opendx_field_free(xx_opendx_field *r) { if(r) { xx_opendx_field_destroy(r); xx_mem_free(r); } }
bool xx_opendx_field_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_opendx_field_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
