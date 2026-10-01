/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/theochem/iodata/blob/master/iodata/formats/fchk.py */
#include "xxfclib/formats/gaussian_fchk/xx_gaussian_fchk.h"
#include "../xx_twelfth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[4],labels[4096];unsigned n,fields=0,ff_count=0;uint64_t atoms=0,zcount=0,ccount=0,budget=8388608,ff_value=0;bool ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;NH_NEED(el_line(&c,&line) && line.n && line.n<=80 && tw_words(&c,&line,t,4,&n) && n==3 && (el_eq(&b,t[0],"SP") || el_eq(&b,t[0],"FOpt") || el_eq(&b,t[0],"Freq") || el_eq(&b,t[0],"Scan")) && nh_add(f,s,&b,"checkpoint-header",0,c.at));
    while(c.at<b.n) {uint64_t begin=c.at,count,i;el_token name;bool real,array,az,coords;uint8_t type;
        NH_NEED(el_line(&c,&line) && line.n>=47 && fields<4096 && b.p[(size_t)line.at+40]==' ' && b.p[(size_t)line.at+41]==' ' && b.p[(size_t)line.at+42]==' ' && b.p[(size_t)line.at+44]==' ');
        name=el_trim(&b,el_slice(line,0,40));NH_NEED(name.n && (el_eq(&b,name,"Force Field") || el_unique(&b,name,labels,fields,&budget)));labels[fields++]=name;type=b.p[(size_t)line.at+43];NH_NEED(type=='I' || type=='R');real=type=='R';
        NH_NEED(el_split(&b,el_slice(line,45,line.n-45),t,4,&n,false) && (n==1 || n==2));array=n==2;
        az=el_eq(&b,name,"Atomic numbers");coords=el_eq(&b,name,"Current cartesian coordinates");
        if(el_eq(&b,name,"Force Field")) {uint64_t v;NH_NEED(!real && !array && el_uint(&b,t[0],&v) && ++ff_count<=2);if(ff_count==2) NH_NEED(v==ff_value);ff_value=v;}
        if(array) {NH_NEED(el_eq(&b,t[0],"N=") && el_uint(&b,t[1],&count) && count<=1000000);if(az) {NH_NEED(!real && count && !zcount);zcount=count;}if(coords) {NH_NEED(real && count && !ccount);ccount=count;}
            for(i=0;i<count;) {el_token values;unsigned got,j,width=real ? 16:12,perline=real ? 5:6;NH_NEED(el_line(&c,&values) && tw_fixed(&b,values,width,&got,real) && got==((count-i)<perline ? (unsigned)(count-i):perline));if(az) for(j=0;j<got;++j) NH_NEED(tw_z(&b,el_trim(&b,el_slice(values,j*width,width))));i+=got;}
        } else {NH_NEED(real ? el_float(&b,t[0]):el_integer(&b,t[0]));NH_NEED(!az && !coords);if(el_eq(&b,name,"Number of atoms")) NH_NEED(!real && el_uint(&b,t[0],&atoms) && atoms && atoms<=100000);}
        NH_NEED(nh_add(f,s,&b,"checkpoint-field",begin,c.at-begin));
    }
    NH_NEED(atoms && zcount==atoms && ccount==atoms*3);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_gaussian_fchk_init(xx_gaussian_fchk *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GAUSSIAN_FCHK,"gaussian_fchk"); } }
xx_gaussian_fchk *xx_gaussian_fchk_create(xx_io_device *d,int64_t b) { xx_gaussian_fchk *r=(xx_gaussian_fchk *)xx_mem_alloc(sizeof(*r)); if(r) xx_gaussian_fchk_init(r,d,b); return r; }
void xx_gaussian_fchk_destroy(xx_gaussian_fchk *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_gaussian_fchk_free(xx_gaussian_fchk *r) { if(r) { xx_gaussian_fchk_destroy(r); xx_mem_free(r); } }
bool xx_gaussian_fchk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_gaussian_fchk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
