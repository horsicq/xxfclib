/* SPDX-License-Identifier: MIT
 * Independently implemented from https://mmcif.wwpdb.org/docs/tutorials/mechanics/pdbx-mmcif-syntax.html */
#include "xxfclib/formats/protein_mmcif/xx_protein_mmcif.h"
#include "../xx_eleventh_data.h"
typedef struct cf_lex {nh_blob *b;uint64_t at,start;unsigned tokens;} cf_lex;
static bool cf_ieq(nh_blob *b,el_token v,const char *word) {uint64_t i;size_t n=xx_rt_strlen(word);if(v.n!=n) return false;for(i=0;i<v.n;++i) {uint8_t x=b->p[(size_t)(v.at+i)],y=(uint8_t)word[i];if(x>='A' && x<='Z') x=(uint8_t)(x+32);if(y>='A' && y<='Z') y=(uint8_t)(y+32);if(x!=y) return false;}return true;}
static bool cf_prefix(nh_blob *b,el_token v,const char *word) {size_t n=xx_rt_strlen(word);return v.n>=n && cf_ieq(b,el_slice(v,0,n),word);}
static bool cf_reserved(nh_blob *b,el_token v,bool quoted) {return !quoted && (b->p[(size_t)v.at]=='_' || cf_ieq(b,v,"loop_") || cf_ieq(b,v,"stop_") || cf_ieq(b,v,"global_") || cf_prefix(b,v,"data_") || cf_prefix(b,v,"save_"));}
static bool cf_next(cf_lex *l,el_token *v,bool *quoted,bool *have) {
    nh_blob *b=l->b;uint64_t begin;uint8_t ch;*have=false;*quoted=false;
    while(l->at<b->n) {ch=b->p[(size_t)l->at];if(ch==' ' || ch=='\t' || ch=='\r' || ch=='\n') {++l->at;continue;}if(ch=='#') {while(l->at<b->n && b->p[(size_t)l->at]!='\n') ++l->at;continue;}break;}
    if(l->at==b->n) { return !fd_stop(b->pd); } if(fd_stop(b->pd) || ++l->tokens>1000000) return false;
    l->start=l->at;begin=l->at;ch=b->p[(size_t)l->at];
    if(ch=='\'' || ch=='"') {
        uint8_t delimiter=ch;*quoted=true;begin=++l->at;
        while(l->at<b->n) {ch=b->p[(size_t)l->at];if(ch=='\n' || ch=='\r' || l->at-begin>1048576) return false;
            if(ch==delimiter && (l->at+1==b->n || b->p[(size_t)l->at+1]==' ' || b->p[(size_t)l->at+1]=='\t' || b->p[(size_t)l->at+1]=='\r' || b->p[(size_t)l->at+1]=='\n')) {v->at=begin;v->n=l->at-begin;++l->at;*have=true;return true;}++l->at;}
        return false;
    }
    if(ch==';' && (!l->at || b->p[(size_t)l->at-1]=='\n')) {
        *quoted=true;begin=++l->at;while(l->at<b->n) {if(l->at-begin>1048576 || fd_stop(b->pd)) return false;if(b->p[(size_t)l->at]==';' && b->p[(size_t)l->at-1]=='\n') {v->at=begin;v->n=l->at-begin;++l->at;if(l->at<b->n && b->p[(size_t)l->at]!='\n' && b->p[(size_t)l->at]!='\r' && b->p[(size_t)l->at]!=' ' && b->p[(size_t)l->at]!='\t') return false;*have=true;return true;}++l->at;}return false;
    }
    while(l->at<b->n) {ch=b->p[(size_t)l->at];if(ch==' ' || ch=='\t' || ch=='\r' || ch=='\n') break;if(l->at-begin>=1048576) return false;++l->at;}
    v->at=begin;v->n=l->at-begin;*have=v->n!=0;return *have;
}
static bool cf_name(nh_blob *b,el_token v) {uint64_t i=0;if(v.n<=2 || v.n>255 || b->p[(size_t)v.at]!='_') return false;while(i<v.n && b->p[(size_t)(v.at+i)]!='[') ++i;if(!el_chars(b,el_slice(v,0,i),"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-",true)) return false;while(i<v.n) {uint64_t start,index;if(b->p[(size_t)(v.at+i++)]!='[') return false;start=i;while(i<v.n && b->p[(size_t)(v.at+i)]!=']') ++i;if(i==v.n || !el_uint(b,el_slice(v,start,i-start),&index) || !index || index>999) return false;++i;}return true;}
static bool cf_unique(nh_blob *b,el_token v,el_token *known,unsigned count,uint64_t *budget) {unsigned i;for(i=0;i<count;++i) {uint64_t j;if(!*budget) return false;--*budget;if(v.n!=known[i].n) continue;for(j=0;j<v.n;++j) {uint8_t x,y;if(!*budget) return false;--*budget;x=b->p[(size_t)(v.at+j)];y=b->p[(size_t)(known[i].at+j)];if(x>='A' && x<='Z') x=(uint8_t)(x+32);if(y>='A' && y<='Z') y=(uint8_t)(y+32);if(x!=y) break;}if(j==v.n) return false;}return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};cf_lex lex={0};el_token token,*known=NULL;uint64_t *ids=NULL,budget=8388608,atom_start=0,atom_end=0;unsigned nk=0,atoms=0;bool ok=false,quoted=false,have=false;
    NH_NEED(nh_load(f,&b,pd) && b.p[b.n-1]=='\n' && nh_ascii(b.p,(size_t)b.n,false));lex.b=&b;known=(el_token *)xx_mem_alloc(4096*sizeof(*known));ids=(uint64_t *)xx_mem_alloc(4096*sizeof(*ids));NH_NEED(known && ids);
    NH_NEED(cf_next(&lex,&token,&quoted,&have) && have && !quoted && cf_prefix(&b,token,"data_") && token.n>5 && token.n<=260);
    NH_NEED(cf_next(&lex,&token,&quoted,&have));
    while(have) {
        if(!quoted && cf_ieq(&b,token,"loop_")) {
            uint64_t begin=lex.start,values=0;el_token row[128];unsigned nc=0;int index[8]={-1,-1,-1,-1,-1,-1,-1,-1};bool atom=false;
            static const char *required[]={"_atom_site.group_PDB","_atom_site.id","_atom_site.type_symbol","_atom_site.label_atom_id","_atom_site.label_comp_id","_atom_site.Cartn_x","_atom_site.Cartn_y","_atom_site.Cartn_z"};
            NH_NEED(cf_next(&lex,&token,&quoted,&have));
            while(have && !quoted && b.p[(size_t)token.at]=='_') {
                unsigned i;NH_NEED(nc<128 && nk<4096 && cf_name(&b,token) && cf_unique(&b,token,known,nk,&budget));known[nk++]=token;if(cf_prefix(&b,token,"_atom_site.")) atom=true;
                for(i=0;i<8;++i) { if(cf_ieq(&b,token,required[i])) index[i]=(int)nc; } ++nc;NH_NEED(cf_next(&lex,&token,&quoted,&have));
            }
            NH_NEED(nc && have && !cf_reserved(&b,token,quoted));
            if(atom) {unsigned i;NH_NEED(!atom_start);atom_start=begin;for(i=0;i<8;++i) NH_NEED(index[i]>=0);}
            do {
                row[values%nc]=token;++values;
                if(atom && values%nc==0) {
                    uint64_t id;unsigned i;NH_NEED(atoms<4096 && (cf_ieq(&b,row[index[0]],"ATOM") || cf_ieq(&b,row[index[0]],"HETATM")) && el_uint(&b,row[index[1]],&id) && id && id<=1000000000);
                    for(i=0;i<atoms;++i) {NH_NEED(budget);--budget;NH_NEED(ids[i]!=id);}ids[atoms++]=id;
                    NH_NEED(row[index[2]].n<=3 && el_chars(&b,row[index[2]],"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",true) && el_ident(&b,row[index[3]]) && el_ident(&b,row[index[4]]));
                    for(i=5;i<8;++i) NH_NEED(el_float(&b,row[index[i]]));
                }
                NH_NEED(cf_next(&lex,&token,&quoted,&have));
            }while(have && !cf_reserved(&b,token,quoted));
            NH_NEED(values%nc==0);if(atom) atom_end=have ? lex.start:b.n;
        } else if(!quoted && cf_name(&b,token)) {
            NH_NEED(nk<4096 && cf_unique(&b,token,known,nk,&budget));known[nk++]=token;NH_NEED(cf_next(&lex,&token,&quoted,&have) && have && !cf_reserved(&b,token,quoted));NH_NEED(cf_next(&lex,&token,&quoted,&have));
        }else goto done;
    }
    NH_NEED(atoms && atom_start && atom_end>atom_start && nh_add(f,s,&b,"metadata",0,atom_start) && nh_add(f,s,&b,"atom-loop",atom_start,atom_end-atom_start));if(atom_end<b.n) NH_NEED(nh_add(f,s,&b,"trailer",atom_end,b.n-atom_end));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(ids);xx_mem_free(known);xx_mem_free(b.p);return ok;
}

void xx_protein_mmcif_init(xx_protein_mmcif *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PROTEIN_MMCIF,"protein_mmcif"); } }
xx_protein_mmcif *xx_protein_mmcif_create(xx_io_device *d,int64_t b) { xx_protein_mmcif *r=(xx_protein_mmcif *)xx_mem_alloc(sizeof(*r)); if(r) xx_protein_mmcif_init(r,d,b); return r; }
void xx_protein_mmcif_destroy(xx_protein_mmcif *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_protein_mmcif_free(xx_protein_mmcif *r) { if(r) { xx_protein_mmcif_destroy(r); xx_mem_free(r); } }
bool xx_protein_mmcif_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_protein_mmcif_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
