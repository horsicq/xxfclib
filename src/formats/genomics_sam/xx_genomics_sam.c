/* SPDX-License-Identifier: MIT
 * Independently implemented from https://samtools.github.io/hts-specs/SAMv1.pdf */
#include "xxfclib/formats/genomics_sam/xx_genomics_sam.h"
#include "../xx_eleventh_data.h"
typedef struct sam_ref {el_token name;uint64_t length;} sam_ref;
static int sam_find(nh_blob *b,el_token name,sam_ref *refs,unsigned count,uint64_t *budget) {unsigned i;for(i=0;i<count;++i) {if(!*budget) return -2;--*budget;if(name.n==refs[i].name.n) {if(*budget<name.n) return -2;*budget-=name.n;if(!xx_rt_memcmp(b->p+(size_t)name.at,b->p+(size_t)refs[i].name.at,(size_t)name.n)) return (int)i;}}return -1;}
static bool sam_tag(nh_blob *b,el_token tag) {
    el_token v;uint8_t type;uint64_t i;
    if(tag.n<5 || b->p[(size_t)tag.at+2]!=':' || b->p[(size_t)tag.at+4]!=':' || !el_chars(b,el_slice(tag,0,1),"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",true) || !el_chars(b,el_slice(tag,1,1),"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",true)) return false;
    type=b->p[(size_t)tag.at+3];v=el_slice(tag,5,tag.n-5);
    if(type=='A') return v.n==1 && b->p[(size_t)v.at]>=33 && b->p[(size_t)v.at]<=126;
    if(type=='i') return el_range(b,v,2147483648U,4294967295U);
    if(type=='f') return el_f32(b,v);
    if(type=='Z') {for(i=0;i<v.n;++i) if(b->p[(size_t)(v.at+i)]<32) return false;return true;}
    if(type=='H') return !(v.n&1) && el_chars(b,v,"0123456789ABCDEF",false);
    if(type=='B') {
        el_token values[512];unsigned n,j;uint8_t kind;
        if(v.n<3 || b->p[(size_t)v.at+1]!=',') { return false; } kind=b->p[(size_t)v.at];
        if(!el_sep(b,el_slice(v,2,v.n-2),',',values,512,&n)) return false;
        for(j=0;j<n;++j) {bool valid=kind=='f' ? el_f32(b,values[j]):kind=='c' ? el_range(b,values[j],128,127):kind=='C' ? el_range(b,values[j],0,255):kind=='s' ? el_range(b,values[j],32768,32767):kind=='S' ? el_range(b,values[j],0,65535):kind=='i' ? el_range(b,values[j],2147483648U,2147483647U):kind=='I' ? el_range(b,values[j],0,4294967295U):false;if(!valid) return false;}return true;
    }return false;
}
static bool sam_cigar(nh_blob *b,el_token cigar,uint64_t *query,uint64_t *reference) {
    uint64_t i=0,q=0,r=0;unsigned ops=0;uint8_t first=0;bool core=false,tail=false;if(el_eq(b,cigar,"*")) {*query=*reference=0;return true;}
    while(i<cigar.n) {
        uint64_t n=0,start=i;uint8_t op;while(i<cigar.n && b->p[(size_t)(cigar.at+i)]>='0' && b->p[(size_t)(cigar.at+i)]<='9') {n=n*10+b->p[(size_t)(cigar.at+i++)]-'0';if(n>1000000000) return false;}
        if(i==start || !n || i==cigar.n || ++ops>65535) { return false; } op=b->p[(size_t)(cigar.at+i++)];
        if(ops==1) first=op;
        if(op=='H') {if(ops!=1 && i!=cigar.n) return false;}
        else if(op=='S') {if(core) tail=true;else if(ops!=1 && !(ops==2 && first=='H')) return false;q+=n;}
        else {if(tail) return false;core=true;if(op=='M' || op=='=' || op=='X') {q+=n;r+=n;}else if(op=='I') q+=n;else if(op=='D' || op=='N') r+=n;else if(op!='P') return false;}
        if(q>1000000000 || r>1000000000) return false;
    }*query=q;*reference=r;return ops!=0;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[150];sam_ref *refs=NULL;uint64_t budget=8388608,tag_budget=8388608,head=0,alignments=0;unsigned nt,nref=0;bool ok=false,body=false,hd=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;refs=(sam_ref *)xx_mem_alloc(1024*sizeof(*refs));NH_NEED(refs);
    while(c.at<b.n) {
        uint64_t start=c.at;unsigned i,j;NH_NEED(el_line(&c,&line) && el_split(&b,line,t,150,&nt,true) && nt);
        if(b.p[(size_t)line.at]=='@') {
            NH_NEED(!body && nt>=2 && t[0].n==3);
            if(el_eq(&b,t[0],"@CO")) {NH_NEED(hd);continue;}
            NH_NEED(el_eq(&b,t[0],"@HD") || el_eq(&b,t[0],"@SQ") || el_eq(&b,t[0],"@RG") || el_eq(&b,t[0],"@PG"));
            for(i=1;i<nt;++i) {NH_NEED(t[i].n>=4 && b.p[(size_t)t[i].at+2]==':' && el_chars(&b,el_slice(t[i],0,1),"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",true) && el_chars(&b,el_slice(t[i],1,1),"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",true));for(j=1;j<i;++j) {NH_NEED(tag_budget);--tag_budget;NH_NEED(xx_rt_memcmp(b.p+(size_t)t[i].at,b.p+(size_t)t[j].at,2)!=0);}}
            if(el_eq(&b,t[0],"@HD")) {
                unsigned versions=0;NH_NEED(!hd && start==0);for(i=1;i<nt;++i) if(el_prefix(&b,t[i],"VN:")) {el_token version=el_slice(t[i],3,t[i].n-3);NH_NEED(version.n==3 && b.p[(size_t)version.at]=='1' && b.p[(size_t)version.at+1]=='.' && b.p[(size_t)version.at+2]>='0' && b.p[(size_t)version.at+2]<='6');++versions;}NH_NEED(versions==1);hd=true;
            } else if(el_eq(&b,t[0],"@SQ")) {
                el_token name={0};uint64_t length=0;NH_NEED(hd && nref<1024);
                for(i=1;i<nt;++i) {if(el_prefix(&b,t[i],"SN:")) name=el_slice(t[i],3,t[i].n-3);if(el_prefix(&b,t[i],"LN:")) NH_NEED(el_uint(&b,el_slice(t[i],3,t[i].n-3),&length));}
                NH_NEED(el_ident(&b,name) && !el_eq(&b,name,"*") && !el_eq(&b,name,"=") && length && length<=2147483647 && sam_find(&b,name,refs,nref,&budget)== -1);refs[nref].name=name;refs[nref++].length=length;
            }else NH_NEED(hd);continue;
        }
        {
            uint64_t flag,pos,mapq,pnext,q,r;int ref,mate;NH_NEED(hd && nt>=11 && nt<=139);
            if(!body) {head=start;body=true;}NH_NEED(++alignments<=1000000 && el_ident(&b,t[0]) && el_uint(&b,t[1],&flag) && flag<=65535 && el_uint(&b,t[3],&pos) && pos<=2147483647 && el_uint(&b,t[4],&mapq) && mapq<=255 && el_uint(&b,t[7],&pnext) && pnext<=2147483647 && el_range(&b,t[8],2147483648U,2147483647U));
            ref=el_eq(&b,t[2],"*") ? -1:sam_find(&b,t[2],refs,nref,&budget);NH_NEED(ref>= -1 && (ref<0 ? !pos:pos<=refs[ref].length));
            mate=el_eq(&b,t[6],"*") ? -1:el_eq(&b,t[6],"=") ? ref:sam_find(&b,t[6],refs,nref,&budget);NH_NEED(mate>= -1 && (mate<0 ? !pnext:pnext<=refs[mate].length));
            NH_NEED(sam_cigar(&b,t[5],&q,&r) && (flag&4 || (ref>=0 && pos && !el_eq(&b,t[5],"*"))) && (ref<0 || !pos || r<=refs[ref].length-pos+1));
            NH_NEED(el_eq(&b,t[9],"*") || el_chars(&b,t[9],"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz=.",true));NH_NEED(el_eq(&b,t[9],"*") || el_eq(&b,t[5],"*") || q==t[9].n);
            if(!el_eq(&b,t[10],"*")) {uint64_t k;NH_NEED(!el_eq(&b,t[9],"*") && t[10].n==t[9].n);for(k=0;k<t[10].n;++k) NH_NEED(b.p[(size_t)(t[10].at+k)]>=33 && b.p[(size_t)(t[10].at+k)]<=126);}
            for(i=11;i<nt;++i) {NH_NEED(sam_tag(&b,t[i]));for(j=11;j<i;++j) {NH_NEED(tag_budget);--tag_budget;NH_NEED(xx_rt_memcmp(b.p+(size_t)t[i].at,b.p+(size_t)t[j].at,2)!=0);}}
        }
    }
    NH_NEED(alignments && nh_add(f,s,&b,"header",0,head) && nh_add(f,s,&b,"alignments",head,b.n-head));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(refs);xx_mem_free(b.p);return ok;
}

void xx_genomics_sam_init(xx_genomics_sam *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_SAM,"genomics_sam"); } }
xx_genomics_sam *xx_genomics_sam_create(xx_io_device *d,int64_t b) { xx_genomics_sam *r=(xx_genomics_sam *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_sam_init(r,d,b); return r; }
void xx_genomics_sam_destroy(xx_genomics_sam *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_sam_free(xx_genomics_sam *r) { if(r) { xx_genomics_sam_destroy(r); xx_mem_free(r); } }
bool xx_genomics_sam_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_sam_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
