/* SPDX-License-Identifier: MIT
 * Primary reference: https://imagemagick.org/magick-vector-graphics/
 * MVG graphics subset: complete balanced graphic contexts, finite viewbox/affine/transforms and checked color/stroke/text/geometric/path commands; original typed records exported. Local drawing metadata retained without rendering or loading fonts; image/delegate/URL/external-resource commands declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/imagemagick_mvg/xx_imagemagick_mvg.h"
#include "../imagemagick_miff/xx_sixteenth_games.h"
static bool hg_quick(Abstractformat *f,uint64_t n) {uint8_t b[20];return n>=32&&pm_read(f,0,b,20)&&hg_tag(b,"push graphic-context",20);}
static bool hg_mvg_color(hg_lex *q) {uint64_t p,z;unsigned i;static const char *const colors[]={"none","black","white","red","blue","green","lime","yellow","cyan","magenta","fuchsia","darkslateblue","gray","grey","transparent","orange","purple"};if(!hg_quoted(q,'\'',&p,&z))return false;if(z==7||z==9){if(q->b[p]=='#'){for(i=1;i<z;++i){uint8_t c=q->b[p+i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;}return true;}}for(i=0;i<17;++i)if(z==xx_rt_strlen(colors[i])&&hg_tag(q->b+p,colors[i],(size_t)z))return true;return false;}
static bool hg_mvg_path(hg_lex *q) {uint64_t at,z;hg_lex t;uint8_t command=0;bool begun=false,open=false;uint32_t groups=0;if(!hg_quoted(q,'\'',&at,&z)||!z)return false;t.b=q->b;t.p=at;t.n=at+z;t.pd=q->pd;t.work=0;t.hash=t.comments=false;t.commas=true;while(hg_skip(&t)&&t.p<t.n){unsigned args,j;double values[7];uint8_t c=t.b[t.p];if((c>='A'&&c<='Z')||(c>='a'&&c<='z')){command=c;++t.p;}else if(!command)return false;c=command;if(c>='a')c-=32;if(!begun&&c!='M')return false;if(c=='Z'){if(!open)return false;open=false;command=0;continue;}args=c=='M'||c=='L'||c=='T'?2:c=='H'||c=='V'?1:c=='C'?6:c=='S'||c=='Q'?4:c=='A'?7:0;if(!args||++groups>1000000)return false;for(j=0;j<args;++j)if(!hg_number(&t,&values[j]))return false;if(c=='A'&&(values[0]<0||values[1]<0||(values[3]!=0&&values[3]!=1)||(values[4]!=0&&values[4]!=1)))return false;if(c=='M'){begun=open=true;command=command=='M'?'L':'l';}else if(!open)return false;}return begun&&hg_end(&t);}
static bool hg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {hg_text lines={b,0,n,0,0,0};unsigned depth=0,commands=0,shapes=0;bool view=false;char label[48];if(!hg_utf(b,n,true,pd))return false;while(hg_line(&lines)){hg_lex q={b,lines.start,lines.stop,pd,0,false,true,false};uint64_t at,z;double v[6];unsigned i,args=0;bool shape=false;if(hg_stop(pd)||!hg_skip(&q))return false;if(q.p==q.n)continue;if(!hg_ident(&q,&at,&z)||++commands>4094)return false;
 if(z==4&&hg_tag(b+at,"push",4)){if((commands==1&&depth)||depth>=64||!hg_kw(&q,"graphic-context"))return false;++depth;}
 else if(z==3&&hg_tag(b+at,"pop",3)){if(!depth||!hg_kw(&q,"graphic-context"))return false;--depth;}
 else {if(!depth)return false;if(z==7&&hg_tag(b+at,"viewbox",7)){if(view||depth!=1)return false;args=4;view=true;}
 else if(z==6&&hg_tag(b+at,"affine",6))args=6;
 else if((z==4&&hg_tag(b+at,"fill",4))||(z==6&&hg_tag(b+at,"stroke",6))){if(!hg_mvg_color(&q))return false;}
 else if((z==12&&hg_tag(b+at,"stroke-width",12))||(z==9&&hg_tag(b+at,"font-size",9))){if(!hg_number(&q,&v[0])||v[0]<0||(z==9&&v[0]==0))return false;}
 else if(z==4&&hg_tag(b+at,"text",4)){if(!hg_number(&q,&v[0])||!hg_number(&q,&v[1])||!hg_quoted(&q,'\'',&at,&z)||!z)return false;shape=true;}
 else if(z==4&&hg_tag(b+at,"path",4)){if(!hg_mvg_path(&q))return false;shape=true;}
 else if((z==9&&hg_tag(b+at,"rectangle",9))||(z==6&&hg_tag(b+at,"circle",6))||(z==4&&hg_tag(b+at,"line",4))){args=4;shape=true;}
 else if(z==7&&hg_tag(b+at,"ellipse",7)){args=6;shape=true;}
 else if((z==9&&hg_tag(b+at,"translate",9))||(z==5&&hg_tag(b+at,"scale",5)))args=2;
 else if(z==6&&hg_tag(b+at,"rotate",6))args=1;else return false;
 for(i=0;i<args;++i)if(!hg_number(&q,&v[i]))return false;if(z==7&&hg_tag(b+at,"viewbox",7)&&(v[2]<=v[0]||v[3]<=v[1]))return false;if(shape)++shapes;}
 if(!hg_end(&q))return false;xx_rt_snprintf(label,sizeof(label),"drawing-command-%u.mvg",commands-1);if(!hg_emit(f,s,label,lines.start,lines.p-lines.start,n))return false;}
 return lines.p==n&&view&&shapes&&!depth&&hg_cover(f,s,"drawing-whitespace.txt",n);}

void xx_imagemagick_mvg_init(xx_imagemagick_mvg *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_IMAGEMAGICK_MVG,"mvg");}}
xx_imagemagick_mvg *xx_imagemagick_mvg_create(xx_io_device *d,int64_t at) {xx_imagemagick_mvg *r=(xx_imagemagick_mvg *)xx_mem_alloc(sizeof(*r));if(r)xx_imagemagick_mvg_init(r,d,at);return r;}
void xx_imagemagick_mvg_destroy(xx_imagemagick_mvg *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_imagemagick_mvg_free(xx_imagemagick_mvg *r) {if(r){xx_imagemagick_mvg_destroy(r);xx_mem_free(r);}}
bool xx_imagemagick_mvg_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_imagemagick_mvg_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
