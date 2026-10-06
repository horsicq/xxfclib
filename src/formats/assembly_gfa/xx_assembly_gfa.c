/* SPDX-License-Identifier: MIT
 * Independently implemented from https://gfa-spec.github.io/GFA-spec/GFA1.html */
#include "xxfclib/formats/assembly_gfa/xx_assembly_gfa.h"
#include "../xx_fourteenth_root.h"
typedef struct gfa_edge {el_token from,to;uint64_t overlap;} gfa_edge;
static bool gfa_overlap(nh_blob *b,el_token t,uint64_t *overlap) {if(t.n<2 || b->p[(size_t)(t.at+t.n-1)]!='M') return false;return el_uint(b,el_slice(t,0,t.n-1),overlap) && *overlap<=F14_MAX_SEQUENCE;}
static bool gfa_tags(nh_blob *b,el_token *t,unsigned begin,unsigned n,uint64_t sequence) {
    unsigned i,j;for(i=begin;i<n;++i) {el_token value;unsigned a,z,type;uint64_t k;
        if(t[i].n<6 || b->p[(size_t)(t[i].at+2)]!=':' || b->p[(size_t)(t[i].at+4)]!=':') { return false; } a=b->p[(size_t)t[i].at];z=b->p[(size_t)(t[i].at+1)];type=b->p[(size_t)(t[i].at+3)];
        if(!((a>='A' && a<='Z') || (a>='a' && a<='z')) || !((z>='A' && z<='Z') || (z>='a' && z<='z') || (z>='0' && z<='9'))) return false;
        for(j=begin;j<i;++j) { if(b->p[(size_t)t[j].at]==a && b->p[(size_t)(t[j].at+1)]==z) return false; } value=el_slice(t[i],5,t[i].n-5);
        if(a=='S' && z=='H') return false;
        if(a=='L' && z=='N') {uint64_t length;if(type!='i' || !el_uint(b,value,&length) || length!=sequence || !sequence) return false;}
        else if(type=='i') {if(!el_range(b,value,INT32_MAX+UINT64_C(1),INT32_MAX)) return false;}
        else if(type=='f') {if(!el_f32(b,value)) return false;}
        else if(type=='A') {if(value.n!=1 || b->p[(size_t)value.at]<33 || b->p[(size_t)value.at]>126) return false;}
        else if(type=='Z') {for(k=0;k<value.n;++k) if(b->p[(size_t)(value.at+k)]<32 || b->p[(size_t)(value.at+k)]>126) return false;}
        else if(type=='H') {if(value.n%2 || !el_chars(b,value,"0123456789ABCDEFabcdef",true)) return false;}
        else return false;
    }return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[128],*paths=NULL;f14_sequence *q=NULL;gfa_edge *edges=NULL;unsigned n=0,en=0,pn=0,nt;uint64_t total=0,budget=10000000;bool header=false,ok=false;
    NH_NEED(nh_load(f,&b,pd) && th_ascii(&b));c.b=&b;q=f14_sequences(&total);edges=(gfa_edge *)xx_mem_alloc(4096*sizeof(*edges));paths=(el_token *)xx_mem_alloc(1024*sizeof(*paths));NH_NEED(q && edges && paths && nh_add(f,s,&b,"assembly-graph",0,b.n));
    while(c.at<b.n) {unsigned index;NH_NEED(el_line(&c,&line));if(!line.n) continue;if(b.p[(size_t)line.at]=='#') continue;NH_NEED(el_split(&b,line,t,128,&nt,true) && nt);
        if(el_eq(&b,t[0],"H")) {NH_NEED(!header && !n && !en && !pn && nt>=2 && el_eq(&b,t[1],"VN:Z:1.0") && gfa_tags(&b,t,1,nt,0));header=true;continue;}
        if(el_eq(&b,t[0],"S")) {NH_NEED(nt>=3 && n<1024 && f14_find(&b,t[1],q,n,&index,&budget) && index==n);q[n].id=t[1];NH_NEED(f14_append(&b,&q[n],t[2],false,true,false) && gfa_tags(&b,t,3,nt,q[n].n));++n;continue;}
        if(el_eq(&b,t[0],"L")) {uint64_t overlap;NH_NEED(nt>=6 && en<4096 && el_ident(&b,t[1]) && el_ident(&b,t[3]) && (el_eq(&b,t[2],"+") || el_eq(&b,t[2],"-")) && (el_eq(&b,t[4],"+") || el_eq(&b,t[4],"-")) && gfa_overlap(&b,t[5],&overlap) && gfa_tags(&b,t,6,nt,0));edges[en].from=t[1];edges[en].to=t[3];edges[en++].overlap=overlap;continue;}
        if(el_eq(&b,t[0],"P")) {el_token members[128],overlaps[128];unsigned mn,on,i;uint64_t previous_overlap=0;NH_NEED(nt>=4 && pn<1024 && el_ident(&b,t[1]) && el_unique(&b,t[1],paths,pn,&budget) && el_sep(&b,t[2],',',members,128,&mn) && mn && mn<=128 && gfa_tags(&b,t,4,nt,0));paths[pn++]=t[1];
            if(el_eq(&b,t[3],"*")) on=0;else NH_NEED(el_sep(&b,t[3],',',overlaps,128,&on) && on==mn-1);
            for(i=0;i<mn;++i) {uint64_t overlap=0;el_token id;unsigned orientation;NH_NEED(members[i].n>=2);orientation=b.p[(size_t)(members[i].at+members[i].n-1)];NH_NEED(orientation=='+' || orientation=='-');id=el_slice(members[i],0,members[i].n-1);NH_NEED(f14_find(&b,id,q,n,&index,&budget) && index<n && (!i || previous_overlap<=q[index].n));if(i<on) NH_NEED(gfa_overlap(&b,overlaps[i],&overlap) && overlap<=q[index].n);previous_overlap=overlap;}
            continue;
        }NH_NEED(false);
    }
    NH_NEED(n);{unsigned i;for(i=0;i<en;++i) {unsigned a,z;NH_NEED(f14_find(&b,edges[i].from,q,n,&a,&budget) && a<n && f14_find(&b,edges[i].to,q,n,&z,&budget) && z<n && edges[i].overlap<=q[a].n && edges[i].overlap<=q[z].n);}for(i=0;i<pn;++i) {unsigned index;NH_NEED(f14_find(&b,paths[i],q,n,&index,&budget) && index==n);}for(i=0;i<n;++i) {char label[64];xx_rt_snprintf(label,sizeof(label),"segment-%04u",i);NH_NEED(f14_export(f,s,&q[i],label,&total));}}
    s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(edges);xx_mem_free(paths);f14_free_sequences(q,1024);xx_mem_free(b.p);return ok;
}

void xx_assembly_gfa_init(xx_assembly_gfa *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ASSEMBLY_GFA,"assembly_gfa"); } }
xx_assembly_gfa *xx_assembly_gfa_create(xx_io_device *d,int64_t b) { xx_assembly_gfa *r=(xx_assembly_gfa *)xx_mem_alloc(sizeof(*r)); if(r) xx_assembly_gfa_init(r,d,b); return r; }
void xx_assembly_gfa_destroy(xx_assembly_gfa *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_assembly_gfa_free(xx_assembly_gfa *r) { if(r) { xx_assembly_gfa_destroy(r); xx_mem_free(r); } }
bool xx_assembly_gfa_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_assembly_gfa_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
