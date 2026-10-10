/* SPDX-License-Identifier: MIT
 * Independently implemented from https://math.nist.gov/MatrixMarket/formats.html */
#include "xxfclib/formats/matrix_market/xx_matrix_market.h"
#include "../common/xx_scientific_text.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[8];unsigned nt,field,sym;uint64_t rows,cols,count,seen=0,head,row=0,column=0;bool coordinate,ok=false;
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;
    BLOB_NEED(scientific_text_line(&c,&line) && scientific_text_split(&b,line,t,8,&nt,false) && nt==5 && scientific_text_eq(&b,t[0],"%%MatrixMarket") && scientific_text_eq(&b,t[1],"matrix"));
    coordinate=scientific_text_eq(&b,t[2],"coordinate");BLOB_NEED(coordinate || scientific_text_eq(&b,t[2],"array"));
    field=scientific_text_eq(&b,t[3],"real") ? 1:scientific_text_eq(&b,t[3],"integer") ? 2:scientific_text_eq(&b,t[3],"complex") ? 3:scientific_text_eq(&b,t[3],"pattern") ? 4:0;
    sym=scientific_text_eq(&b,t[4],"general") ? 1:scientific_text_eq(&b,t[4],"symmetric") ? 2:scientific_text_eq(&b,t[4],"skew-symmetric") ? 3:scientific_text_eq(&b,t[4],"hermitian") ? 4:0;
    BLOB_NEED(field && sym && (coordinate || field!=4) && (sym!=4 || field==3));
    do {BLOB_NEED(scientific_text_line(&c,&line));} while(!line.n || b.p[(size_t)line.at]=='%');
    BLOB_NEED(scientific_text_split(&b,line,t,8,&nt,false) && nt==(coordinate ? 3U:2U) && scientific_text_uint(&b,t[0],&rows) && scientific_text_uint(&b,t[1],&cols) && rows && cols && rows<=1000000000 && cols<=1000000000 && (sym==1 || rows==cols));
    if(coordinate) BLOB_NEED(scientific_text_uint(&b,t[2],&count));
    else {BLOB_NEED(binary_mul(rows,sym==1 ? cols:(sym==3 ? rows-1:rows+1),&count));if(sym!=1) count/=2;}
    BLOB_NEED(count<=1000000);head=c.at;if(!coordinate) {column=1;row=sym==3 ? 2:1;}
    while(c.at<b.n) {
        BLOB_NEED(scientific_text_line(&c,&line));if(!line.n || b.p[(size_t)line.at]=='%') continue;
        BLOB_NEED(seen<count && scientific_text_split(&b,line,t,8,&nt,false));
        if(coordinate) {BLOB_NEED(nt==(field==4 ? 2U:field==3 ? 4U:3U) && scientific_text_uint(&b,t[0],&row) && scientific_text_uint(&b,t[1],&column) && row && row<=rows && column && column<=cols && (sym==1 || row>=column) && (sym!=3 || row!=column));}
        else BLOB_NEED(nt==(field==3 ? 2U:1U));
        {unsigned at=coordinate ? 2:0;if(field!=4) BLOB_NEED(field==2 ? scientific_text_integer(&b,t[at]):scientific_text_float(&b,t[at]));if(field==3) BLOB_NEED(scientific_text_float(&b,t[at+1]) && (sym!=4 || row!=column || scientific_text_zero(&b,t[at+1])));}
        ++seen;if(!coordinate && ++row>rows) {++column;row=sym==1 ? 1:sym==3 ? column+1:column;}
    }
    BLOB_NEED(seen==count && blob_add(f,s,&b,"metadata",0,head) && blob_add(f,s,&b,"values",head,b.n-head));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_matrix_market_init(xx_matrix_market *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MATRIX_MARKET,"matrix_market"); } }
xx_matrix_market *xx_matrix_market_create(xx_io_device *d,int64_t b) { xx_matrix_market *r=(xx_matrix_market *)xx_mem_alloc(sizeof(*r)); if(r) xx_matrix_market_init(r,d,b); return r; }
void xx_matrix_market_destroy(xx_matrix_market *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_matrix_market_free(xx_matrix_market *r) { if(r) { xx_matrix_market_destroy(r); xx_mem_free(r); } }
bool xx_matrix_market_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_matrix_market_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
