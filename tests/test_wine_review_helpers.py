#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Fault-inject the actual patched Wine stack and directory-rename helpers.
Set PROSPERO_WINE_PATCHED_SOURCE to a tree with this branch's series applied.
The extracted production functions are compiled on the host; no PS5/GPU needed.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def function(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if text[end] == '{': depth += 1
        elif text[end] == '}': depth -= 1
        end += 1
    return text[start:end] + '\n'

STACK_HEADER = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
typedef int BOOL, NTSTATUS;
typedef unsigned int UINT;
typedef struct {void *ExceptionAddress;int ExceptionCode,NumberParameters;} EXCEPTION_RECORD;
typedef unsigned char BYTE;
typedef size_t SIZE_T, ULONG_PTR;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_NO_MEMORY ((int)0xc0000017)
#define STATUS_STACK_OVERFLOW ((int)0xc00000fd)
#define VPROT_READ 1
#define VPROT_WRITE 2
#define VPROT_COMMITTED 4
#define VPROT_GUARD 8
#define PAGE 4096
#define HOST 16384
#define max(a,b) ((a)>(b)?(a):(b))
#define ROUND_SIZE(a,s,m) ((((uintptr_t)(a)&(m))+(s)+(m))&~(m))
#define VIRTUAL_DEBUG_DUMP_VIEW(v) ((void)(v))
#define ERR(...) ((void)0)
#define WARN(...) ((void)0)
#define ROUND_ADDR(a,m) ((char*)((uintptr_t)(a)&~(m)))
#define PtrToUlong(p) ((uint32_t)(uintptr_t)(p))
static const size_t page_size=PAGE, host_page_size=HOST, host_page_mask=HOST-1, granularity_mask=65535;
static unsigned char arena[2*1024*1024] __attribute__((aligned(65536)));
static BYTE protections[sizeof(arena)/PAGE];
struct file_view {void *base;size_t size;};
static struct file_view mapped;
static struct {size_t MaximumStackSize,CommittedStackSize;} main_image_info={1024*1024,HOST};
typedef struct {void *OldStackBase,*OldStackLimit,*DeallocationStack,*StackBase,*StackLimit;} INITIAL_TEB;
typedef struct {struct {void *StackLimit;} Tib;} TEB;
typedef struct {struct {uint32_t StackLimit;} Tib;} WOW_TEB;
static WOW_TEB wow;
struct thread_data {TEB *teb;};
struct thread_stack_info {char *start,*limit,*end;size_t guaranteed;int is_wow;};
static int virtual_mutex, calls, fail_call, deleted, map_failure;
static void server_enter_uninterrupted_section(int *mutex,sigset_t *set){(void)mutex;(void)set;}
static void server_leave_uninterrupted_section(int *mutex,sigset_t *set){(void)mutex;(void)set;}
static void set_page_vprot(void *addr,size_t size,BYTE value)
{size_t start=((char*)addr-(char*)arena)/PAGE;for(size_t i=0;i<size/PAGE;i++)protections[start+i]=value;}
static void set_page_vprot_bits(void *addr,size_t size,BYTE set,BYTE clear)
{size_t start=((char*)addr-(char*)arena)/PAGE;for(size_t i=0;i<size/PAGE;i++)protections[start+i]=(protections[start+i]&~clear)|set;}
static int mprotect_range(void *addr,size_t size,BYTE set,BYTE clear)
{(void)addr;(void)size;(void)set;(void)clear;return ++calls==fail_call?-1:0;}
static int map_view(struct file_view **view,void *base,size_t size,size_t mask,BYTE prot,uintptr_t lo,uintptr_t hi,int flags)
{(void)base;(void)mask;(void)lo;(void)hi;(void)flags;if(map_failure)return STATUS_NO_MEMORY;mapped=(struct file_view){arena,size};*view=&mapped;set_page_vprot(arena,size,prot);return 0;}
static void delete_view(struct file_view *view){deleted++;set_page_vprot(view->base,view->size,0);}
static WOW_TEB *get_wow_teb(TEB *teb){(void)teb;return &wow;}
static struct thread_stack_info exception_info;
static jmp_buf abort_jump;static int aborted,locked;
static void abort_thread(int code) __attribute__((noreturn));
static void abort_thread(int code){(void)code;aborted++;longjmp(abort_jump,1);}
static int is_inside_thread_stack(struct thread_data *data,void *ptr,struct thread_stack_info *info)
{(void)data;(void)ptr;*info=exception_info;return 1;}
static int is_inside_signal_stack(struct thread_data *data,void *ptr){(void)data;(void)ptr;return 0;}
static BYTE get_page_vprot(void *ptr){return protections[((char*)ptr-(char*)arena)/PAGE];}
static BYTE get_host_page_vprot(void *ptr){return get_page_vprot(ptr);}
static void mutex_lock(int *mutex){(void)mutex;assert(!locked);locked=1;}
static void mutex_unlock(int *mutex){(void)mutex;assert(locked);locked=0;}

'''
STACK_TEST = r'''
int main(void)
{
    INITIAL_TEB stack,before;
    memset(&stack,0xa5,sizeof(stack));before=stack;
    fail_call=1;
    assert(virtual_alloc_thread_stack(&stack,0,0,1024*1024,HOST,1)==STATUS_NO_MEMORY);
    assert(deleted==1 && !memcmp(&stack,&before,sizeof(stack)));
    calls=deleted=0;fail_call=0;
    assert(virtual_alloc_thread_stack(&stack,0,0,1024*1024,HOST,1)==0);
    assert(stack.StackLimit==(char*)stack.StackBase-HOST);
    TEB teb={{arena+12*HOST}};struct thread_data data={&teb};
    struct thread_stack_info info={(char*)arena,(char*)arena+12*HOST,(char*)arena+20*HOST,HOST,0};
    memset(protections,VPROT_READ|VPROT_WRITE,sizeof(protections));
    calls=0;fail_call=1;
    assert(grow_thread_stack_to(&data,(char*)arena+10*HOST,&info)==STATUS_NO_MEMORY);
    assert(!(protections[10*HOST/PAGE]&VPROT_COMMITTED));
    assert(teb.Tib.StackLimit==arena+12*HOST);
    calls=0;fail_call=2;
    assert(grow_thread_stack(&data,(char*)arena+10*HOST,&info)==STATUS_NO_MEMORY);
    assert(!(protections[9*HOST/PAGE]&VPROT_COMMITTED));
    assert(!(protections[10*HOST/PAGE]&VPROT_COMMITTED));
    assert(teb.Tib.StackLimit==arena+12*HOST);
    calls=0;fail_call=0;
    assert(grow_thread_stack_to(&data,(char*)arena+10*HOST,&info)==0);
    assert(teb.Tib.StackLimit==arena+10*HOST);
    assert(protections[9*HOST/PAGE]&(VPROT_COMMITTED|VPROT_GUARD));
    calls=0;fail_call=1;exception_info=info;teb.Tib.StackLimit=arena+12*HOST;
    memset(protections,VPROT_READ|VPROT_WRITE,sizeof(protections));
    EXCEPTION_RECORD rec={0};
    if(!setjmp(abort_jump)) {
        virtual_setup_exception(&data,arena+10*HOST+128,256,&rec);
        assert(!"inaccessible exception stack returned");
    }
    assert(aborted==1 && !locked);
    puts("Wine stack backing: failed initial allocation, failed reserve growth and guard failure propagate without publishing inaccessible pages");
}
'''
DIRECTORY_HEADER = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "wine/list.h"
#define STATUS_OBJECT_NAME_INVALID 1
struct closed_fd {char *unix_name;};
struct fd {char *unix_name;struct closed_fd *closed;struct list ps5_directory_entry;};
static struct list ps5_directories=LIST_INIT(ps5_directories);
static unsigned fail_alloc, allocs;
static void set_error(int status){(void)status;}
static void file_set_error(void){}
static void *mem_alloc(size_t size){if(++allocs==fail_alloc)return NULL;return malloc(size);}
'''
DIRECTORY_TEST = r'''
static void register_fd(struct fd *fd,struct closed_fd *closed,const char *name)
{fd->unix_name=strdup(name);fd->closed=closed;closed->unix_name=fd->unix_name;list_add_tail(&ps5_directories,&fd->ps5_directory_entry);}
int main(void)
{
    char temp[]="/tmp/pw-rename-XXXXXX",old[PATH_MAX],next[PATH_MAX],child[PATH_MAX],sibling[PATH_MAX],blocked[PATH_MAX],entry[PATH_MAX];
    assert(mkdtemp(temp));
    snprintf(old,sizeof(old),"%s/parent",temp);snprintf(next,sizeof(next),"%s/renamed",temp);
    snprintf(child,sizeof(child),"%s/parent/child",temp);snprintf(sibling,sizeof(sibling),"%s/parent-other",temp);
    snprintf(blocked,sizeof(blocked),"%s/blocked",temp);snprintf(entry,sizeof(entry),"%s/blocked/file",temp);
    assert(!mkdir(old,0700)&&!mkdir(child,0700)&&!mkdir(sibling,0700)&&!mkdir(blocked,0700));
    int file=open(entry,O_WRONLY|O_CREAT,0600);assert(file>=0);close(file);
    struct stat expected,actual;assert(!stat(child,&expected));
    struct fd root,one,two,other;struct closed_fd cr,co,ct,cs;
    register_fd(&root,&cr,old);register_fd(&one,&co,child);register_fd(&two,&ct,child);register_fd(&other,&cs,sibling);
    for(unsigned fail=1;fail<9;fail++) {
        allocs=0;fail_alloc=fail;
        struct ps5_rename_path *plan=ps5_prepare_rename(&root,next);
        ps5_finish_rename(plan,0);
        assert(!strcmp(root.unix_name,old)&&!strcmp(one.unix_name,child)&&!strcmp(other.unix_name,sibling));
    }
    allocs=0;fail_alloc=0;
    struct ps5_rename_path *plan=ps5_prepare_rename(&root,next);assert(plan);
    assert(!rename(old,next));ps5_finish_rename(plan,1);
    assert(!strcmp(root.unix_name,next)&&!strcmp(one.unix_name,two.unix_name));
    int fd=open(one.unix_name,O_RDONLY|O_DIRECTORY);assert(fd>=0);
    assert(!fstat(fd,&actual)&&actual.st_ino==expected.st_ino&&actual.st_dev==expected.st_dev);close(fd);
    assert(!strcmp(other.unix_name,sibling));
    plan=ps5_prepare_rename(&root,blocked);assert(plan);
    assert(rename(root.unix_name,blocked)==-1);ps5_finish_rename(plan,0);
    assert(!strcmp(root.unix_name,next)&&!access(one.unix_name,F_OK));
    assert(!rmdir(one.unix_name)&&!rmdir(next)&&!rmdir(sibling)&&!unlink(entry)&&!rmdir(blocked)&&!rmdir(temp));
    free(root.unix_name);free(one.unix_name);free(two.unix_name);free(other.unix_name);
    puts("Wine parked directory rename: descendants keep their inode, sibling prefixes stay unchanged, allocation/rename failures preserve names");
}
'''


def main():
    source = Path(os.environ.get('PROSPERO_WINE_PATCHED_SOURCE', '.deps/wine-ps5/source'))
    compiler = shutil.which(os.environ.get('CC', 'cc'))
    if not source.is_dir() or not compiler:
        print('Wine review helpers skipped: needs a patched Wine source tree and host C compiler')
        return
    virtual = (source/'dlls/ntdll/unix/virtual.c').read_text()
    stack = STACK_HEADER + '\n'.join(function(virtual, marker) for marker in (
        'static inline BOOL stack_commit_on_demand(', 'NTSTATUS virtual_alloc_thread_stack(',
        'static NTSTATUS grow_thread_stack(',
        'static NTSTATUS grow_thread_stack_to(', 'static BOOL is_stack_reserve(',
        'void *virtual_setup_exception(')) + STACK_TEST
    fd = (source/'server/fd.c').read_text()
    start = fd.index('struct ps5_rename_path {')
    end = fd.index('\n#endif', start)
    directory = DIRECTORY_HEADER + function(fd, 'static char *realpath_dup(') + fd[start:end] + DIRECTORY_TEST
    with tempfile.TemporaryDirectory() as temp:
        for name, text in (('stack', stack), ('directory', directory)):
            c = Path(temp)/(name+'.c')
            exe = Path(temp)/name
            c.write_text(text)
            subprocess.run([compiler,'-std=gnu11','-O1','-g','-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer','-I'+str(source/'include'),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    main()
