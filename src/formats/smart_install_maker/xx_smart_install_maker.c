/* SPDX-License-Identifier: MIT
 * Independent C implementation of the published Smart Install Maker layout.
 * Primary grammar: Binary Refinery xtsim (BSD-3-Clause), Jesko Huttenhain.
 * No upstream Python code is compiled or executed by this reader.
 */
#include "xxfclib/formats/smart_install_maker/xx_smart_install_maker.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzx/xx_lzx.h"
#include "xxfclib/algo/quantum/xx_quantum.h"
#include "xxfclib/data/xx_data.h"
#ifdef _WIN32
#include <windows.h>
#endif
#define SIM_TYPE ((xx_file_type_t)2764)
#define SIM_CAP (UINT64_C(64)*1024*1024)
#define SIM_BUDGET (UINT64_C(256)*1024*1024)
#define SIM_FIXED (UINT64_C(128)*1024)
typedef struct sim_budget { uint64_t used,limit,max;xx_pd_struct *pd; } sim_budget;
typedef struct sim_reader { const uint8_t *p;size_t n,at; } sim_reader;
typedef struct sim_string { const uint8_t *p;size_t n; } sim_string;
typedef struct sim_context { uint64_t retained; } sim_context;
typedef struct sim_folder { uint32_t at;uint16_t blocks,type; } sim_folder;
static bool sim_stop(sim_budget *b) {return b->pd && xx_pd_is_stopped(b->pd);}
static bool sim_take(sim_budget *b,uint64_t n) {if(sim_stop(b)||n>b->limit-b->used)return false;b->used+=n;return true;}
static void sim_secret_free(void *p) {if(p){xx_mem_zero(p,xx_rt_strlen((const char *)p)+1);xx_mem_free(p);}}
static bool sim_input(Abstractformat *f,uint8_t *image,size_t n,xx_pd_struct *pd) {
    size_t done=0;bool ok=xx_io_seek64(f->device,f->base_address,SEEK_SET)==0;int level=xx_pd_enter_level(pd,n,"Smart Install Maker input");
    while(ok && done<n) {size_t target=done+(n-done>65536?65536:n-done);
        while(done<target) {ssize_t got;if(xx_pd_is_stopped(pd)){ok=false;break;}got=xx_io_read(f->device,image+done,target-done);if(got<=0||(size_t)got>target-done){ok=false;break;}done+=(size_t)got;}
        xx_pd_set_current(pd,level,done);if(xx_pd_is_stopped(pd))ok=false;
    }
    if(level>=0) {xx_pd_leave_level(pd,level); } return ok&&!xx_pd_is_stopped(pd);
}
static bool sim_range(size_t n,size_t p,size_t z) {return p<=n && z<=n-p;}
static bool sim_string_read(sim_reader *r,sim_string *s) {
    size_t at=r->at;while(r->at<r->n && r->p[r->at]) {if(r->at-at>=4096)return false;++r->at;}
    if(r->at==r->n) {return false; } s->p=r->p+at;s->n=r->at-at;++r->at;return true;
}
static bool sim_number(sim_string s,uint32_t *out) {
    size_t i;uint32_t n=0;if(!s.n || s.n>10)return false;
    for(i=0;i<s.n;++i) {unsigned c=s.p[i];if(c<'0'||c>'9'||n>(UINT32_MAX-(c-'0'))/10)return false;n=n*10+c-'0';}*out=n;return true;
}
static bool sim_utf8(const uint8_t *p,size_t n) {
    size_t i=0;while(i<n) {uint32_t c=p[i++],min=0;unsigned k=0;if(c<128)continue;
        if(c>=0xc2 && c<=0xdf){c&=31;k=1;min=128;}else if(c>=0xe0 && c<=0xef){c&=15;k=2;min=2048;}else if(c>=0xf0 && c<=0xf4){c&=7;k=3;min=65536;}else return false;
        while(k--) {if(i==n || (p[i]&0xc0)!=0x80)return false;c=(c<<6)|(p[i++]&63);}if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;
    }return true;
}
static unsigned sim_codepage(uint32_t lcid) {
    switch(lcid&1023U) {case 0x19:case 0x22:case 0x23:return 1251;case 0x05:case 0x0e:case 0x15:case 0x18:case 0x1b:case 0x24:return 1250;
    case 0x08:return 1253;case 0x1f:return 1254;case 0x0d:return 1255;case 0x01:return 1256;case 0x2a:return 1258;case 0x1e:return 874;
    case 0x11:return 932;case 0x12:return 949;case 0x04:return lcid==0x804||lcid==0x1004?936:950;default:return 1252;}
}
static char *sim_decode(sim_string s,unsigned codepage,sim_budget *b) {
    char *out;size_t cap=s.n*4+1;if(!sim_take(b,cap))return NULL;out=(char *)xx_mem_alloc(cap);if(!out)return NULL;
#ifdef _WIN32
    {wchar_t wide[4097];int n=0,k=0;if(s.n)n=MultiByteToWideChar(codepage,codepage==65001?MB_ERR_INVALID_CHARS:0,(const char *)s.p,(int)s.n,wide,4096);
    if((s.n&&!n)|| (n && !(k=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,n,out,(int)cap-1,NULL,NULL)))){xx_mem_free(out);return NULL;}out[k]=0;}
#else
    (void)codepage;
    {size_t i;for(i=0;i<s.n;++i)if(s.p[i]>=128){xx_mem_free(out);return NULL;}if(s.n)xx_rt_memcpy(out,s.p,s.n);out[s.n]=0;}
#endif
    if(!sim_utf8((const uint8_t *)out,xx_rt_strlen(out))) {xx_mem_free(out);return NULL;}return out;
}
static bool sim_reserved(const char *p,size_t n) {
    char name[16];size_t i=0;while(i<n && p[i]!='.' && i<15){unsigned c=(unsigned char)p[i];name[i]=(char)(c>='a'&&c<='z'?c-32:c);++i;}
    if(i==15 && i<n && p[i]!='.') {return false; } while(i && name[i-1]==' ')--i;name[i]=0;
    if(!xx_rt_strcmp(name,"CON")||!xx_rt_strcmp(name,"PRN")||!xx_rt_strcmp(name,"AUX")||!xx_rt_strcmp(name,"NUL")||!xx_rt_strcmp(name,"CONIN$")||!xx_rt_strcmp(name,"CONOUT$"))return true;
    if(i>=3 && (!xx_rt_memcmp(name,"COM",3)||!xx_rt_memcmp(name,"LPT",3))) {
        if(i==4&&name[3]>='1'&&name[3]<='9')return true;
        if(i==5&&(unsigned char)name[3]==0xc2&&((unsigned char)name[4]==0xb9||(unsigned char)name[4]==0xb2||(unsigned char)name[4]==0xb3))return true;
    }return false;
}
static bool sim_equal(const char *a,const char *b) {
    if(pm_same_path(a,b))return true;
#ifdef _WIN32
    {wchar_t *x=xx_str_utf8_to_unicode(a),*y=xx_str_utf8_to_unicode(b);bool same=x&&y&&CompareStringOrdinal(x,-1,y,-1,TRUE)==CSTR_EQUAL;xx_str_wfree(x);xx_str_wfree(y);return same;}
#else
    return false;
#endif
}
static bool sim_safe(const char *s) {
    const char *part=s,*p;size_t n;if(!s||!s[0]||s[0]=='/'||s[0]=='\\')return false;
    for(p=s;;++p) {unsigned c=(unsigned char)*p;if(c && (c<32||c==':'||c=='<'||c=='>'||c=='"'||c=='|'||c=='?'||c=='*'))return false;
        if(c=='/'||c=='\\'||!c) {n=(size_t)(p-part);if(!n||(n==1&&part[0]=='.')||(n==2&&part[0]=='.'&&part[1]=='.')||part[n-1]=='.'||part[n-1]==' '||sim_reserved(part,n))return false;if(!c)return true;part=p+1;}}
}
static char *sim_path(sim_string source,unsigned cp,const char *prefix,bool runtime,sim_budget *b) {
    static const char *mask[37]={NULL,"ProgramFiles","WindowsDir","SystemDir","InstallPath","TempDir","Desktop","QuickLaunch","ProgramsDir","StartMenu","MyDocuments","Favorites","SendTo","UserProfile","StartUp","FontsDir","CommonFiles","SystemDrive","CurrentDirectory",NULL,"UserName","Language","ComputerName",NULL,NULL,NULL,"AppData","CommonAppData","CommonDesktop","CommonDocuments","CommonFavourites","CommonPrograms","CommonStartMenu","CommonStartup","Templates","CommonTemplates","ProgramFiles64"};
    static const char *temp[21]={NULL,NULL,NULL,NULL,"header.png","wizard.bmp","background.bmp","folder.png","group.png","password.png",NULL,NULL,NULL,NULL,NULL,"license1.rtf","information.rtf",NULL,NULL,NULL,"license2.rtf"};
    char *decoded=sim_decode(source,cp,b),*out;size_t i=0,k=0,cap;
    if(!decoded) {return NULL; } cap=xx_rt_strlen(decoded)*4+128;if(!sim_take(b,cap)){xx_mem_free(decoded);return NULL;}out=(char *)xx_mem_alloc(cap);if(!out){xx_mem_free(decoded);return NULL;}
    if(runtime && !xx_rt_strncmp(decoded,"$inst\\",6)) {uint32_t v;sim_string t={(const uint8_t *)decoded+6,xx_rt_strlen(decoded+6)};if(t.n>4 && !xx_rt_strcmp(decoded+xx_rt_strlen(decoded)-4,".tmp")) {t.n-=4;if(sim_number(t,&v) && v<21 && temp[v]) {xx_rt_snprintf(out,cap,"%s/%s",prefix,temp[v]);goto ready;}}}
    k=(size_t)xx_rt_snprintf(out,cap,"%s/",prefix);
    if(decoded[0] && decoded[1]==':' && (decoded[2]=='\\'||decoded[2]=='/') && ((decoded[0]>='a'&&decoded[0]<='z')||(decoded[0]>='A'&&decoded[0]<='Z'))) {out[k++]='$';xx_rt_memcpy(out+k,"Drive",5);k+=5;out[k++]=(char)(decoded[0]>='a'?decoded[0]-32:decoded[0]);out[k++]='/';i=3;}
    while(decoded[i]) {if(decoded[i]=='@' && !xx_rt_strncmp(decoded+i,"@$&%",4) && decoded[i+4] && decoded[i+5] && decoded[i+4]>='0'&&decoded[i+4]<='9'&&decoded[i+5]>='0'&&decoded[i+5]<='9') {unsigned id=(decoded[i+4]-'0')*10+decoded[i+5]-'0';if(id<37&&mask[id]) {size_t n=xx_rt_strlen(mask[id]);out[k++]='$';xx_rt_memcpy(out+k,mask[id],n);k+=n;i+=6;continue;}}
        out[k++]=decoded[i]=='\\'?'/':decoded[i];++i;}
    out[k]=0;
ready:
    xx_mem_free(decoded);if(!sim_safe(out)){xx_mem_free(out);return NULL;}return out;
}
static bool sim_transfer(Abstractformat *f,pm_member *m,xx_io_device *dest,xx_pd_struct *pd) {
    uint8_t buffer[16384];int64_t at=0;int level=xx_pd_enter_level(pd,(uint64_t)m->size,"Smart Install Maker");bool ok=true;
    while(at<m->size && ok) {size_t n=(uint64_t)(m->size-at)>sizeof(buffer)?sizeof(buffer):(size_t)(m->size-at),done=0;const uint8_t *data=m->memory?m->memory+(size_t)at:buffer;
        if(xx_pd_is_stopped(pd)||(!m->memory&&!pm_read(f,m->offset-f->base_address+at,buffer,n))){ok=false;break;}
        while(dest && done<n) {ssize_t got=xx_io_write(dest,data+done,n-done);if(got<=0||(size_t)got>n-done||xx_pd_is_stopped(pd)){ok=false;break;}done+=(size_t)got;}
        at+=(int64_t)n;xx_pd_set_current(pd,level,(uint64_t)at);if(xx_pd_is_stopped(pd))ok=false;
    }
    if(level>=0) {xx_pd_leave_level(pd,level); } return ok&&!xx_pd_is_stopped(pd);
}
static bool sim_add(Abstractformat *f,pm_stream *s,char *name,int64_t at,const uint8_t *memory,size_t size,uint16_t method,sim_budget *b) {
    pm_member *m;size_t i,old=s->capacity;sim_context *ctx;
    if(!name||size>b->max||size>SIM_CAP||s->count>=65536 || !sim_take(b,sizeof(sim_context)))goto bad;
    for(i=0;i<s->count;++i)if(sim_equal(s->items[i].display_name,name))goto bad;
    if(s->count==s->capacity) {size_t cap=s->capacity?s->capacity*2:8;if(!sim_take(b,cap*sizeof(pm_member)))goto bad;if(!pm_add(f,s,"sim",0,0))goto bad;b->used-=old*sizeof(pm_member);}
    else if(!pm_add(f,s,"sim",0,0))goto bad;
    m=&s->items[s->count-1];m->display_name=name;name=NULL;m->offset=at;m->size=(int64_t)size;m->packed_size=memory?-1:(int64_t)size;m->compression_method=method;m->read_all=sim_transfer;
    ctx=(sim_context *)xx_mem_alloc(sizeof(*ctx));if(!ctx)goto bad;m->context=ctx;m->free_context=xx_mem_free;ctx->retained=0;
    if(memory && size) {if(!sim_take(b,size))return false;m->memory=(uint8_t *)xx_mem_alloc(size);if(!m->memory)return false;xx_rt_memcpy(m->memory,memory,size);}
    return true;
bad:xx_mem_free(name);return false;
}
static uint32_t sim_checksum(const uint8_t *p,size_t n) {
    uint32_t sum=0;while(n>=4){sum^=xx_data_get_u32(p, 4, 0, false);p+=4;n-=4;}if(n==3)sum^=(uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2];else if(n==2)sum^=(uint32_t)p[0]<<8|p[1];else if(n)sum^=p[0];return sum;
}
static bool sim_cab(Abstractformat *f,pm_stream *s,const uint8_t *raw,size_t n,const char *prefix,sim_string *names,size_t name_count,unsigned cp,sim_budget *b) {
    uint16_t nf,nfiles,flags;uint8_t fr=0,dr=0;size_t at,files_at,i,j;sim_folder *folders=NULL;bool ok=false;
    if(n<32||xx_data_get_u32(raw, 4, 0, false)||xx_data_get_u32(raw+8, 4, 0, false)||xx_data_get_u32(raw+16, 4, 0, false)||xx_data_get_u32(raw+4, 4, 0, false)!=(uint64_t)n+4||raw[20]!=3||raw[21]!=1)return false;
    nf=xx_data_get_u16(raw+22, 2, 0, false);nfiles=xx_data_get_u16(raw+24, 2, 0, false);flags=xx_data_get_u16(raw+26, 2, 0, false);files_at=xx_data_get_u32(raw+12, 4, 0, false);
    /* SIM writes a valid header-only CAB when no runtime files are bundled. */
    if(!nf&&!nfiles) return !flags&&n==32U&&files_at==36U;
    if(!nf||nf>4096||!nfiles||(flags&3)||flags>4||files_at<4||files_at-4>=n)return false;files_at-=4;at=32;
    if(flags&4) {size_t reserve;if(!sim_range(n,at,4))return false;reserve=xx_data_get_u16(raw+at, 2, 0, false);fr=raw[at+2];dr=raw[at+3];at+=4;if(!sim_range(n,at,reserve))return false;at+=reserve;}
    if(!sim_take(b,nf*sizeof(*folders))) {return false; } folders=(sim_folder *)xx_mem_calloc(nf,sizeof(*folders));if(!folders)return false;
    for(i=0;i<nf;++i) {if(!sim_range(n,at,8+fr))goto done;folders[i].at=xx_data_get_u32(raw+at, 4, 0, false);folders[i].blocks=xx_data_get_u16(raw+at+4, 2, 0, false);folders[i].type=xx_data_get_u16(raw+at+6, 2, 0, false);if(folders[i].at<4||!folders[i].blocks||(folders[i].type&15)>3)goto done;folders[i].at-=4;at+=8+fr;}
    if(at>files_at)goto done;
    /* CAB data cannot alias the cabinet header, folder table or file table. */
    {sim_reader files={raw,n,files_at};for(i=0;i<nfiles;++i) {sim_string name;if(sim_stop(b)||!sim_range(n,files.at,16)||xx_data_get_u16(raw+files.at+8, 2, 0, false)>=nf)goto done;files.at+=16;if(!sim_string_read(&files,&name))goto done;}
    for(i=0;i<nf;++i)if(folders[i].at<files.at||folders[i].at>=n)goto done;}
    for(i=0;i<nf;++i) {
        sim_folder *folder=folders+i;size_t total=0,pos=folder->at,arrays=folder->blocks*(sizeof(uint8_t *)+2*sizeof(size_t)),workspace=0;uint8_t *decoded=NULL;const uint8_t **blocks=NULL;size_t *packed=NULL,*plain=NULL;bool folder_ok=false;uint16_t method=folder->type&15;unsigned bits=(folder->type>>8)&31;
        if(!sim_take(b,arrays)) {goto done; } blocks=(const uint8_t **)xx_mem_calloc(folder->blocks,sizeof(*blocks));packed=(size_t *)xx_mem_calloc(folder->blocks,sizeof(*packed));plain=(size_t *)xx_mem_calloc(folder->blocks,sizeof(*plain));if(!blocks||!packed||!plain)goto folder_done;
        for(j=0;j<folder->blocks;++j) {uint32_t checksum;uint16_t z,y;if(sim_stop(b)||!sim_range(n,pos,8+dr))goto folder_done;checksum=xx_data_get_u32(raw+pos, 4, 0, false);z=xx_data_get_u16(raw+pos+4, 2, 0, false);y=xx_data_get_u16(raw+pos+6, 2, 0, false);pos+=8+dr;
            if(!z||!y||y>32768||!sim_range(n,pos,z)||total>SIM_CAP-y)goto folder_done;
            if(checksum && checksum!=(sim_checksum(raw+pos,z)^((uint32_t)y<<16|z)))goto folder_done;
            blocks[j]=raw+pos;packed[j]=z;plain[j]=y;total+=y;pos+=z;}
        for(j=0;j<nf;++j)if(j!=i&&folders[j].at>=folder->at&&folders[j].at<pos)goto folder_done;
        if(method==3) {if(bits<15||bits>21)goto folder_done;workspace=((size_t)1<<bits)+65536;}else if(method==2) {if(bits<10||bits>21)goto folder_done;workspace=((size_t)1<<bits)+65536;}else if(method==1)workspace=131072;
        if(!sim_take(b,total+workspace)) {goto folder_done; } decoded=(uint8_t *)xx_mem_alloc(total);if(!decoded)goto folder_done;
        if(method<=1) {size_t offset=0;for(j=0;j<folder->blocks;++j) {size_t wrote=0;if(sim_stop(b))goto folder_done;
            if(!method) {if(packed[j]!=plain[j])goto folder_done;xx_rt_memcpy(decoded+offset,blocks[j],plain[j]);}
            else if(packed[j]<2||blocks[j][0]!='C'||blocks[j][1]!='K'||!xx_deflate_decompress_memory_with_dictionary(blocks[j]+2,packed[j]-2,decoded+offset,plain[j],&wrote,decoded+(offset>32768?offset-32768:0),offset>32768?32768:offset,false)||wrote!=plain[j])goto folder_done;
            offset+=plain[j];}}
        else {size_t wrote=0;bool success=method==3?xx_lzx_cab_decode(blocks,packed,plain,folder->blocks,bits,decoded,total,&wrote):xx_quantum_cab_decode(blocks,packed,plain,folder->blocks,bits,decoded,total,&wrote);if(!success||wrote!=total||sim_stop(b))goto folder_done;}
        {sim_reader files={raw,n,files_at};for(j=0;j<nfiles;++j) {size_t off,z;uint16_t fi,attrs;sim_string name;char *path;uint32_t numeric;if(!sim_range(n,files.at,16))goto folder_done;z=xx_data_get_u32(raw+files.at, 4, 0, false);off=xx_data_get_u32(raw+files.at+4, 4, 0, false);fi=xx_data_get_u16(raw+files.at+8, 2, 0, false);attrs=xx_data_get_u16(raw+files.at+14, 2, 0, false);files.at+=16;if(fi>=nf||!sim_string_read(&files,&name))goto folder_done;if(fi!=i)continue;if(off>total||z>total-off)goto folder_done;
            if(names && sim_number(name,&numeric)&&numeric<name_count){name=names[numeric];attrs&=(uint16_t)0xFF7FU;}path=sim_path(name,attrs&128?65001:cp,prefix,!names,b);if(!path||!sim_add(f,s,path,-1,decoded+off,z,method,b))goto folder_done;}}
        folder_ok=true;
folder_done:
        xx_mem_free(decoded);xx_mem_free(blocks);xx_mem_free(packed);xx_mem_free(plain);if(folder_ok)b->used-=arrays+total+workspace;if(!folder_ok)goto done;
    }
    ok=true;
done:xx_mem_free(folders);if(ok)b->used-=nf*sizeof(*folders);return ok;
}

/* A SIM installer stores a cabinet set as fixed-size chunks without each
 * cabinet's MSCF magic. Join the CAB directory and split CFDATA fragments in
 * RAM, then use the same bounded codecs and member checks as a single cabinet.
 * This preserves LZX history across the cabinet boundary. Continuation file
 * records are checked against the earlier declaration, never emitted twice. */
typedef struct sim_cab_part {const uint8_t *p;size_t n,folders,files;uint16_t nf,nfiles,flags,index,set;bool from_previous;} sim_cab_part;
typedef struct sim_cab_file {uint8_t record[16];uint16_t folder;bool seen;} sim_cab_file;
static void sim_put16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void sim_put32(uint8_t *p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static bool sim_cab_set(Abstractformat *f,pm_stream *s,const uint8_t *input,size_t n,
                        uint32_t chunks,uint32_t chunk_size,uint32_t rest,
                        sim_string *names,size_t name_count,unsigned cp,sim_budget *b) {
    sim_cab_part parts[64];sim_cab_file *files=NULL;uint8_t *merged=NULL;
    size_t part_count=(size_t)chunks+(rest!=0U),offset=0U,total_folders=0U,unique=0U;
    size_t k,i,j,folder_count=0U,at,capacity,output,charged=0U,pending=0U,pending_at=0U;
    uint16_t folder_types[4096],folder_blocks[4096];bool ok=false;
    if(part_count<2U||part_count>64U||!chunks||!chunk_size||!name_count||name_count>65535U||n>SIM_CAP-65536U) return false;
    xx_mem_zero(parts,sizeof(parts));xx_mem_zero(folder_types,sizeof(folder_types));xx_mem_zero(folder_blocks,sizeof(folder_blocks));
    for(k=0U;k<part_count;++k) {
        sim_cab_part *part=parts+k;sim_reader rd;size_t z=k<chunks?chunk_size:rest;
        if(sim_stop(b)||!sim_range(n,offset,z)||z<32U)goto done;
        part->p=input+offset;part->n=z;offset+=z;
        if(xx_data_get_u32(part->p,4,0,false)||xx_data_get_u32(part->p+4,4,0,false)!=(uint64_t)z+4U||
           xx_data_get_u32(part->p+8,4,0,false)||xx_data_get_u32(part->p+16,4,0,false)||part->p[20]!=3U||part->p[21]!=1U)goto done;
        part->nf=xx_data_get_u16(part->p+22,2,0,false);part->nfiles=xx_data_get_u16(part->p+24,2,0,false);
        part->flags=xx_data_get_u16(part->p+26,2,0,false);part->set=xx_data_get_u16(part->p+28,2,0,false);part->index=xx_data_get_u16(part->p+30,2,0,false);
        if(!part->nf||part->nf>4096U||!part->nfiles||part->flags!=(uint16_t)((k?1U:0U)|(k+1U<part_count?2U:0U))||
           part->index!=k||(k&&part->set!=parts[0].set))goto done;
        at=xx_data_get_u32(part->p+12,4,0,false);if(at<4U||at-4U>=z)goto done;part->files=at-4U;
        rd.p=part->p;rd.n=z;rd.at=32U;
        for(i=0U;i<(size_t)((k?2U:0U)+(k+1U<part_count?2U:0U));++i){sim_string link;if(!sim_string_read(&rd,&link))goto done;}
        part->folders=rd.at;if(!sim_range(z,rd.at,(size_t)part->nf*8U)||rd.at+(size_t)part->nf*8U>part->files)goto done;
        rd.at=part->files;
        for(i=0U;i<part->nfiles;++i){sim_string name;uint16_t fi;
            if(!sim_range(z,rd.at,16U))goto done;fi=xx_data_get_u16(part->p+rd.at+8U,2,0,false);rd.at+=16U;
            if(!sim_string_read(&rd,&name))goto done;
            if(fi==0xfffdU||fi==0xffffU)part->from_previous=true;
            else if(fi!=0xfffeU&&fi>=part->nf)goto done;
        }
        if((k==0U&&part->from_previous)||part->nf>4096U-total_folders)goto done;
        total_folders+=part->nf-(part->from_previous?1U:0U);
        for(i=0U;i<part->nf;++i){size_t data=xx_data_get_u32(part->p+part->folders+i*8U,4,0,false);if(data<4U||data-4U<rd.at||data-4U>=z)goto done;}
    }
    if(offset!=n||!total_folders||total_folders>4096U)goto done;
    capacity=n+65536U;
    if(!sim_take(b,name_count*sizeof(*files)+capacity))goto done;charged=name_count*sizeof(*files)+capacity;
    files=(sim_cab_file *)xx_mem_calloc(name_count,sizeof(*files));merged=(uint8_t *)xx_mem_alloc(capacity);if(!files||!merged)goto done;
    at=32U+total_folders*8U;output=at;
    /* Reserve the unique numeric file table before appending compressed data. */
    for(i=0U;i<name_count;++i){char numeric[16];int length=xx_rt_snprintf(numeric,sizeof(numeric),"%u",(unsigned)i);if(length<1||!sim_range(capacity,output,17U+(size_t)length))goto done;output+=17U+(size_t)length;}
    xx_mem_zero(merged,output);sim_put32(merged+12U,(uint32_t)(at+4U));merged[20]=3U;merged[21]=1U;
    sim_put16(merged+22U,(uint16_t)total_folders);sim_put16(merged+24U,(uint16_t)name_count);
    for(k=0U;k<part_count;++k){sim_cab_part *part=parts+k;size_t first=folder_count-(part->from_previous?1U:0U);sim_reader rd={part->p,part->n,part->files};
        for(i=0U;i<part->nfiles;++i){sim_string name;uint32_t numeric;uint16_t fi,gfi;const uint8_t *record=part->p+rd.at;
            fi=xx_data_get_u16(record+8U,2,0,false);rd.at+=16U;if(!sim_string_read(&rd,&name)||!sim_number(name,&numeric)||numeric>=name_count)goto done;
            if(fi==0xfffdU)gfi=(uint16_t)first;
            else if(fi==0xfffeU)gfi=(uint16_t)(first+part->nf-1U);
            else if(fi==0xffffU){if(part->nf!=1U)goto done;gfi=(uint16_t)first;}
            else gfi=(uint16_t)(first+fi);
            if(gfi>=total_folders)goto done;
            if(files[numeric].seen){if(fi!=0xfffdU&&fi!=0xffffU)goto done;if(files[numeric].folder!=gfi||xx_rt_memcmp(files[numeric].record,record,8U)||xx_rt_memcmp(files[numeric].record+10U,record+10U,6U))goto done;}
            else {if(fi==0xfffdU||fi==0xffffU)goto done;files[numeric].seen=true;files[numeric].folder=gfi;xx_rt_memcpy(files[numeric].record,record,16U);++unique;}
        }
        for(i=0U;i<part->nf;++i){const uint8_t *folder=part->p+part->folders+i*8U;size_t global=first+i,pos=xx_data_get_u32(folder,4,0,false)-4U;uint16_t blocks=xx_data_get_u16(folder+4U,2,0,false),type=xx_data_get_u16(folder+6U,2,0,false);
            if(!blocks||(type&15U)>3U||(i==0U&&part->from_previous&&folder_types[global]!=type))goto done;
            if(!(i==0U&&part->from_previous)){if(pending)goto done;folder_types[global]=type;sim_put32(merged+32U+global*8U,(uint32_t)(output+4U));sim_put16(merged+38U+global*8U,type);}
            for(j=0U;j<blocks;++j){uint32_t checksum;uint16_t packed,plain;
                if(sim_stop(b)||!sim_range(part->n,pos,8U))goto done;
                checksum=xx_data_get_u32(part->p+pos,4,0,false);packed=xx_data_get_u16(part->p+pos+4U,2,0,false);plain=xx_data_get_u16(part->p+pos+6U,2,0,false);pos+=8U;
                if(!packed||plain>32768U||!sim_range(part->n,pos,packed)||(!plain&&(j+1U!=blocks||i+1U!=part->nf||k+1U==part_count)))goto done;
                if(checksum&&checksum!=(sim_checksum(part->p+pos,packed)^((uint32_t)plain<<16U|packed)))goto done;
                if(!pending){pending_at=output;if(!sim_range(capacity,output,8U))goto done;output+=8U;}
                if(packed>65535U-pending||!sim_range(capacity,output,packed))goto done;
                xx_rt_memcpy(merged+output,part->p+pos,packed);output+=packed;pending+=packed;pos+=packed;
                if(plain){if(folder_blocks[global]==65535U)goto done;++folder_blocks[global];sim_put16(merged+pending_at+4U,(uint16_t)pending);sim_put16(merged+pending_at+6U,plain);sim_put32(merged+pending_at,sim_checksum(merged+pending_at+8U,pending)^((uint32_t)plain<<16U|(uint32_t)pending));pending=0U;}
            }
            if(pos>part->n || (i+1U<part->nf && pos>xx_data_get_u32(part->p+part->folders+(i+1U)*8U,4,0,false)-4U))goto done;
        }
        folder_count=first+part->nf;
    }
    if(pending||unique!=name_count||folder_count!=total_folders)goto done;
    for(i=0U;i<name_count;++i){char numeric[16];int length=xx_rt_snprintf(numeric,sizeof(numeric),"%u",(unsigned)i);if(!files[i].seen)goto done;xx_rt_memcpy(merged+at,files[i].record,16U);sim_put16(merged+at+8U,files[i].folder);at+=16U;xx_rt_memcpy(merged+at,numeric,(size_t)length+1U);at+=(size_t)length+1U;}
    for(i=0U;i<total_folders;++i)sim_put16(merged+36U+i*8U,folder_blocks[i]);
    sim_put32(merged+4U,(uint32_t)output+4U);
    ok=sim_cab(f,s,merged,output,"content",names,name_count,cp,b);
done:xx_mem_free(files);xx_mem_free(merged);if(charged)b->used-=charged;return ok;
}
static uint64_t sim_option_bytes(const xx_list_s *o) {
    uint64_t n=0;size_t i;if(!o)return 0;if(o->count>1024)return UINT64_MAX;
    for(i=0;i<o->count;++i) {const xx_meta *m=(const xx_meta *)xx_list_at(o,i);uint64_t z=sizeof(*m);if(!m)return UINT64_MAX;
        if(m->var.type==XX_VAR_TYPE_STRING||m->var.type==XX_VAR_TYPE_STRING_VIEW){if(m->var.val.str.len>65536)return UINT64_MAX;z+=m->var.val.str.len+1;}
        else if(m->var.type==XX_VAR_TYPE_WSTRING||m->var.type==XX_VAR_TYPE_WSTRING_VIEW){if(m->var.val.wstr.len>32768)return UINT64_MAX;z+=(m->var.val.wstr.len+1)*sizeof(wchar_t);}
        else if(m->var.type==XX_VAR_TYPE_BYTES||m->var.type==XX_VAR_TYPE_BYTES_VIEW){if(m->var.val.bytes.size>65536)return UINT64_MAX;z+=m->var.val.bytes.size;}
        if(n>UINT64_MAX-z) {return UINT64_MAX; } n+=z;}
    return n;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    static const unsigned widths[]={4,7,3,2,8,3,2,6,6,6,2,2,4},indices[]={98,50,96,31,54,67,93,40,25,24,45,20,26};
    xx_smart_install_maker *format=(xx_smart_install_maker *)f;const xx_var *v;int64_t length=pm_available(f);sim_budget b={SIM_FIXED,SIM_BUDGET,UINT64_MAX,pd};uint8_t *image=NULL;sim_string header[255],*names=NULL,t;uint64_t so,ro,co,rl;size_t end,i,j,k,at,name_count=0;uint32_t counts[13],chunks,chunk_size,rest,lcid=1033,messages;sim_reader reader;unsigned cp=1252;bool ok=false;
    v=xx_format_resolve_extra_parameter(f,format->parse_options,XX_META_ID_OPT_MEMORY_LIMIT);if(v&&xx_var_get_u64(v)<b.limit)b.limit=xx_var_get_u64(v);v=xx_format_resolve_extra_parameter(f,format->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE);if(v)b.max=xx_var_get_u64(v);
    {uint64_t opts=sim_option_bytes(format->parse_options),defaults=sim_option_bytes(&f->list_extra_parameters);if(b.used>b.limit||opts>b.limit-b.used||defaults>b.limit-b.used-opts)return false;b.used+=opts+defaults;}
    if(length<4096+36||(uint64_t)length>SIM_CAP||!sim_take(&b,(uint64_t)length))return false;
    image=(uint8_t *)xx_mem_alloc((size_t)length);if(!image||!sim_input(f,image,(size_t)length,pd))goto done;end=(size_t)length-36;
    so=xx_data_get_u64(image+end, 8, 0, false);rl=xx_data_get_u64(image+end+8, 8, 0, false);ro=xx_data_get_u64(image+end+16, 8, 0, false);co=xx_data_get_u64(image+end+24, 8, 0, false);
    if(image[end+35]!=0xf1||image[end+32]>1||image[end+34]<119||so<4096||so>=ro||ro>co||co>end||rl>co-ro)goto done;
    if(!sim_range(end,(size_t)so,21)||xx_rt_memcmp(image+so,"Smart Install Maker v.",21)) {
        size_t found=0;for(i=4096;i+21<=end;++i) {if(sim_stop(&b))goto done;if(!xx_rt_memcmp(image+i,"Smart Install Maker v.",21))found=i;}
        if(!found) {goto done; } if(found>=so){uint64_t delta=found-so;if(delta>end-co)goto done;so+=delta;ro+=delta;co+=delta;}else{uint64_t delta=so-found;if(delta>ro||delta>co)goto done;so-=delta;ro-=delta;co-=delta;}
        if(so>=ro||ro>co||co>end||rl>co-ro)goto done;
    }
    reader.p=image+(size_t)so;reader.n=(size_t)(ro-so);reader.at=0;
    for(i=0;i<image[end+34];++i)if(!sim_string_read(&reader,header+i))goto done;
    for(i=0;i<13;++i)if(!sim_number(header[indices[i]],counts+i)||counts[i]>65536)goto done;
    name_count=counts[5];if(name_count&&!sim_take(&b,name_count*sizeof(*names)))goto done;names=name_count?(sim_string *)xx_mem_calloc(name_count,sizeof(*names)):NULL;if(name_count&&!names)goto done;
    for(i=0;i<13;++i)for(j=0;j<counts[i];++j)for(k=0;k<widths[i];++k) {if(sim_stop(&b)||!sim_string_read(&reader,&t))goto done;if(i==5&&k==1)names[j]=t;if(i==12&&j==0&&k==2&&!sim_number(t,&lcid))goto done;}
    if(!sim_string_read(&reader,&t)||!sim_number(header[57],&messages)||messages>65536||(uint64_t)messages*counts[12]>65536)goto done;
    for(i=0;i<(size_t)messages*counts[12];++i)if(!sim_string_read(&reader,&t))goto done;
    if(reader.at!=reader.n || !sim_number(header[117],&chunks)||!sim_number(header[95],&chunk_size)||!sim_number(header[118],&rest)) {goto done; } cp=sim_codepage(lcid);
    {sim_string descriptor={image+(size_t)so,(size_t)(ro-so)};char *name=xx_str_dup("setup/strings.bin");if(!sim_take(&b,18)||!sim_add(f,s,name,f->base_address+(int64_t)so,NULL,descriptor.n,0,&b))goto done;}
    if(image[end+32]) {if(!sim_cab(f,s,image+(size_t)ro,(size_t)rl,"runtime",NULL,0,cp,&b))goto done;}
    else {sim_reader runtime={image+(size_t)ro,(size_t)rl,0};for(i=0;i<image[end+33];++i) {sim_string name,z;uint32_t size;char *path;if(!sim_string_read(&runtime,&name)||!sim_string_read(&runtime,&z)||!sim_number(z,&size)||!sim_range(runtime.n,runtime.at,size))goto done;path=sim_path(name,cp,"runtime",true,&b);if(!path||!sim_add(f,s,path,f->base_address+(int64_t)ro+(int64_t)runtime.at,NULL,size,0,&b))goto done;runtime.at+=size;}if(runtime.at!=runtime.n)goto done;}
    if(rl!=co-ro)goto done;
    if(!chunks&&!rest) {at=(size_t)co;for(i=0;i<name_count;++i) {uint32_t size;char *path;if(!sim_range(end,at,24))goto done;size=xx_data_get_u32(image+at+4, 4, 0, false);at+=24;if(!sim_range(end,at,size))goto done;path=sim_path(names[i],cp,"data",false,&b);if(!path||!sim_add(f,s,path,f->base_address+(int64_t)at,NULL,size,0,&b))goto done;at+=size;}if(at!=end)goto done;}
    else {uint64_t span=(uint64_t)chunks*chunk_size+rest;if(span!=end-co||!span)goto done;
        if(chunks>1U||(chunks&&rest)){if(!sim_cab_set(f,s,image+(size_t)co,(size_t)span,chunks,chunk_size,rest,names,name_count,cp,&b))goto done;}
        else if(!sim_cab(f,s,image+(size_t)co,(size_t)span,"content",names,name_count,cp,&b))goto done;}
    b.used-=(uint64_t)length+name_count*sizeof(*names);for(i=0;i<s->count;++i)((sim_context *)s->items[i].context)->retained=b.used;s->size=length;ok=true;
done:xx_mem_free(names);xx_mem_free(image);if(!ok&&!xx_pd_is_stopped(pd))xx_pd_set_error(pd,XXFC_ERR_GENERIC,"Unsupported, malformed or over-budget Smart Install Maker package");return ok;
}
static xx_archive_record_state *sim_create_records(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    xx_smart_install_maker *format=(xx_smart_install_maker *)f;const xx_list_s *prior=format->parse_options;xx_archive_record_state *state=NULL;pm_stream *s;size_t i;
    uint64_t limit=SIM_BUDGET,bytes=sim_option_bytes(options);const xx_var *v=xx_format_resolve_extra_parameter(f,options,XX_META_ID_OPT_MEMORY_LIMIT);if(v&&xx_var_get_u64(v)<limit)limit=xx_var_get_u64(v);if(bytes>limit||xx_pd_is_stopped(pd))return NULL;
    state=(xx_archive_record_state *)xx_mem_alloc(sizeof(*state));if(!state)return NULL;xx_archive_record_state_init(state,f);
    for(i=0;options&&i<options->count;++i) {const xx_meta *item=(const xx_meta *)xx_list_at(options,i);xx_meta copy;bool copied=false;if(!item)goto fail;xx_meta_init(&copy,item->meta_id);
        if(item->var.type==XX_VAR_TYPE_STRING||item->var.type==XX_VAR_TYPE_STRING_VIEW) {size_t n=item->var.val.str.len;char *text=(char *)xx_mem_alloc(n+1);if(text&&(!n||item->var.val.str.ptr)){if(n)xx_rt_memcpy(text,item->var.val.str.ptr,n);text[n]=0;copied=xx_rt_strlen(text)==n&&xx_var_set_str_take(&copy.var,text,n);}if(!copied)xx_mem_free(text);}
        else if(item->var.type==XX_VAR_TYPE_WSTRING||item->var.type==XX_VAR_TYPE_WSTRING_VIEW) {size_t n=item->var.val.wstr.len,j;wchar_t *text=(wchar_t *)xx_mem_alloc((n+1)*sizeof(*text));if(text&&(!n||item->var.val.wstr.ptr)){if(n)xx_rt_memcpy(text,item->var.val.wstr.ptr,n*sizeof(*text));text[n]=0;for(j=0;j<n&&text[j];++j){}copied=j==n&&xx_var_set_wstr_take(&copy.var,text,n);}if(!copied)xx_mem_free(text);}
        else if(item->var.type==XX_VAR_TYPE_BYTES_VIEW)copied=xx_var_set_bytes(&copy.var,item->var.val.bytes.data,item->var.val.bytes.size);else copied=xx_var_copy(&copy.var,&item->var);
        if(copied&&item->meta_id==XX_META_ID_OPT_PASSWORD&&copy.var.type==XX_VAR_TYPE_STRING)copy.var.free_fn=sim_secret_free;
        if(!copied||!xx_list_append(&state->options,&copy)){xx_meta_cleanup(&copy);goto fail;}}
    format->parse_options=&state->options;s=pm_open(f,pd);format->parse_options=prior;if(!s)goto fail;state->internal_state=s;state->free_internal=pm_free_stream;state->total_records=(int64_t)s->count;state->has_record=s->count&&pm_record(state);if(s->count&&!state->has_record)goto fail;return state;
fail:format->parse_options=prior;xx_archive_record_state_free(state);return NULL;
}
static bool sim_unpack(Abstractformat *f,xx_archive_record_state *state,xx_pd_struct *pd) {
    const xx_var *v;uint64_t limit=SIM_BUDGET;pm_stream *s;sim_context *ctx;if(!state||!state->has_record||state->format!=f||xx_pd_is_stopped(pd))return false;s=(pm_stream *)state->internal_state;ctx=(sim_context *)s->items[s->index].context;
    v=xx_format_resolve_extra_parameter(f,&state->options,XX_META_ID_OPT_MEMORY_LIMIT);if(v&&xx_var_get_u64(v)<limit)limit=xx_var_get_u64(v);if(ctx->retained>limit)return false;
    return pm_unpack(f,state,pd);
}
bool xx_smart_install_maker_has_candidate_device(xx_io_device *device,int64_t base) {
    uint8_t footer[36];int64_t cursor,size;size_t got=0;uint64_t so,ro,co,rl,end;bool candidate=false;
    if(!device||base<0||(cursor=xx_io_tell(device))<0)return false;
    size=xx_io_size(device);if(size<base)goto done;
    size-=base;if(size<4096+36||(uint64_t)size>SIM_CAP)goto done;
    if(xx_io_seek64(device,base+size-36,SEEK_SET)!=0)goto done;
    while(got<sizeof(footer)) {ssize_t n=xx_io_read(device,footer+got,sizeof(footer)-got);if(n<=0||(size_t)n>sizeof(footer)-got)goto done;got+=(size_t)n;}
    end=(uint64_t)size-36;so=xx_data_get_u64(footer, 8, 0, false);rl=xx_data_get_u64(footer+8, 8, 0, false);ro=xx_data_get_u64(footer+16, 8, 0, false);co=xx_data_get_u64(footer+24, 8, 0, false);
    /* The full parser verifies the string signature and handles rebased stubs. */
    candidate=footer[35]==0xf1&&footer[32]<=1&&footer[34]>=119&&so>=4096&&so<ro&&ro<=co&&co<=end&&rl==co-ro;
done:if(xx_io_seek64(device,cursor,SEEK_SET)!=0)return false;return candidate;
}
xx_smart_install_maker *xx_smart_install_maker_create(xx_io_device *d,int64_t b) {xx_smart_install_maker *r;if(!d||b<0)return NULL;r=(xx_smart_install_maker *)xx_mem_calloc(1,sizeof(*r));if(r){pm_init(&r->format,d,b,SIM_TYPE,"exe");r->format.create_archive_records_reading=sim_create_records;r->format.unpack_current_archive_record=sim_unpack;}return r;}
void xx_smart_install_maker_free(xx_smart_install_maker *r) {if(r){xx_format_cleanup_extra_parameters(&r->format);xx_mem_free(r);}}
