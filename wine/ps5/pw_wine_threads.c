/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pw_wine_threads.h"
#include "pw_wine_tune.h"
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct Entry { long tid; pthread_t thread; } Entry;

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Entry entries[PW_WINE_THREADS_MAX];
static unsigned count;

static Entry *find_locked(long tid)
{
    for(unsigned i=0;i<count;i++)
        if(entries[i].tid==tid)return &entries[i];
    return NULL;
}

int pw_wine_thread_register(long tid,pthread_t thread)
{
    int status=0;
    pthread_mutex_lock(&lock);
    Entry *entry=find_locked(tid);
    if(entry)entry->thread=thread;
    else if(count<PW_WINE_THREADS_MAX)entries[count++]=(Entry){tid,thread};
    else status=-1;
    pthread_mutex_unlock(&lock);
    return status;
}
void pw_wine_thread_unregister(long tid)
{
    pthread_mutex_lock(&lock);
    Entry *entry=find_locked(tid);
    if(entry)*entry=entries[--count];
    pthread_mutex_unlock(&lock);
}
/* The call is made with the table locked: a thread leaves the table before
 * it exits, so a handle found here is still a live thread's. */
int pw_wine_thread_kill(pid_t pid,long tid,int signal)
{
    int status;
    if(pid!=getpid()){errno=ESRCH;return -1;}
    pthread_mutex_lock(&lock);
    Entry *entry=find_locked(tid);
    status=entry?pthread_kill(entry->thread,signal):ESRCH;
    pthread_mutex_unlock(&lock);
    if(status){errno=status;return -1;}
    return 0;
}
int pw_wine_thread_set_priority(long tid,int policy,int priority)
{
    struct sched_param param={0};
    int status;
    param.sched_priority=priority;
    pthread_mutex_lock(&lock);
    Entry *entry=find_locked(tid);
    status=entry?pthread_setschedparam(entry->thread,policy,&param):ESRCH;
    pthread_mutex_unlock(&lock);
    return status;
}

#ifdef __PROSPERO__
/* The console's cpuset calls take a 64-bit set (ntdll patch 0881). */
extern int cpuset_getaffinity(int level,int which,id_t id,size_t size,unsigned long long *mask);
extern int cpuset_setaffinity(int level,int which,id_t id,size_t size,const unsigned long long *mask);
static int set_affinity(long tid,unsigned long long mask)
{
    unsigned long long process=0;
    /* CPU_LEVEL_WHICH, CPU_WHICH_PID / CPU_WHICH_TID */
    if(!cpuset_getaffinity(3,2,-1,sizeof(process),&process)&&process)mask&=process;
    if(!mask)return EINVAL;
    return cpuset_setaffinity(3,1,(id_t)tid,sizeof(mask),&mask)?errno:0;
}
#else
static int set_affinity(long tid,unsigned long long mask){(void)tid;(void)mask;return ENOSYS;}
#endif

int pw_wine_thread_tune(long tid,const unsigned short *name,size_t length)
{
    const char *tune=getenv("PW_WINE_TUNE");
    unsigned long long mask;
    int priority,affinity=0,scheduled=0;
    char text[64];
    size_t i;

    if(!tune||!*tune)return 0;
    for(i=0;i<length&&i<sizeof(text)-1;i++)text[i]=name[i]<0x80&&name[i]>=0x20?(char)name[i]:'?';
    text[i]=0;
    if(!pw_wine_tune_thread(tune,text,&mask,&priority))return 0;
    if(mask)affinity=set_affinity(tid,mask);
    if(priority){
        struct sched_param param={0};
        param.sched_priority=priority;
        pthread_mutex_lock(&lock);
        Entry *entry=find_locked(tid);
        scheduled=entry?pthread_setschedparam(entry->thread,SCHED_FIFO,&param):ESRCH;
        pthread_mutex_unlock(&lock);
    }
    fprintf(stderr,"PW_WINE_TUNE thread tid=%ld name=\"%s\" mask=%llx priority=%d affinity_rc=%d priority_rc=%d\n",
            tid,text,mask,priority,affinity,scheduled);
    return affinity?affinity:scheduled;
}
