/* SPDX-License-Identifier: MIT
 * Primary reference: https://learn.microsoft.com/en-us/windows/win32/direct3d9/dx9-graphics-reference-x-file-format
 * Microsoft X text0303/32-bit meshes: complete standard typed frames/materials/mesh data with finite values and bounded resolved indexes, including bounded XSkinMeshHeader and SkinWeights records. Recognized standard templates are validated; original top-level templates and objects exported without skinning evaluation. Compressed/binary/animation/custom templates declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/directx_x/xx_directx_x.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
typedef struct fg_x_name {uint64_t at,z;unsigned kind;} fg_x_name;
typedef struct fg_x_state {fg_lex q;fg_x_name *names,*refs;unsigned nn,nr,meshes,objects;uint32_t work;} fg_x_state;
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t b[16];return n>=32&&pm_read(f,0,b,16)&&fg_tag(b,"xof 0303txt 0032",16);}
static bool fg_x_equal(fg_x_state *st,fg_x_name a,fg_x_name b) {uint64_t i;if(a.z!=b.z)return false;for(i=0;i<a.z;++i){if(++st->work>16000000||fg_stop(st->q.pd)||st->q.b[a.at+i]!=st->q.b[b.at+i])return false;}return true;}
static bool fg_x_add(fg_x_state *st,uint64_t at,uint64_t z,unsigned kind,bool ref) {fg_x_name v={at,z,kind};unsigned i;if(ref){if(st->nr>=4096)return false;st->refs[st->nr++]=v;return true;}if(st->nn>=4096)return false;for(i=0;i<st->nn;++i)if(fg_x_equal(st,v,st->names[i])||st->work>16000000)return false;st->names[st->nn++]=v;return true;}
static bool fg_x_int(fg_lex *q,int32_t *v) {return fg_integer(q,v)&&*v>=0&&fg_char(q,';');}
static bool fg_x_float(fg_lex *q,bool color) {double v;return fg_number(q,&v)&&(!color||(v>=0&&v<=1))&&fg_char(q,';');}
static bool fg_x_vectors(fg_lex *q,uint32_t count,unsigned width,bool color) {uint32_t i;unsigned j;for(i=0;i<count;++i){for(j=0;j<width;++j)if(!fg_x_float(q,color))return false;if(!fg_char(q,i+1<count?',':';'))return false;}return true;}
static bool fg_x_indexes(fg_lex *q,uint32_t count,uint32_t vertices) {uint32_t i,j;int32_t width,index;for(i=0;i<count;++i){if(!fg_x_int(q,&width)||width<3||width>64)return false;for(j=0;j<(uint32_t)width;++j){if(!fg_integer(q,&index)||index<0||(uint32_t)index>=vertices||!fg_char(q,j+1<(uint32_t)width?',':';'))return false;}if(!fg_char(q,i+1<count?',':';'))return false;}return true;}
static bool fg_x_template(fg_x_state *st) {
 static const char *const names[]={"XSkinMeshHeader","VertexDuplicationIndices","SkinWeights","AnimTicksPerSecond"};
 static const char *const body[]={"<3cf169ce-ff7c-44ab-93c0-f78f62d172e2>WORDnMaxSkinWeightsPerVertex;WORDnMaxSkinWeightsPerFace;WORDnBones;","<b8d65549-d7c9-4995-89cf-53a9a8b031e3>DWORDnIndices;DWORDnOriginalVertices;arrayDWORDindices[nIndices];","<6f0d123b-bad2-4167-a0d0-80224f25fabb>STRINGtransformNodeName;DWORDnWeights;arrayDWORDvertexIndices[nWeights];arrayFLOATweights[nWeights];Matrix4x4matrixOffset;","<9e415a43-7ba6-4a73-8743-b73d47e88476>DWORDAnimTicksPerSecond;"};
 fg_lex *q=&st->q;uint64_t at,z;unsigned kind=4;size_t i=0;if(!fg_kw(q,"template")||!fg_ident(q,&at,&z))return false;for(kind=0;kind<4;++kind)if(z==xx_rt_strlen(names[kind])&&fg_tag(q->b+at,names[kind],(size_t)z))break;if(kind==4||!fg_char(q,'{'))return false;
 while(body[kind][i]){uint8_t c;if(!fg_skip(q)||q->p==q->n)return false;c=q->b[q->p++];if(c!=(uint8_t)body[kind][i++])return false;}
 return fg_char(q,'}');
}
static bool fg_x_object(fg_x_state *st,unsigned parent,unsigned depth,uint32_t vertices,uint32_t faces) {
 fg_lex *q=&st->q;unsigned kind=0,seen=0;uint64_t at,z;int32_t count,value;uint32_t nv=0,nf=0;unsigned i;bool named=false;
 if(depth>32||++st->objects>4096)return false;
 if(fg_kw(q,"Frame"))kind=1;else if(fg_kw(q,"Mesh"))kind=2;else if(fg_kw(q,"Material"))kind=3;else if(fg_kw(q,"FrameTransformMatrix"))kind=4;else if(fg_kw(q,"MeshNormals"))kind=5;else if(fg_kw(q,"MeshTextureCoords"))kind=6;else if(fg_kw(q,"MeshMaterialList"))kind=7;else if(fg_kw(q,"VertexDuplicationIndices"))kind=8;else if(fg_kw(q,"XSkinMeshHeader"))kind=9;else if(fg_kw(q,"SkinWeights"))kind=10;else if(fg_kw(q,"AnimTicksPerSecond"))kind=11;else if(fg_kw(q,"TextureFilename"))kind=12;else return false;
 if(parent==0){if(kind!=1&&kind!=2&&kind!=3&&kind!=11)return false;}else if(parent==1){if(kind!=1&&kind!=2&&kind!=4)return false;}else if(parent==2){if(kind<5||kind>10)return false;}else if(parent==3){if(kind!=12)return false;}else if(parent==7){if(kind!=3)return false;}else return false;
 if(!fg_skip(q)) {return false; } if(q->p<q->n&&q->b[q->p]!='{'){if(!fg_ident(q,&at,&z))return false;named=true;if((kind==1||kind==3)&&!fg_x_add(st,at,z,kind,false))return false;}
 if(!fg_char(q,'{'))return false;
 if(kind==4){for(i=0;i<16;++i){double v;if(!fg_number(q,&v)||!fg_char(q,i<15?',':';'))return false;}if(!fg_char(q,';'))return false;}
 else if(kind==3){if(!fg_x_vectors(q,1,4,true)||!fg_x_float(q,false)||!fg_x_vectors(q,1,3,true)||!fg_x_vectors(q,1,3,true))return false;while(fg_skip(q)&&q->p<q->n&&q->b[q->p]!='}')if(!fg_x_object(st,kind,depth+1,0,0))return false;}
 else if(kind==12){if(!fg_quoted(q,'"',&at,&z)||!z||!fg_char(q,';'))return false;}
 else if(kind==11){if(!fg_x_int(q,&count)||count<1||count>1000000)return false;}
 else if(kind==9){if(!fg_x_int(q,&count)||count<1||count>1024||!fg_x_int(q,&count)||count<1||count>65536||!fg_x_int(q,&count)||count<1||count>4096)return false;}
 else if(kind==8){if(!fg_x_int(q,&count)||count!=(int32_t)vertices||!fg_x_int(q,&value)||value<1||(uint32_t)value>vertices)return false;for(i=0;i<(unsigned)count;++i){int32_t index;if(!fg_integer(q,&index)||index<0||index>=value||!fg_char(q,i+1<(unsigned)count?',':';'))return false;}}
 else if(kind==10){double weight;if(!fg_quoted(q,'"',&at,&z)||!z||!fg_x_add(st,at,z,1,true)||!fg_char(q,';')||!fg_x_int(q,&count)||count<1||(uint32_t)count>vertices)return false;for(i=0;i<(unsigned)count;++i){if(!fg_integer(q,&value)||value<0||(uint32_t)value>=vertices||!fg_char(q,i+1<(unsigned)count?',':';'))return false;}for(i=0;i<(unsigned)count;++i)if(!fg_number(q,&weight)||weight<=0||weight>1||!fg_char(q,i+1<(unsigned)count?',':';'))return false;for(i=0;i<16;++i)if(!fg_number(q,&weight)||!fg_char(q,i<15?',':';'))return false;if(!fg_char(q,';'))return false;}
 else if(kind==6){if(!fg_x_int(q,&count)||(uint32_t)count!=vertices||!fg_x_vectors(q,(uint32_t)count,2,false))return false;}
 else if(kind==5){if(!fg_x_int(q,&count)||count<1||count>1000000||!fg_x_vectors(q,(uint32_t)count,3,false))return false;nv=(uint32_t)count;if(!fg_x_int(q,&count)||(uint32_t)count!=faces||!fg_x_indexes(q,faces,nv))return false;}
 else if(kind==7){int32_t materials;if(!fg_x_int(q,&materials)||materials<1||materials>4096||!fg_x_int(q,&count)||(uint32_t)count!=faces)return false;for(i=0;i<(unsigned)count;++i){if(!fg_integer(q,&value)||value<0||value>=materials||!fg_char(q,i+1<(unsigned)count?',':';'))return false;}for(i=0;i<(unsigned)materials;++i){fg_lex save=*q;if(fg_char(q,'{')){if(!fg_ident(q,&at,&z)||!fg_x_add(st,at,z,3,true)||!fg_char(q,'}'))return false;}else {*q=save;if(!fg_x_object(st,7,depth+1,0,0))return false;}}}
 else {if(kind==2){if(!fg_x_int(q,&count)||count<3||count>1000000||!fg_x_vectors(q,(uint32_t)count,3,false))return false;nv=(uint32_t)count;if(!fg_x_int(q,&count)||count<1||count>1000000||!fg_x_indexes(q,(uint32_t)count,nv))return false;nf=(uint32_t)count;++st->meshes;}
 while(fg_skip(q)&&q->p<q->n&&q->b[q->p]!='}'){unsigned bit=0;fg_lex save=*q;if(fg_kw(q,"FrameTransformMatrix"))bit=1;else if(fg_kw(q,"MeshNormals"))bit=2;else if(fg_kw(q,"MeshTextureCoords"))bit=4;else if(fg_kw(q,"MeshMaterialList"))bit=8;else if(fg_kw(q,"VertexDuplicationIndices"))bit=16;else if(fg_kw(q,"XSkinMeshHeader"))bit=32;*q=save;if(bit&&(seen&bit))return false;seen|=bit;if(!fg_x_object(st,kind,depth+1,nv,nf))return false;}}
 (void)named;return fg_char(q,'}');
}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_x_state st;unsigned i,j;bool ok=false;xx_mem_zero(&st,sizeof(st));st.q.b=b;st.q.p=16;st.q.n=n;st.q.pd=pd;st.q.comments=true;
 if(n<16||!fg_tag(b,"xof 0303txt 0032",16)||!fg_utf(b,n,true,pd)||!fg_emit(f,s,"descriptor.x",0,16,n)) {return false; } st.names=(fg_x_name *)xx_mem_alloc(sizeof(fg_x_name)*4096);st.refs=(fg_x_name *)xx_mem_alloc(sizeof(fg_x_name)*4096);if(!st.names||!st.refs)goto done;
 while(fg_skip(&st.q)&&st.q.p<n){uint64_t p=st.q.p;fg_lex save=st.q;bool temp=fg_kw(&st.q,"template");char label[48];st.q=save;if(!(temp?fg_x_template(&st):fg_x_object(&st,0,0,0,0)))goto done;xx_rt_snprintf(label,sizeof(label),"object-%u.x",(unsigned)s->count);if(!fg_emit(f,s,label,p,st.q.p-p,n))goto done;}
 if(!st.meshes||!fg_end(&st.q)) {goto done; } for(i=0;i<st.nr;++i){bool found=false;for(j=0;j<st.nn;++j)if(fg_x_equal(&st,st.refs[i],st.names[j])){if(st.refs[i].kind!=st.names[j].kind)goto done;found=true;break;}if(!found||st.work>16000000)goto done;}ok=fg_cover(f,s,"framing.x",n);
done:if(st.names)xx_mem_free(st.names);if(st.refs)xx_mem_free(st.refs);return ok;
}

void xx_directx_x_init(xx_directx_x *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_DIRECTX_X,"x");}}
xx_directx_x *xx_directx_x_create(xx_io_device *d,int64_t at) {xx_directx_x *r=(xx_directx_x *)xx_mem_alloc(sizeof(*r));if(r)xx_directx_x_init(r,d,at);return r;}
void xx_directx_x_destroy(xx_directx_x *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_directx_x_free(xx_directx_x *r) {if(r){xx_directx_x_destroy(r);xx_mem_free(r);}}
bool xx_directx_x_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_directx_x_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
