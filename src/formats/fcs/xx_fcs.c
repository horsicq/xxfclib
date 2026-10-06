/* SPDX-License-Identifier: MIT
 * Independently implemented from https://pmc.ncbi.nlm.nih.gov/articles/PMC2892967/ */
#include "xxfclib/formats/fcs/xx_fcs.h"
#include "../xx_sixth_data.h"

typedef struct fcs_pair {char *key,*value;} fcs_pair;
static const char *fcs_find(fcs_pair *p,unsigned n,const char *key) {unsigned i;for(i=0;i<n;++i) if(!xx_rt_strcmp(p[i].key,key)) return p[i].value;return NULL;}
static bool fcs_num(fcs_pair *p,unsigned n,const char *key,uint64_t *v) {const char *s=fcs_find(p,n,key);if(!s || !*s || *s<'0' || *s>'9') return false;return sd_uint(s,v) && s[xx_rt_strlen(s)-1]>='0' && s[xx_rt_strlen(s)-1]<='9';}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[58];uint64_t ts,te,ds,de,an0,an1,par,tot,begin,end,next,zero,n;char *text=NULL,*p,*dest,*limit;fcs_pair *pairs=NULL;unsigned count=0,i,width;bool ok=false;int64_t available=pm_available(f);const char *value;
    if(fd_stop(pd) || available<58 || !pm_read(f,0,h,58) || (xx_rt_memcmp(h,"FCS3.0",6) && xx_rt_memcmp(h,"FCS3.1",6)) || xx_rt_memcmp(h+6,"    ",4)) return false;
    if(!sd_fixed_uint(h+10,8,&ts) || !sd_fixed_uint(h+18,8,&te) || !sd_fixed_uint(h+26,8,&ds) || !sd_fixed_uint(h+34,8,&de) || !sd_fixed_uint(h+42,8,&an0) || !sd_fixed_uint(h+50,8,&an1) || an0 || an1 || ts<58 || te<ts || te-ts>=262144 || !fd_range(ts,te-ts+1,(uint64_t)available)) return false;
    n=te-ts+1;text=(char *)xx_mem_alloc((size_t)n+1);pairs=(fcs_pair *)xx_mem_alloc(4096*sizeof(*pairs));if(!text || !pairs || !pm_read(f,(int64_t)ts,text,(size_t)n)) goto done;text[n]=0;
    {char delim=text[0];if(!delim) goto done;p=text+1;dest=p;limit=text+n;
        while(p<limit) {char *tokens[2];unsigned t;
            if(fd_stop(pd) || count>=4096) goto done;
            for(t=0;t<2;++t) {bool ended=false;tokens[t]=dest;
                while(p<limit) {char ch=*p++;if(ch==delim) {if(p<limit && *p==delim) {++p;*dest++=delim;}else {ended=true;break;}}else {if(!ch) goto done;*dest++=ch;}}
                if(!ended || dest==tokens[t]) { goto done; } *dest++=0;
            }
            if(tokens[0][0]!='$') { /* Nonstandard optional keywords are retained. */ }
            for(i=0;i<count;++i) if(!xx_rt_strcmp(pairs[i].key,tokens[0])) goto done;
            pairs[count].key=tokens[0];pairs[count].value=tokens[1];++count;
        }
    }
    if(!fcs_num(pairs,count,"$PAR",&par) || !par || par>256 || !fcs_num(pairs,count,"$TOT",&tot) || !tot || !fcs_num(pairs,count,"$BEGINDATA",&begin) || !fcs_num(pairs,count,"$ENDDATA",&end) || begin<=te || end<begin || (ds && ds!=begin) || (de && de!=end) || !fcs_num(pairs,count,"$NEXTDATA",&next) || next) goto done;
    for(i=0;i<4;++i) {static const char *keys[]={"$BEGINANALYSIS","$ENDANALYSIS","$BEGINSTEXT","$ENDSTEXT"};if(!fcs_num(pairs,count,keys[i],&zero) || zero) goto done;}
    value=fcs_find(pairs,count,"$MODE");if(!value || xx_rt_strcmp(value,"L")) goto done;
    value=fcs_find(pairs,count,"$DATATYPE");if(!value) goto done;if(!xx_rt_strcmp(value,"F")) width=4;else if(!xx_rt_strcmp(value,"D")) width=8;else goto done;
    value=fcs_find(pairs,count,"$BYTEORD");if(!value || (xx_rt_strcmp(value,"1,2,3,4") && xx_rt_strcmp(value,"4,3,2,1"))) goto done;
    for(i=1;i<=par;++i) {char key[32];uint64_t bits,range;
        xx_rt_snprintf(key,sizeof(key),"$P%uB",i);if(!fcs_num(pairs,count,key,&bits) || bits!=width*8) goto done;
        xx_rt_snprintf(key,sizeof(key),"$P%uR",i);if(!fcs_num(pairs,count,key,&range) || !range) goto done;
        xx_rt_snprintf(key,sizeof(key),"$P%uE",i);value=fcs_find(pairs,count,key);if(!value || xx_rt_strcmp(value,"0,0")) goto done;
        xx_rt_snprintf(key,sizeof(key),"$P%uN",i);value=fcs_find(pairs,count,key);if(!value || !*value) goto done;
    }
    if(!fd_mul(par,tot,&n) || !fd_mul(n,width,&n) || n!=end-begin+1 || !fd_range(begin,n,(uint64_t)available) || !pm_add(f,s,"fcs-header.txt",0,58) || !pm_add(f,s,"fcs-text.txt",(int64_t)ts,(int64_t)(te-ts+1)) || !pm_add(f,s,"events.bin",(int64_t)begin,(int64_t)n)) goto done;
    s->size=(int64_t)(begin+n);ok=true;
done:if(text) xx_mem_free(text);if(pairs) xx_mem_free(pairs);return ok;
}

void xx_fcs_init(xx_fcs *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_FCS,"fcs"); } }
xx_fcs *xx_fcs_create(xx_io_device *d,int64_t b) { xx_fcs *r=(xx_fcs *)xx_mem_alloc(sizeof(*r)); if(r) xx_fcs_init(r,d,b); return r; }
void xx_fcs_destroy(xx_fcs *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_fcs_free(xx_fcs *r) { if(r) { xx_fcs_destroy(r); xx_mem_free(r); } }
bool xx_fcs_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_fcs_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
