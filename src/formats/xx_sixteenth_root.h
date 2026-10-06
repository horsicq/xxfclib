/* SPDX-License-Identifier: MIT. Bounded inert scientific input grammars. */
#ifndef XX_SIXTEENTH_ROOT_H
#define XX_SIXTEENTH_ROOT_H
#include "xx_thirteenth_root.h"
static bool f16_line(el_lines *c,el_token *v) {
    uint64_t a=c->at,z;nh_blob *b=c->b;
    if(a>=b->n || ++c->lines>65536 || fd_stop(b->pd)) return false;
    while(c->at<b->n && b->p[(size_t)c->at]!=10) {
        uint8_t x=b->p[(size_t)c->at];
        if(!x || x>126 || (x<32 && x!=9 && x!=13) || c->at-a>=4096) {c->at=b->n+1;return false;}++c->at;
    }
    z=c->at;if(c->at<b->n) ++c->at;
    if(z>a && b->p[(size_t)z-1]==13) --z;
    v->at=a;v->n=z-a;
    {uint64_t i;for(i=a;i<z;++i) if(b->p[(size_t)i]==13) {c->at=b->n+1;return false;}}
    return true;
}
static bool f16_words(el_lines *c,el_token *line,el_token *t,unsigned cap,unsigned *n,const char *comments) {
    while(c->at<c->b->n) {
        if(!f16_line(c,line)) { return false; } *line=th_comment(c->b,*line,comments);
        if(!line->n) continue;
        if(!el_split(c->b,*line,t,cap,n,false)) {c->at=c->b->n+1;return false;}return true;
    }return false;
}
static bool f16_finish(el_lines *c,const char *comments) {
    el_token line;
    while(c->at<c->b->n) if(!f16_line(c,&line) || th_comment(c->b,line,comments).n) return false;
    return c->at==c->b->n && !fd_stop(c->b->pd);
}
static bool f16_in(nh_blob *b,el_token t,const char *const *names,unsigned n) {
    unsigned i;for(i=0;i<n;++i) if(th_eq(b,t,names[i])) return true;return false;
}
static unsigned f16_atomic_number(nh_blob *b,el_token t) {
    unsigned i;for(i=1;i<=118;++i) if(th_eq(b,t,tw_elements[i])) return i;return 0;
}
static bool f16_identifier(nh_blob *b,el_token t) {
    return t.n && t.n<=128 && el_chars(b,t,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.+",true);
}
static bool f16_same_key(nh_blob *b,el_token a,el_token z) {
    uint64_t i;if(a.n!=z.n) return false;
    for(i=0;i<a.n;++i) {unsigned x=b->p[(size_t)(a.at+i)],y=b->p[(size_t)(z.at+i)];if(x>='A' && x<='Z') x+=32;if(y>='A' && y<='Z') y+=32;if(x!=y) return false;}return true;
}
static bool f16_cell_valid(nh_blob *b,el_token t[3][3]) {
    double a[3][3],d;unsigned i,j;
    for(i=0;i<3;++i) for(j=0;j<3;++j) {if(!el_float(b,t[i][j])) return false;a[i][j]=tw_value(b,t[i][j]);}
    d=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    return d==d && ((d>1e-18 && d<1e240) || (d< -1e-18 && d> -1e240));
}
static bool f16_cell_comments(el_lines *c,const char *comments) {
    el_token row,t[3][3];unsigned i,n;
    for(i=0;i<3;++i) if(!f16_words(c,&row,t[i],3,&n,comments) || n!=3) return false;
    return f16_cell_valid(c->b,t);
}
static bool f16_cell(el_lines *c) {return f16_cell_comments(c,"#");}
static bool f16_atom(nh_blob *b,el_token *t,unsigned n,unsigned symbol,unsigned xyz) {
    return n>=xyz+3 && symbol<n && f16_atomic_number(b,t[symbol]) && th_floats(b,t+xyz,3);
}
static uint64_t f16_row_start(nh_blob *b,el_token row) {
    uint64_t at=row.at;while(at && b->p[(size_t)at-1]!=10) --at;return at;
}
static bool f16_add_row(Abstractformat *f,pm_stream *s,nh_blob *b,const char *label,el_token row,uint64_t end) {
    uint64_t at=f16_row_start(b,row);return at<end && nh_add(f,s,b,label,at,end-at);
}
static bool f16_vasp(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token row,t[128],species[128];unsigned n,k,ns,count=0;uint64_t v,coords;bool selective=false;c.b=b;
    if(!f16_line(&c,&row) || !el_trim(b,row).n || !f16_words(&c,&row,t,128,&n,"#") || n!=1 || !th_positive(b,t[0]) || !f16_cell(&c)) return false;
    if(!f16_words(&c,&row,t,128,&ns,"#") || !ns || ns>118) return false;
    for(k=0;k<ns;++k) {unsigned j;if(!f16_atomic_number(b,t[k])) return false;for(j=0;j<k;++j) if(th_same(b,t[k],species[j])) return false;species[k]=t[k];}
    if(!f16_words(&c,&row,t,128,&n,"#") || n!=ns) return false;
    for(k=0;k<n;++k) {if(!el_uint(b,t[k],&v) || !v || v>4090-count) return false;count+=(unsigned)v;}
    if(!f16_words(&c,&row,t,128,&n,"#") || !n) return false;
    if(th_eq(b,t[0],"Selective")) {if(n>2 || (n==2 && !th_eq(b,t[1],"dynamics"))) return false;selective=true;if(!f16_words(&c,&row,t,128,&n,"#") || n!=1) return false;}
    else if(n!=1) return false;
    if(!th_eq(b,t[0],"Direct") && !th_eq(b,t[0],"Cartesian")) { return false; } coords=c.at;
    if(!nh_add(f,s,b,"structure-header",0,coords)) return false;
    for(k=0;k<count;++k) {
        if(!f16_words(&c,&row,t,128,&n,"#") || n!=(selective?6U:3U) || !th_floats(b,t,3)) return false;
        if(selective) {unsigned j;for(j=3;j<6;++j) if(!th_eq(b,t[j],"T") && !th_eq(b,t[j],"F")) return false;}
        if(!f16_add_row(f,s,b,"atomic-position",row,c.at)) return false;
    }return f16_finish(&c,"#");
}
static bool f16_aims(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token row,t[8],cell[3][3];unsigned n,atoms=0,vectors=0,mode=0;bool constrained=false,spin=false,charge=false;c.b=b;
    while(f16_words(&c,&row,t,8,&n,"#")) {
        const char *label;
        if(th_eq(b,t[0],"lattice_vector")) {
            unsigned j;if(atoms || vectors==3 || n!=4 || !th_floats(b,t+1,3)) return false;
            for(j=0;j<3;++j) { cell[vectors][j]=t[j+1]; } ++vectors;label="lattice-vector";
        }else if(th_eq(b,t[0],"atom") || th_eq(b,t[0],"atom_frac")) {
            unsigned m=th_eq(b,t[0],"atom_frac")?2U:1U;
            if(n!=5 || ++atoms>4090 || (mode && mode!=m) || (m==2 && vectors!=3) || !f16_atom(b,t,n,4,1)) return false;
            mode=m;constrained=spin=charge=false;label="atomic-position";
        }else if(th_eq(b,t[0],"constrain_relaxation")) {
            if(!atoms || constrained || n!=2 || (!th_eq(b,t[1],".true.") && !th_eq(b,t[1],"x") && !th_eq(b,t[1],"y") && !th_eq(b,t[1],"z"))) return false;
            constrained=true;label="atom-constraint";
        }else if(th_eq(b,t[0],"initial_moment") || th_eq(b,t[0],"initial_charge")) {
            bool isspin=th_eq(b,t[0],"initial_moment");if(!atoms || n!=2 || !el_float(b,t[1]) || (isspin?spin:charge)) return false;
            if(isspin) spin=true;else charge=true;label=isspin?"atom-spin":"atom-charge";
        }else return false;
        if(!f16_add_row(f,s,b,label,row,c.at)) return false;
    }return c.at==b->n && atoms && (!vectors || (vectors==3 && f16_cell_valid(b,cell))) && !fd_stop(b->pd);
}
static bool f16_orca(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token row,t[64];unsigned n,atoms=0,options=0;uint64_t mult,begin;c.b=b;
    if(!f16_words(&c,&row,t,64,&n,"#") || n<2 || !el_eq(b,t[0],"!") || row.n>1024) return false;
    {unsigned i;for(i=1;i<n;++i) if(!el_chars(b,t[i],"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_+*-()/=,.",true)) return false;}
    while(f16_words(&c,&row,t,64,&n,"#")) {
        if(th_eq(b,t[0],"%maxcore")) {uint64_t x;if((options&1) || n!=2 || !el_uint(b,t[1],&x) || !x || x>1048576) return false;options|=1;}
        else if(th_eq(b,t[0],"%pal")) {
            if((options&2) || n!=1 || !f16_words(&c,&row,t,64,&n,"#") || n!=2 || !th_eq(b,t[0],"nprocs") || !el_uint(b,t[1],&mult) || !mult || mult>65536 || !f16_words(&c,&row,t,64,&n,"#") || n!=1 || !th_eq(b,t[0],"end")) { return false; } options|=2;
        }else break;
    }
    if(n!=3 || !th_eq(b,t[0],"*xyz") || !el_range(b,t[1],1000,1000) || !el_uint(b,t[2],&mult) || !mult || mult>1000) return false;
    begin=c.at;if(!nh_add(f,s,b,"calculation-header",0,begin)) return false;
    while(f16_words(&c,&row,t,64,&n,"#")) {
        if(n==1 && el_eq(b,t[0],"*")) return atoms && f16_add_row(f,s,b,"geometry-terminator",row,c.at) && f16_finish(&c,"#");
        if(n!=4 || ++atoms>4090 || !f16_atom(b,t,n,0,1) || !f16_add_row(f,s,b,"atomic-position",row,c.at)) return false;
    }return false;
}
static bool f16_gaussian_link0(nh_blob *b,el_token key,el_token value) {
    static const char *const units[]={"B","W","KB","KW","MB","MW","GB","GW"};
    uint64_t x,k=0;value=el_trim(b,value);
    if(!value.n || value.n>255) return false;
    if(th_eq(b,key,"chk")) {
        for(k=0;k<value.n;++k) if(b->p[(size_t)(value.at+k)]<=32) return false;
        return true;
    }
    if(th_eq(b,key,"nprocshared")) return el_uint(b,value,&x) && x && x<=65536;
    if(!th_eq(b,key,"mem")) return false;
    while(k<value.n && b->p[(size_t)(value.at+k)]>='0' && b->p[(size_t)(value.at+k)]<='9') ++k;
    if(!k || !el_uint(b,el_slice(value,0,k),&x) || !x || x>1048576) return false;
    return k==value.n || f16_in(b,el_slice(value,k,value.n-k),units,8);
}
static bool f16_gaussian(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token row,t[16],cell[3][3];unsigned n,atoms=0,vectors=0,title=0;uint64_t mult,begin;bool route=false;c.b=b;
    while(f16_line(&c,&row)) {
        row=el_trim(b,row);if(!row.n) {if(route) break;return false;}
        if(b->p[(size_t)row.at]=='%') {
            uint64_t k=0;if(route || row.n>255) return false;
            while(k<row.n && b->p[(size_t)(row.at+k)]!='=') ++k;
            if(k<2 || k==row.n) return false;
            if(!f16_gaussian_link0(b,el_slice(row,1,k-1),el_slice(row,k+1,row.n-k-1))) return false;
        }else {
            if(!route && b->p[(size_t)row.at]!='#') { return false; } route=true;
            if(row.n>1024 || !el_chars(b,row,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 \t#!=/()_,.+-*",true)) return false;
        }
    }if(!route || !row.n) {if(!route) return false;}
    while(f16_line(&c,&row)) {row=el_trim(b,row);if(!row.n) break;if(++title>8 || row.n>255) return false;}
    if(!title || !f16_words(&c,&row,t,16,&n,"!") || n!=2 || !el_range(b,t[0],1000,1000) || !el_uint(b,t[1],&mult) || !mult || mult>1000) return false;
    begin=c.at;if(!nh_add(f,s,b,"calculation-header",0,begin)) return false;
    while(f16_line(&c,&row)) {
        row=el_trim(b,row);if(!row.n) break;
        if(!el_split(b,row,t,16,&n,false) || n!=4 || !th_floats(b,t+1,3)) return false;
        if(th_eq(b,t[0],"TV")) {unsigned j;if(!atoms || vectors==3) return false;for(j=0;j<3;++j) cell[vectors][j]=t[j+1];++vectors;}
        else if(vectors || !f16_atomic_number(b,t[0]) || ++atoms>4090) return false;
        if(!f16_add_row(f,s,b,vectors?"lattice-vector":"atomic-position",row,c.at)) return false;
    }return atoms && (!vectors || (vectors==3 && f16_cell_valid(b,cell))) && f16_finish(&c,"");
}
static bool f16_demon(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token row,t[16];unsigned n,atoms=0,seen=0;uint64_t begin;c.b=b;
    while(f16_words(&c,&row,t,16,&n,"#")) {
        unsigned bit;
        if(th_eq(b,t[0],"GEOMETRY")) break;
        if(th_eq(b,t[0],"TITLE")) {if(n<2) return false;bit=1;}
        else if(th_eq(b,t[0],"SCFTYPE")) {if(n!=2 || (!th_eq(b,t[1],"RKS") && !th_eq(b,t[1],"UKS") && !th_eq(b,t[1],"RHF") && !th_eq(b,t[1],"UHF"))) return false;bit=2;}
        else if(th_eq(b,t[0],"VXCTYPE")) {if(n!=2 || !f16_identifier(b,t[1])) return false;bit=4;}
        else if(th_eq(b,t[0],"GUESS")) {if(n!=2 || !f16_identifier(b,t[1])) return false;bit=8;}
        else if(th_eq(b,t[0],"PRINT")) {if(n<2) return false;bit=16;}
        else if(th_eq(b,t[0],"BASIS")) {if(n!=2 || t[1].n<3 || b->p[(size_t)t[1].at]!='(' || b->p[(size_t)(t[1].at+t[1].n-1)]!=')' || !f16_identifier(b,el_slice(t[1],1,t[1].n-2))) return false;bit=32;}
        else return false;
        if(seen&bit) { return false; } seen|=bit;
    }
    if((seen&47)!=47 || n!=3 || !th_eq(b,t[0],"GEOMETRY") || !th_eq(b,t[1],"CARTESIAN") || !th_eq(b,t[2],"ANGSTROM")) return false;
    begin=c.at;if(!nh_add(f,s,b,"calculation-header",0,begin)) return false;
    while(f16_words(&c,&row,t,16,&n,"#")) {
        uint64_t k=0,z;el_token symbol,labelnum;
        if(n!=6 || ++atoms>4090 || !th_floats(b,t+1,3) || !el_uint(b,t[4],&z) || !th_positive(b,t[5])) return false;
        while(k<t[0].n && ((b->p[(size_t)(t[0].at+k)]>='A' && b->p[(size_t)(t[0].at+k)]<='Z') || (b->p[(size_t)(t[0].at+k)]>='a' && b->p[(size_t)(t[0].at+k)]<='z'))) ++k;
        symbol=el_slice(t[0],0,k);labelnum=el_slice(t[0],k,t[0].n-k);
        if(z!=f16_atomic_number(b,symbol) || !z || !el_uint(b,labelnum,&k) || k!=atoms || !f16_add_row(f,s,b,"atomic-position",row,c.at)) return false;
    }return c.at==b->n && atoms && !fd_stop(b->pd);
}
static bool f16_assignment(nh_blob *b,el_token line,el_token *key,el_token *value) {
    uint64_t at=0;line=el_trim(b,line);
    while(at<line.n && b->p[(size_t)(line.at+at)]!='=') ++at;
    if(!at || at==line.n) return false;
    *key=el_trim(b,el_slice(line,0,at));*value=el_trim(b,el_slice(line,at+1,line.n-at-1));
    if(!f16_identifier(b,*key) || !value->n) return false;
    if(b->p[(size_t)(value->at+value->n-1)]==',') {--value->n;*value=el_trim(b,*value);}
    if(!value->n || value->n>255) return false;
    if(b->p[(size_t)value->at]=='\'' || b->p[(size_t)value->at]=='"') {
        uint8_t quote=b->p[(size_t)value->at];uint64_t i;
        if(value->n<3 || b->p[(size_t)(value->at+value->n-1)]!=quote) return false;
        for(i=1;i+1<value->n;++i) if(b->p[(size_t)(value->at+i)]==quote || b->p[(size_t)(value->at+i)]<32) return false;
        *value=el_slice(*value,1,value->n-2);return true;
    }return el_float(b,*value) || f16_identifier(b,*value);
}
static bool f16_qe(Abstractformat *f,pm_stream *s,nh_blob *b) {
    static const char *const groups[]={"&CONTROL","&SYSTEM","&ELECTRONS","&IONS","&CELL","&FCP","&RISM"};
    el_lines c={0};el_token row,t[16],species[118],keys[32];unsigned n,g=0,block=0,keycount=0,required=0,cards=0,k,atoms=0,types=0;uint64_t start=0,count;bool in=false,calculation=false;c.b=b;
    while(f16_words(&c,&row,t,16,&n,"!")) {
        if(in) {
            if(n==1 && el_eq(b,t[0],"/")) {if(!nh_add(f,s,b,"namelist",start,c.at-start)) return false;in=false;continue;}
            {el_token key,value;unsigned j;
                if(!f16_assignment(b,row,&key,&value) || keycount==32) return false;
                for(j=0;j<keycount;++j) { if(f16_same_key(b,key,keys[j])) return false; } keys[keycount++]=key;
                if(block==1) {
                    if(th_eq(b,key,"calculation")) {if(!th_eq(b,value,"scf")) return false;calculation=true;}
                    else if(!th_eq(b,key,"prefix") && !th_eq(b,key,"outdir") && !th_eq(b,key,"pseudo_dir") && !th_eq(b,key,"verbosity") && !th_eq(b,key,"disk_io")) return false;
                }else if(block==2) {
                    if(th_eq(b,key,"nat")) {if(!el_uint(b,value,&count) || !count || count>4090) return false;atoms=(unsigned)count;required|=1;}
                    else if(th_eq(b,key,"ntyp")) {if(!el_uint(b,value,&count) || !count || count>118) return false;types=(unsigned)count;required|=2;}
                    else if(th_eq(b,key,"ibrav")) {if(!el_zero(b,value)) return false;required|=4;}
                    else if(th_eq(b,key,"ecutwfc") || th_eq(b,key,"ecutrho")) {if(!th_positive(b,value)) return false;if(th_eq(b,key,"ecutwfc")) required|=8;}
                    else if(th_eq(b,key,"tot_charge")) {if(!el_float(b,value)) return false;}
                    else return false;
                }else if(block==3) {
                    if(th_eq(b,key,"conv_thr") || th_eq(b,key,"mixing_beta")) {if(!th_positive(b,value)) return false;}
                    else if(th_eq(b,key,"electron_maxstep")) {if(!el_uint(b,value,&count) || !count || count>100000) return false;}
                    else return false;
                }else return false;
            }
        }else {
            unsigned match;
            for(match=g;match<7;++match) if(n==1 && th_eq(b,t[0],groups[match])) break;
            if(match==7) break;
            if((g==0 && match!=0) || (g==1 && match!=1) || (g==2 && match!=2)) return false;
            block=match+1;g=block;start=f16_row_start(b,row);keycount=0;in=true;
        }
    }
    if(in || g<3 || required!=15 || !calculation || n!=1 || !th_eq(b,t[0],"ATOMIC_SPECIES")) return false;
    if(!f16_add_row(f,s,b,"species-declaration",row,c.at)) return false;
    for(k=0;k<types;++k) {
        unsigned j;
        if(!f16_words(&c,&row,t,16,&n,"!#") || n!=3 || !f16_atomic_number(b,t[0]) || !th_positive(b,t[1]) || !el_ident(b,t[2])) return false;
        for(j=0;j<k;++j) { if(th_same(b,t[0],species[j])) return false; } species[k]=t[0];
        if(!f16_add_row(f,s,b,"atomic-species",row,c.at)) return false;
    }
    while(f16_words(&c,&row,t,16,&n,"!#")) {
        if(th_eq(b,t[0],"K_POINTS")) {
            if((cards&1) || n!=2 || (!th_eq(b,t[1],"gamma") && !th_eq(b,t[1],"automatic"))) { return false; } cards|=1;start=f16_row_start(b,row);
            if(th_eq(b,t[1],"automatic")) {unsigned j;if(!f16_words(&c,&row,t,16,&n,"!#") || n!=6) return false;for(j=0;j<6;++j) if(!el_uint(b,t[j],&count) || (j<3?(!count || count>1024):count>1)) return false;}
            if(!nh_add(f,s,b,"k-point-grid",start,c.at-start)) return false;
        }else if(th_eq(b,t[0],"CELL_PARAMETERS")) {
            if((cards&2) || n!=2 || (!th_eq(b,t[1],"angstrom") && !th_eq(b,t[1],"bohr"))) { return false; } cards|=2;start=f16_row_start(b,row);
            if(!f16_cell_comments(&c,"!#") || !nh_add(f,s,b,"lattice-vectors",start,c.at-start)) return false;
        }else if(th_eq(b,t[0],"ATOMIC_POSITIONS")) {
            if((cards&4) || n!=2 || (!th_eq(b,t[1],"angstrom") && !th_eq(b,t[1],"bohr") && !th_eq(b,t[1],"crystal"))) { return false; } cards|=4;
            if(!f16_add_row(f,s,b,"coordinate-declaration",row,c.at)) return false;
            for(k=0;k<atoms;++k) {
                unsigned j;
                if(!f16_words(&c,&row,t,16,&n,"!#") || (n!=4 && n!=7) || !th_floats(b,t+1,3)) return false;
                for(j=0;j<types;++j) { if(th_same(b,t[0],species[j])) break; } if(j==types) return false;
                if(n==7) for(j=4;j<7;++j) if(!el_uint(b,t[j],&count) || count>1) return false;
                if(!f16_add_row(f,s,b,"atomic-position",row,c.at)) return false;
            }
        }else return false;
    }return c.at==b->n && cards==7 && !fd_stop(b->pd);
}
static unsigned f16_cp2k_keyword_bit(nh_blob *b,el_token key) {
    static const char *const keywords[]={"ABC","PRINT_LEVEL","PROJECT","RUN_TYPE","METHOD","BASIS_SET_FILE_NAME","POTENTIAL_FILE_NAME","BASIS_SET","POTENTIAL","CUTOFF","REL_CUTOFF","EPS_SCF","SCF_GUESS","FORCE_PAW","ALPHA_WEIGHTS","EPS_DEFAULT","GAPW_ACCURATE_XCINT","GAPW_1C_BASIS"};
    unsigned i;for(i=0;i<18;++i) if(th_eq(b,key,keywords[i])) return 1U<<i;return 0;
}
static bool f16_cp2k(Abstractformat *f,pm_stream *s,nh_blob *b) {
    static const char *const sections[]={"GLOBAL","FORCE_EVAL","DFT","MGRID","PRINT","DERIVATIVES","QS","SCF","XC","XC_FUNCTIONAL","SUBSYS","CELL","COORD","KIND"};
    static const char *const runs[]={"ENERGY","ENERGY_FORCE"};
    static const char *const levels[]={"SILENT","LOW","MEDIUM","HIGH","DEBUG"};
    static const char *const guesses[]={"ATOMIC","CORE","RANDOM"};
    el_lines c={0};el_token row,t[16],stack[16];unsigned seen[16]={0},n,depth=0,global=0,force=0,subsys=0,coords=0,cells=0,cellrows=0,atoms=0,method=0;uint64_t topstart=0;c.b=b;
    while(f16_words(&c,&row,t,16,&n,"#!")) {
        if(b->p[(size_t)t[0].at]=='&') {
            el_token name=el_slice(t[0],1,t[0].n-1);
            if(th_eq(b,name,"END")) {
                if(!depth || n>2 || (n==2 && !th_same(b,t[1],stack[depth-1]))) return false;
                if(th_eq(b,stack[depth-1],"COORD") && !atoms) return false;
                if(th_eq(b,stack[depth-1],"CELL") && cellrows!=1) return false;
                --depth;if(!depth && !nh_add(f,s,b,"input-section",topstart,c.at-topstart)) return false;
            }else {
                if(depth==16 || !f16_in(b,name,sections,14) || (th_eq(b,name,"KIND") || th_eq(b,name,"XC_FUNCTIONAL")?n!=2:n!=1)) return false;
                if(!depth) {if(th_eq(b,name,"GLOBAL")) {if(global++ || force) return false;}else if(th_eq(b,name,"FORCE_EVAL")) {if(!global || force++) return false;}else return false;topstart=f16_row_start(b,row);}
                else if(th_eq(b,name,"SUBSYS")) {if(!th_eq(b,stack[depth-1],"FORCE_EVAL") || subsys++) return false;}
                else if(th_eq(b,name,"CELL") || th_eq(b,name,"COORD") || th_eq(b,name,"KIND")) {
                    if(!th_eq(b,stack[depth-1],"SUBSYS")) return false;
                    if(th_eq(b,name,"CELL") && cells++) return false;
                    if(th_eq(b,name,"COORD") && coords++) return false;
                    if(th_eq(b,name,"KIND") && (n!=2 || !f16_atomic_number(b,t[1]))) return false;
                }else if(th_eq(b,name,"DFT")) {if(!th_eq(b,stack[depth-1],"FORCE_EVAL")) return false;}
                else if(th_eq(b,name,"MGRID") || th_eq(b,name,"QS") || th_eq(b,name,"SCF") || th_eq(b,name,"XC")) {if(!th_eq(b,stack[depth-1],"DFT")) return false;}
                else if(th_eq(b,name,"XC_FUNCTIONAL")) {if(!th_eq(b,stack[depth-1],"XC") || n!=2 || !f16_identifier(b,t[1])) return false;}
                else if(th_eq(b,name,"DERIVATIVES")) {if(!th_eq(b,stack[depth-1],"PRINT")) return false;}
                else if(!th_eq(b,name,"PRINT") || !th_eq(b,stack[depth-1],"DFT")) return false;
                seen[depth]=0;stack[depth++]=name;
            }
        }else {
            el_token parent;if(!depth) return false;parent=stack[depth-1];
            if(!th_eq(b,parent,"COORD")) {unsigned bit=f16_cp2k_keyword_bit(b,t[0]);if(!bit || (seen[depth-1]&bit)) return false;seen[depth-1]|=bit;}
            if(th_eq(b,parent,"COORD")) {if(n!=4 || ++atoms>4090 || !f16_atom(b,t,n,0,1) || !f16_add_row(f,s,b,"atomic-position",row,c.at)) return false;}
            else if(th_eq(b,parent,"CELL")) {if(cellrows++ || n!=4 || !th_eq(b,t[0],"ABC") || !th_positive(b,t[1]) || !th_positive(b,t[2]) || !th_positive(b,t[3]) || !f16_add_row(f,s,b,"cell-dimensions",row,c.at)) return false;}
            else if(th_eq(b,parent,"GLOBAL")) {
                if(n!=2) return false;
                if(th_eq(b,t[0],"PRINT_LEVEL")) {if(!f16_in(b,t[1],levels,5)) return false;}
                else if(th_eq(b,t[0],"RUN_TYPE")) {if(!f16_in(b,t[1],runs,2)) return false;}
                else if(!th_eq(b,t[0],"PROJECT") || !f16_identifier(b,t[1])) return false;
            }
            else if(th_eq(b,parent,"FORCE_EVAL")) {if(n!=2 || !th_eq(b,t[0],"METHOD") || !th_eq(b,t[1],"Quickstep") || method++) return false;}
            else if(th_eq(b,parent,"DFT")) {if(n!=2 || (!th_eq(b,t[0],"BASIS_SET_FILE_NAME") && !th_eq(b,t[0],"POTENTIAL_FILE_NAME")) || t[1].n>255) return false;}
            else if(th_eq(b,parent,"KIND")) {if(n!=2 || (!th_eq(b,t[0],"BASIS_SET") && !th_eq(b,t[0],"POTENTIAL")) || !f16_identifier(b,t[1])) return false;}
            else if(th_eq(b,parent,"MGRID")) {if(n!=2 || (!th_eq(b,t[0],"CUTOFF") && !th_eq(b,t[0],"REL_CUTOFF")) || !th_positive(b,t[1])) return false;}
            else if(th_eq(b,parent,"SCF")) {if(n!=2 || (th_eq(b,t[0],"EPS_SCF") ? !th_positive(b,t[1]):(!th_eq(b,t[0],"SCF_GUESS") || !f16_in(b,t[1],guesses,3)))) return false;}
            else if(th_eq(b,parent,"QS")) {
                if(th_eq(b,t[0],"FORCE_PAW")) {if(n!=1) return false;}
                else if(n!=2) return false;
                else if(th_eq(b,t[0],"ALPHA_WEIGHTS") || th_eq(b,t[0],"EPS_DEFAULT")) {if(!th_positive(b,t[1])) return false;}
                else if(th_eq(b,t[0],"GAPW_ACCURATE_XCINT")) {if(!th_eq(b,t[1],"T") && !th_eq(b,t[1],"F")) return false;}
                else if((!th_eq(b,t[0],"METHOD") && !th_eq(b,t[0],"GAPW_1C_BASIS")) || !f16_identifier(b,t[1])) return false;
            }else return false;
        }
    }return c.at==b->n && !depth && global==1 && force==1 && subsys==1 && cells==1 && coords==1 && method==1 && atoms && !fd_stop(b->pd);
}
static bool f16_nwchem(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token row,t[16];unsigned n,geometry=0,basis=0,scf=0,dft=0,atoms=0;uint64_t begin,x;c.b=b;
    while(f16_words(&c,&row,t,16,&n,"#")) {
        if(th_eq(b,t[0],"geometry")) {
            bool crystal=false;if(geometry++ || atoms || n>8) return false;
            {unsigned j,seen=0;for(j=1;j<n;++j) {
                unsigned bit;
                if(th_eq(b,t[j],"units")) {bit=1;if(++j==n || (!th_eq(b,t[j],"angstrom") && !th_eq(b,t[j],"au"))) return false;}
                else if(th_eq(b,t[j],"nocenter")) bit=2;
                else if(th_eq(b,t[j],"noautosym")) bit=4;
                else if(th_eq(b,t[j],"noautoz")) bit=8;
                else { return false; } if(seen&bit) return false;seen|=bit;
            }}
            begin=f16_row_start(b,row);
            while(f16_words(&c,&row,t,16,&n,"#")) {
                if(n==1 && th_eq(b,t[0],"end")) break;
                if(th_eq(b,t[0],"system")) {
                    if(crystal || atoms || n!=4 || !th_eq(b,t[1],"crystal") || !th_eq(b,t[2],"units") || !th_eq(b,t[3],"angstrom")) return false;
                    crystal=true;
                    if(!f16_words(&c,&row,t,16,&n,"#") || n!=1 || !th_eq(b,t[0],"lattice_vectors") || !f16_cell(&c) || !f16_words(&c,&row,t,16,&n,"#") || n!=1 || !th_eq(b,t[0],"end")) return false;
                }else if(n!=4 || ++atoms>4090 || !f16_atom(b,t,n,0,1) || !f16_add_row(f,s,b,"atomic-position",row,c.at)) return false;
            }if(!atoms || n!=1 || !th_eq(b,t[0],"end") || !nh_add(f,s,b,"geometry-section",begin,c.at-begin)) return false;
        }else if(th_eq(b,t[0],"basis")) {
            if(basis++ || (n!=1 && (n!=2 || !th_eq(b,t[1],"noprint")))) { return false; } begin=f16_row_start(b,row);
            if(!f16_words(&c,&row,t,16,&n,"#") || n!=3 || !el_eq(b,t[0],"*") || !th_eq(b,t[1],"library") || !el_ident(b,t[2]) || !f16_words(&c,&row,t,16,&n,"#") || n!=1 || !th_eq(b,t[0],"end") || !nh_add(f,s,b,"basis-section",begin,c.at-begin)) return false;
        }else if(th_eq(b,t[0],"scf") || th_eq(b,t[0],"dft")) {
            bool isscf=th_eq(b,t[0],"scf");unsigned seen=0;
            if(n!=1 || (isscf?scf++:dft++)) { return false; } begin=f16_row_start(b,row);
            while(f16_words(&c,&row,t,16,&n,"#")) {
                unsigned bit;if(n==1 && th_eq(b,t[0],"end")) break;
                if(n!=2) return false;
                if(isscf && th_eq(b,t[0],"nopen")) {if(!el_uint(b,t[1],&x) || x>1000) return false;bit=1;}
                else if(!isscf && th_eq(b,t[0],"xc")) {if(!f16_identifier(b,t[1])) return false;bit=2;}
                else if(!isscf && (th_eq(b,t[0],"mult") || th_eq(b,t[0],"maxiter"))) {if(!el_uint(b,t[1],&x) || !x || x>100000) return false;bit=th_eq(b,t[0],"mult")?4U:8U;}
                else if(isscf && th_eq(b,t[0],"thresh")) {if(!th_positive(b,t[1])) return false;bit=16;}
                else { return false; } if(seen&bit) return false;seen|=bit;
            }if(n!=1 || !th_eq(b,t[0],"end") || !nh_add(f,s,b,"method-section",begin,c.at-begin)) return false;
        }else if(th_eq(b,t[0],"task")) {
            if(n!=3 || geometry!=1 || basis!=1 || (!th_eq(b,t[1],"scf") && !th_eq(b,t[1],"dft")) || (th_eq(b,t[1],"scf")?(!scf || dft):(!dft || scf)) || (!th_eq(b,t[2],"energy") && !th_eq(b,t[2],"gradient") && !th_eq(b,t[2],"optimize"))) return false;
            return f16_add_row(f,s,b,"task-declaration",row,c.at) && f16_finish(&c,"#");
        }else {
            if(th_eq(b,t[0],"title")) {if(n<2 || row.n>255) return false;}
            else if(th_eq(b,t[0],"start") || th_eq(b,t[0],"permanent_dir") || th_eq(b,t[0],"scratch_dir")) {if(n!=2 || t[1].n>255) return false;}
            else if(th_eq(b,t[0],"charge")) {if(n!=2 || !el_range(b,t[1],1000,1000)) return false;}
            else if(th_eq(b,t[0],"memory")) {if(n!=3 || !el_uint(b,t[1],&x) || !x || x>1048576 || (!th_eq(b,t[2],"mb") && !th_eq(b,t[2],"mw") && !th_eq(b,t[2],"gb"))) return false;}
            else if(n!=1 || !th_eq(b,t[0],"echo")) return false;
            if(!f16_add_row(f,s,b,"input-declaration",row,c.at)) return false;
        }
    }return false;
}
static bool f16_gamess(Abstractformat *f,pm_stream *s,nh_blob *b) {
    static const char *const contrl[]={"RUNTYP","MULT","SCFTYP","ICHARG","COORD","UNITS"};
    static const char *const basis[]={"GBASIS","NGAUSS","NDFUNC","NPFUNC","POLAR"};
    el_lines c={0};el_token row,t[16],keys[16];unsigned n,group,atoms=0;c.b=b;
    for(group=0;group<2;++group) {
        unsigned count=0,seen=0;uint64_t begin;
        if(!f16_words(&c,&row,t,16,&n,"!") || n!=1 || !th_eq(b,t[0],group?"$BASIS":"$CONTRL")) { return false; } begin=f16_row_start(b,row);
        while(f16_words(&c,&row,t,16,&n,"!")) {
            unsigned j;if(n==1 && th_eq(b,t[0],"$END")) break;
            for(j=0;j<n;++j) {
                el_token key,value;unsigned k;
                if(!f16_assignment(b,t[j],&key,&value) || count==16 || !f16_in(b,key,group?basis:contrl,group?5U:6U)) return false;
                for(k=0;k<count;++k) { if(f16_same_key(b,key,keys[k])) return false; } keys[count++]=key;
                if(th_eq(b,key,"RUNTYP")) {if(!th_eq(b,value,"ENERGY") && !th_eq(b,value,"GRADIENT") && !th_eq(b,value,"OPTIMIZE")) return false;seen|=1;}
                else if(th_eq(b,key,"SCFTYP")) {if(!th_eq(b,value,"RHF") && !th_eq(b,value,"UHF") && !th_eq(b,value,"ROHF")) return false;seen|=2;}
                else if(th_eq(b,key,"MULT")) {uint64_t x;if(!el_uint(b,value,&x) || !x || x>1000) return false;seen|=4;}
                else if(th_eq(b,key,"ICHARG")) {if(!el_range(b,value,1000,1000)) return false;}
                else if(th_eq(b,key,"COORD")) {if(!th_eq(b,value,"UNIQUE")) return false;}
                else if(th_eq(b,key,"UNITS")) {if(!th_eq(b,value,"ANGS")) return false;}
                else if(th_eq(b,key,"GBASIS")) {if(!f16_identifier(b,value)) return false;seen|=8;}
                else if(th_eq(b,key,"POLAR")) {if(!f16_identifier(b,value)) return false;}
                else {uint64_t x;if(!el_uint(b,value,&x) || x>20) return false;}
            }
        }if(n!=1 || !th_eq(b,t[0],"$END") || (group?!(seen&8):(seen&7)!=7) || !nh_add(f,s,b,group?"basis-group":"control-group",begin,c.at-begin)) return false;
    }
    if(!f16_words(&c,&row,t,16,&n,"!") || n!=1 || !th_eq(b,t[0],"$DATA") || !f16_line(&c,&row) || !el_trim(b,row).n || !f16_words(&c,&row,t,16,&n,"!") || n!=1 || !th_eq(b,t[0],"C1") || !f16_add_row(f,s,b,"symmetry-declaration",row,c.at)) return false;
    while(f16_words(&c,&row,t,16,&n,"!")) {
        if(n==1 && th_eq(b,t[0],"$END")) return atoms && f16_add_row(f,s,b,"data-terminator",row,c.at) && f16_finish(&c,"!");
        if(n!=5 || ++atoms>4090 || !f16_atom(b,t,n,0,2) || !el_float(b,t[1]) || tw_value(b,t[1])!=(double)f16_atomic_number(b,t[0]) || !f16_add_row(f,s,b,"atomic-position",row,c.at)) return false;
    }return false;
}
static bool f16_abinit(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token row,t[128];unsigned n,atoms=0,types=0,seen=0;uint64_t x,begin;c.b=b;
    while(f16_words(&c,&row,t,128,&n,"#")) {
        unsigned bit;
        if(th_eq(b,t[0],"natom") || th_eq(b,t[0],"ntypat")) {
            bool isatoms=th_eq(b,t[0],"natom");bit=isatoms?1U:2U;
            if(n!=2 || (seen&bit) || !el_uint(b,t[1],&x) || !x || x>(isatoms?4090U:118U)) return false;
            if(isatoms) atoms=(unsigned)x;else types=(unsigned)x;
        }else if(th_eq(b,t[0],"acell")) {
            unsigned j;bit=4;if(n!=1 || (seen&bit)) return false;begin=f16_row_start(b,row);
            if(!f16_words(&c,&row,t,128,&n,"#") || (n!=3 && n!=4) || (n==4 && !th_eq(b,t[3],"Angstrom") && !th_eq(b,t[3],"Bohr"))) return false;
            for(j=0;j<3;++j) if(!th_positive(b,t[j])) return false;
            if(!nh_add(f,s,b,"cell-scale",begin,c.at-begin)) { return false; } seen|=bit;continue;
        }else if(th_eq(b,t[0],"rprim")) {
            bit=8;if(n!=1 || (seen&bit) || !(seen&4)) return false;begin=f16_row_start(b,row);
            if(!f16_cell(&c) || !nh_add(f,s,b,"lattice-vectors",begin,c.at-begin)) { return false; } seen|=bit;continue;
        }else if(th_eq(b,t[0],"znucl")) {
            unsigned j,k;uint64_t elements[118];bit=16;if((seen&bit) || !types || n!=types+1) return false;
            for(j=0;j<types;++j) {if(!el_uint(b,t[j+1],&x) || !x || x>118) return false;for(k=0;k<j;++k) if(elements[k]==x) return false;elements[j]=x;}
        }else if(th_eq(b,t[0],"typat")) {
            unsigned count=0;bit=32;if(n!=1 || !atoms || !types || !(seen&16) || (seen&bit)) return false;begin=f16_row_start(b,row);
            while(count<atoms) {unsigned j;if(!f16_words(&c,&row,t,128,&n,"#") || !n || n>atoms-count) return false;for(j=0;j<n;++j) if(!el_uint(b,t[j],&x) || !x || x>types) return false;count+=n;}
            if(!nh_add(f,s,b,"atom-type-indices",begin,c.at-begin)) { return false; } seen|=bit;continue;
        }else if(th_eq(b,t[0],"xcart")) {
            unsigned j;bit=64;if(n!=1 || !atoms || (seen&63)!=63 || (seen&bit) || !f16_add_row(f,s,b,"coordinate-declaration",row,c.at)) return false;
            for(j=0;j<atoms;++j) if(!f16_words(&c,&row,t,128,&n,"#") || n!=3 || !th_floats(b,t,3) || !f16_add_row(f,s,b,"atomic-position",row,c.at)) return false;
            seen|=bit;continue;
        }else if(th_eq(b,t[0],"ecut")) {bit=128;if((seen&bit) || (n!=2 && (n!=3 || (!th_eq(b,t[2],"eV") && !th_eq(b,t[2],"Ha")))) || !th_positive(b,t[1])) return false;}
        else if(th_eq(b,t[0],"ixc")) {bit=256;if((seen&bit) || n!=2 || !el_uint(b,t[1],&x) || x>100000) return false;}
        else if(th_eq(b,t[0],"nsppol")) {bit=512;if((seen&bit) || n!=2 || !el_uint(b,t[1],&x) || (x!=1 && x!=2)) return false;}
        else if(th_eq(b,t[0],"chkprim") || th_eq(b,t[0],"chkexit")) {bit=th_eq(b,t[0],"chkprim")?1024U:2048U;if((seen&bit) || n!=2 || !el_uint(b,t[1],&x) || x>1) return false;}
        else return false;
        seen|=bit;if(!f16_add_row(f,s,b,"input-declaration",row,c.at)) return false;
    }return c.at==b->n && (seen&127)==127 && !fd_stop(b->pd);
}
static bool f16_parse(Abstractformat *f,pm_stream *s,nh_blob *b) {
    if(!th_ascii(b) || fd_stop(b->pd)) return false;
    switch(f->file_type) {
    case XX_FILE_TYPE_VASP_POSCAR:return f16_vasp(f,s,b);
    case XX_FILE_TYPE_QUANTUM_ESPRESSO_INPUT:return f16_qe(f,s,b);
    case XX_FILE_TYPE_CP2K_INPUT:return f16_cp2k(f,s,b);
    case XX_FILE_TYPE_NWCHEM_INPUT:return f16_nwchem(f,s,b);
    case XX_FILE_TYPE_GAMESS_INPUT:return f16_gamess(f,s,b);
    case XX_FILE_TYPE_GAUSSIAN_INPUT:return f16_gaussian(f,s,b);
    case XX_FILE_TYPE_ABINIT_INPUT:return f16_abinit(f,s,b);
    case XX_FILE_TYPE_AIMS_GEOMETRY:return f16_aims(f,s,b);
    case XX_FILE_TYPE_ORCA_INPUT:return f16_orca(f,s,b);
    case XX_FILE_TYPE_DEMON_INPUT:return f16_demon(f,s,b);
    default:return false;
    }
}
#endif
