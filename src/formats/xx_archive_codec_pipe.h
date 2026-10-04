/* SPDX-License-Identifier: MIT. RAM-only bounded decoder pipe client. */
#ifndef XX_ARCHIVE_CODEC_PIPE_H
#define XX_ARCHIVE_CODEC_PIPE_H
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/rt/xx_rt.h"
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
#include <sys/resource.h>
#include <fcntl.h>
#endif
typedef enum af_status { AF_OK,AF_UNAVAILABLE,AF_CANCELLED,AF_TIMEOUT,AF_IO,AF_FORMAT,AF_LIMIT } af_status;
typedef struct af_process {
#ifdef _WIN32
 HANDLE input,output,process,job,write_event;
#else
 int input,output;pid_t process;
#endif
 uint64_t start;unsigned timeout;xx_pd_struct *pd;af_status status;
} af_process;
static uint64_t af_clock(void){
#ifdef _WIN32
 return GetTickCount64();
#else
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000U+t.tv_nsec/1000000U;
#endif
}
static bool af_live(af_process *p){if(p->pd&&xx_pd_is_stopped(p->pd)){p->status=AF_CANCELLED;return false;}if(af_clock()-p->start>=p->timeout){p->status=AF_TIMEOUT;return false;}return true;}
static bool af_input(af_process *p,void *data,size_t count){size_t done=0;while(done<count){if(!af_live(p))return false;
#ifdef _WIN32
 DWORD available=0,got=0;if(!PeekNamedPipe(p->output,NULL,0,NULL,&available,NULL)){p->status=AF_IO;return false;}if(!available){if(WaitForSingleObject(p->process,0)==WAIT_OBJECT_0){p->status=AF_IO;return false;}Sleep(5);continue;}if(available>count-done)available=(DWORD)(count-done);if(!ReadFile(p->output,(uint8_t *)data+done,available,&got,NULL)||!got){p->status=AF_IO;return false;}done+=got;
#else
 struct pollfd poller={p->output,POLLIN,0};ssize_t got;int ready=poll(&poller,1,5);if(ready<0&&errno==EINTR)continue;if(ready<0){p->status=AF_IO;return false;}if(!ready)continue;got=read(p->output,(uint8_t *)data+done,count-done);if(got<0&&errno==EINTR)continue;if(got<=0){p->status=AF_IO;return false;}done+=(size_t)got;
#endif
 }return true;}
static bool af_output(af_process *p,const void *data,size_t count){size_t done=0;while(done<count){size_t want=count-done>65536U?65536U:count-done;if(!af_live(p))return false;
#ifdef _WIN32
 OVERLAPPED pending;DWORD wrote=0,error;BOOL submitted;
 xx_rt_memset(&pending,0,sizeof(pending));pending.hEvent=p->write_event;
 if(!ResetEvent(p->write_event)){p->status=AF_IO;return false;}
 submitted=WriteFile(p->input,(const uint8_t *)data+done,(DWORD)want,&wrote,&pending);
 if(!submitted){error=GetLastError();if(error!=ERROR_IO_PENDING){p->status=AF_IO;return false;}
  for(;;){DWORD ready;
   if(!af_live(p)){CancelIoEx(p->input,&pending);(void)GetOverlappedResult(p->input,&pending,&wrote,TRUE);return false;}
   ready=WaitForSingleObject(p->write_event,5);
   if(ready==WAIT_OBJECT_0)break;
   if(ready!=WAIT_TIMEOUT){p->status=AF_IO;CancelIoEx(p->input,&pending);(void)GetOverlappedResult(p->input,&pending,&wrote,TRUE);return false;}
  }
 }
 if(!GetOverlappedResult(p->input,&pending,&wrote,FALSE)||!wrote||wrote>want){p->status=AF_IO;return false;}done+=wrote;
#else
 ssize_t wrote=send(p->input,(const uint8_t *)data+done,want,MSG_NOSIGNAL);if(wrote<0&&errno==EINTR)continue;if(wrote<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){struct pollfd poller={p->input,POLLOUT,0};(void)poll(&poller,1,5);continue;}if(wrote<=0){p->status=AF_IO;return false;}done+=(size_t)wrote;
#endif
 }return true;}
static uint32_t af_le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t af_le64(const uint8_t *p){return af_le32(p)|((uint64_t)af_le32(p+4)<<32);}
static void af_put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void af_put64(uint8_t *p,uint64_t v){af_put32(p,(uint32_t)v);af_put32(p+4,(uint32_t)(v>>32));}
static void af_close(af_process *p){
#ifdef _WIN32
 if(p->input)CloseHandle(p->input);if(p->output)CloseHandle(p->output);if(p->write_event)CloseHandle(p->write_event);if(p->process){if(WaitForSingleObject(p->process,100)!=WAIT_OBJECT_0){TerminateProcess(p->process,2);WaitForSingleObject(p->process,1000);}CloseHandle(p->process);}if(p->job)CloseHandle(p->job);
#else
 if(p->input>=0)close(p->input);if(p->output>=0)close(p->output);if(p->process>0){int status;if(waitpid(p->process,&status,WNOHANG)==0){kill(p->process,SIGKILL);while(waitpid(p->process,&status,0)<0&&errno==EINTR){}}}
#endif
}
static bool af_start(af_process *p,const char *helper,uint64_t memory){
#ifdef _WIN32
 wchar_t path[32768],pipe_name[160],*converted=NULL,*command=NULL;HANDLE child_read=NULL,child_write=NULL;SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};STARTUPINFOEXW si;PROCESS_INFORMATION pi;SIZE_T attribute_size=0;HANDLE handles[2];bool ok=false,attributes_live=false;JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;static volatile LONG pipe_counter;
 xx_rt_memset(&si,0,sizeof(si));xx_rt_memset(&pi,0,sizeof(pi));si.StartupInfo.cb=sizeof(si);if(helper){converted=xx_str_utf8_to_unicode(helper);if(!converted)goto done;}else{DWORD n=GetModuleFileNameW(NULL,path,32768);if(!n||n>=32768U)goto done;while(n&&path[n-1]!=L'\\'&&path[n-1]!=L'/')--n;if(!n||n+32U>=32768U)goto done;xx_rt_memcpy(path+n,L"xfu_archive_codec_helper.exe",sizeof(L"xfu_archive_codec_helper.exe"));}
 {const wchar_t *use=converted?converted:path;size_t n;for(n=0;n<=32764U&&use[n];++n)if(use[n]==L'"')goto done;if(n>32764U)goto done;command=(wchar_t *)HeapAlloc(GetProcessHeap(),0,(n+3U)*sizeof(wchar_t));if(!command)goto done;command[0]=L'"';xx_rt_memcpy(command+1,use,n*sizeof(wchar_t));command[n+1]=L'"';command[n+2]=0;}
 /* An overlapped local pipe keeps stalled helper input cancelable. Include
  * the thread ID because this private client is compiled by several readers. */
 {char ascii[160];size_t i;int n=xx_rt_snprintf(ascii,sizeof(ascii),"\\\\.\\pipe\\xfu-codec-%lu-%lu-%llu-%ld",(unsigned long)GetCurrentProcessId(),(unsigned long)GetCurrentThreadId(),(unsigned long long)GetTickCount64(),(long)InterlockedIncrement(&pipe_counter));if(n<0||(size_t)n>=sizeof(ascii))goto done;for(i=0;i<=(size_t)n;++i)pipe_name[i]=(wchar_t)(unsigned char)ascii[i];}
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
 int in[2]={-1,-1},out[2]={-1,-1};char path[4096];pid_t pid;p->input=p->output=-1;if(!helper){ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);if(n<=0||n>=(ssize_t)sizeof(path)-32)return false;while(n&&path[n-1]!='/')--n;if(!n)return false;xx_rt_memcpy(path+n,"xfu_archive_codec_helper",sizeof("xfu_archive_codec_helper"));helper=path;}if(socketpair(AF_UNIX,SOCK_STREAM,0,in)||socketpair(AF_UNIX,SOCK_STREAM,0,out))goto failed;pid=fork();if(pid<0)goto failed;if(!pid){struct rlimit limit;limit.rlim_cur=limit.rlim_max=(rlim_t)(memory+32U*1024U*1024U);if(setrlimit(RLIMIT_AS,&limit))_exit(126);dup2(in[0],STDIN_FILENO);dup2(out[1],STDOUT_FILENO);dup2(out[1],STDERR_FILENO);close(in[0]);close(in[1]);close(out[0]);close(out[1]);execl(helper,helper,(char *)NULL);_exit(127);}close(in[0]);close(out[1]);p->input=in[1];p->output=out[0];p->process=pid;fcntl(p->input,F_SETFL,fcntl(p->input,F_GETFL)|O_NONBLOCK);return true;
failed:if(in[0]>=0)close(in[0]);if(in[1]>=0)close(in[1]);if(out[0]>=0)close(out[0]);if(out[1]>=0)close(out[1]);return false;
#endif
}
static bool af_decode(ac_blob *b,unsigned kind,uint8_t *out,uint32_t size) {
 af_process p;uint8_t h[32];uint64_t received=0,worker;bool ok=false;
 xx_mem_zero(&p,sizeof(p));p.status=AF_FORMAT;p.start=af_clock();p.timeout=60000U;p.pd=b->pd;
#ifndef _WIN32
 p.input=p.output=-1;
#endif
 if(b->used>b->limit || (worker=b->limit-b->used)<65536U)return ac_error(b,"decoder workspace exceeds archive memory limit");
 if(worker>UINT64_C(1024)*1024U*1024U)worker=UINT64_C(1024)*1024U*1024U;
 if(!af_live(&p))goto done;
 if(!af_start(&p,NULL,worker)){p.status=AF_UNAVAILABLE;goto done;}
 xx_rt_memcpy(h,"AFC1",4);af_put32(h+4,kind);af_put64(h+8,b->n);af_put64(h+16,size);af_put64(h+24,worker);
 if(!af_output(&p,h,32))goto done;
 for(;;){unsigned type;if(!af_input(&p,h,4))goto done;type=af_le32(h);
  if(type==1U){uint64_t at;uint32_t n;if(!af_input(&p,h,12))goto done;at=af_le64(h);n=af_le32(h+8);if(!n||n>65536U||at>b->n||n>b->n-at)goto done;af_put32(h,n);if(!af_output(&p,h,4)||!af_output(&p,b->p+(uint32_t)at,n))goto done;
  }else if(type==3U){uint32_t n;if(!af_input(&p,h,4))goto done;n=af_le32(h);if(!n||n>65536U||received>size||n>size-received||!af_input(&p,out+(uint32_t)received,n))goto done;received+=n;
  }else if(type==4U){if(!af_input(&p,h,12)||af_le32(h)||af_le64(h+4)!=size||received!=size)goto done;p.status=AF_OK;ok=af_live(&p);break;
  }else goto done;
 }
done:af_close(&p);if(!ok && (!b->pd||!xx_pd_is_stopped(b->pd)))ac_error(b,p.status==AF_UNAVAILABLE?"archive codec helper unavailable":p.status==AF_TIMEOUT?"archive codec timed out":p.status==AF_LIMIT?"archive codec memory limit":"archive codec rejected damaged, unsupported or over-budget stream");return ok;
}
#endif
