/* SPDX-License-Identifier: MIT. Bounded scientific tables and trace records. */
#ifndef XX_FIFTEENTH_ROOT_H
#define XX_FIFTEENTH_ROOT_H
#include "xx_thirteenth_root.h"
#define F15_NEED(x) do { if(!(x)) goto done; } while(0)
static bool f15_charge(nh_blob *b,uint64_t *budget,uint64_t n) {
    if(fd_stop(b->pd) || n>*budget) { return false; } *budget-=n;return true;
}
static bool f15_identifier(nh_blob *b,el_token t) {
    return t.n<=255 && el_chars(b,t,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.:|+-",true);
}
static bool f15_words(el_lines *c,el_token *line,el_token *t,unsigned cap,unsigned *nt) {
    while(c->at<c->b->n) {
        if(!el_line(c,line)) return false;
        *line=el_trim(c->b,*line);
        if(!line->n || c->b->p[(size_t)line->at]=='#') continue;
        return el_split(c->b,*line,t,cap,nt,false);
    }return false;
}
static bool f15_finish(el_lines *c) {
    el_token v;
    while(c->at<c->b->n) {if(!el_line(c,&v)) return false;v=el_trim(c->b,v);if(v.n && c->b->p[(size_t)v.at]!='#') return false;}
    return c->at==c->b->n && !fd_stop(c->b->pd);
}
static bool f15_csv(nh_blob *b,el_token t,uint64_t *position,uint64_t *value) {
    uint64_t start=*position;
    if(start>=t.n) return false;
    while(*position<t.n && b->p[(size_t)(t.at+*position)]!=',') ++*position;
    if(!el_uint(b,el_slice(t,start,*position-start),value)) return false;
    if(*position<t.n) { ++*position; } return true;
}
static bool f15_unique(nh_blob *b,el_token t,el_token *known,unsigned count,uint64_t *budget) {
    unsigned i;
    for(i=0;i<count;++i) {
        if(!f15_charge(b,budget,1)) return false;
        if(t.n==known[i].n) {
            if(!f15_charge(b,budget,t.n)) return false;
            if(!xx_rt_memcmp(b->p+(size_t)t.at,b->p+(size_t)known[i].at,(size_t)t.n)) return false;
        }
    }return true;
}
/* Track options are inert metadata; quoted values never cause external reads. */
static bool f15_track(nh_blob *b,el_token line,bool wig) {
    el_token keys[64];uint64_t at=5,budget=131072;unsigned count=0;bool type=false;
    if(!el_prefix(b,line,"track") || (line.n>5 && b->p[(size_t)(line.at+5)]!=' ' && b->p[(size_t)(line.at+5)]!='\t')) return false;
    while(at<line.n) {
        uint64_t start;el_token key,value;
        while(at<line.n && (b->p[(size_t)(line.at+at)]==' ' || b->p[(size_t)(line.at+at)]=='\t')) ++at;
        if(at==line.n) { break; } start=at;
        while(at<line.n && b->p[(size_t)(line.at+at)]!='=') ++at;
        key=el_slice(line,start,at-start);
        if(at==line.n || !el_chars(b,key,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_",true) || key.n>64 || count==64 || !f15_unique(b,key,keys,count,&budget)) return false;
        keys[count++]=key;++at;start=at;
        if(at<line.n && b->p[(size_t)(line.at+at)]=='"') {
            start=++at;while(at<line.n && b->p[(size_t)(line.at+at)]!='"') ++at;
            if(at==line.n) { return false; } value=el_slice(line,start,at-start);++at;
        } else {
            while(at<line.n && b->p[(size_t)(line.at+at)]!=' ' && b->p[(size_t)(line.at+at)]!='\t') ++at;
            value=el_slice(line,start,at-start);
        }
        if(!value.n || value.n>1024 || (at<line.n && b->p[(size_t)(line.at+at)]!=' ' && b->p[(size_t)(line.at+at)]!='\t')) return false;
        if(el_eq(b,key,"type")) {if(wig && !el_eq(b,value,"wiggle_0")) return false;type=true;}
    }return count && (!wig || type);
}
static bool f15_bed(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token line,t[12];unsigned nt,count=0,fields=0;bool body=false;c.b=b;
    if(!th_ascii(b)) return false;
    while(c.at<b->n) {
        uint64_t start=c.at,a,z;unsigned i;
        if(!el_line(&c,&line)) return false;
        if(!line.n || b->p[(size_t)line.at]=='#') {if(!nh_add(f,s,b,"metadata",start,c.at-start)) return false;continue;}
        if(el_prefix(b,line,"track")) {if(body || !f15_track(b,line,false) || !nh_add(f,s,b,"track",start,c.at-start)) return false;continue;}
        if(el_prefix(b,line,"browser ")) {
            if(body || !el_split(b,line,t,12,&nt,false) || nt!=3 || !el_eq(b,t[1],"position") || !el_ident(b,t[2]) || !nh_add(f,s,b,"browser",start,c.at-start)) { return false; } continue;
        }
        if(!el_split(b,line,t,12,&nt,true) || nt<3 || ++count>4094 || !f15_identifier(b,t[0]) || !el_uint(b,t[1],&a) || !el_uint(b,t[2],&z) || a>z || z>UINT32_MAX) return false;
        if(fields && nt!=fields) { return false; } fields=nt;body=true;
        if(nt>=4 && !el_ident(b,t[3])) return false;
        if(nt>=5) {uint64_t score;if(!el_uint(b,t[4],&score) || score>1000) return false;}
        if(nt>=6 && (t[5].n!=1 || !el_chars(b,t[5],"+-.",true))) return false;
        if(nt>=7) {uint64_t x;if(!el_uint(b,t[6],&x) || x<a || x>z) return false;
            if(nt>=8) {uint64_t y;if(!el_uint(b,t[7],&y) || y<x || y>z) return false;}}
        if(nt>=9 && !el_eq(b,t[8],"0")) {
            uint64_t position=0,x;for(i=0;i<3;++i) if(!f15_csv(b,t[8],&position,&x) || x>255) return false;
            if(position!=t[8].n || b->p[(size_t)(t[8].at+t[8].n-1)]==',') return false;
        }
        if(nt==10 || nt==11) return false;
        if(nt==12) {
            uint64_t n,p=0,q=0,previous=0;
            if(!el_uint(b,t[9],&n) || !n || n>4096) return false;
            for(i=0;i<n;++i) {uint64_t length,offset;
                if(!f15_csv(b,t[10],&p,&length) || !f15_csv(b,t[11],&q,&offset) || !length || (!i && offset) || offset<previous || offset>z-a || length>z-a-offset) return false;
                previous=offset+length;
            }
            if(previous!=z-a || p!=t[10].n || q!=t[11].n) return false;
        }
        if(!nh_add(f,s,b,"interval",start,c.at-start)) return false;
    }return count!=0;
}
static bool f15_wiggle(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token line,t[8];unsigned nt,count=0,blocks=0;uint64_t block=0,position=0,step=0,span=1,rows=0;bool fixed=false,active=false,track_pending=false;c.b=b;
    if(!th_ascii(b)) return false;
    while(c.at<b->n) {
        uint64_t start=c.at;
        if(!el_line(&c,&line)) { return false; } line=el_trim(b,line);
        if(!line.n || b->p[(size_t)line.at]=='#') continue;
        if(el_prefix(b,line,"track")) {
            if(track_pending || (active && !rows) || !f15_track(b,line,true)) return false;
            track_pending=true;continue;
        }
        if(el_prefix(b,line,"fixedStep") || el_prefix(b,line,"variableStep")) {
            unsigned seen=0,i;el_token chrom={0};
            if(active && (!rows || !nh_add(f,s,b,"wiggle-block",block,start-block))) return false;
            if(++blocks>4094 || !el_split(b,line,t,8,&nt,false) || nt<2) return false;
            fixed=el_eq(b,t[0],"fixedStep");if(!fixed && !el_eq(b,t[0],"variableStep")) return false;
            span=1;position=0;step=0;
            for(i=1;i<nt;++i) {
                uint64_t a=0;el_token key,value;unsigned bit;
                while(a<t[i].n && b->p[(size_t)(t[i].at+a)]!='=') ++a;
                if(!a || a==t[i].n) { return false; } key=el_slice(t[i],0,a);value=el_slice(t[i],a+1,t[i].n-a-1);
                if(el_eq(b,key,"chrom")) {bit=1;chrom=value;if(!f15_identifier(b,value)) return false;}
                else if(el_eq(b,key,"start")) {bit=2;if(!fixed || !el_uint(b,value,&position) || !position) return false;}
                else if(el_eq(b,key,"step")) {bit=4;if(!fixed || !el_uint(b,value,&step) || !step) return false;}
                else if(el_eq(b,key,"span")) {bit=8;if(!el_uint(b,value,&span) || !span) return false;}
                else return false;
                if(seen&bit) { return false; } seen|=bit;
            }
            if(!chrom.n || (fixed && (seen&7)!=7) || position>UINT32_MAX || step>UINT32_MAX || span>UINT32_MAX) return false;
            if(!active && start && !nh_add(f,s,b,"metadata",0,start)) return false;
            active=true;track_pending=false;block=start;rows=0;continue;
        }
        if(!active || track_pending || !el_split(b,line,t,8,&nt,false) || nt!=(fixed?1U:2U) || ++count>262144 || !el_float(b,t[nt-1])) return false;
        if(!fixed) {uint64_t next;if(!el_uint(b,t[0],&next) || !next || (rows && next<=position)) return false;position=next;}
        if(position>UINT32_MAX || span-1>UINT32_MAX-position) return false;
        if(fixed) position+=step;
        ++rows;
    }return active && !track_pending && rows && nh_add(f,s,b,"wiggle-block",block,b->n-block);
}
static bool f15_attributes(nh_blob *b,el_token t,bool gene,uint64_t *budget) {
    uint64_t at=0;el_token keys[128];unsigned count=0;bool gid=false,tid=false;
    while(at<t.n) {
        uint64_t start;el_token key,value;
        while(at<t.n && (b->p[(size_t)(t.at+at)]==' ' || b->p[(size_t)(t.at+at)]=='\t')) ++at;
        if(at==t.n) { break; } start=at;
        while(at<t.n && b->p[(size_t)(t.at+at)]!=' ' && b->p[(size_t)(t.at+at)]!='\t') ++at;
        key=el_slice(t,start,at-start);
        if(count==128 || key.n>64 || !el_chars(b,key,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_",true) || !f15_unique(b,key,keys,count,budget)) return false;
        keys[count++]=key;
        while(at<t.n && (b->p[(size_t)(t.at+at)]==' ' || b->p[(size_t)(t.at+at)]=='\t')) ++at;
        if(at==t.n || b->p[(size_t)(t.at+at++)]!='"') { return false; } start=at;
        while(at<t.n && b->p[(size_t)(t.at+at)]!='"') {
            if(b->p[(size_t)(t.at+at)]=='\\') {++at;if(at==t.n || (b->p[(size_t)(t.at+at)]!='"' && b->p[(size_t)(t.at+at)]!='\\')) return false;}
            if(!f15_charge(b,budget,1)) { return false; } ++at;
        }
        if(at==t.n || at-start>4096) { return false; } value=el_slice(t,start,at-start);++at;
        if(at==t.n || b->p[(size_t)(t.at+at++)]!=';') return false;
        if(at<t.n && b->p[(size_t)(t.at+at)]!=' ' && b->p[(size_t)(t.at+at)]!='\t') return false;
        if(el_eq(b,key,"gene_id")) {if(!value.n) return false;gid=true;}
        if(el_eq(b,key,"transcript_id")) {if(!gene && !value.n) return false;tid=true;}
    }return gid && tid;
}
static bool f15_gtf(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token line,t[9];unsigned nt,count=0;uint64_t budget=16777216;c.b=b;
    if(!th_ascii(b)) return false;
    while(c.at<b->n) {
        uint64_t start=c.at,a,z;
        if(!el_line(&c,&line)) return false;
        if(!line.n || b->p[(size_t)line.at]=='#') {if(el_prefix(b,line,"##gff-version 3")) return false;if(!nh_add(f,s,b,"metadata",start,c.at-start)) return false;continue;}
        if(!el_split(b,line,t,9,&nt,true) || nt!=9 || ++count>4094 || !f15_identifier(b,t[0]) || !el_ident(b,t[1]) || !el_ident(b,t[2]) || !el_uint(b,t[3],&a) || !el_uint(b,t[4],&z) || !a || a>z || z>UINT32_MAX || (!el_eq(b,t[5],".") && !el_float(b,t[5])) || t[6].n!=1 || !el_chars(b,t[6],"+-.",true) || t[7].n!=1 || !el_chars(b,t[7],"012.",true)) return false;
        if(el_eq(b,t[2],"CDS") && el_eq(b,t[7],".")) return false;
        if(!f15_attributes(b,t[8],el_eq(b,t[2],"gene"),&budget) || !nh_add(f,s,b,"annotation-feature",start,c.at-start)) return false;
    }return count!=0;
}
static bool f15_agp(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token line,t[9],*names=NULL,current={0};unsigned nt,count=0,objects=0;uint64_t end=0,part=0,budget=8388608;bool ok=false;c.b=b;
    F15_NEED(th_ascii(b));names=(el_token *)xx_mem_alloc(1024*sizeof(*names));F15_NEED(names);
    while(c.at<b->n) {
        uint64_t start=c.at,a,z,p,x,y;bool same;
        F15_NEED(el_line(&c,&line));
        if(!line.n || b->p[(size_t)line.at]=='#') {F15_NEED(nh_add(f,s,b,"metadata",start,c.at-start));continue;}
        F15_NEED(el_split(b,line,t,9,&nt,true) && nt==9 && ++count<=4094 && el_ident(b,t[0]) && el_uint(b,t[1],&a) && el_uint(b,t[2],&z) && a && a<=z && z<=UINT32_MAX && el_uint(b,t[3],&p) && t[4].n==1 && el_chars(b,t[4],"ADFGOPWNU",true));
        same=current.n && th_same(b,current,t[0]);
        if(!same) {F15_NEED(objects<1024 && f15_unique(b,t[0],names,objects,&budget) && a==1 && p==1);names[objects++]=t[0];current=t[0];}
        else F15_NEED(a==end+1 && p==part+1);
        if(el_eq(b,t[4],"N") || el_eq(b,t[4],"U")) {
            F15_NEED(el_uint(b,t[5],&x) && x==z-a+1 && (!el_eq(b,t[4],"U") || x==100) && (el_eq(b,t[6],"scaffold") || el_eq(b,t[6],"contig") || el_eq(b,t[6],"centromere") || el_eq(b,t[6],"short_arm") || el_eq(b,t[6],"heterochromatin") || el_eq(b,t[6],"telomere") || el_eq(b,t[6],"repeat") || el_eq(b,t[6],"contamination")) && (el_eq(b,t[7],"yes") || el_eq(b,t[7],"no")));
            if(el_eq(b,t[7],"no")) F15_NEED(el_eq(b,t[8],"na"));
            else {el_token evidence[16];unsigned n,i;F15_NEED(el_sep(b,t[8],';',evidence,16,&n));for(i=0;i<n;++i) F15_NEED(el_eq(b,evidence[i],"paired-ends") || el_eq(b,evidence[i],"align_genus") || el_eq(b,evidence[i],"align_xgenus") || el_eq(b,evidence[i],"align_trnscpt") || el_eq(b,evidence[i],"within_clone") || el_eq(b,evidence[i],"clone_contig") || el_eq(b,evidence[i],"map") || el_eq(b,evidence[i],"strobe") || el_eq(b,evidence[i],"pcr") || el_eq(b,evidence[i],"proximity_ligation") || el_eq(b,evidence[i],"unspecified"));}
        } else F15_NEED(el_ident(b,t[5]) && el_uint(b,t[6],&x) && el_uint(b,t[7],&y) && x && x<=y && y<=UINT32_MAX && y-x==z-a && (el_eq(b,t[8],"+") || el_eq(b,t[8],"-") || el_eq(b,t[8],"?") || el_eq(b,t[8],"0") || el_eq(b,t[8],"na")));
        end=z;part=p;F15_NEED(nh_add(f,s,b,"assembly-component",start,c.at-start));
    }ok=count!=0;
done:if(names) xx_mem_free(names);return ok;
}
static bool f15_spi1d(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token line,t[4];unsigned nt,components;uint64_t n,i,start;c.b=b;
    if(!th_ascii(b) || !f15_words(&c,&line,t,4,&nt) || nt!=2 || !el_eq(b,t[0],"Version") || !el_eq(b,t[1],"1")) return false;
    if(!f15_words(&c,&line,t,4,&nt) || nt!=3 || !el_eq(b,t[0],"From") || !el_float(b,t[1]) || !el_float(b,t[2]) || tw_value(b,t[1])>=tw_value(b,t[2])) return false;
    if(!f15_words(&c,&line,t,4,&nt) || nt!=2 || !el_eq(b,t[0],"Length") || !el_uint(b,t[1],&n) || !n || n>262144) return false;
    if(!f15_words(&c,&line,t,4,&nt) || nt!=2 || !el_eq(b,t[0],"Components") || !el_uint(b,t[1],&i) || !i || i>3) { return false; } components=(unsigned)i;
    if(!f15_words(&c,&line,t,4,&nt) || nt!=1 || !el_eq(b,t[0],"{")) { return false; } start=c.at;
    if(!nh_add(f,s,b,"lut-header",0,start)) return false;
    for(i=0;i<n;++i) if(!f15_words(&c,&line,t,4,&nt) || nt!=components || !th_floats(b,t,nt)) return false;
    if(!nh_add(f,s,b,"curve-values",start,c.at-start) || !f15_words(&c,&line,t,4,&nt) || nt!=1 || !el_eq(b,t[0],"}") || !f15_finish(&c)) return false;
    return true;
}
static bool f15_spi3d(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token line,t[6];unsigned nt;uint64_t n[3],i,total,start,slab;c.b=b;
    if(!th_ascii(b) || !f15_words(&c,&line,t,6,&nt) || nt!=2 || !el_eq(b,t[0],"SPILUT") || !el_eq(b,t[1],"1.0")) return false;
    if(!f15_words(&c,&line,t,6,&nt) || nt!=2 || !el_eq(b,t[0],"3") || !el_eq(b,t[1],"3")) return false;
    if(!f15_words(&c,&line,t,6,&nt) || nt!=3) return false;
    for(i=0;i<3;++i) if(!el_uint(b,t[i],&n[i]) || !n[i] || n[i]>256) return false;
    total=n[0]*n[1]*n[2];if(total>262144 || !nh_add(f,s,b,"lut-header",0,c.at)) return false;
    slab=n[1]*n[2];start=c.at;
    for(i=0;i<total;++i) {
        uint64_t x,y,z;
        if(!f15_words(&c,&line,t,6,&nt) || nt!=6 || !el_uint(b,t[0],&x) || !el_uint(b,t[1],&y) || !el_uint(b,t[2],&z) || x!=i/slab || y!=(i/n[2])%n[1] || z!=i%n[2] || !th_floats(b,t+3,3)) return false;
        if((i+1)%slab==0) {if(!nh_add(f,s,b,"red-axis-slab",start,c.at-start)) return false;start=c.at;}
    }return f15_finish(&c);
}
static bool f15_csp(Abstractformat *f,pm_stream *s,nh_blob *b) {
    el_lines c={0};el_token line,*t=NULL;unsigned nt,axis;uint64_t n,i,total,start;bool dim3,ok=false;c.b=b;
    F15_NEED(th_ascii(b));t=(el_token *)xx_mem_alloc(4096*sizeof(*t));F15_NEED(t);
    F15_NEED(f15_words(&c,&line,t,4096,&nt) && nt==1 && el_eq(b,t[0],"CSPLUTV100"));
    F15_NEED(f15_words(&c,&line,t,4096,&nt) && nt==1 && (el_eq(b,t[0],"1D") || el_eq(b,t[0],"3D")));dim3=el_eq(b,t[0],"3D");
    start=c.at;
    F15_NEED(f15_words(&c,&line,t,4096,&nt));
    if(nt==1 && el_eq(b,t[0],"BEGIN METADATA")) goto done;
    if(nt==2 && el_eq(b,t[0],"BEGIN") && el_eq(b,t[1],"METADATA")) {
        bool ended=false;unsigned lines=0;
        while(c.at<b->n) {F15_NEED(el_line(&c,&line) && ++lines<=1024 && c.at-start<=65536);line=el_trim(b,line);if(el_eq(b,line,"END METADATA")) {ended=true;break;}}
        F15_NEED(ended && f15_words(&c,&line,t,4096,&nt));
    }
    F15_NEED(nh_add(f,s,b,"lut-header",0,line.at));
    for(axis=0;axis<3;++axis) {
        double previous=0;
        start=line.at;F15_NEED(nt==1 && el_uint(b,t[0],&n) && n>=2 && n<=4096);
        F15_NEED(f15_words(&c,&line,t,4096,&nt) && nt==n);
        for(i=0;i<n;++i) {double x;F15_NEED(el_float(b,t[i]));x=tw_value(b,t[i]);F15_NEED(!i || x>previous);previous=x;}
        F15_NEED(f15_words(&c,&line,t,4096,&nt) && nt==n && th_floats(b,t,nt) && nh_add(f,s,b,"prelut-channel",start,c.at-start));
        F15_NEED(f15_words(&c,&line,t,4096,&nt));
    }
    if(dim3) {uint64_t a,z;F15_NEED(nt==3 && el_uint(b,t[0],&a) && el_uint(b,t[1],&n) && el_uint(b,t[2],&z) && a && n && z && a<=256 && n<=256 && z<=256);total=a*n*z;}
    else F15_NEED(nt==1 && el_uint(b,t[0],&total));
    F15_NEED(total && total<=262144);start=line.at;
    for(i=0;i<total;++i) F15_NEED(f15_words(&c,&line,t,4096,&nt) && nt==3 && th_floats(b,t,3));
    F15_NEED(nh_add(f,s,b,"grid-values",start,c.at-start) && f15_finish(&c));ok=true;
done:if(t) xx_mem_free(t);return ok;
}
static bool f15_abif(Abstractformat *f,pm_stream *s,nh_blob *b) {
    uint64_t *keys=NULL;uint64_t directory,capacity,count,i,extent,budget=1048576;bool ok=false;
    F15_NEED(b->n>=34 && !xx_rt_memcmp(b->p,"ABIF",4) && xx_data_get_u16(b->p+4, 2, 0, true)>=100 && xx_data_get_u16(b->p+4, 2, 0, true)<200 && !xx_rt_memcmp(b->p+6,"tdir",4) && xx_data_get_u16(b->p+14, 2, 0, true)==1023 && xx_data_get_u16(b->p+16, 2, 0, true)==28);
    count=xx_data_get_u32(b->p+18, 4, 0, true);capacity=xx_data_get_u32(b->p+22, 4, 0, true);directory=xx_data_get_u32(b->p+26, 4, 0, true);
    F15_NEED(count && count<=4094 && capacity>=count*28 && capacity%28==0 && capacity<=4096*28 && directory>=34 && nh_span(b,directory,capacity));
    extent=directory+capacity;keys=(uint64_t *)xx_mem_alloc(8192*sizeof(*keys));F15_NEED(keys);xx_mem_zero(keys,8192*sizeof(*keys));
    F15_NEED(nh_add(f,s,b,"file-header",0,34) && nh_add(f,s,b,"tag-directory",directory,capacity));
    for(i=0;i<count;++i) {
        const uint8_t *p=b->p+(size_t)(directory+i*28);uint64_t name=xx_data_get_u32(p, 4, 0, true),number=xx_data_get_u32(p+4, 4, 0, true),key=(name<<32)|number,at,size=xx_data_get_u32(p+16, 4, 0, true),elements=xx_data_get_u32(p+12, 4, 0, true),slot=(key^(key>>33))&8191;unsigned type=xx_data_get_u16(p+8, 2, 0, true),width=xx_data_get_u16(p+10, 2, 0, true),j;char label[64];
        for(j=0;j<4;++j) F15_NEED(p[j]>=33 && p[j]<=126);
        F15_NEED(width && elements<=67108864 && (uint64_t)width*elements==size && ((type>=1 && type<=20 && type!=6 && type!=9 && type!=14 && type!=15 && type!=16 && type!=17 && type!=20) || type>=1024));
        if(type==1 || type==2 || type==13 || type==18 || type==19) F15_NEED(width==1);
        if(type==3 || type==4) F15_NEED(width==2);
        if(type==5 || type==7 || type==10 || type==11) F15_NEED(width==4);
        if(type==8) F15_NEED(width==8);
        if(type==12) F15_NEED(width==10);
        while(keys[slot]) {F15_NEED(f15_charge(b,&budget,1) && keys[slot]!=key);slot=(slot+1)&8191;}keys[slot]=key;
        at=size<=4 ? directory+i*28+20:xx_data_get_u32(p+20, 4, 0, true);
        F15_NEED(nh_span(b,at,size) && (size<=4 || (at>=34 && (at+size<=directory || at>=directory+capacity))));
        if(at+size>extent) extent=at+size;
        if(type==7 || type==8) F15_NEED(nh_floats(b,at,size,width,true));
        if(type==18) F15_NEED(size && b->p[(size_t)at]==size-1);
        if(type==19) F15_NEED(size && b->p[(size_t)(at+size-1)]==0);
        xx_rt_snprintf(label,sizeof(label),"tag-%c%c%c%c-%u",p[0],p[1],p[2],p[3],(unsigned)number);
        F15_NEED(nh_add(f,s,b,label,at,size));
    }F15_NEED(extent==b->n);ok=true;
done:if(keys) xx_mem_free(keys);return ok;
}
static bool f15_scf(Abstractformat *f,pm_stream *s,nh_blob *b) {
    uint64_t samples,bases,offset[4],length[4],extent=128,i,j;unsigned width;static const char *const channels[]={"A","C","G","T"};
    if(b->n<128 || xx_rt_memcmp(b->p,".scf",4) || xx_rt_memcmp(b->p+36,"3.00",4)) return false;
    samples=xx_data_get_u32(b->p+4, 4, 0, true);bases=xx_data_get_u32(b->p+12, 4, 0, true);width=xx_data_get_u32(b->p+40, 4, 0, true);
    if(!samples || samples>1048576 || !bases || bases>1048576 || (width!=1 && width!=2) || xx_data_get_u32(b->p+44, 4, 0, true)>4 || xx_data_get_u32(b->p+16, 4, 0, true)>bases || xx_data_get_u32(b->p+20, 4, 0, true)>bases) return false;
    offset[0]=xx_data_get_u32(b->p+8, 4, 0, true);length[0]=samples*4*width;offset[1]=xx_data_get_u32(b->p+24, 4, 0, true);length[1]=bases*12;
    offset[2]=xx_data_get_u32(b->p+32, 4, 0, true);length[2]=xx_data_get_u32(b->p+28, 4, 0, true);offset[3]=xx_data_get_u32(b->p+52, 4, 0, true);length[3]=xx_data_get_u32(b->p+48, 4, 0, true);
    for(i=0;i<4;++i) {if(!length[i]) {if(offset[i]>b->n) return false;continue;}if(offset[i]<128 || !nh_span(b,offset[i],length[i])) return false;if(offset[i]+length[i]>extent) extent=offset[i]+length[i];for(j=0;j<i;++j) if(length[j] && offset[i]<offset[j]+length[j] && offset[j]<offset[i]+length[i]) return false;}
    if(extent!=b->n || !nh_add(f,s,b,"trace-header",0,128)) return false;
    for(i=0;i<bases;++i) {uint64_t peak=xx_data_get_u32(b->p+(size_t)(offset[1]+i*4), 4, 0, true);uint8_t ch=b->p[(size_t)(offset[1]+bases*8+i)];if(!(i&1023) && fd_stop(b->pd)) return false;if(peak>=samples || (i && peak<xx_data_get_u32(b->p+(size_t)(offset[1]+(i-1)*4), 4, 0, true)) || !ch || !xx_rt_strchr("ACGTNRYKMSWBDHVX-acgtnrykmswbdhvx",ch)) return false;}
    for(i=0;i<4;++i) {char label[32];xx_rt_snprintf(label,sizeof(label),"encoded-trace-%s",channels[i]);if(!nh_add(f,s,b,label,offset[0]+i*samples*width,samples*width)) return false;}
    if(!nh_add(f,s,b,"base-positions",offset[1],bases*4)) return false;
    for(i=0;i<4;++i) {char label[32];xx_rt_snprintf(label,sizeof(label),"base-probability-%s",channels[i]);if(!nh_add(f,s,b,label,offset[1]+bases*4+i*bases,bases)) return false;}
    if(!nh_add(f,s,b,"basecalls",offset[1]+bases*8,bases) || !nh_add(f,s,b,"base-reserved",offset[1]+bases*9,bases*3)) return false;
    /* Maintainer files also use a length-delimited comment block without NUL. */
    if(length[2]) {
        uint64_t text_size=length[2]-(b->p[(size_t)(offset[2]+length[2]-1)]==0);
        for(i=0;i<text_size;) {size_t n=(size_t)(text_size-i>65536 ? 65536:text_size-i);if(fd_stop(b->pd) || !nh_ascii(b->p+(size_t)(offset[2]+i),n,false)) return false;i+=n;}
        if(!nh_add(f,s,b,"comments",offset[2],length[2])) return false;
    }
    return !length[3] || nh_add(f,s,b,"private-data",offset[3],length[3]);
}
typedef struct f15_sff_entry {el_token name;uint64_t offset;bool indexed;} f15_sff_entry;
static bool f15_sff_index(Abstractformat *f,pm_stream *s,nh_blob *b,uint64_t at,uint64_t size,f15_sff_entry *reads,unsigned count) {
    uint64_t table,end=at+size,manifest=0,budget=8388608;unsigned i;
    if(size<12 || !nh_span(b,at,size) || xx_rt_memcmp(b->p+(size_t)at+4,"1.00",4)) return false;
    if(!xx_rt_memcmp(b->p+(size_t)at,".mft",4)) {if(size<16) return false;manifest=xx_data_get_u32(b->p+(size_t)at+8, 4, 0, true);if(manifest>65536 || manifest+xx_data_get_u32(b->p+(size_t)at+12, 4, 0, true)!=size-16 || !nh_ascii(b->p+(size_t)at+16,(size_t)manifest,false)) return false;table=at+16+manifest;if(!nh_add(f,s,b,"index-header",at,16) || !nh_add(f,s,b,"index-manifest",at+16,manifest)) return false;}
    else if(!xx_rt_memcmp(b->p+(size_t)at,".srt",4)) {if(!nh_zero(b,at+8,4) || !nh_add(f,s,b,"index-header",at,12)) return false;table=at+12;}
    else return false;
    at=table;
    for(i=0;i<count;++i) {
        uint64_t start=at,offset=0;unsigned j,k;bool found=false;
        while(at<end && b->p[(size_t)at]) {if(at-start>=255 || b->p[(size_t)at]<33 || b->p[(size_t)at]>126) return false;++at;}
        if(at==start || end-at<6 || b->p[(size_t)at]) { return false; } ++at;
        for(k=0;k<4;++k) {if(b->p[(size_t)at]==255) return false;offset=offset*255+b->p[(size_t)at++];}
        if(b->p[(size_t)at++]!=255) return false;
        for(j=0;j<count;++j) {if(!f15_charge(b,&budget,1)) return false;if(reads[j].name.n==at-start-6) {if(!f15_charge(b,&budget,reads[j].name.n)) return false;if(!xx_rt_memcmp(b->p+(size_t)start,b->p+(size_t)reads[j].name.at,(size_t)reads[j].name.n)) {if(reads[j].indexed || reads[j].offset!=offset) return false;reads[j].indexed=true;found=true;break;}}}
        if(!found) return false;
    }return at==end && nh_add(f,s,b,"index-table",table,end-table);
}
static bool f15_sff(Abstractformat *f,pm_stream *s,nh_blob *b) {
    uint64_t index,index_size,at,header,flows,key,count,i,budget=8388608;f15_sff_entry *reads=NULL;bool skipped=false,ok=false;
    F15_NEED(b->n>=31 && !xx_rt_memcmp(b->p,".sff",4) && xx_data_get_u32(b->p+4, 4, 0, true)==1);
    index=xx_data_get_u64(b->p+8, 8, 0, true);index_size=xx_data_get_u32(b->p+16, 4, 0, true);count=xx_data_get_u32(b->p+20, 4, 0, true);header=xx_data_get_u16(b->p+24, 2, 0, true);key=xx_data_get_u16(b->p+26, 2, 0, true);flows=xx_data_get_u16(b->p+28, 2, 0, true);
    F15_NEED(count && count<=512 && flows && flows<=4096 && key<=256 && b->p[30]==1 && header==((31+key+flows+7)&~UINT64_C(7)) && nh_span(b,0,header) && (!index==!index_size) && (!index || (index>=header && index%8==0 && nh_span(b,index,index_size) && nh_span(b,index,(index_size+7)&~UINT64_C(7)))));
    for(i=0;i<flows+key;++i) F15_NEED(xx_rt_strchr("ACGTN",b->p[(size_t)(31+i)]) && b->p[(size_t)(31+i)]);
    F15_NEED(nh_zero(b,31+key+flows,header-(31+key+flows)) && nh_add(f,s,b,"flowgram-header",0,header));
    reads=(f15_sff_entry *)xx_mem_alloc((size_t)count*sizeof(*reads));F15_NEED(reads);xx_mem_zero(reads,(size_t)count*sizeof(*reads));at=header;
    for(i=0;i<count;++i) {
        uint64_t begin,name,n,data,padded,j,sum=0;
        if(index && at==index) {F15_NEED(!skipped && nh_zero(b,index+index_size,((index_size+7)&~UINT64_C(7))-index_size));at+=((index_size+7)&~UINT64_C(7));skipped=true;}
        begin=at;F15_NEED(nh_span(b,at,16));header=xx_data_get_u16(b->p+(size_t)at, 2, 0, true);name=xx_data_get_u16(b->p+(size_t)at+2, 2, 0, true);n=xx_data_get_u32(b->p+(size_t)at+4, 4, 0, true);
        F15_NEED(name && name<=255 && n && n<=1048576 && header==((16+name+7)&~UINT64_C(7)) && nh_span(b,at,header) && nh_zero(b,at+16+name,header-(16+name)));
        reads[i].name.at=at+16;reads[i].name.n=name;reads[i].offset=at;
        F15_NEED(el_ident(b,reads[i].name));
        for(j=0;j<i;++j) {F15_NEED(f15_charge(b,&budget,1));if(reads[i].name.n==reads[j].name.n) {F15_NEED(f15_charge(b,&budget,name));F15_NEED(!th_same(b,reads[i].name,reads[j].name));}}
        for(j=8;j<16;j+=2) F15_NEED(xx_data_get_u16(b->p+(size_t)at+j, 2, 0, true)<=n);
        at+=header;data=flows*2+n*3;padded=(data+7)&~UINT64_C(7);
        F15_NEED(nh_span(b,at,padded) && nh_zero(b,at+data,padded-data) && (!index || skipped || at+padded<=index));
        for(j=0;j<n;++j) {sum+=b->p[(size_t)(at+flows*2+j)];F15_NEED(sum<=flows && b->p[(size_t)(at+flows*2+n+j)] && xx_rt_strchr("ACGTNacgtn",b->p[(size_t)(at+flows*2+n+j)]));}
        F15_NEED(nh_add(f,s,b,"read-header",begin,header) && nh_add(f,s,b,"flow-values",at,flows*2) && nh_add(f,s,b,"flow-indices",at+flows*2,n) && nh_add(f,s,b,"basecalls",at+flows*2+n,n) && nh_add(f,s,b,"qualities",at+flows*2+n*2,n));
        at+=padded;
    }
    if(index && at==index) {F15_NEED(!skipped && nh_zero(b,index+index_size,((index_size+7)&~UINT64_C(7))-index_size));at+=((index_size+7)&~UINT64_C(7));skipped=true;}
    F15_NEED(at==b->n && (!index || (skipped && f15_sff_index(f,s,b,index,index_size,reads,(unsigned)count))));ok=true;
done:if(reads) xx_mem_free(reads);return ok;
}
static bool f15_parse(Abstractformat *f,pm_stream *s,nh_blob *b) {
    switch(f->file_type) {
        case XX_FILE_TYPE_GENOMICS_BED:return f15_bed(f,s,b);
        case XX_FILE_TYPE_GENOMICS_WIGGLE:return f15_wiggle(f,s,b);
        case XX_FILE_TYPE_GENOMICS_GTF:return f15_gtf(f,s,b);
        case XX_FILE_TYPE_GENOMICS_AGP:return f15_agp(f,s,b);
        case XX_FILE_TYPE_SEQUENCING_ABIF:return f15_abif(f,s,b);
        case XX_FILE_TYPE_SEQUENCING_SCF:return f15_scf(f,s,b);
        case XX_FILE_TYPE_GENOMICS_SFF:return f15_sff(f,s,b);
        case XX_FILE_TYPE_LUT_SPI1D:return f15_spi1d(f,s,b);
        case XX_FILE_TYPE_LUT_SPI3D:return f15_spi3d(f,s,b);
        case XX_FILE_TYPE_LUT_CINESPACE_CSP:return f15_csp(f,s,b);
        default:return false;
    }
}
#undef F15_NEED
#endif
