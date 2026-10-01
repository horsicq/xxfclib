/* SPDX-License-Identifier: MIT
 * Primary reference: https://fontforge.org/docs/techref/sfdformat.html
 * FontForge SFD1.0 outline subset: typed font metrics and complete counted glyphs with checked encodings, finite move/line/cubic contours and balanced spline/character/font terminators. Fore opens each legacy outline; one immediate optional SplineSet token is accepted. Original descriptor and glyph programs exported; bitmap/layer/reference/kerning/lookup extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/fontforge_sfd/xx_fontforge_sfd.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t b[12];return n>=24&&pm_read(f,0,b,12)&&fg_tag(b,"SplineFontDB:",12);}
static bool fg_sfd_field(fg_text *q,const char *key) {size_t z=xx_rt_strlen(key);fg_space(q);if(!fg_span(q->t,z+1,q->stop)||!fg_tag(q->b+q->t,key,z)||q->b[q->t+z]!=':')return false;q->t+=z+1;fg_space(q);return true;}
static bool fg_sfd_string(fg_text *q) {uint64_t z=q->stop-q->t;return z>0&&z<=1024;}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_text q={b,0,n,0,0,0};fg_ids ids={0};int32_t slots=0,count=0,i,value;uint32_t required=0;bool ok=false,any=false;double v;uint64_t header=0;
 if(!fg_utf(b,n,true,pd)||!fg_next(&q)||!fg_sfd_field(&q,"SplineFontDB")||!fg_num(&q,&v)||v!=1||!fg_done(&q))return false;
 while(fg_next(&q)){if(fg_stop(pd))return false;if(fg_sfd_field(&q,"BeginChars")){if(!fg_i(&q,&slots)||slots<1||slots>1114112||!fg_i(&q,&count)||count<1||count>4000||count>slots||!fg_done(&q))return false;header=q.p;break;}
 if(fg_sfd_field(&q,"FontName")){if(required&1||!fg_sfd_string(&q))return false;required|=1;}
 else if(fg_sfd_field(&q,"Ascent")){if(required&2||!fg_i(&q,&value)||value<1||value>1000000||!fg_done(&q))return false;required|=2;}
 else if(fg_sfd_field(&q,"Descent")){if(required&4||!fg_i(&q,&value)||value<0||value>1000000||!fg_done(&q))return false;required|=4;}
 else if(fg_sfd_field(&q,"FullName")||fg_sfd_field(&q,"FamilyName")||fg_sfd_field(&q,"Weight")||fg_sfd_field(&q,"Copyright")||fg_sfd_field(&q,"Comments")||fg_sfd_field(&q,"Version")||fg_sfd_field(&q,"Encoding")){if(!fg_sfd_string(&q))return false;}
 else if(fg_sfd_field(&q,"ItalicAngle")||fg_sfd_field(&q,"UnderlinePosition")||fg_sfd_field(&q,"UnderlineWidth")){if(!fg_num(&q,&v)||!fg_done(&q))return false;}
 else if(fg_sfd_field(&q,"NeedsXUIDChange")||fg_sfd_field(&q,"AntiAlias")){if(!fg_i(&q,&value)||value<0||value>1||!fg_done(&q))return false;}
 else if(fg_sfd_field(&q,"DisplaySize")){if(!fg_i(&q,&value)||value<-4096||value>4096||!fg_done(&q))return false;}else return false;
 }
 if(!header||required!=7||!fg_emit(f,s,"font-descriptor.sfd",0,header,n)||!fg_ids_init(&ids,(uint32_t)count))return false;
 for(i=0;i<count;++i){uint64_t start;bool encoding=false,width=false,program=false,move=false,explicit_open=false;char label[48];
 if(!fg_next(&q)||!fg_sfd_field(&q,"StartChar")||!fg_sfd_string(&q))goto done;start=q.start;
 while(fg_next(&q)){if(fg_stop(pd))goto done;
 if(fg_word(&q,"EndChar")){if(!fg_done(&q)||!encoding||!width||program)goto done;break;}
 if(fg_sfd_field(&q,"Encoding")){int32_t unicode;if(encoding||!fg_i(&q,&value)||value<0||value>=slots||!fg_id(&ids,(uint32_t)value+1,true,pd)||!fg_i(&q,&unicode)||unicode<-1||unicode>1114111||!fg_done(&q))goto done;encoding=true;}
 else if(fg_sfd_field(&q,"Width")||fg_sfd_field(&q,"VWidth")){bool horizontal=fg_tag(b+q.start,"Width:",6);if(!fg_i(&q,&value)||value<0||value>1000000||!fg_done(&q)||(horizontal&&width))goto done;if(horizontal)width=true;}
 else if(fg_sfd_field(&q,"Flags")){uint64_t j;if(!fg_sfd_string(&q))goto done;for(j=q.t;j<q.stop;++j)if(b[j]<'A'||b[j]>'Z')goto done;}
 else if(fg_word(&q,"Fore")){if(!fg_done(&q)||program)goto done;program=true;move=false;explicit_open=false;}
 else if(fg_word(&q,"SplineSet")){if(!program||move||explicit_open||!fg_done(&q))goto done;explicit_open=true;}
 else if(fg_word(&q,"EndSplineSet")){if(!program||!move||!fg_done(&q))goto done;program=false;any=true;}
 else {double xy[6];unsigned k=0;uint8_t command;if(!program)goto done;while(k<6&&fg_num(&q,&xy[k]))++k;fg_space(&q);if(q.t==q.stop)goto done;command=b[q.t++];if((command=='m'||command=='l')&&k!=2)goto done;if(command=='c'&&k!=6)goto done;if(command!='m'&&command!='l'&&command!='c')goto done;if(command!='m'&&!move)goto done;if(command=='m')move=true;if(!fg_i(&q,&value)||value<0||value>65535||!fg_done(&q))goto done;}
 }
 if(program)goto done;xx_rt_snprintf(label,sizeof(label),"glyph-%u.sfd",(unsigned)i);if(!fg_emit(f,s,label,start,q.p-start,n))goto done;
 }
 if(!any||!fg_next(&q)||!fg_word(&q,"EndChars")||!fg_done(&q)||!fg_next(&q)||!fg_word(&q,"EndSplineFont")||!fg_done(&q)||!fg_emit(f,s,"terminator.sfd",q.start,q.p-q.start,n)||fg_next(&q)||q.p!=q.end||!fg_cover(f,s,"whitespace.sfd",n))goto done;ok=true;
done:xx_mem_free(ids.values);return ok;
}

void xx_fontforge_sfd_init(xx_fontforge_sfd *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_FONTFORGE_SFD,"sfd");}}
xx_fontforge_sfd *xx_fontforge_sfd_create(xx_io_device *d,int64_t at) {xx_fontforge_sfd *r=(xx_fontforge_sfd *)xx_mem_alloc(sizeof(*r));if(r)xx_fontforge_sfd_init(r,d,at);return r;}
void xx_fontforge_sfd_destroy(xx_fontforge_sfd *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_fontforge_sfd_free(xx_fontforge_sfd *r) {if(r){xx_fontforge_sfd_destroy(r);xx_mem_free(r);}}
bool xx_fontforge_sfd_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_fontforge_sfd_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
