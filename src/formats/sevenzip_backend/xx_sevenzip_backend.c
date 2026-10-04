/* SPDX-License-Identifier: MIT. Original, bounded pipe client.
 * The separately built 7-Zip engine is an independent executable, never linked.
 */
#include "xxfclib/formats/sevenzip_backend/xx_sevenzip_backend.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <wchar.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#endif
typedef struct sb_process {
#ifdef _WIN32
 HANDLE input,output,process,job,write_event;
#else
 int input,output;pid_t process;
#endif
 uint64_t start;unsigned timeout;xx_pd_struct *pd;xx_sevenzip_backend_status status;
} sb_process;
static uint64_t sb_clock(void){
#ifdef _WIN32
 return GetTickCount64();
#else
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000U+t.tv_nsec/1000000U;
#endif
}
static bool sb_live(sb_process *p){if(p->pd&&xx_pd_is_stopped(p->pd)){p->status=XX_SEVENZIP_BACKEND_CANCELLED;return false;}if(sb_clock()-p->start>=p->timeout){p->status=XX_SEVENZIP_BACKEND_TIMEOUT;return false;}return true;}
static bool sb_input(sb_process *p,void *data,size_t count){size_t done=0;
#ifdef _WIN32
 unsigned idle=0;
#endif
 while(done<count){if(!sb_live(p))return false;
#ifdef _WIN32
 DWORD available=0,got=0;if(!PeekNamedPipe(p->output,NULL,0,NULL,&available,NULL)){p->status=XX_SEVENZIP_BACKEND_IO;return false;}if(!available){if(WaitForSingleObject(p->process,0)==WAIT_OBJECT_0){p->status=XX_SEVENZIP_BACKEND_IO;return false;}
 /* Most pipe gaps are a ready helper processing our previous response.
  * Yield briefly before sleeping: a scheduler tick per protocol frame can
  * exhaust the deadline while reading an otherwise valid large member. */
 if(idle<128U){++idle;SwitchToThread();}else Sleep(1);continue;}
 idle=0;if(available>count-done)available=(DWORD)(count-done);if(!ReadFile(p->output,(uint8_t *)data+done,available,&got,NULL)||!got){p->status=XX_SEVENZIP_BACKEND_IO;return false;}done+=got;
#else
 struct pollfd poller={p->output,POLLIN,0};ssize_t got;int ready=poll(&poller,1,5);if(ready<0&&errno==EINTR)continue;if(ready<0){p->status=XX_SEVENZIP_BACKEND_IO;return false;}if(!ready)continue;got=read(p->output,(uint8_t *)data+done,count-done);if(got<0&&errno==EINTR)continue;if(got<=0){p->status=XX_SEVENZIP_BACKEND_IO;return false;}done+=(size_t)got;
#endif
 }return true;}
static bool sb_output(sb_process *p,const void *data,size_t count){size_t done=0;while(done<count){size_t want=count-done>65536U?65536U:count-done;if(!sb_live(p))return false;
#ifdef _WIN32
 OVERLAPPED pending;DWORD wrote=0,error;BOOL submitted;
 xx_rt_memset(&pending,0,sizeof(pending));pending.hEvent=p->write_event;
 if(!ResetEvent(p->write_event)){p->status=XX_SEVENZIP_BACKEND_IO;return false;}
 submitted=WriteFile(p->input,(const uint8_t *)data+done,(DWORD)want,&wrote,&pending);
 if(!submitted){error=GetLastError();if(error!=ERROR_IO_PENDING){p->status=XX_SEVENZIP_BACKEND_IO;return false;}
  for(;;){DWORD ready;
   if(!sb_live(p)){CancelIoEx(p->input,&pending);(void)GetOverlappedResult(p->input,&pending,&wrote,TRUE);return false;}
   ready=WaitForSingleObject(p->write_event,5);
   if(ready==WAIT_OBJECT_0)break;
   if(ready!=WAIT_TIMEOUT){p->status=XX_SEVENZIP_BACKEND_IO;CancelIoEx(p->input,&pending);(void)GetOverlappedResult(p->input,&pending,&wrote,TRUE);return false;}
  }
 }
 if(!GetOverlappedResult(p->input,&pending,&wrote,FALSE)||!wrote||wrote>want){p->status=XX_SEVENZIP_BACKEND_IO;return false;}done+=wrote;
#else
 ssize_t wrote=send(p->input,(const uint8_t *)data+done,want,MSG_NOSIGNAL);if(wrote<0&&errno==EINTR)continue;if(wrote<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){struct pollfd poller={p->input,POLLOUT,0};(void)poll(&poller,1,5);continue;}if(wrote<=0){p->status=XX_SEVENZIP_BACKEND_IO;return false;}done+=(size_t)wrote;
#endif
 }return true;}
static uint32_t sb_le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t sb_le64(const uint8_t *p){return sb_le32(p)|((uint64_t)sb_le32(p+4)<<32);}
static void sb_put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void sb_put64(uint8_t *p,uint64_t v){sb_put32(p,(uint32_t)v);sb_put32(p+4,(uint32_t)(v>>32));}
static void sb_close(sb_process *p){
#ifdef _WIN32
 if(p->input)CloseHandle(p->input);if(p->output)CloseHandle(p->output);if(p->write_event)CloseHandle(p->write_event);if(p->process){if(WaitForSingleObject(p->process,100)!=WAIT_OBJECT_0){TerminateProcess(p->process,2);WaitForSingleObject(p->process,1000);}CloseHandle(p->process);}if(p->job)CloseHandle(p->job);
#else
 if(p->input>=0)close(p->input);if(p->output>=0)close(p->output);if(p->process>0){int status;if(waitpid(p->process,&status,WNOHANG)==0){kill(p->process,SIGKILL);while(waitpid(p->process,&status,0)<0&&errno==EINTR){}}}
#endif
}
static bool sb_start(sb_process *p,const char *helper,uint64_t memory){
#ifdef _WIN32
 wchar_t path[32768],pipe_name[128],*converted=NULL,*command=NULL;HANDLE child_read=NULL,child_write=NULL;SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};STARTUPINFOEXW si;PROCESS_INFORMATION pi;SIZE_T attribute_size=0;HANDLE handles[2];bool ok=false,attributes_live=false;JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;static volatile LONG pipe_counter;
 xx_rt_memset(&si,0,sizeof(si));xx_rt_memset(&pi,0,sizeof(pi));si.StartupInfo.cb=sizeof(si);if(helper){converted=xx_str_utf8_to_unicode(helper);if(!converted)goto done;}else{DWORD n=GetModuleFileNameW(NULL,path,32768);if(!n||n>=32768U)goto done;while(n&&path[n-1]!=L'\\'&&path[n-1]!=L'/')--n;if(!n||n+24U>=32768U)goto done;xx_rt_memcpy(path+n,L"xfu_sevenzip_helper.exe",sizeof(L"xfu_sevenzip_helper.exe"));}
 {const wchar_t *use=converted?converted:path;size_t n;for(n=0;n<=32764U&&use[n];++n)if(use[n]==L'"')goto done;if(n>32764U)goto done;command=(wchar_t *)HeapAlloc(GetProcessHeap(),0,(n+3U)*sizeof(wchar_t));if(!command)goto done;command[0]=L'"';xx_rt_memcpy(command+1,use,n*sizeof(wchar_t));command[n+1]=L'"';command[n+2]=0;}
 /* The parent writes an overlapped local pipe, so a helper which stops
  * draining its input cannot prevent cancellation or exhaust the deadline. */
 {char ascii[128];size_t i;int n=xx_rt_snprintf(ascii,sizeof(ascii),"\\\\.\\pipe\\xfu-sevenzip-%lu-%llu-%ld",(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64(),(long)InterlockedIncrement(&pipe_counter));if(n<0||(size_t)n>=sizeof(ascii))goto done;for(i=0;i<=(size_t)n;++i)pipe_name[i]=(wchar_t)(unsigned char)ascii[i];}
 p->input=CreateNamedPipeW(pipe_name,PIPE_ACCESS_OUTBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,NULL);
 if(p->input==INVALID_HANDLE_VALUE){p->input=NULL;goto done;}
 p->write_event=CreateEventW(NULL,TRUE,FALSE,NULL);if(!p->write_event)goto done;
 child_read=CreateFileW(pipe_name,GENERIC_READ,0,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
 if(child_read==INVALID_HANDLE_VALUE){child_read=NULL;goto done;}
 {OVERLAPPED connection;DWORD error;xx_rt_memset(&connection,0,sizeof(connection));connection.hEvent=p->write_event;
  if(!ConnectNamedPipe(p->input,&connection)){error=GetLastError();if(error!=ERROR_PIPE_CONNECTED){if(error==ERROR_IO_PENDING){DWORD ignored;CancelIoEx(p->input,&connection);(void)GetOverlappedResult(p->input,&connection,&ignored,TRUE);}goto done;}}
 }
 if(!CreatePipe(&p->output,&child_write,&sa,65536)||!SetHandleInformation(p->output,HANDLE_FLAG_INHERIT,0))goto done;
 InitializeProcThreadAttributeList(NULL,1,0,&attribute_size);si.lpAttributeList=HeapAlloc(GetProcessHeap(),0,attribute_size);if(!si.lpAttributeList||!InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&attribute_size))goto done;attributes_live=true;handles[0]=child_read;handles[1]=child_write;if(!UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),NULL,NULL))goto done;
 si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdInput=child_read;si.StartupInfo.hStdOutput=child_write;si.StartupInfo.hStdError=child_write;
 p->job=CreateJobObjectW(NULL,NULL);if(!p->job)goto done;xx_rt_memset(&limits,0,sizeof(limits));limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;limits.ProcessMemoryLimit=(SIZE_T)(memory+32U*1024U*1024U);if(!SetInformationJobObject(p->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))goto done;
 if(!CreateProcessW(converted?converted:path,command,NULL,NULL,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,NULL,&si.StartupInfo,&pi))goto done;p->process=pi.hProcess;if(!AssignProcessToJobObject(p->job,pi.hProcess)){TerminateProcess(pi.hProcess,2);CloseHandle(pi.hThread);goto done;}if(ResumeThread(pi.hThread)==(DWORD)-1){CloseHandle(pi.hThread);goto done;}CloseHandle(pi.hThread);ok=true;
done:if(child_read)CloseHandle(child_read);if(child_write)CloseHandle(child_write);if(si.lpAttributeList){if(attributes_live)DeleteProcThreadAttributeList(si.lpAttributeList);HeapFree(GetProcessHeap(),0,si.lpAttributeList);}if(command)HeapFree(GetProcessHeap(),0,command);if(converted)xx_str_wfree(converted);return ok;
#else
 int in[2]={-1,-1},out[2]={-1,-1};char path[4096];pid_t pid;(void)memory;p->input=p->output=-1;if(!helper){ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);if(n<=0||n>=(ssize_t)sizeof(path)-24)return false;while(n&&path[n-1]!='/')--n;if(!n)return false;xx_rt_memcpy(path+n,"xfu_sevenzip_helper",sizeof("xfu_sevenzip_helper"));helper=path;}if(socketpair(AF_UNIX,SOCK_STREAM,0,in)||socketpair(AF_UNIX,SOCK_STREAM,0,out))goto failed;pid=fork();if(pid<0)goto failed;if(!pid){dup2(in[0],STDIN_FILENO);dup2(out[1],STDOUT_FILENO);dup2(out[1],STDERR_FILENO);close(in[0]);close(in[1]);close(out[0]);close(out[1]);execl(helper,helper,(char *)NULL);_exit(127);}close(in[0]);close(out[1]);p->input=in[1];p->output=out[0];p->process=pid;fcntl(p->input,F_SETFL,fcntl(p->input,F_GETFL)|O_NONBLOCK);return true;
failed:if(in[0]>=0)close(in[0]);if(in[1]>=0)close(in[1]);if(out[0]>=0)close(out[0]);if(out[1]>=0)close(out[1]);return false;
#endif
}

static bool sb_frame(sb_process *p,uint32_t type,const void *data,uint32_t count){
 uint8_t h[12];sb_put32(h,UINT32_C(0x3150375a));sb_put32(h+4,type);sb_put32(h+8,count);
 return sb_output(p,h,12)&&sb_output(p,data,count);
}
static bool sb_run(xx_io_device *source,int64_t base,int64_t length,const char *handler,uint32_t index,uint64_t expected,xx_io_device *dest,const xx_sevenzip_backend_options *opts,xx_sevenzip_backend_entry_fn callback,void *user){
 sb_process p;uint8_t h[48],buffer[65536],body[40];char name[65537];
 const char *password=opts&&opts->password?opts->password:"";
 const char *source_path=opts?opts->source_path:NULL,*source_name=NULL,*part;
 xx_io_device *volumes[64]; char *volume_names[64]; uint32_t volume_count=0;
 size_t sn=0;
 size_t hn=handler?xx_rt_strlen(handler):0,pn=xx_rt_strlen(password);uint64_t received=0,listed=0;
 uint64_t memory=opts&&opts->memory_limit?opts->memory_limit:UINT64_C(256)*1024*1024;
 uint64_t max=opts?opts->max_member_size:UINT64_MAX;int64_t total,saved=-1;bool ok=false;int decode_level=-1;
 /* Per-member TEST also uses READ: DATA is discarded in bounded RAM, and
  * its exact length/ceiling is checked without retesting unrelated members. */
 unsigned operation=callback?1U:2U;
 xx_rt_memset(volumes,0,sizeof(volumes));xx_rt_memset(volume_names,0,sizeof(volume_names));
 xx_rt_memset(&p,0,sizeof(p));p.status=XX_SEVENZIP_BACKEND_FORMAT;p.start=sb_clock();
 p.timeout=opts&&opts->timeout_ms?opts->timeout_ms:60000U;p.pd=opts?opts->pd:NULL;
#ifndef _WIN32
 p.input=p.output=-1;
#endif
 if(!source||dest==source||hn>64||pn>65536||(total=xx_io_size(source))<0||base<0||base>total)goto done;
 if(length<0)length=total-base;if(length>total-base)goto done;
 if(memory>UINT64_C(256)*1024*1024)memory=UINT64_C(256)*1024*1024;
 if(memory<262144||(expected!=UINT64_MAX&&expected>max)){p.status=XX_SEVENZIP_BACKEND_LIMIT;goto done;}
 if(source_path){source_name=source_path;for(part=source_path;*part;++part)if(*part=='/'||*part=='\\')source_name=part+1;sn=xx_rt_strlen(source_name);if(sn>65536)goto done;}
 if(!sb_live(&p))goto done;saved=xx_io_tell(source);
 /* Decoded DATA bytes measure progress for a single selected member.
  * Source offsets are not monotonic for solid archives or random readers;
  * unknown output sizes stay indeterminate until the caller completes TEST. */
 if(operation==2)decode_level=xx_pd_enter_level(p.pd,expected==UINT64_MAX?0:expected,"7-Zip decode");
 if(!sb_live(&p))goto done;
 if(!sb_start(&p,opts?opts->helper_path:NULL,memory)){p.status=XX_SEVENZIP_BACKEND_UNAVAILABLE;goto done;}
 sb_put32(h,operation);sb_put32(h+4,(sn?1U:0U)|(opts&&opts->start_only?2U:0U));sb_put64(h+8,(uint64_t)length);sb_put64(h+16,memory);
 /* Helper zero means unlimited, parent enforces the literal zero ceiling. */
 sb_put64(h+24,max?max:UINT64_MAX);sb_put32(h+32,index);sb_put32(h+36,(uint32_t)hn);sb_put32(h+40,(uint32_t)pn);
 {uint8_t f[12];sb_put32(f,UINT32_C(0x3150375a));sb_put32(f+4,1);sb_put32(f+8,44U+(uint32_t)hn+(uint32_t)pn+(sn?4U+(uint32_t)sn:0));
 if(!sb_output(&p,f,12)||!sb_output(&p,h,44)||!sb_output(&p,handler,hn)||!sb_output(&p,password,pn))goto done;
 if(sn){sb_put32(f,(uint32_t)sn);if(!sb_output(&p,f,4)||!sb_output(&p,source_name,sn))goto done;}}
 for(;;){uint32_t type,count;if(!sb_input(&p,h,12))goto done;
 if(sb_le32(h)!=UINT32_C(0x3150375a)){p.status=XX_SEVENZIP_BACKEND_FORMAT;goto done;}
 type=sb_le32(h+4);count=sb_le32(h+8);if(count>1048576){p.status=XX_SEVENZIP_BACKEND_FORMAT;goto done;}
 if(type==2){uint64_t at,extent;uint32_t want,id=0;size_t read=0;xx_io_device *input;
  if((count!=12&&count!=16)||!sb_input(&p,body,count))goto done;
  if(count==16)id=sb_le32(body);at=sb_le64(body+(count==16?4:0));want=sb_le32(body+(count==16?12:8));
  if(id>volume_count){p.status=XX_SEVENZIP_BACKEND_FORMAT;goto done;}
  input=id?volumes[id-1]:source;extent=id?(uint64_t)xx_io_size(input):(uint64_t)length;
  if(!want||want>65536||at>extent||want>extent-at){p.status=XX_SEVENZIP_BACKEND_FORMAT;goto done;}
  if(xx_io_seek64(input,(id?0:base)+(int64_t)at,SEEK_SET)!=0){p.status=XX_SEVENZIP_BACKEND_IO;goto done;}
  while(read<want){ssize_t n;if(!sb_live(&p))goto done;n=xx_io_read(input,buffer+read,want-read);if(n<=0||(size_t)n>want-read){p.status=XX_SEVENZIP_BACKEND_IO;goto done;}read+=(size_t)n;}
  sb_put32(h,UINT32_C(0x3150375a));sb_put32(h+4,3);sb_put32(h+8,8+want);sb_put32(body,0);sb_put32(body+4,want);
  if(!sb_output(&p,h,12)||!sb_output(&p,body,8)||!sb_output(&p,buffer,want))goto done;
 }else if(type==4){xx_sevenzip_backend_entry e;uint32_t flags,n;
  if(operation!=1||count<36||!sb_input(&p,body,36))goto done;
  e.index=sb_le32(body);flags=sb_le32(body+4);e.size=sb_le64(body+8);e.packed_size=sb_le64(body+16);e.mtime=(int64_t)sb_le64(body+24);n=sb_le32(body+32);
  if(!n||n>65536||count!=36+n||flags>7||++listed>1000000||(e.size!=UINT64_MAX&&e.size>max)){p.status=XX_SEVENZIP_BACKEND_LIMIT;goto done;}
  if(!sb_input(&p,name,n))goto done;name[n]=0;if(xx_rt_strlen(name)!=n){p.status=XX_SEVENZIP_BACKEND_FORMAT;goto done;}
  e.path=name;e.directory=(flags&1)!=0;e.encrypted=(flags&2)!=0;
  if(!callback(user,&e)){if(p.status==XX_SEVENZIP_BACKEND_FORMAT)p.status=XX_SEVENZIP_BACKEND_LIMIT;goto done;}
 }else if(type==5){size_t wrote=0;
  if(operation==1||count>65536||received>max||count>max-received||
    (expected!=UINT64_MAX&&(received>expected||count>expected-received))){p.status=XX_SEVENZIP_BACKEND_LIMIT;goto done;}
  if(!sb_input(&p,buffer,count))goto done;
  while(dest&&wrote<count){ssize_t n;if(!sb_live(&p))goto done;n=xx_io_write(dest,buffer+wrote,count-wrote);if(n<=0||(size_t)n>count-wrote){p.status=XX_SEVENZIP_BACKEND_IO;goto done;}wrote+=(size_t)n;}
  received+=count;xx_pd_set_current(p.pd,decode_level,received);
  if(!sb_live(&p))goto done;
 }else if(type==8){
  if(!count||count>64||!sb_input(&p,name,count))goto done;name[count]=0;
  if(xx_rt_strlen(name)!=count){p.status=XX_SEVENZIP_BACKEND_FORMAT;goto done;}
  if(opts&&opts->detected_handler&&opts->detected_handler_capacity>count)xx_rt_memcpy(opts->detected_handler,name,count+1);
 }else if(type==9){uint32_t id=UINT32_MAX,j;uint64_t extent=0;
  if(!count||count>1024||!sb_input(&p,name,count))goto done;name[count]=0;
  if(xx_rt_strlen(name)!=count||xx_rt_strchr(name,'/')||xx_rt_strchr(name,'\\')||xx_rt_strchr(name,':')||!xx_rt_strcmp(name,".")||!xx_rt_strcmp(name,"..")){p.status=XX_SEVENZIP_BACKEND_FORMAT;goto done;}
  if(source_name&&!xx_rt_strcmp(name,source_name)){id=0;extent=(uint64_t)length;}
  else if(source_path){
   for(j=0;j<volume_count;++j)if(!xx_rt_strcmp(volume_names[j],name)){id=j+1;extent=(uint64_t)xx_io_size(volumes[j]);break;}
   if(id==UINT32_MAX&&volume_count>=64){p.status=XX_SEVENZIP_BACKEND_LIMIT;goto done;}
   if(id==UINT32_MAX&&volume_count<64){size_t prefix=(size_t)(source_name-source_path);char *path;
    if(prefix>32768||prefix+count+1>32768){p.status=XX_SEVENZIP_BACKEND_LIMIT;goto done;}
    path=(char *)xx_mem_alloc(prefix+count+1);if(!path){p.status=XX_SEVENZIP_BACKEND_LIMIT;goto done;}
    xx_rt_memcpy(path,source_path,prefix);xx_rt_memcpy(path+prefix,name,count+1);
    volumes[volume_count]=xx_io_file_open(path,"rb");xx_mem_free(path);
    if(volumes[volume_count]){int64_t size=xx_io_size(volumes[volume_count]);
     if(size<0){xx_io_close(volumes[volume_count]);volumes[volume_count]=NULL;}
     else{volume_names[volume_count]=xx_str_dup(name);if(!volume_names[volume_count]){xx_io_close(volumes[volume_count]);volumes[volume_count]=NULL;p.status=XX_SEVENZIP_BACKEND_LIMIT;goto done;}
      id=++volume_count;extent=(uint64_t)size;}
    }
   }
  }
  sb_put32(body,id);sb_put64(body+4,extent);if(!sb_frame(&p,10,body,12))goto done;
 }else if(type==6){uint32_t code;uint32_t remain;
  if(count<4||!sb_input(&p,body,4))goto done;code=sb_le32(body);remain=count-4;
  while(remain){uint32_t chunk=remain>65536?65536:remain;if(!sb_input(&p,buffer,chunk))goto done;remain-=chunk;}
  p.status=code==0?XX_SEVENZIP_BACKEND_OK:code==2?XX_SEVENZIP_BACKEND_UNSUPPORTED:code==3?XX_SEVENZIP_BACKEND_PASSWORD:code==4?XX_SEVENZIP_BACKEND_LIMIT:code==5?XX_SEVENZIP_BACKEND_CANCELLED:code==6?XX_SEVENZIP_BACKEND_IO:XX_SEVENZIP_BACKEND_FORMAT;
  if(code==0&&operation==2&&expected!=UINT64_MAX&&received!=expected)p.status=XX_SEVENZIP_BACKEND_FORMAT;
  ok=p.status==XX_SEVENZIP_BACKEND_OK&&sb_live(&p);break;
 }else{p.status=XX_SEVENZIP_BACKEND_FORMAT;goto done;}
 }
done:sb_close(&p);while(volume_count){--volume_count;xx_io_close(volumes[volume_count]);xx_str_free(volume_names[volume_count]);}
 if(saved>=0&&xx_io_seek64(source,saved,SEEK_SET)!=0){p.status=XX_SEVENZIP_BACKEND_IO;ok=false;}
 if(decode_level>=0)xx_pd_leave_level(p.pd,decode_level);
 /* The final notification can request cancellation, too. Do not report a
  * successful decode after the observer has stopped the operation. */
 if(p.pd&&xx_pd_is_stopped(p.pd)){p.status=XX_SEVENZIP_BACKEND_CANCELLED;ok=false;}
 if(opts&&opts->status)*opts->status=p.status;return ok;
}
bool xx_sevenzip_backend_list(xx_io_device *s,int64_t b,int64_t n,const char *h,const xx_sevenzip_backend_options *o,xx_sevenzip_backend_entry_fn cb,void *u){if(!cb)return false;return sb_run(s,b,n,h,0,UINT64_MAX,NULL,o,cb,u);}
bool xx_sevenzip_backend_read(xx_io_device *s,int64_t b,int64_t n,const char *h,uint32_t i,uint64_t size,xx_io_device *out,const xx_sevenzip_backend_options *o){return sb_run(s,b,n,h,i,size,out,o,NULL,NULL);}
