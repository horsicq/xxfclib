/* SPDX-License-Identifier: MIT
 * Independently implemented from https://samtools.github.io/hts-specs/VCFv4.3.pdf */
#include "xxfclib/formats/genomics_vcf/xx_genomics_vcf.h"
#include "../xx_eleventh_data.h"
static bool vcf_key(nh_blob *b,el_token v) {return v.n && v.n<=255 && el_chars(b,v,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-",true);}
static bool vcf_meta(nh_blob *b,el_token line) {
    uint64_t i=2,eq;bool quoted=false,escaped=false;unsigned brackets=0;if(!el_prefix(b,line,"##") || line.n<4) return false;
    while(i<line.n && b->p[(size_t)(line.at+i)]!='=') ++i;eq=i;if(eq==line.n || !vcf_key(b,el_slice(line,2,eq-2)) || ++i==line.n) return false;
    for(;i<line.n;++i) {uint8_t ch=b->p[(size_t)(line.at+i)];if(escaped) {if(ch!='\\' && ch!='"') return false;escaped=false;continue;}if(quoted && ch=='\\') {escaped=true;continue;}if(ch=='"') quoted=!quoted;else if(!quoted && ch=='<') {if(++brackets>1) return false;}else if(!quoted && ch=='>') {if(!brackets) return false;--brackets;}}
    return !quoted && !escaped && !brackets;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[10];uint64_t count=0,budget=8388608;unsigned nt;bool ok=false,columns=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;NH_NEED(el_line(&c,&line) && line.n==20 && el_prefix(&b,line,"##fileformat=VCFv4.") && b.p[(size_t)line.at+19]>='1' && b.p[(size_t)line.at+19]<='3');
    while(c.at<b.n) {
        uint64_t start=c.at;NH_NEED(el_line(&c,&line) && line.n);
        if(!columns) {
            if(el_prefix(&b,line,"##")) {NH_NEED(!el_prefix(&b,line,"##fileformat") && vcf_meta(&b,line));continue;}
            NH_NEED(el_eq(&b,line,"#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO"));columns=true;NH_NEED(nh_add(f,s,&b,"header",0,c.at));continue;
        }
        NH_NEED(++count<=4094 && el_split(&b,line,t,10,&nt,true) && nt==8);
        {uint64_t pos;el_token alts[512];unsigned na,i;NH_NEED(el_ident(&b,t[0]) && el_uint(&b,t[1],&pos) && pos && pos<=2147483647 && el_chars(&b,t[3],"ACGTNacgtn",true) && (el_eq(&b,t[5],".") || (el_float(&b,t[5]) && b.p[(size_t)t[5].at]!='-')));
         NH_NEED(el_sep(&b,t[4],',',alts,512,&na));
         for(i=0;i<na;++i) {if(el_eq(&b,alts[i],".") || el_eq(&b,alts[i],"*")) continue;if(b.p[(size_t)alts[i].at]=='<') {NH_NEED(alts[i].n>2 && b.p[(size_t)(alts[i].at+alts[i].n-1)]=='>' && el_chars(&b,el_slice(alts[i],1,alts[i].n-2),"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_:.-",true));}else NH_NEED(el_chars(&b,alts[i],"ACGTNacgtn",true));}
         NH_NEED(el_ident(&b,t[2]));
        }
        if(!el_eq(&b,t[6],".") && !el_eq(&b,t[6],"PASS")) {el_token filter[128];unsigned n,i;NH_NEED(el_sep(&b,t[6],';',filter,128,&n));for(i=0;i<n;++i) NH_NEED(vcf_key(&b,filter[i]) && !el_eq(&b,filter[i],"PASS"));}
        if(!el_eq(&b,t[7],".")) {
            el_token attrs[128],keys[128];unsigned n,i,j;NH_NEED(el_sep(&b,t[7],';',attrs,128,&n));
            for(i=0;i<n;++i) {uint64_t at=0,k;while(at<attrs[i].n && b.p[(size_t)(attrs[i].at+at)]!='=') ++at;keys[i]=el_slice(attrs[i],0,at);NH_NEED(vcf_key(&b,keys[i]));for(j=0;j<i;++j) {NH_NEED(budget);--budget;NH_NEED(keys[i].n!=keys[j].n || xx_rt_memcmp(b.p+(size_t)keys[i].at,b.p+(size_t)keys[j].at,(size_t)keys[i].n));}if(at<attrs[i].n) {NH_NEED(at+1<attrs[i].n);for(k=at+1;k<attrs[i].n;++k) {uint8_t ch=b.p[(size_t)(attrs[i].at+k)];NH_NEED(ch>32 && ch!='=' && ch!=';');if(ch=='%') {NH_NEED(k+2<attrs[i].n && el_chars(&b,el_slice(attrs[i],k+1,2),"0123456789ABCDEFabcdef",true));k+=2;}}}}
        }
        NH_NEED(nh_add(f,s,&b,"variant",start,c.at-start));
    }
    NH_NEED(columns && count);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_genomics_vcf_init(xx_genomics_vcf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_VCF,"genomics_vcf"); } }
xx_genomics_vcf *xx_genomics_vcf_create(xx_io_device *d,int64_t b) { xx_genomics_vcf *r=(xx_genomics_vcf *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_vcf_init(r,d,b); return r; }
void xx_genomics_vcf_destroy(xx_genomics_vcf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_vcf_free(xx_genomics_vcf *r) { if(r) { xx_genomics_vcf_destroy(r); xx_mem_free(r); } }
bool xx_genomics_vcf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_vcf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
