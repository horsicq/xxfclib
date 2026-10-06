/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/daveho/Galasm/master/src/jedec.c
 * JEDEC fuse stream: complete STX/ETX framing, fuse count/default/security fields and bounded nonoverlapping bit assignments, checked packed-fuse checksum and transmission checksum when nonzero. Original device notes/typed fields plus decoded packed fuse bits exported; test vectors and vendor extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/jedec_fuse/xx_jedec_fuse.h"
#include "../wbmp_image/xx_fifteenth_games.h"
static bool vg_quick(Abstractformat *f,uint64_t n) {uint8_t c;return n>=16&&pm_read(f,0,&c,1)&&c==2;}
static int vg_hex(uint8_t c) {if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}
static bool vg_jhex(vg_text *q,uint32_t *v) {unsigned i;vg_space(q);*v=0;for(i=0;i<4;++i){int h;if(q->t==q->stop||(h=vg_hex(q->b[q->t++]))<0)return false;*v=(*v<<4)|(unsigned)h;}return vg_done(q);}
static bool vg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {uint64_t p=1,first,etx,end,j;uint32_t qf=0,expected=0,transmitted=0,checksum=0,seen=0,fields=0;unsigned pass;uint8_t *bits=NULL,*assigned=NULL;bool ok=false;char label[48];if(n<16||b[0]!=2)return false;while(p<n&&b[p]!='*'){if(b[p]<32&&b[p]!=9&&b[p]!=10&&b[p]!=13)return false;++p;}first=p;while(p<n&&b[p]!=3)++p;if(p==n)return false;etx=p;end=etx+1;while(end<n&&b[end]!=10&&b[end]!=13&&b[end]!=32&&b[end]!=9)++end;{vg_text q={b,etx+1,n,0,end,etx+1};if(!vg_jhex(&q,&transmitted))return false;}for(j=end;j<n;++j)if(b[j]!=32&&b[j]!=9&&b[j]!=10&&b[j]!=13)return false;for(j=0;j<=etx;++j)checksum=(checksum+b[j])&65535;if(transmitted&&transmitted!=checksum)return false;if(!vg_utf(b+1,etx-1,true,pd))return false;
 for(pass=0;pass<2;++pass){p=first;seen=0;fields=0;while(p<etx){uint64_t start=p,stop;vg_text q;int32_t v;if(vg_stop(pd)||b[p++]!='*')goto done;stop=p;while(stop<etx&&b[stop]!='*')++stop;q.b=b;q.p=p;q.end=stop;q.start=p;q.stop=stop;q.t=p;while(q.t<q.stop&&(b[q.t]==32||b[q.t]==9||b[q.t]==10||b[q.t]==13))++q.t;while(q.stop>q.t&&(b[q.stop-1]==32||b[q.stop-1]==9||b[q.stop-1]==10||b[q.stop-1]==13))--q.stop;if(q.t==q.stop){if(stop!=etx)goto done;p=stop;break;}if(++fields>4000)goto done;
 if(vg_tag(b+q.t,"QF",2)){q.t+=2;if((seen&1)||!vg_i(&q,&v)||v<1||v>1048576||!vg_done(&q))goto done;seen|=1;if(!pass)qf=(uint32_t)v;else if((uint32_t)v!=qf)goto done;}
 else if(b[q.t]=='F'){++q.t;if((seen&2)||!vg_i(&q,&v)||v<0||v>1||!vg_done(&q))goto done;seen|=2;if(pass){xx_mem_zero(bits,(qf+7)/8);if(v)for(j=0;j<(qf+7)/8;++j)bits[j]=255;if(qf%8)bits[(qf-1)/8]&=(uint8_t)((1U<<(qf%8))-1);}}
 else if(b[q.t]=='G'){++q.t;if((seen&4)||!vg_i(&q,&v)||v<0||v>1||!vg_done(&q))goto done;seen|=4;}
 else if(vg_tag(b+q.t,"QP",2)){q.t+=2;if((seen&8)||!vg_i(&q,&v)||v<1||v>1024||!vg_done(&q))goto done;seen|=8;}
 else if(b[q.t]=='L'){uint32_t offset,count=0;++q.t;if(!vg_i(&q,&v)||v<0||(pass&&(!(seen&2)||(uint32_t)v>=qf)))goto done;offset=(uint32_t)v;while(q.t<q.stop){uint8_t c=b[q.t++];if(c==32||c==9||c==10||c==13)continue;if(c!='0'&&c!='1')goto done;if(pass){uint32_t at=offset+count;if(at>=qf||(assigned[at/8]&(1U<<(at%8))))goto done;assigned[at/8]|=(uint8_t)(1U<<(at%8));if(c=='1')bits[at/8]|=(uint8_t)(1U<<(at%8));else bits[at/8]&=(uint8_t)~(1U<<(at%8));}if(++count>1048576)goto done;}if(!count)goto done;}
 else if(b[q.t]=='C'){++q.t;if((seen&16)||!vg_jhex(&q,&expected))goto done;seen|=16;}
 else if(b[q.t]=='N'){++q.t;if(q.t==q.stop)goto done;}
 else { goto done; } if(pass){xx_rt_snprintf(label,sizeof(label),"fuse-field-%u.jed",fields-1);if(!vg_emit(f,s,label,start,stop-start,n))goto done;}p=stop;}
 if(p!=etx||(seen&23)!=23) {goto done; } if(!pass){bits=(uint8_t *)xx_mem_alloc((qf+7)/8);assigned=(uint8_t *)xx_mem_alloc((qf+7)/8);if(!bits||!assigned)goto done;xx_mem_zero(assigned,(qf+7)/8);if(!vg_emit(f,s,"device-notes.jed",0,first,n))goto done;}}
 checksum=0;for(j=0;j<(qf+7)/8;++j)checksum=(checksum+bits[j])&65535;if(checksum!=expected||!vg_cover(f,s,"framing.jed",n)||!vg_memory(f,s,"fuses.bits",bits,(qf+7)/8))goto done;bits=NULL;ok=true;done:if(bits)xx_mem_free(bits);if(assigned)xx_mem_free(assigned);return ok;}

void xx_jedec_fuse_init(xx_jedec_fuse *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_JEDEC_FUSE,"jed");}}
xx_jedec_fuse *xx_jedec_fuse_create(xx_io_device *d,int64_t at) {xx_jedec_fuse *r=(xx_jedec_fuse *)xx_mem_alloc(sizeof(*r));if(r)xx_jedec_fuse_init(r,d,at);return r;}
void xx_jedec_fuse_destroy(xx_jedec_fuse *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_jedec_fuse_free(xx_jedec_fuse *r) {if(r){xx_jedec_fuse_destroy(r);xx_mem_free(r);}}
bool xx_jedec_fuse_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_jedec_fuse_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
