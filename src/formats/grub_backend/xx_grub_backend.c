/* SPDX-License-Identifier: MIT. Original, bounded pipe client.
 * The separately built GPL helper is an independent executable, never linked.
 */
#include "xxfclib/formats/grub_backend/xx_grub_backend.h"
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
#include <fcntl.h>
#endif
typedef struct gb_process {
#ifdef _WIN32
 HANDLE input,output,process,job;
#else
 int input,output;pid_t process;
#endif
 uint64_t start;unsigned timeout;xx_pd_struct *pd;xx_grub_backend_status status;
} gb_process;
static uint64_t gb_clock(void){
#ifdef _WIN32
 return GetTickCount64();
#else
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000U+t.tv_nsec/1000000U;
#endif
}
static bool gb_live(gb_process *p){if(p->pd&&xx_pd_is_stopped(p->pd)){p->status=XX_GRUB_BACKEND_CANCELLED;return false;}if(gb_clock()-p->start>=p->timeout){p->status=XX_GRUB_BACKEND_TIMEOUT;return false;}return true;}
static bool gb_input(gb_process *p,void *data,size_t count){size_t done=0;
#ifdef _WIN32
 unsigned idle=0;
#endif
 while(done<count){if(!gb_live(p))return false;
#ifdef _WIN32
 DWORD available=0,got=0;if(!PeekNamedPipe(p->output,NULL,0,NULL,&available,NULL)){p->status=XX_GRUB_BACKEND_IO;return false;}if(!available){if(WaitForSingleObject(p->process,0)==WAIT_OBJECT_0){p->status=XX_GRUB_BACKEND_IO;return false;}
 /* Most pipe gaps are a ready helper processing our previous response.
  * Yield briefly before sleeping: a scheduler tick per protocol frame can
  * exhaust the deadline while reading an otherwise valid large member. */
 if(idle<128U){++idle;SwitchToThread();}else Sleep(1);continue;}
 idle=0;if(available>count-done)available=(DWORD)(count-done);if(!ReadFile(p->output,(uint8_t *)data+done,available,&got,NULL)||!got){p->status=XX_GRUB_BACKEND_IO;return false;}done+=got;
#else
 struct pollfd poller={p->output,POLLIN,0};ssize_t got;int ready=poll(&poller,1,5);if(ready<0&&errno==EINTR)continue;if(ready<0){p->status=XX_GRUB_BACKEND_IO;return false;}if(!ready)continue;got=read(p->output,(uint8_t *)data+done,count-done);if(got<0&&errno==EINTR)continue;if(got<=0){p->status=XX_GRUB_BACKEND_IO;return false;}done+=(size_t)got;
#endif
 }return true;}
static bool gb_output(gb_process *p,const void *data,size_t count){size_t done=0;while(done<count){size_t want=count-done>65536U?65536U:count-done;if(!gb_live(p))return false;
#ifdef _WIN32
 DWORD wrote=0;if(!WriteFile(p->input,(const uint8_t *)data+done,(DWORD)want,&wrote,NULL)||!wrote){p->status=XX_GRUB_BACKEND_IO;return false;}done+=wrote;
#else
 ssize_t wrote=send(p->input,(const uint8_t *)data+done,want,MSG_NOSIGNAL);if(wrote<0&&errno==EINTR)continue;if(wrote<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){struct pollfd poller={p->input,POLLOUT,0};(void)poll(&poller,1,5);continue;}if(wrote<=0){p->status=XX_GRUB_BACKEND_IO;return false;}done+=(size_t)wrote;
#endif
 }return true;}
static uint32_t gb_le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t gb_le64(const uint8_t *p){return gb_le32(p)|((uint64_t)gb_le32(p+4)<<32);}
static void gb_put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void gb_put64(uint8_t *p,uint64_t v){gb_put32(p,(uint32_t)v);gb_put32(p+4,(uint32_t)(v>>32));}
static void gb_close(gb_process *p){
#ifdef _WIN32
 if(p->input)CloseHandle(p->input);if(p->output)CloseHandle(p->output);if(p->process){if(WaitForSingleObject(p->process,100)!=WAIT_OBJECT_0){TerminateProcess(p->process,2);WaitForSingleObject(p->process,1000);}CloseHandle(p->process);}if(p->job)CloseHandle(p->job);
#else
 if(p->input>=0)close(p->input);if(p->output>=0)close(p->output);if(p->process>0){int status;if(waitpid(p->process,&status,WNOHANG)==0){kill(p->process,SIGKILL);while(waitpid(p->process,&status,0)<0&&errno==EINTR){}}}
#endif
}
static bool gb_start(gb_process *p,const char *helper,uint64_t memory){
#ifdef _WIN32
 wchar_t path[32768],*converted=NULL,*command=NULL;HANDLE child_read=NULL,child_write=NULL;SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};STARTUPINFOEXW si;PROCESS_INFORMATION pi;SIZE_T attribute_size=0;HANDLE handles[2];bool ok=false,attributes_live=false;JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
 ZeroMemory(&si,sizeof(si));ZeroMemory(&pi,sizeof(pi));si.StartupInfo.cb=sizeof(si);if(helper){converted=xx_str_utf8_to_unicode(helper);if(!converted)goto done;}else{DWORD n=GetModuleFileNameW(NULL,path,32768);while(n&&path[n-1]!=L'\\'&&path[n-1]!=L'/')--n;if(!n||n+24U>=32768U)goto done;wcscpy(path+n,L"xfu_grub_fs_helper.exe");}
 {const wchar_t *use=converted?converted:path;size_t n=wcslen(use);if(wcschr(use,L'"')||n>32764U)goto done;command=(wchar_t *)HeapAlloc(GetProcessHeap(),0,(n+3U)*sizeof(wchar_t));if(!command)goto done;command[0]=L'"';memcpy(command+1,use,n*sizeof(wchar_t));command[n+1]=L'"';command[n+2]=0;}
 if(!CreatePipe(&child_read,&p->input,&sa,65536)||!CreatePipe(&p->output,&child_write,&sa,65536)||!SetHandleInformation(p->input,HANDLE_FLAG_INHERIT,0)||!SetHandleInformation(p->output,HANDLE_FLAG_INHERIT,0))goto done;
 InitializeProcThreadAttributeList(NULL,1,0,&attribute_size);si.lpAttributeList=HeapAlloc(GetProcessHeap(),0,attribute_size);if(!si.lpAttributeList||!InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&attribute_size))goto done;attributes_live=true;handles[0]=child_read;handles[1]=child_write;if(!UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),NULL,NULL))goto done;
 si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdInput=child_read;si.StartupInfo.hStdOutput=child_write;si.StartupInfo.hStdError=child_write;
 p->job=CreateJobObjectW(NULL,NULL);if(!p->job)goto done;ZeroMemory(&limits,sizeof(limits));limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;limits.ProcessMemoryLimit=(SIZE_T)(memory+32U*1024U*1024U);if(!SetInformationJobObject(p->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))goto done;
 if(!CreateProcessW(converted?converted:path,command,NULL,NULL,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,NULL,&si.StartupInfo,&pi))goto done;p->process=pi.hProcess;if(!AssignProcessToJobObject(p->job,pi.hProcess)){TerminateProcess(pi.hProcess,2);CloseHandle(pi.hThread);goto done;}if(ResumeThread(pi.hThread)==(DWORD)-1){CloseHandle(pi.hThread);goto done;}CloseHandle(pi.hThread);ok=true;
done:if(child_read)CloseHandle(child_read);if(child_write)CloseHandle(child_write);if(si.lpAttributeList){if(attributes_live)DeleteProcThreadAttributeList(si.lpAttributeList);HeapFree(GetProcessHeap(),0,si.lpAttributeList);}if(command)HeapFree(GetProcessHeap(),0,command);if(converted)xx_str_wfree(converted);return ok;
#else
 int in[2]={-1,-1},out[2]={-1,-1};char path[4096];pid_t pid;(void)memory;p->input=p->output=-1;if(!helper){ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);if(n<=0||n>=(ssize_t)sizeof(path)-24)return false;while(n&&path[n-1]!='/')--n;if(!n)return false;strcpy(path+n,"xfu_grub_fs_helper");helper=path;}if(socketpair(AF_UNIX,SOCK_STREAM,0,in)||socketpair(AF_UNIX,SOCK_STREAM,0,out))goto failed;pid=fork();if(pid<0)goto failed;if(!pid){dup2(in[0],STDIN_FILENO);dup2(out[1],STDOUT_FILENO);dup2(out[1],STDERR_FILENO);close(in[0]);close(in[1]);close(out[0]);close(out[1]);execl(helper,helper,(char *)NULL);_exit(127);}close(in[0]);close(out[1]);p->input=in[1];p->output=out[0];p->process=pid;fcntl(p->input,F_SETFL,fcntl(p->input,F_GETFL)|O_NONBLOCK);return true;
failed:if(in[0]>=0)close(in[0]);if(in[1]>=0)close(in[1]);if(out[0]>=0)close(out[0]);if(out[1]>=0)close(out[1]);return false;
#endif
}
static bool gb_run(xx_io_device *source,int64_t base,int64_t length,const char *filesystem,const char *path,uint64_t expected,xx_io_device *dest,const xx_grub_backend_options *opts,xx_grub_backend_entry_fn callback,void *user){gb_process p;uint8_t h[40],buffer[65536];char member[4097];size_t fsn,pn=path?strlen(path):0;uint64_t received=0,listed=0,memory=opts&&opts->memory_limit?opts->memory_limit:256U*1024U*1024U,max=opts?opts->max_member_size:UINT64_MAX;int64_t total,saved=-1;bool ok=false;unsigned operation=path?2U:1U;
 memset(&p,0,sizeof(p));p.status=XX_GRUB_BACKEND_FORMAT;p.start=gb_clock();p.timeout=opts&&opts->timeout_ms?opts->timeout_ms:60000U;p.pd=opts?opts->pd:NULL;
#ifndef _WIN32
 p.input=p.output=-1;
#endif
 if(!source||!filesystem||!(fsn=strlen(filesystem))||fsn>16U||pn>4096U||(path&&(!pn||path[0]!='/'))||(total=xx_io_size(source))<0||base<0||base>total)goto done;if(length<0)length=total-base;if(length>total-base)goto done;if(memory>256U*1024U*1024U)memory=256U*1024U*1024U;if(memory<65536U||expected>max){p.status=XX_GRUB_BACKEND_LIMIT;goto done;}if(!gb_live(&p))goto done;saved=xx_io_tell(source);
 if(!gb_start(&p,opts?opts->helper_path:NULL,memory)){p.status=XX_GRUB_BACKEND_UNAVAILABLE;goto done;}memcpy(h,"GFS1",4);gb_put32(h+4,operation);gb_put64(h+8,(uint64_t)length);gb_put64(h+16,memory);gb_put64(h+24,max);gb_put32(h+32,(uint32_t)fsn);gb_put32(h+36,(uint32_t)pn);if(!gb_output(&p,h,40)||!gb_output(&p,filesystem,fsn)||!gb_output(&p,path,pn))goto done;
 for(;;){uint32_t type;if(!gb_input(&p,h,4))goto done;type=gb_le32(h);
  if(type==1U){uint64_t at;uint32_t count;size_t read=0;if(!gb_input(&p,h,12))goto done;at=gb_le64(h);count=gb_le32(h+8);if(!count||count>sizeof(buffer)||at>(uint64_t)length||count>(uint64_t)length-at){p.status=XX_GRUB_BACKEND_FORMAT;goto done;}if(xx_io_seek64(source,base+(int64_t)at,SEEK_SET)!=0){p.status=XX_GRUB_BACKEND_IO;goto done;}while(read<count){ssize_t n;if(!gb_live(&p))goto done;n=xx_io_read(source,buffer+read,count-read);if(n<=0||(size_t)n>count-read){p.status=XX_GRUB_BACKEND_IO;goto done;}read+=(size_t)n;}gb_put32(h,count);if(!gb_output(&p,h,4)||!gb_output(&p,buffer,count))goto done;
  }else if(type==2U){uint32_t flags,count;xx_grub_backend_entry e;if(operation!=1U||!gb_input(&p,h,24))goto done;flags=gb_le32(h);e.size=gb_le64(h+4);e.mtime=(int64_t)gb_le64(h+12);count=gb_le32(h+20);if(flags>3U||!count||count>4096U||e.size>max||++listed>100000U){p.status=XX_GRUB_BACKEND_FORMAT;goto done;}if(!gb_input(&p,member,count))goto done;member[count]=0;if(strlen(member)!=count||member[0]!='/'){p.status=XX_GRUB_BACKEND_FORMAT;goto done;}e.path=member;e.directory=(flags&1U)!=0;e.has_mtime=(flags&2U)!=0;if(callback&&!callback(user,&e))goto done;
  }else if(type==3U){uint32_t count;size_t wrote=0;if(operation!=2U||!gb_input(&p,h,4))goto done;count=gb_le32(h);if(!count||count>sizeof(buffer)||received>expected||count>expected-received){p.status=XX_GRUB_BACKEND_FORMAT;goto done;}if(!gb_input(&p,buffer,count))goto done;while(dest&&wrote<count){ssize_t n;if(!gb_live(&p))goto done;n=xx_io_write(dest,buffer+wrote,count-wrote);if(n<=0||(size_t)n>count-wrote){p.status=XX_GRUB_BACKEND_IO;goto done;}wrote+=(size_t)n;}received+=count;
  }else if(type==4U){uint32_t code;uint64_t count;if(!gb_input(&p,h,12))goto done;code=gb_le32(h);count=gb_le64(h+4);if(code||count!=(operation==1U?listed:received)||(operation==2U&&received!=expected)){p.status=XX_GRUB_BACKEND_FORMAT;goto done;}p.status=XX_GRUB_BACKEND_OK;ok=gb_live(&p);break;
  }else{p.status=XX_GRUB_BACKEND_FORMAT;goto done;}
 }
done:gb_close(&p);if(saved>=0&&xx_io_seek64(source,saved,SEEK_SET)!=0){p.status=XX_GRUB_BACKEND_IO;ok=false;}if(opts&&opts->status)*opts->status=p.status;return ok;}
bool xx_grub_backend_list(xx_io_device *s,int64_t b,int64_t n,const char *fs,const xx_grub_backend_options *o,xx_grub_backend_entry_fn cb,void *u){return gb_run(s,b,n,fs,NULL,0,NULL,o,cb,u);}
bool xx_grub_backend_read(xx_io_device *s,int64_t b,int64_t n,const char *fs,const char *path,uint64_t size,xx_io_device *out,const xx_grub_backend_options *o){if(!path){if(o&&o->status)*o->status=XX_GRUB_BACKEND_FORMAT;return false;}return gb_run(s,b,n,fs,path,size,out,o,NULL,NULL);}
