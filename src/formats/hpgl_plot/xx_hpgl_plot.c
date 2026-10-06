/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/gnuplot/gnuplot/master/term/hpgl.trm
 * HPGL initialized uppercase semicolon command subset: complete IN/SP/PA/PR/PU/PD/IP/SC/CI/AA/AR/PW/LT/VS/SI/SR/DI/DR/CS/CA/SS plus DT and terminated UTF8 LB text; exact operand counts and finite bounded geometry. Requires at least one drawing with selected pen. Original vector commands exported; replay/rendering, PCL carriers and all other commands declined. Primary original unavailable; independent wire controls only.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/hpgl_plot/xx_hpgl_plot.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t c;return n>=4&&pm_read(f,0,&c,1)&&c>=9&&c<=126;}
static bool hp_values(const uint8_t *b,uint64_t start,uint64_t end,double values[32],unsigned *count) {
 tb_text q={b,0,end,start,end,start};unsigned k=0;bool comma=false;
 while(q.t<end){uint64_t stop=q.t;tb_space(&q);if(q.t==end)break;while(stop<end&&b[stop]!=',')++stop;q.stop=stop;if(k==32||!tb_num(&q,&values[k++])||!tb_done(&q))return false;q.t=stop;comma=stop<end;if(comma)++q.t;q.stop=end;}
 *count=k;return !comma;
}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=0,start,end;bool initialized=false,drawing=false,pen=false;unsigned commands=0;uint8_t terminator=3;
 while(p<n){uint8_t a,c;double values[32];unsigned k,i;start=p;while(p<n&&(b[p]==9||b[p]==10||b[p]==13||b[p]==32))++p;if(p==n){if(!tb_emit(f,s,"whitespace.hpgl",start,n-start,n))return false;break;}
  if(tb_stop(pd)||++commands>4094||!tb_span(p,2,n)) {return false; } a=b[p++];c=b[p++];if(a<'A'||a>'Z'||c<'A'||c>'Z')return false;
  if(a=='L'&&c=='B'){end=p;while(p<n&&b[p]!=terminator){if(p-end>=4096)return false;++p;}if(p==n||!initialized||!tb_utf(b+end,p-end,false,pd))return false;++p;if(p<n&&b[p]==';')++p;drawing|=pen;}
  else {end=p;while(p<n&&b[p]!=';'){if(p-end>=2048)return false;++p;}if(p==n)return false;
   if(a=='D'&&c=='T'){if(p-end>1)return false;terminator=p==end?3:b[end];if(!terminator||terminator==';')return false;}
   else {if(!hp_values(b,end,p,values,&k))return false;for(i=0;i<k;++i)if(values[i]< -100000000||values[i]>100000000)return false;
    if(a=='I'&&c=='N'){if(k||initialized)return false;initialized=true;pen=false;}
    else if(!initialized)return false;
    else if(a=='S'&&c=='P'){if(k!=1||values[0]<0||values[0]>255||values[0]!=(int32_t)values[0])return false;pen=values[0]!=0;}
    else if((a=='P'&&(c=='A'||c=='R'||c=='U'||c=='D'))){if(k&1)return false;for(i=0;i<k;++i)if(values[i]< -100000000||values[i]>100000000)return false;if(c=='D'&&k&&pen)drawing=true;}
    else if((a=='I'&&c=='P')||(a=='S'&&c=='C')){if(k!=0&&k!=4&&!(a=='S'&&k==5))return false;if(k>=4&&(a=='S'?(values[0]==values[1]||values[2]==values[3]):(values[0]==values[2]||values[1]==values[3])))return false;}
    else if(a=='C'&&c=='I'){if((k!=1&&k!=2)||values[0]<=0||values[0]>100000000)return false;drawing|=pen;}
    else if(a=='A'&&(c=='A'||c=='R')){if((k!=3&&k!=4)||values[2]< -36000||values[2]>36000)return false;drawing|=pen;}
    else if(a=='P'&&c=='W'){if((k!=1&&k!=2)||values[0]<0||values[0]>1000)return false;}
    else if(a=='L'&&c=='T'){if(k>2|| (k&& (values[0]< -8||values[0]>8)))return false;}
    else if(a=='V'&&c=='S'){if(k>2|| (k&& (values[0]<0||values[0]>1000)))return false;}
    else if((a=='S'&&(c=='I'||c=='R'))||(a=='D'&&(c=='I'||c=='R'))){if(k!=2)return false;}
    else if(a=='C'&&(c=='S'||c=='A')){if(k!=1||values[0]<0||values[0]>127||values[0]!=(int32_t)values[0])return false;}
    else if(a=='S'&&c=='S'){if(k)return false;}
    else return false;
   }++p;
  }if(!tb_emit(f,s,"command.hpgl",start,p-start,n))return false;
 }if(!initialized||!drawing)return false;s->size=(int64_t)n;return true;
}

void xx_hpgl_plot_init(xx_hpgl_plot *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_HPGL_PLOT,"hpgl");}}
xx_hpgl_plot *xx_hpgl_plot_create(xx_io_device *d,int64_t at) {xx_hpgl_plot *r=(xx_hpgl_plot *)xx_mem_alloc(sizeof(*r));if(r)xx_hpgl_plot_init(r,d,at);return r;}
void xx_hpgl_plot_destroy(xx_hpgl_plot *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_hpgl_plot_free(xx_hpgl_plot *r) {if(r){xx_hpgl_plot_destroy(r);xx_mem_free(r);}}
bool xx_hpgl_plot_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_hpgl_plot_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
