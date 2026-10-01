/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://fossies.org/linux/xfig/doc/FORMAT3.2
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/xfig/xx_xfig.h"
#include "../xx_fifth_data.h"

typedef struct sm_line { const uint8_t *p; size_t n,begin,stop; } sm_line;
typedef struct sm_text { const uint8_t *p; size_t n,at; unsigned lines; xx_pd_struct *pd; } sm_text;
typedef struct sm_token { const uint8_t *p; size_t n; } sm_token;
static bool sm_line_read(sm_text *r,sm_line *line) {
    size_t begin=r->at,end; if(fd_stop(r->pd) || begin>=r->n || ++r->lines>262144) return false;
    while(r->at<r->n && r->p[r->at]!='\n') { uint8_t b=r->p[r->at]; if((b<32 && b!='\t' && b!='\r') || b>126 || r->at-begin>=4096) return false; ++r->at; }
    if(r->at==r->n) return false; end=r->at++; if(end>begin && r->p[end-1]=='\r') --end;
    line->p=r->p+begin; line->n=end-begin; line->begin=begin; line->stop=r->at; return true;
}
static bool sm_line_equal(sm_line *line,const char *text) { size_t n=xx_rt_strlen(text); return line->n==n && !xx_rt_memcmp(line->p,text,n); }
static unsigned sm_split(sm_line *line,sm_token *tokens,unsigned cap) {
    size_t at=0,begin; unsigned count=0;
    while(at<line->n) { while(at<line->n && (line->p[at]==' ' || line->p[at]=='\t')) ++at; if(at==line->n) break; begin=at;
        while(at<line->n && line->p[at]!=' ' && line->p[at]!='\t') ++at; if(count==cap) return cap+1;
        tokens[count].p=line->p+begin; tokens[count++].n=at-begin;
    } return count;
}
static bool sm_integer(sm_token t,int32_t *out) {
    size_t at=0; uint32_t v=0; bool neg=false;
    if(!t.n || t.n>10) return false; if(t.p[at]=='-' || t.p[at]=='+') neg=t.p[at++]=='-'; if(at==t.n) return false;
    while(at<t.n) { if(t.p[at]<'0' || t.p[at]>'9' || v>1000000) return false; v=v*10+t.p[at++]-'0'; }
    if(v>10000000) return false; *out=neg ? -(int32_t)v:(int32_t)v; return true;
}
static bool sm_real(sm_token t,bool nonnegative,bool nonzero) {
    size_t at=0; unsigned whole=0,fraction=0; bool dot=false,digit=false,any=false,negative=false;
    if(!t.n || t.n>32) return false; if(t.p[at]=='-' || t.p[at]=='+') negative=t.p[at++]=='-';
    while(at<t.n) { uint8_t b=t.p[at++]; if(b=='.') { if(dot) return false; dot=true; continue; }
        if(b<'0' || b>'9') return false; digit=true; any=any || b!='0'; if(dot) { if(++fraction>16) return false; } else if(++whole>7) return false;
    } return digit && (!nonnegative || !negative || !any) && (!nonzero || any);
}
static bool sm_color_ref(int32_t v,const uint8_t *colors) { return (v>=-1 && v<=31) || (v>=32 && v<=543 && colors[v-32]); }
static bool sm_fig_parse(Abstractformat *f,pm_stream *s,const uint8_t *data,size_t n,xx_pd_struct *pd) {
    sm_text r={data,n,0,0,pd}; sm_line line; sm_token tokens[64]; uint8_t colors[512]; unsigned stage,count=0,total_points=0; bool geometry=false; int32_t v[20]; size_t header;
    xx_mem_zero(colors,sizeof(colors));
    for(stage=0;stage<9;++stage) {
        do { if(!sm_line_read(&r,&line)) return false; } while(stage && line.n && line.p[0]=='#');
        if(stage==0 && !sm_line_equal(&line,"#FIG 3.2")) return false;
        if(stage==1 && !sm_line_equal(&line,"Landscape") && !sm_line_equal(&line,"Portrait")) return false;
        if(stage==2 && !sm_line_equal(&line,"Center") && !sm_line_equal(&line,"FlushLeft")) return false;
        if(stage==3 && !sm_line_equal(&line,"Inches") && !sm_line_equal(&line,"Metric")) return false;
        if(stage==4 && !sm_line_equal(&line,"Letter") && !sm_line_equal(&line,"Legal") && !sm_line_equal(&line,"Ledger") && !sm_line_equal(&line,"Tabloid") && !sm_line_equal(&line,"A4") && !sm_line_equal(&line,"A3") && !sm_line_equal(&line,"A2") && !sm_line_equal(&line,"A1") && !sm_line_equal(&line,"A0") && !sm_line_equal(&line,"B4") && !sm_line_equal(&line,"B3")) return false;
        if(stage==5 && (sm_split(&line,tokens,2)!=1 || !sm_real(tokens[0],true,true))) return false;
        if(stage==6 && !sm_line_equal(&line,"Single") && !sm_line_equal(&line,"Multiple")) return false;
        if(stage==7 && (sm_split(&line,tokens,2)!=1 || !sm_integer(tokens[0],v) || v[0]<-3 || v[0]>543)) return false;
        if(stage==8 && (sm_split(&line,tokens,3)!=2 || !sm_integer(tokens[0],v) || v[0]<1 || v[0]>1000000 || !sm_integer(tokens[1],v+1) || v[1]!=2)) return false;
    }
    header=r.at; if(!pm_add(f,s,"fig-header.txt",0,(int64_t)header)) return false;
    while(r.at<n) { unsigned fields,i; size_t begin,stop; char label[48];
        if(!sm_line_read(&r,&line)) return false; if(!line.n || line.p[0]=='#') continue;
        fields=sm_split(&line,tokens,20); begin=line.begin; if(!fields || fields>20 || !sm_integer(tokens[0],v) || ++count>4096) return false;
        if(v[0]==0) {
            if(geometry || fields!=3 || !sm_integer(tokens[1],v+1) || v[1]<32 || v[1]>543 || colors[v[1]-32] || tokens[2].n!=7 || tokens[2].p[0]!='#') return false;
            for(i=1;i<7;++i) { uint8_t b=tokens[2].p[i]; if(!((b>='0' && b<='9') || (b>='a' && b<='f') || (b>='A' && b<='F'))) return false; } colors[v[1]-32]=1;
        } else if(v[0]==1 || v[0]==2) { unsigned expected=v[0]==1 ? 20:16; int32_t kind=v[0]; geometry=true;
            if(fields!=expected) return false;
            for(i=1;i<expected;++i) { if(i==9 || (kind==1 && i==11)) { if(!sm_real(tokens[i],i==9,false)) return false; }
                else if(!sm_integer(tokens[i],v+i)) return false;
            }
            if(v[1]<1 || v[1]>4 || v[2]<0 || v[2]>5 || v[3]<0 || v[3]>10000 || !sm_color_ref(v[4],colors) || !sm_color_ref(v[5],colors) || v[6]<0 || v[6]>999 || v[7]!=-1 || v[8]<-1 || v[8]>62) return false;
            if(kind==1) { if(v[10]!=1 || v[14]<=0 || v[15]<=0 || ((v[1]==3 || v[1]==4) && v[14]!=v[15])) return false; }
            else { unsigned got=0,needed; int32_t first[2]={0},last[2]={0};
                if(v[10]<0 || v[10]>2 || v[11]<0 || v[11]>2 || v[12]<0 || v[13] || v[14] || v[15]<2 || (unsigned)v[15]>65536-total_points || ((v[1]==2 || v[1]==4) && v[15]!=5) || (v[1]==3 && v[15]<4)) return false;
                needed=(unsigned)v[15]*2; total_points+=(unsigned)v[15];
                while(got<needed) { unsigned nt,j; if(!sm_line_read(&r,&line) || !(nt=sm_split(&line,tokens,64)) || nt>64 || nt>needed-got) return false;
                    for(j=0;j<nt;++j) { int32_t coord; if(!sm_integer(tokens[j],&coord)) return false; if(got<2) first[got]=coord; last[got&1]=coord; ++got; }
                }
                if(v[1]!=1 && (first[0]!=last[0] || first[1]!=last[1])) return false;
            }
        } else return false;
        stop=r.at; xx_rt_snprintf(label,sizeof(label),"fig-object-%u-type-%d.txt",count-1,v[0]); if(!pm_add(f,s,label,(int64_t)begin,(int64_t)(stop-begin))) return false;
    } s->size=(int64_t)n; return geometry && count>0;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    int64_t n=pm_available(f); uint8_t *data; bool ok=false;
    if(n<32 || n>8388608) return false; data=(uint8_t *)xx_mem_alloc((size_t)n); if(!data) return false;
    if(pm_read(f,0,data,(size_t)n)) ok=sm_fig_parse(f,s,data,(size_t)n,pd); xx_mem_free(data); return ok;
}

void xx_xfig_init(xx_xfig *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_XFIG,"fig"); } }
xx_xfig *xx_xfig_create(xx_io_device *d,int64_t b) { xx_xfig *r=(xx_xfig *)xx_mem_alloc(sizeof(*r)); if(r) xx_xfig_init(r,d,b); return r; }
void xx_xfig_destroy(xx_xfig *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_xfig_free(xx_xfig *r) { if(r) { xx_xfig_destroy(r); xx_mem_free(r); } }
bool xx_xfig_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_xfig_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
