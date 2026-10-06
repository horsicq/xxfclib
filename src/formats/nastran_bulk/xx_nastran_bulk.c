/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/nasa/NASTRAN-95/master/um/BULK.TXT
 * NASTRAN bounded mesh bulk-data subset: small/free-field GRID and standard shell/solid connectivity with unique IDs, finite coordinates, zero coordinate systems and resolved local node references. Original mesh cards exported; solver/external includes/large-field/property/constraint/load extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/nastran_bulk/xx_nastran_bulk.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t c;return n>=24&&pm_read(f,0,&c,1)&&(c=='$'||c=='G'||c=='B'||c=='C'||c==32);}
static bool fg_nas_fields(fg_text *q,char field[24][65],unsigned *count) {
 uint64_t p=q->start,end=q->stop;unsigned n=0;bool comma=false;uint64_t i;for(i=p;i<end;++i){if(q->b[i]=='$'){end=i;break;}if(q->b[i]==',')comma=true;}
 while(p<end){uint64_t stop=comma?p:p+8,j,z;if(!comma&&stop>end)stop=end;else if(comma)while(stop<end&&q->b[stop]!=',')++stop;if(n>=24)return false;j=p;while(j<stop&&(q->b[j]==32||q->b[j]==9))++j;z=stop;while(z>j&&(q->b[z-1]==32||q->b[z-1]==9))--z;if(z-j>64)return false;xx_mem_copy(field[n],q->b+j,(size_t)(z-j));field[n][z-j]=0;++n;p=stop;if(comma&&p<end)++p;}
 while(n&&field[n-1][0]==0) {--n; } *count=n;return n>0;
}
static bool fg_nas_int(const char *p,int32_t *v,bool blank) {fg_text q;size_t z=xx_rt_strlen(p);if(!z&&blank){*v=0;return true;}q.b=(const uint8_t *)p;q.t=0;q.stop=z;return fg_i(&q,v)&&q.t==z;}
static bool fg_nas_real(const char *p,double *v,bool blank) {uint8_t b[80];size_t z=xx_rt_strlen(p),i,o=0;fg_text q;bool exponent=false;if(!z&&blank){*v=0;return true;}if(z>64)return false;for(i=0;i<z;++i){uint8_t c=(uint8_t)p[i];if(c=='D'||c=='d')c='E';if(c=='E'||c=='e')exponent=true;else if(i&&!exponent&&(c=='+'||c=='-')){b[o++]='E';exponent=true;}b[o++]=c;}q.b=b;q.t=0;q.stop=o;return fg_num(&q,v)&&q.t==o;}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_ids nodes={0},elements={0};bool ok=false;unsigned pass,nodecount=0,elementcount=0;char fields[24][65],label[48];
 if(!fg_utf(b,n,true,pd)||!fg_ids_init(&nodes,1000000)||!fg_ids_init(&elements,1000000))goto done;
 for(pass=0;pass<2;++pass){fg_text q={b,0,n,0,0,0};bool begin=false,end=false,card=false;
 while(fg_line(&q)){unsigned count,arity=0,i;int32_t id,value;double v;bool grid=false;fg_space(&q);if(fg_stop(pd))goto done;if(q.t==q.stop||b[q.t]=='$')continue;if(end)goto done;
 if(fg_word(&q,"BEGIN")&&fg_word(&q,"BULK")){if(begin||card||!fg_done(&q))goto done;begin=true;continue;}
 q.t=q.start;if(fg_word(&q,"ENDDATA")){if(!card||!fg_done(&q))goto done;end=true;continue;}
 if(!fg_nas_fields(&q,fields,&count))goto done;
 if(!xx_rt_strcmp(fields[0],"GRID"))grid=true;
 else if(!xx_rt_strcmp(fields[0],"CTRIA3"))arity=3;else if(!xx_rt_strcmp(fields[0],"CQUAD4")||!xx_rt_strcmp(fields[0],"CTETRA"))arity=4;else if(!xx_rt_strcmp(fields[0],"CPYRA")||!xx_rt_strcmp(fields[0],"CPYRAM"))arity=5;else if(!xx_rt_strcmp(fields[0],"CPENTA"))arity=6;else if(!xx_rt_strcmp(fields[0],"CHEXA"))arity=8;else if(!xx_rt_strcmp(fields[0],"CBAR"))arity=2;else goto done;
 if(count<2||!fg_nas_int(fields[1],&id,false)||id<1) {goto done; } card=true;
 if(grid){if(count<6||count>9||!fg_nas_int(fields[2],&value,true)||value||!fg_nas_real(fields[3],&v,false)||!fg_nas_real(fields[4],&v,false)||!fg_nas_real(fields[5],&v,false))goto done;for(i=6;i<count;++i)if(!fg_nas_int(fields[i],&value,true)||value)goto done;if(!pass){if(!fg_id(&nodes,(uint32_t)id,true,pd)||++nodecount>1000000)goto done;}}
 else {bool bar=!xx_rt_strcmp(fields[0],"CBAR");unsigned expected=3+arity+(bar?3:0);if(count!=expected||!fg_nas_int(fields[2],&value,true)||value<0)goto done;if(!pass){if(!fg_id(&elements,(uint32_t)id,true,pd)||++elementcount>1000000)goto done;}
 for(i=0;i<arity;++i){if(!fg_nas_int(fields[3+i],&value,false)||value<1||(pass&&!fg_id(&nodes,(uint32_t)value,false,pd)))goto done;{unsigned j;int32_t other;for(j=0;j<i;++j)if(!fg_nas_int(fields[3+j],&other,false)||other==value)goto done;}}
 if(bar)for(i=5;i<8;++i)if(!fg_nas_real(fields[i],&v,false))goto done;}
 if(pass){xx_rt_snprintf(label,sizeof(label),grid?"node-%u.bdf":"element-%u.bdf",(unsigned)id);if(!fg_emit(f,s,label,q.start,q.p-q.start,n))goto done;}
 }
 if(!card||(begin&&!end)||q.p!=q.end)goto done;
 }
 ok=nodecount>=3&&elementcount&&fg_cover(f,s,"framing.bdf",n);
done:if(nodes.values)xx_mem_free(nodes.values);if(elements.values)xx_mem_free(elements.values);return ok;
}

void xx_nastran_bulk_init(xx_nastran_bulk *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_NASTRAN_BULK,"bdf");}}
xx_nastran_bulk *xx_nastran_bulk_create(xx_io_device *d,int64_t at) {xx_nastran_bulk *r=(xx_nastran_bulk *)xx_mem_alloc(sizeof(*r));if(r)xx_nastran_bulk_init(r,d,at);return r;}
void xx_nastran_bulk_destroy(xx_nastran_bulk *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_nastran_bulk_free(xx_nastran_bulk *r) {if(r){xx_nastran_bulk_destroy(r);xx_mem_free(r);}}
bool xx_nastran_bulk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_nastran_bulk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
