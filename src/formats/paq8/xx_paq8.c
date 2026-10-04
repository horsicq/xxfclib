/* SPDX-License-Identifier: MIT. PAQ8F/JD/L/O grammar with RAM-only GPL codec helper. */
#include "xxfclib/formats/paq8/xx_paq8.h"
#include "../xx_legacy_archive.h"
#include "../xx_archive_codec_pipe.h"
typedef struct paq_member {uint32_t at,size,name,length;} paq_member;
static bool pq_parse(Abstractformat *f,pm_stream *s,ac_blob *b){uint32_t at=0,count=0,total=0,cap=0;paq_member *members=NULL;uint8_t *out=NULL;bool ok=false;const char *names[]={"paq8l","paq8f","paq8jd","paq8o"};
 for(unsigned i=0;i<4;++i){uint32_t n=(uint32_t)strlen(names[i]);if(b->n>=n+6U&&!memcmp(b->p,names[i],n)&&b->p[n]==' '&&b->p[n+1]=='-'&&b->p[n+2]>='0'&&b->p[n+2]<='9'&&b->p[n+3]==13&&b->p[n+4]==10){at=n+5U;break;}}
 if(!at)return ac_error(b,"PAQ8 revision unsupported; supported revisions F/JD/L/O");
 while(at<b->n&&b->p[at]!=26U){uint32_t size=0,start,end;unsigned digits=0;if(!ac_poll(b))goto done;
  while(at<b->n&&b->p[at]>='0'&&b->p[at]<='9'){unsigned d=b->p[at++]-'0';if(size>(AC_MAX_BYTES-d)/10U)goto done;size=size*10U+d;++digits;}
  if(!digits||at>=b->n||b->p[at++]!=9U||size>AC_MAX_BYTES-total||count==AC_MAX_MEMBERS)goto done;start=at;
  while(at<b->n&&b->p[at]!=13U){if(b->p[at]<32U||b->p[at]==127U||at-start>=4096U)goto done;++at;}end=at;
  if(end==start||at+1U>=b->n||b->p[at]!=13U||b->p[at+1]!=10U)goto done;at+=2U;
  if(count==cap){uint32_t next=cap?cap*2U:8U;uint64_t bytes=(uint64_t)next*sizeof(*members);paq_member *m;if(bytes>b->limit-b->used)goto done;m=(paq_member *)xx_mem_realloc(members,(size_t)bytes);if(!m)goto done;members=m;b->used+=(next-cap)*sizeof(*members);cap=next;}
  members[count].at=total;members[count].size=size;members[count].name=start;members[count].length=end-start;++count;total+=size;
 }
 if(!count||at>=b->n||b->p[at++]!=26U)goto done;out=ac_alloc(b,total);if(!out||!af_decode(b,2,out,total))goto done;
 for(uint32_t i=0;i<count;++i){char name[128];uint8_t *member=ac_alloc(b,members[i].size);uint32_t length=members[i].length;if(!member)goto done;if(length>96U)length=96U;memcpy(name,b->p+members[i].name,length);name[length]=0;memcpy(member,out+members[i].at,members[i].size);if(!ac_memory(f,s,b,name,member,members[i].size,0,1))goto done;}
 ok=true;
done:ac_release(b,out,total);if(members){xx_mem_free(members);b->used-=(uint64_t)cap*sizeof(*members);}return ok;
}
AC_PARSE(pq_parse)
AC_DEFINE(paq8,XX_FILE_TYPE_PAQ8,"paq8l")
