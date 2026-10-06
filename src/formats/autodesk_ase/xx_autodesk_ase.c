/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/ASE/ASEParser.cpp
 * Autodesk ASE200 static ASCII triangle meshes: complete typed scene, Standard materials, node transforms, ordered unique vertex/face IDs, finite coordinates and local index/material-reference checks. Original encoded sections exported. Nonzero animation times, UV/normal/color lists, external maps, hierarchy nodes and other material/node extensions declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/autodesk_ase/xx_autodesk_ase.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[19];return n>=20&&pm_read(f,0,b,19)&&tb_tag(b,"*3DSMAX_ASCIIEXPORT",18);}
static bool ae_open(tb_text *q) {return tb_word(q,"{")&&tb_done(q);}
static bool ae_bool(tb_text *q,unsigned count) {unsigned i;int32_t v;for(i=0;i<count;++i)if(!tb_i(q,&v)||v<0||v>1)return false;return tb_done(q);}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tb_text q={b,0,n,0,0,0};unsigned stack[4],depth=0,state=0,objects=0,matdecl=0,mats=0;uint32_t nv=0,nf=0,vertices=0,faces=0;unsigned meshflags=0,objflags=0,tmflags=0;uint64_t start=0,covered;bool scene=false,material=false;int32_t v;const char *str;
 if(!tb_utf(b,n,false,pd)||!tb_line(&q)||!tb_word(&q,"*3DSMAX_ASCIIEXPORT")||!tb_i(&q,&v)||v!=200||!tb_done(&q)||!tb_emit(f,s,"descriptor.ase",0,q.p,n)) {return false; } covered=q.p;
 while(q.p<n){if(tb_stop(pd))return false;if(!tb_next(&q)){if(q.p<n)return false;break;}
  if(tb_word(&q,"}")){if(!depth||!tb_done(&q))return false;
   if(state==2&&mats!=matdecl) {return false; } if(state==4&&(objflags!=7))return false;if(state==5&&(tmflags&15)!=15)return false;
   if(state==6&&(meshflags!=15)) {return false; } if(state==7&&vertices!=nv)return false;if(state==8&&faces!=nf)return false;
   state=stack[--depth];if(!depth){if(!tb_emit(f,s,"section.ase",start,q.p-start,n))return false;covered=q.p;}continue;
  }
  if(!depth){start=covered;
   if(tb_word(&q,"*COMMENT")){if(!tb_string(&q)||!tb_done(&q))return false;continue;}
   if(tb_word(&q,"*SCENE")){if(scene||!ae_open(&q))return false;scene=true;state=1;}
   else if(tb_word(&q,"*MATERIAL_LIST")){if(material||objects||!ae_open(&q))return false;material=true;state=2;}
   else if(tb_word(&q,"*GEOMOBJECT")){if(++objects>1024||!ae_open(&q))return false;state=4;objflags=0;}
   else { return false; } stack[depth++]=0;continue;
  }
  if(state==1){
   if(tb_word(&q,"*SCENE_FILENAME")){if(!tb_string(&q)||!tb_done(&q))return false;}
   else if(tb_word(&q,"*SCENE_FIRSTFRAME")||tb_word(&q,"*SCENE_LASTFRAME")){if(!tb_i(&q,&v)||!tb_done(&q))return false;}
   else if(tb_word(&q,"*SCENE_FRAMESPEED")||tb_word(&q,"*SCENE_TICKSPERFRAME")){if(!tb_i(&q,&v)||v<1||v>1000000||!tb_done(&q))return false;}
   else if(tb_word(&q,"*SCENE_BACKGROUND_STATIC")||tb_word(&q,"*SCENE_AMBIENT_STATIC")){if(!tb_nums(&q,3))return false;}else return false;
  }else if(state==2){if(tb_word(&q,"*MATERIAL_COUNT")){if(matdecl||!tb_i(&q,&v)||v<1||v>4096||!tb_done(&q))return false;matdecl=(unsigned)v;}
   else if(tb_word(&q,"*MATERIAL")){if(!tb_i(&q,&v)||v!=(int32_t)mats||mats>=matdecl||!ae_open(&q))return false;++mats;stack[depth++]=state;state=3;}else return false;
  }else if(state==3){
   if(tb_word(&q,"*MATERIAL_NAME")){if(!tb_string(&q)||!tb_done(&q))return false;}
   else if(tb_word(&q,"*MATERIAL_CLASS")){if(!tb_word(&q,"\"Standard\"")||!tb_done(&q))return false;}
   else if(tb_word(&q,"*MATERIAL_AMBIENT")||tb_word(&q,"*MATERIAL_DIFFUSE")||tb_word(&q,"*MATERIAL_SPECULAR")){if(!tb_nums(&q,3))return false;}
   else if(tb_word(&q,"*MATERIAL_SHINE")||tb_word(&q,"*MATERIAL_SHINESTRENGTH")||tb_word(&q,"*MATERIAL_TRANSPARENCY")||tb_word(&q,"*MATERIAL_WIRESIZE")||tb_word(&q,"*MATERIAL_XP_FALLOFF")||tb_word(&q,"*MATERIAL_SELFILLUM")){double x;if(!tb_num(&q,&x)||x<0||!tb_done(&q))return false;}
   else if(tb_word(&q,"*MATERIAL_SHADING")){if((!tb_word(&q,"Blinn")&&!tb_word(&q,"Phong")&&!tb_word(&q,"Metal")&&!tb_word(&q,"Constant"))||!tb_done(&q))return false;}
   else if(tb_word(&q,"*MATERIAL_FALLOFF")){if((!tb_word(&q,"In")&&!tb_word(&q,"Out"))||!tb_done(&q))return false;}
   else if(tb_word(&q,"*MATERIAL_XP_TYPE")){if((!tb_word(&q,"Filter")&&!tb_word(&q,"Subtractive")&&!tb_word(&q,"Additive"))||!tb_done(&q))return false;}else return false;
  }else if(state==4){
   if(tb_word(&q,"*NODE_NAME")){if(objflags&1||!tb_string(&q)||!tb_done(&q))return false;objflags|=1;}
   else if(tb_word(&q,"*NODE_TM")){if(objflags&2||!ae_open(&q))return false;objflags|=2;tmflags=0;stack[depth++]=state;state=5;}
   else if(tb_word(&q,"*MESH")){if(objflags&4||!ae_open(&q))return false;objflags|=4;meshflags=0;nv=nf=vertices=faces=0;stack[depth++]=state;state=6;}
   else if(tb_word(&q,"*PROP_MOTIONBLUR")||tb_word(&q,"*PROP_CASTSHADOW")||tb_word(&q,"*PROP_RECVSHADOW")){if(!ae_bool(&q,1))return false;}
   else if(tb_word(&q,"*MATERIAL_REF")){if(!tb_i(&q,&v)||v<0||(unsigned)v>=matdecl||!tb_done(&q))return false;}else return false;
  }else if(state==5){unsigned row;str=NULL;for(row=0;row<4;++row){const char *key[]={"*TM_ROW0","*TM_ROW1","*TM_ROW2","*TM_ROW3"};if(tb_word(&q,key[row])){if(tmflags&(1U<<row)||!tb_nums(&q,3))return false;tmflags|=1U<<row;str=key[row];break;}}if(str)continue;
   if(tb_word(&q,"*NODE_NAME")){if(!tb_string(&q)||!tb_done(&q))return false;}
   else if(tb_word(&q,"*INHERIT_POS")||tb_word(&q,"*INHERIT_ROT")||tb_word(&q,"*INHERIT_SCL")){if(!ae_bool(&q,3))return false;}
   else if(tb_word(&q,"*TM_POS")||tb_word(&q,"*TM_ROTAXIS")||tb_word(&q,"*TM_SCALE")||tb_word(&q,"*TM_SCALEAXIS")){if(!tb_nums(&q,3))return false;}
   else if(tb_word(&q,"*TM_ROTANGLE")||tb_word(&q,"*TM_SCALEAXISANG")){if(!tb_nums(&q,1))return false;}else return false;
  }else if(state==6){if(tb_word(&q,"*TIMEVALUE")){if(!tb_i(&q,&v)||v!=0||!tb_done(&q))return false;}
   else if(tb_word(&q,"*MESH_NUMVERTEX")){if(meshflags&1||!tb_i(&q,&v)||v<3||v>1000000||!tb_done(&q))return false;nv=(uint32_t)v;meshflags|=1;}
   else if(tb_word(&q,"*MESH_NUMFACES")){if(meshflags&2||!tb_i(&q,&v)||v<1||v>1000000||!tb_done(&q))return false;nf=(uint32_t)v;meshflags|=2;}
   else if(tb_word(&q,"*MESH_VERTEX_LIST")){if(!(meshflags&1)||(meshflags&4)||!ae_open(&q))return false;meshflags|=4;stack[depth++]=state;state=7;}
   else if(tb_word(&q,"*MESH_FACE_LIST")){if((meshflags&7)!=7||(meshflags&8)||!ae_open(&q))return false;meshflags|=8;stack[depth++]=state;state=8;}else return false;
  }else if(state==7){if(vertices>=nv||!tb_word(&q,"*MESH_VERTEX")||!tb_i(&q,&v)||v!=(int32_t)vertices++||!tb_nums(&q,3))return false;}
  else if(state==8){const char *keys[]={"A:","B:","C:","AB:","BC:","CA:"};int32_t indexes[3];unsigned j;
   if(faces>=nf||!tb_word(&q,"*MESH_FACE")||!tb_i(&q,&v)||v!=(int32_t)faces++||q.t==q.stop||b[q.t++]!=':')return false;
   for(j=0;j<6;++j){if(!tb_word(&q,keys[j])||!tb_i(&q,&v)||v<0||(j<3?(uint32_t)v>=nv:v>1))return false;if(j<3)indexes[j]=v;}
   if(indexes[0]==indexes[1]||indexes[0]==indexes[2]||indexes[1]==indexes[2]||!tb_word(&q,"*MESH_SMOOTHING"))return false;
   do{if(!tb_i(&q,&v)||v<0||v>32)return false;tb_space(&q);if(q.t==q.stop||b[q.t]!=',')break;++q.t;}while(true);
   if(!tb_word(&q,"*MESH_MTLID")||!tb_i(&q,&v)||v<0||v>65535||!tb_done(&q))return false;
  }else return false;
 }if(depth||!scene||!objects)return false;if(covered<n&&!tb_emit(f,s,"trailing.ase",covered,n-covered,n))return false;s->size=(int64_t)n;return true;
}

void xx_autodesk_ase_init(xx_autodesk_ase *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_AUTODESK_ASE,"ase");}}
xx_autodesk_ase *xx_autodesk_ase_create(xx_io_device *d,int64_t at) {xx_autodesk_ase *r=(xx_autodesk_ase *)xx_mem_alloc(sizeof(*r));if(r)xx_autodesk_ase_init(r,d,at);return r;}
void xx_autodesk_ase_destroy(xx_autodesk_ase *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_autodesk_ase_free(xx_autodesk_ase *r) {if(r){xx_autodesk_ase_destroy(r);xx_mem_free(r);}}
bool xx_autodesk_ase_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_autodesk_ase_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
