/* SPDX-License-Identifier: MIT
 * Independently implemented from https://darlinglab.org/mauve/user-guide/files.html */
#include "xxfclib/formats/alignment_mauve/xx_alignment_mauve.h"
#include "../xx_fourteenth_root.h"
static bool mauve_coords(nh_blob *b,el_token t,el_token *id,uint64_t *start,uint64_t *end) {
    el_token fields[2],positions[2];unsigned n;uint64_t number;
    return el_sep(b,t,':',fields,2,&n) && n==2 && el_uint(b,fields[0],&number) && number && number<=1024 && (*id=fields[0],true) && el_sep(b,fields[1],'-',positions,2,&n) && n==2 && el_uint(b,positions[0],start) && el_uint(b,positions[1],end) && ((!*start && !*end) || (*start && *end>=*start && *end-*start<F14_MAX_SEQUENCE));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[16];f14_sequence *q=NULL;uint64_t *expected=NULL,total=0,budget=10000000;unsigned n=0,blocks=0,nt;bool ok=false;
    NH_NEED(nh_load(f,&b,pd) && th_ascii(&b));c.b=&b;q=f14_sequences(&total);expected=(uint64_t *)xx_mem_alloc(1024*sizeof(*expected));NH_NEED(q && expected);
    NH_NEED(el_line(&c,&line) && el_eq(&b,line,"#FormatVersion Mauve1") && nh_add(f,s,&b,"alignment-text",0,b.n));
    while(c.at<b.n) {el_token trim;NH_NEED(el_line(&c,&line));trim=el_trim(&b,line);if(!trim.n) continue;
        if(b.p[(size_t)trim.at]=='#') continue;
        if(el_eq(&b,trim,"=")) {unsigned i;NH_NEED(n>=2 && blocks<1024);for(i=0;i<n;++i) NH_NEED(!expected[i] || expected[i]==q[i].real);NH_NEED(f14_alignment_export(f,s,q,n,&total));n=0;++blocks;continue;}
        if(b.p[(size_t)trim.at]=='>') {el_token id;unsigned index;uint64_t start,end;
            NH_NEED(el_split(&b,trim,t,16,&nt,false) && nt>=4 && nt<=8 && el_eq(&b,t[0],">") && mauve_coords(&b,t[1],&id,&start,&end) && (el_eq(&b,t[2],"+") || el_eq(&b,t[2],"-")) && el_ident(&b,t[3]) && n<1024 && f14_find(&b,id,q,n,&index,&budget) && index==n);
            if(nt>4) { NH_NEED(el_eq(&b,t[4],"#") && nt>=6); } if(n) NH_NEED(q[n-1].n && (!expected[n-1] || expected[n-1]==q[n-1].real));q[n].id=id;expected[n]=start ? end-start+1:0;++n;continue;
        }
        NH_NEED(n && f14_append(&b,&q[n-1],trim,true,true,false));
    }
    NH_NEED(blocks && !n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(expected);f14_free_sequences(q,1024);xx_mem_free(b.p);return ok;
}

void xx_alignment_mauve_init(xx_alignment_mauve *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ALIGNMENT_MAUVE,"alignment_mauve"); } }
xx_alignment_mauve *xx_alignment_mauve_create(xx_io_device *d,int64_t b) { xx_alignment_mauve *r=(xx_alignment_mauve *)xx_mem_alloc(sizeof(*r)); if(r) xx_alignment_mauve_init(r,d,b); return r; }
void xx_alignment_mauve_destroy(xx_alignment_mauve *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_alignment_mauve_free(xx_alignment_mauve *r) { if(r) { xx_alignment_mauve_destroy(r); xx_mem_free(r); } }
bool xx_alignment_mauve_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_alignment_mauve_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
