/* SPDX-License-Identifier: MIT
 * Independently implemented from https://iupac.org/what-we-do/digital-standards/jcamp-dx/ */
#include "xxfclib/formats/jcamp_dx/xx_jcamp_dx.h"
#include "../xx_twelfth_root.h"
static bool tw_jkey(nh_blob *b,el_token line,char key[64],el_token *value) {uint64_t i=2;unsigned n=0;if(line.n<4 || b->p[(size_t)line.at]!='#' || b->p[(size_t)line.at+1]!='#') return false;while(i<line.n && b->p[(size_t)(line.at+i)]!='=') {uint8_t ch=b->p[(size_t)(line.at+i++)];if(ch==' ' || ch=='-' || ch=='/' || ch=='_') continue;if(ch>='a' && ch<='z') ch=(uint8_t)(ch-32);if(n>=63 || !((ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch=='.' || ch=='$')) return false;key[n++]=(char)ch;}if(!n || i==line.n) return false;key[n]=0;*value=el_trim(b,el_slice(line,i+1,line.n-i-1));return true;}
static bool tw_near(double a,double z) {double delta=a-z,scale=z<0 ? -z:z;if(delta<0) delta=-delta;return delta<=0.00001*(scale>1 ? scale:1);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,value,t[128];char names[256][64];unsigned labels=0,required=0,n;uint64_t points=0,seen=0;double first=0,last=0,xf=1,yf=1;bool ok=false,data=false,pairs=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;
    while(c.at<b.n) {uint64_t at=c.at;char key[64];unsigned i;NH_NEED(el_line(&c,&line) && tw_jkey(&b,line,key,&value) && labels<256);for(i=0;i<labels;++i) NH_NEED(xx_rt_strcmp(key,names[i]));xx_rt_memcpy(names[labels++],key,xx_rt_strlen(key)+1);
        if(!xx_rt_strcmp(key,"END")) {NH_NEED(data && seen==points && !value.n && nh_add(f,s,&b,"end",at,c.at-at) && tw_trailing(&c));ok=true;break;}
        NH_NEED(!data && value.n);
        if(!xx_rt_strcmp(key,"TITLE")) required|=1;
        else if(!xx_rt_strcmp(key,"JCAMPDX")) {NH_NEED(el_float(&b,value) && tw_value(&b,value)>=4 && tw_value(&b,value)<6);required|=2;}
        else if(!xx_rt_strcmp(key,"DATATYPE")) {NH_NEED(!el_eq(&b,value,"LINK"));required|=4;}
        else if(!xx_rt_strcmp(key,"XUNITS")) required|=8;
        else if(!xx_rt_strcmp(key,"YUNITS")) required|=16;
        else if(!xx_rt_strcmp(key,"FIRSTX")) {NH_NEED(el_float(&b,value));first=tw_value(&b,value);required|=32;}
        else if(!xx_rt_strcmp(key,"LASTX")) {NH_NEED(el_float(&b,value));last=tw_value(&b,value);required|=64;}
        else if(!xx_rt_strcmp(key,"XFACTOR")) {NH_NEED(el_float(&b,value));xf=tw_value(&b,value);NH_NEED(xf>0);required|=128;}
        else if(!xx_rt_strcmp(key,"YFACTOR")) {NH_NEED(el_float(&b,value));yf=tw_value(&b,value);NH_NEED(yf!=0);required|=256;}
        else if(!xx_rt_strcmp(key,"NPOINTS")) {NH_NEED(el_uint(&b,value,&points) && points>=1 && points<=1000000);required|=512;}
        else if(!xx_rt_strcmp(key,"XYDATA") || !xx_rt_strcmp(key,"XYPOINTS")) {
            pairs=!xx_rt_strcmp(key,"XYPOINTS");NH_NEED(required==1023 && (pairs ? el_eq(&b,value,"(XY..XY)"):el_eq(&b,value,"(X++(Y..Y))")) && (points>1 || tw_near(first,last)));data=true;
        } else NH_NEED(xx_rt_strcmp(key,"BLOCKS") && xx_rt_strcmp(key,"NTUPLES") && xx_rt_strcmp(key,"DATATABLE"));
        NH_NEED(nh_add(f,s,&b,"labeled-record",at,c.at-at));
        if(data) while(c.at<b.n && b.p[(size_t)c.at]!='#') {
            double x;at=c.at;NH_NEED(tw_words(&c,&line,t,128,&n) && n>=2 && tw_floats(&b,t,0,n));
            if(pairs) {NH_NEED(!(n&1) && n/2<=points-seen);for(i=0;i<n;i+=2) {x=tw_value(&b,t[i])*xf;NH_NEED(tw_near(x,x));if(!seen) NH_NEED(tw_near(x,first));++seen;if(seen==points) NH_NEED(tw_near(x,last));}}
            else {NH_NEED(n-1<=points-seen);x=tw_value(&b,t[0])*xf;NH_NEED(tw_near(x,first+(points>1 ? (last-first)/(double)(points-1)*(double)seen:0)));seen+=n-1;}
            NH_NEED(nh_add(f,s,&b,"spectral-data",at,c.at-at));
        }
    }
    NH_NEED(ok);s->size=(int64_t)b.n;
done:xx_mem_free(b.p);return ok;
}

void xx_jcamp_dx_init(xx_jcamp_dx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_JCAMP_DX,"jcamp_dx"); } }
xx_jcamp_dx *xx_jcamp_dx_create(xx_io_device *d,int64_t b) { xx_jcamp_dx *r=(xx_jcamp_dx *)xx_mem_alloc(sizeof(*r)); if(r) xx_jcamp_dx_init(r,d,b); return r; }
void xx_jcamp_dx_destroy(xx_jcamp_dx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_jcamp_dx_free(xx_jcamp_dx *r) { if(r) { xx_jcamp_dx_destroy(r); xx_mem_free(r); } }
bool xx_jcamp_dx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_jcamp_dx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
