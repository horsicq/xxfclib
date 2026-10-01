/* SPDX-License-Identifier: MIT
 * Independently implemented from https://genome.ucsc.edu/FAQ/FAQformat.html#format5 */
#include "xxfclib/formats/alignment_maf/xx_alignment_maf.h"
#include "../xx_fourteenth_root.h"
static bool maf_header(nh_blob *b,el_token line) {el_token t[16];unsigned n,i;if(!el_split(b,line,t,16,&n,false) || n<2 || !el_eq(b,t[0],"##maf") || !el_eq(b,t[1],"version=1")) return false;for(i=2;i<n;++i) if(!el_prefix(b,t[i],"scoring=") || t[i].n<=8 || !el_ident(b,el_slice(t[i],8,t[i].n-8))) return false;return n<=3;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[8];f14_sequence *q=NULL;unsigned blocks=0,n=0,nt;uint64_t total=0,budget=10000000;bool ok=false;
    NH_NEED(nh_load(f,&b,pd) && th_ascii(&b));c.b=&b;q=f14_sequences(&total);NH_NEED(q);
    NH_NEED(el_line(&c,&line) && maf_header(&b,line) && nh_add(f,s,&b,"alignment-text",0,b.n));
    while(c.at<b.n) {el_token trim;NH_NEED(el_line(&c,&line));trim=el_trim(&b,line);if(!trim.n || b.p[(size_t)trim.at]=='#') continue;
        NH_NEED(el_split(&b,trim,t,8,&nt,false) && (nt==1 || nt==2) && el_eq(&b,t[0],"a"));if(nt==2) NH_NEED(el_prefix(&b,t[1],"score=") && el_float(&b,el_slice(t[1],6,t[1].n-6)));n=0;
        while(c.at<b.n) {unsigned index;uint64_t start,size,source;NH_NEED(el_line(&c,&line));trim=el_trim(&b,line);if(!trim.n) break;if(b.p[(size_t)trim.at]=='#') continue;
            NH_NEED(el_split(&b,trim,t,8,&nt,false) && nt==7 && el_eq(&b,t[0],"s") && n<1024 && f14_find(&b,t[1],q,n,&index,&budget) && index==n && el_uint(&b,t[2],&start) && el_uint(&b,t[3],&size) && size && (el_eq(&b,t[4],"+") || el_eq(&b,t[4],"-")) && el_uint(&b,t[5],&source) && source && start<=source && size<=source-start);
            q[n].id=t[1];NH_NEED(f14_append(&b,&q[n],t[6],true,true,false) && q[n].real==size);++n;
        }
        NH_NEED(blocks<1024 && f14_alignment_export(f,s,q,n,&total));++blocks;
    }NH_NEED(blocks);s->size=(int64_t)b.n;ok=true;
done:f14_free_sequences(q,1024);xx_mem_free(b.p);return ok;
}

void xx_alignment_maf_init(xx_alignment_maf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ALIGNMENT_MAF,"alignment_maf"); } }
xx_alignment_maf *xx_alignment_maf_create(xx_io_device *d,int64_t b) { xx_alignment_maf *r=(xx_alignment_maf *)xx_mem_alloc(sizeof(*r)); if(r) xx_alignment_maf_init(r,d,b); return r; }
void xx_alignment_maf_destroy(xx_alignment_maf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_alignment_maf_free(xx_alignment_maf *r) { if(r) { xx_alignment_maf_destroy(r); xx_mem_free(r); } }
bool xx_alignment_maf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_alignment_maf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
