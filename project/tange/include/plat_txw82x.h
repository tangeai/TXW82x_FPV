#ifndef __plat_txw82x_h__
#define __plat_txw82x_h__

#include "basedef.h"

/* 线程/Event 使用了81x一样的封装 */

#ifdef __TXW82X__
#include "osal_file.h"

#define SA_FILE F_FILE
//#define stdout  (F_FILE*)1
//#define stderr  (F_FILE*)1

#if 1 //no fs
#define SA_fopen(path, mode)                osal_fopen(path, mode)
#define SA_fclose(fp)                       osal_fclose(fp)
#define SA_fread(ptr, ele_size, size, fp)   osal_fread(ptr, ele_size, size, fp)
#define SA_fwrite(ptr, ele_size, size, fp)  osal_fwrite(ptr, ele_size, size, fp)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int SA_fseek(SA_FILE *fp, long offset, int whence);
#define SA_rewind(fp)                       (void)osal_fseek(fp, 0)
#define SA_ftell(fp)                        osal_ftell(fp) 
#define SA_feof(fp)                         (osal_ftell(fp) >= osal_fsize(fp))
#define SA_remove(path)                     osal_unlink(path)
#define SA_rmdir(path)                      osal_unlink_dir(path, 0)
#define SA_fflush(fp) //fflush(fp)
#define WRAP_FPRINTF  1
int SA_fprintf(SA_FILE *fp, const char *fmt, ...);
#define SA_setbuffer(fp, buff, size) //setbuffer(fp, buff, size)
#define SA_fsync(fp)                        osal_fsync(fp)
#define SA_mkdir(path)                      osal_fmkdir(path)
long SA_GetFileLength(const char *path);

typedef void * SA_DIRSCAN;
typedef void * SA_DENTRY;

/* SA_BOOL SA_ScanBegin(SA_DIRSCAN scan, const char *path); */
#define SA_ScanBegin(scan, path) (scan = osal_opendir(path))

//ext: extension
//有的实现很烂的文件系统要求提供扫描文件的扩展名, 但在linux/liteos下不需要此参数。
//为了兼容, 写成带扩展名的形式
#define SA_ScanBegin_(scan, path, ext) (scan = osal_opendir(path))

/* SA_ScanEnd(SA_DIRSCAN scan) */
#define SA_ScanEnd(scan) osal_closedir(scan)

/* SA_BOOL SA_ScanNext(SA_DIRSCAN scan, SA_DENTRY de); */
#define SA_ScanNext(scan, de) (de = osal_readdir(scan))

/* const char *SA_DENAME(SA_HDENTRY de); */
#define SA_DENAME(de) osal_dirent_name(de)

#define SA_DE_ISDIR(de) osal_dirent_isdir(de)

#endif //fs

//#include "osal/csky/typesdef.h"
#if 0
#ifndef __CSKY__
#define __CSKY__
#endif
#include <osal/task.h>
#endif
#include "osal/mutex.h"
#include "osal/semaphore.h"
#include "osal/time.h"
#include "osal/sleep.h"

#ifdef __cplusplus
extern "C" {
#endif

#define __need_SA_platInit__
void SA_platInit();

#if 1
#define SA_INFINITE osWaitForever

/*
 * Mutex
 */
typedef struct os_mutex SA_MUTEX;

#define SA_MutexInit(x) os_mutex_init(&x)
#define SA_MutexUninit(x) os_mutex_del(&x)
#define SA_MutexLock(x) os_mutex_lock(&x, SA_INFINITE/*timeout in ms*/)
#define SA_MutexUnlock(x) os_mutex_unlock(&x)
//#define SA_MutexTryLock(x) os_mutex_lock(&x, 0/*timeout in ms*/)

/*
 * Sem
 */
typedef struct os_semaphore SA_SEM;
#define SA_SemInit(x, init_val) os_sema_init(&x, init_val)
#define SA_SemUninit(x) os_sema_del(&x)
#define SA_SemWait(x) os_sema_down(&x, SA_INFINITE/*timeout in ms*/)
#define SA_SemPost(x) os_sema_up(&x)

/*
 * Event
 */
typedef struct os_semaphore SA_EVENT;

#define SA_EventInit(x) os_sema_init(&x, 0)
#define SA_EventUninit(x) os_sema_del(&x)
#define SA_EventWait(x) os_sema_down(&x, SA_INFINITE)
#define SA_EventSet(x) os_sema_up(&x)
#define SA_EventWaitTimed(x, wait_ms) os_sema_down(&x, wait_ms)

#else

#include <lib/posix/pthread.h>

#define SA_INFINITE 0xFFFFFFFF  //for all {timeout} parameters

#define SA_MUTEX	pthread_mutex_t

#define SA_DEFINEMUTEX(x) pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER
#define SA_MutexInit(x) pthread_mutex_init(&x, NULL)
#define SA_MutexUninit(x) pthread_mutex_destroy(&x)
#define SA_MutexLock(x) pthread_mutex_lock(&x)
#define SA_MutexUnlock(x) pthread_mutex_unlock(&x)
#define SA_MutexTryLock(x) (pthread_mutex_trylock(&x) == 0)

typedef sem_t SA_EVENT;

#define SA_EventInit(e)		sem_init(&e, 0, 0)
#define SA_EventUninit(e)	sem_destroy(&e)
#define SA_EventSet(e)		sem_post(&e) 
#define SA_EventWait(e)		sem_wait(&e)
SA_BOOL __txw81x_EventWaitTimed(SA_EVENT *e, DWORD ms);
#define SA_EventWaitTimed(e, ms) __txw81x_EventWaitTimed(&e, ms)

typedef sem_t SA_SEM;
#define SA_SemInit(sem, init_value) sem_init(&sem, 0, init_value)
#define SA_SemUninit(sem)	    sem_destroy(&sem)
#define SA_SemWait(sem) sem_wait(&sem)
#define SA_SemPost(sem) sem_post(&sem)

#endif

/*
 * Thread
 */
struct txwTask;
typedef struct txwTask *  SA_HTHREAD;
#define SA_HTHREAD_NULL		NULL
#define SA_HTHREAD_IS_VALID(h) (h)
#define SA_HTHREAD_CLEAR(h) h = NULL 
#define SA_THREAD_RETTYPE	void
#define SA_THREAD_RETVALUE(r)
typedef SA_THREAD_RETTYPE (*SA_ThreadRoutine)(void*);

#define DECL_THREAD(func, param) void *func(void *param)
#define RET_THREAD(v) return (void*)v

SA_HTHREAD txw81x_ThreadCreateWithStackSize(SA_ThreadRoutine routine, void *arg, int stack_size, const char *name);
/** SA_BOOL SA_ThreadCreateWithStackSize(SA_HTHREAD handle, SA_ThreadRoutine routine, void *arg, ...) */
#define SA_ThreadCreateWithStackSize(handle, routine, arg, stack_size, name) (handle = txw81x_ThreadCreateWithStackSize(routine, arg, stack_size, name))
#define SA_ThreadCreate(handle, routine, arg, name) (handle = txw81x_ThreadCreateWithStackSize(routine, arg, 0, name))
#define SA_SET_CURRENT_THREAD_NAME(name)

SA_HTHREAD SA_ThreadGetCurrentHandle();
void SA_ThreadCloseHandle(SA_HTHREAD hThread);
void SA_ThreadWaitUntilTerminate(SA_HTHREAD);

int SA_ThreadSuspend(SA_HTHREAD h);
int SA_ThreadResume(SA_HTHREAD h);

#if 0

#define SA_THREAD_RETTYPE void
#define SA_THREAD_RETVALUE(r)
typedef SA_THREAD_RETTYPE (*SA_ThreadRoutine)(void*);

#define DECL_THREAD(func, param) void func(void *param)
#define RET_THREAD(v) return

typedef struct os_task* SA_HTHREAD;
#define SA_HTHREAD_IS_VALID(h) (h)
#define SA_HTHREAD_CLEAR(h) memst(h, 0, sizeof(struct os_task))
#define SA_ThreadWaitUntilTerminate(hThread)
#define SA_ThreadCreateWithStackSize(hthd, routine, arg, stack_size, name)
#define SA_ThreadCreate(hthd, routine, arg, name)
//#define SA_ThreadGetCurrentHandle() os_task_current()
#define SA_ThreadCloseHandle(h) ...

#define SA_SET_CURRENT_THREAD_NAME(name)

#define SA_ThreadSuspend(h) os_task_suspend(h)
#define SA_ThreadResume(h) os_task_resume(h)

#endif



/*
 * Time
 */
uint32_t SA_GetTickCount();
#define SA_Tick2Ms(tick) (tick)

#include <osal/sleep.h>
#define SA_Sleep(ms) os_sleep_ms(ms)

#include <time.h>
#define SA_time(t) time(t)

#define NO_TIMEZONE_SUPPORT 1
#define NO_GETTIMEOFDAY_SUPPORT 1
#define NO_STRTOK_R         1
#define NO_LOCALTIME_R      1

#define SA_srand(x) srand(x)
#define SA_rand() rand()

#ifdef __cplusplus
}
#endif

#endif

#endif

