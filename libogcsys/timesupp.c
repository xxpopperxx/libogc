#include <config.h>
#include <_ansi.h>
#include <_syslist.h>
#include <errno.h>
#undef errno
extern int errno;

#include "asm.h"
#include "processor.h"
#include "lwp.h"
#include "lwp_threadq.h"
#include "timesupp.h"
#include "exi.h"
#include "system.h"

/* time variables */
static lwpq_t time_exi_wait;
static lwp_thrqueue timedwait_queue;

extern u32 __SYS_GetRTC(u32 *gctime);
extern syssram* __SYS_LockSram();
extern u32 __SYS_UnlockSram(u32 write);

unsigned long _DEFUN(gettick,(),
						  _NOARGS)
{
	__asm__ __volatile__ (
		"1:	mftb	3\n\
		    blr"
			: : : "memory");
	return 0;
}

long long _DEFUN(gettime,(),
						  _NOARGS)
{
	__asm__ __volatile__ (
		"1:	mftbu	3\n\
		    mftb	4\n\
		    mftbu	5\n\
			cmpw	3,5\n\
			bne		1b\n\
			blr"
			: : : "memory");
	return 0;
}

void _DEFUN(settime,(t),
			long long t)
{
	__asm__ __volatile__ (
		"mtspr	284,4\n\
		 mtspr	285,3\n\
	     blr"
		 : : : "memory");
}

u32 diff_sec(long long start,long long end)
{
	s64 diff;

	diff = diff_ticks(end,start);
	return ticks_to_secs(diff);
}

u32 diff_msec(long long start,long long end)
{
	s64 diff;

	diff = diff_ticks(end,start);
	return ticks_to_millisecs(diff);
}

u32 diff_usec(long long start,long long end)
{
	s64 diff;

	diff = diff_ticks(end,start);
	return ticks_to_microsecs(diff);
}

u32 diff_nsec(long long start,long long end)
{
	s64 diff;

	diff = diff_ticks(end,start);
	return ticks_to_nanosecs(diff);
}

void __timesystem_init()
{
	__lwp_threadqueue_init(&timedwait_queue,LWP_THREADQ_MODEPRIORITY,LWP_STATES_LOCALLY_BLOCKED,6);
	LWP_InitQueue(&time_exi_wait);
}


void timespec_substract(const struct timespec *tp_start,const struct timespec *tp_end,struct timespec *result)
{
	struct timespec start_st = *tp_start;
	struct timespec *start = &start_st;
	u32 nsecpersec = TB_NSPERSEC;

	if(tp_end->tv_nsec<start->tv_nsec) {
		int secs = (start->tv_nsec - tp_end->tv_nsec)/nsecpersec+1;
		start->tv_nsec -= nsecpersec * secs;
		start->tv_sec += secs;
	}
	if((tp_end->tv_nsec - start->tv_nsec)>nsecpersec) {
		int secs = (start->tv_nsec - tp_end->tv_nsec)/nsecpersec;
		start->tv_nsec += nsecpersec * secs;
		start->tv_sec -= secs;
	}

	result->tv_sec = (tp_end->tv_sec - start->tv_sec);
	result->tv_nsec = (tp_end->tv_nsec - start->tv_nsec);
}

unsigned int timespec_to_interval(const struct timespec *time)
{
	u32 ticks;

	ticks = (time->tv_sec*TB_USPERSEC)/TB_USPERTICK;
	ticks += (time->tv_nsec/TB_NSPERUS)/TB_USPERTICK;

	if(ticks) return ticks;

	return 1;
}

unsigned long long timespec_to_ticks(const struct timespec *time)
{
	u64 ticks;

	ticks = secs_to_ticks(time->tv_sec);
	ticks += nanosecs_to_ticks(time->tv_nsec);

	return ticks;
}

int clock_gettime(struct timespec *tp)
{
	syssram* sram;
	u32 gctime;

	if(!tp) return -1;

	if(!__SYS_GetRTC(&gctime)) return -1;

	sram = __SYS_LockSram();
	gctime += sram->counter_bias;
	__SYS_UnlockSram(0);
	gctime += 946684800;

	tp->tv_sec = gctime;
	tp->tv_nsec = gettick();

	return 0;
}

// this function spins till timeout is reached
void _DEFUN(udelay,(us),
			int us)
{
	long long start, end;
	start = gettime();
	while (1)
	{
		end = gettime();
		if (diff_usec(start,end) >= us)
			break;
	}
}

unsigned int _DEFUN(nanosleep,(tb),
           struct timespec *tb)
{
	u32 level,timeout;
	lwp_cntrl *exec;

	__lwp_thread_dispatchdisable();

	exec = _thr_executing;
	exec->wait.ret_code = TB_SUCCESSFUL;

	if(tb->tv_nsec<TB_USPERTICK) {
		udelay(tb->tv_nsec);
		return TB_SUCCESSFUL;
	}

	timeout = timespec_to_interval(tb);

	_CPU_ISR_Disable(level);
	__lwp_threadqueue_csenter(&timedwait_queue);
	exec->wait.queue = &timedwait_queue;
	exec->id = TB_REQ;
	exec->wait.ret_arg = NULL;
	exec->wait.ret_arg_1 = NULL;
	_CPU_ISR_Restore(level);
	__lwp_threadqueue_enqueue(&timedwait_queue,timeout);

	__lwp_thread_dispatchenable();
	return TB_SUCCESSFUL;
}

static u32 __time_exi_unlock()
{
	LWP_WakeThread(time_exi_wait);
	return 1;
}

static void __time_exi_wait()
{
	u32 ret,level;

	_CPU_ISR_Disable(level);
	do {
		if((ret=EXI_Lock(EXI_CHANNEL_0,EXI_DEVICE_1,__time_exi_unlock))==1) break;
		LWP_SleepThread(time_exi_wait);
	}while(ret==0);
	_CPU_ISR_Restore(level);
}

time_t _DEFUN(time,(timer),
			  time_t *timer)
{
	time_t gctime = 0;
	u32 command;
	syssram* sram;


	__time_exi_wait();

	command = 0x20000000;
	EXI_Select(EXI_CHANNEL_0,EXI_DEVICE_1,EXI_SPEED8MHZ);
	EXI_Imm(EXI_CHANNEL_0,&command,4,EXI_WRITE,NULL);
	EXI_Sync(EXI_CHANNEL_0);
	EXI_Imm(EXI_CHANNEL_0,&gctime,4,EXI_READ,NULL);
	EXI_Sync(EXI_CHANNEL_0);
	EXI_Deselect(EXI_CHANNEL_0);
	EXI_Unlock(EXI_CHANNEL_0);
	
	sram = __SYS_LockSram();
	gctime += sram->counter_bias;
	__SYS_UnlockSram(0);

	gctime += 946684800;

	if (timer) *timer = gctime;
	return gctime;
}
