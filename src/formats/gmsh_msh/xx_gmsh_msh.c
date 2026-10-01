/* SPDX-License-Identifier: MIT
 * Primary reference: https://gmsh.info/doc/texinfo/#MSH-file-format-version-2-_0028Legacy_0029
 * Gmsh MSH2.2 ASCII: exact section/count framing, unique node/element IDs, finite coordinates, complete supported element topology/tags and bounded physical names/data. Original mesh sections exported; binary/newer versions and unknown sections unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/gmsh_msh/xx_gmsh_msh.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[11];return n>=12&&pm_read(f,0,b,11)&&tb_tag(b,"$MeshFormat",11);}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 static const uint8_t nodes_per_type[32]={0,2,3,4,4,8,6,5,3,6,9,10,27,18,14,1,8,20,15,13,9,10,12,15,15,21,4,5,6,20,35,56};
 tb_text q={b,0,n,0,0,0};tb_ids nodes={0},elements={0};uint32_t seen=0,nodecount=0,elementcount=0;uint64_t start;int32_t count,i,j,tag;bool result=false;char label[48];
#define GM(x) do{if(!(x))goto done;}while(0)
 GM(tb_utf(b,n,false,pd)&&tb_next(&q)&&tb_word(&q,"$MeshFormat")&&tb_done(&q)&&tb_next(&q)&&tb_word(&q,"2.2")&&tb_word(&q,"0")&&tb_word(&q,"8")&&tb_done(&q)&&tb_next(&q)&&tb_word(&q,"$EndMeshFormat")&&tb_done(&q)&&tb_emit(f,s,"descriptor.msh",0,q.p,n));
 while(q.p<n){GM(!tb_stop(pd));start=q.p;if(!tb_next(&q)){GM(q.p==n);break;}
  if(tb_word(&q,"$PhysicalNames")){GM(!(seen&1)&&!(seen&6)&&tb_done(&q)&&tb_next(&q)&&tb_i(&q,&count)&&count>=0&&count<=4096&&tb_done(&q));seen|=1;
   for(i=0;i<count;++i){int32_t dim;GM(tb_next(&q)&&tb_i(&q,&dim)&&dim>=0&&dim<=3&&tb_i(&q,&tag)&&tag>0&&tb_string(&q)&&tb_done(&q));}GM(tb_next(&q)&&tb_word(&q,"$EndPhysicalNames")&&tb_done(&q));GM(tb_emit(f,s,"physical-names.msh",start,q.p-start,n));
  }
  else if(tb_word(&q,"$Nodes")){GM(!(seen&2)&&!(seen&4)&&tb_done(&q)&&tb_next(&q)&&tb_i(&q,&count)&&count>=3&&count<=1000000&&tb_done(&q)&&tb_ids_init(&nodes,(uint32_t)count));seen|=2;nodecount=(uint32_t)count;
   for(i=0;i<count;++i)GM(tb_next(&q)&&tb_i(&q,&tag)&&tag>0&&tb_id(&nodes,(uint32_t)tag,true,pd)&&tb_nums(&q,3));GM(tb_next(&q)&&tb_word(&q,"$EndNodes")&&tb_done(&q)&&tb_emit(f,s,"nodes.msh",start,q.p-start,n));
  }
  else if(tb_word(&q,"$Elements")){GM((seen&2)&&!(seen&4)&&tb_done(&q)&&tb_next(&q)&&tb_i(&q,&count)&&count>0&&count<=1000000&&tb_done(&q)&&tb_ids_init(&elements,(uint32_t)count));seen|=4;elementcount=(uint32_t)count;
   for(i=0;i<count;++i){int32_t type,tags;GM(tb_next(&q)&&tb_i(&q,&tag)&&tag>0&&tb_id(&elements,(uint32_t)tag,true,pd)&&tb_i(&q,&type)&&type>0&&type<32&&tb_i(&q,&tags)&&tags>=0&&tags<=16);
    for(j=0;j<tags;++j)GM(tb_i(&q,&tag));for(j=0;j<nodes_per_type[type];++j)GM(tb_i(&q,&tag)&&tag>0&&tb_id(&nodes,(uint32_t)tag,false,pd));GM(tb_done(&q));
   }GM(tb_next(&q)&&tb_word(&q,"$EndElements")&&tb_done(&q)&&tb_emit(f,s,"elements.msh",start,q.p-start,n));
  }
  else if(tb_word(&q,"$NodeData")||tb_word(&q,"$ElementData")){bool node=tb_tag(b+q.t-9,"$NodeData",9);int32_t stringtags,realtags,inttags,components=0,entries=0,k;tb_ids data_ids={0};uint32_t limit=node?nodecount:elementcount;const char *end=node?"$EndNodeData":"$EndElementData";
   GM((seen&6)==6&&tb_done(&q)&&tb_next(&q)&&tb_i(&q,&stringtags)&&stringtags>0&&stringtags<=16&&tb_done(&q));for(k=0;k<stringtags;++k)GM(tb_next(&q)&&tb_string(&q)&&tb_done(&q));
   GM(tb_next(&q)&&tb_i(&q,&realtags)&&realtags>=0&&realtags<=16&&tb_done(&q));for(k=0;k<realtags;++k)GM(tb_next(&q)&&tb_nums(&q,1));
   GM(tb_next(&q)&&tb_i(&q,&inttags)&&inttags>=3&&inttags<=16&&tb_done(&q));for(k=0;k<inttags;++k){GM(tb_next(&q)&&tb_i(&q,&tag)&&tb_done(&q));if(k==1)components=tag;if(k==2)entries=tag;}GM(components>0&&components<=16&&entries>=0&&(uint32_t)entries<=limit&&tb_ids_init(&data_ids,(uint32_t)entries));
   for(k=0;k<entries;++k){if(!tb_next(&q)||!tb_i(&q,&tag)||tag<=0||!tb_id(node?&nodes:&elements,(uint32_t)tag,false,pd)||!tb_id(&data_ids,(uint32_t)tag,true,pd)||!tb_nums(&q,(unsigned)components)){xx_mem_free(data_ids.values);goto done;}}
   xx_mem_free(data_ids.values);GM(tb_next(&q)&&tb_word(&q,end)&&tb_done(&q));xx_rt_snprintf(label,sizeof(label),"%s-%u.msh",node?"node-data":"element-data",(unsigned)s->count);GM(tb_emit(f,s,label,start,q.p-start,n));
  }
  else goto done;
 }
 GM((seen&6)==6);if(!tb_cover(f,s,"comments.msh",n))goto done;s->size=(int64_t)n;result=true;
done:xx_mem_free(nodes.values);xx_mem_free(elements.values);return result;
#undef GM
}

void xx_gmsh_msh_init(xx_gmsh_msh *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_GMSH_MSH,"msh");}}
xx_gmsh_msh *xx_gmsh_msh_create(xx_io_device *d,int64_t at) {xx_gmsh_msh *r=(xx_gmsh_msh *)xx_mem_alloc(sizeof(*r));if(r)xx_gmsh_msh_init(r,d,at);return r;}
void xx_gmsh_msh_destroy(xx_gmsh_msh *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gmsh_msh_free(xx_gmsh_msh *r) {if(r){xx_gmsh_msh_destroy(r);xx_mem_free(r);}}
bool xx_gmsh_msh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_gmsh_msh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
