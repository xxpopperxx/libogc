#include <stdlib.h>
#include <unistd.h>
#include "asm.h"
#include "processor.h"
#include "cache.h"
#include "lwp.h"
#include "irq.h"
#include "system.h"
#include "dvd.h"

#define DVD_BRK				(1<<0)
#define DVD_DE_MSK			(1<<1)
#define DVD_DE_INT			(1<<2)
#define DVD_TC_MSK			(1<<3)
#define DVD_TC_INT			(1<<4)
#define DVD_BRK_MSK			(1<<5)
#define DVD_BRK_INT			(1<<6)

#define DVD_CVR_INT			(1<<2)
#define DVD_CVR_MSK			(1<<1)
#define DVD_CVR_STATE		(1<<0)

#define DVD_DI_MODE			(1<<2)
#define DVD_DI_DMA			(1<<1)
#define DVD_DI_START		(1<<0)

#define DVD_DISKIDSIZE		0x20

#define DVD_READSECTOR		0xA8000000
#define DVD_READDISKID		0xA8000040

#define _SHIFTL(v, s, w)	\
    ((u32) (((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w)	\
    ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))

typedef struct _dvdcmd {
	u32 cmd;
	void *buffer;
	u32 len;
	u32 offset;
	DVDCallback cb;
} dvdcmd;

typedef struct _dvdcmd2 {
	void *buffer;
	u32 len;
	u32 offset;
} dvdcmd2;

static u32 *__dvd_idtmp = NULL;

static u32 __dvd_initflag = 0;
static u32 __dvd_stopnextint = 0;
static u32 __dvd_resetoccured = 0;	
static u32 __dvd_waitcoverclose = 0;
static u32 __dvd_breaking = 0;
static u32 __dvd_lastlen;
static u32 __dvd_nextcmdnum;
static u32 __dvd_workaround;
static u32 __dvd_workaroundseek;
static u32 __dvd_lastcmdwasread;
static lwpq_t __dvd_wait_queue;
static sysalarm __dvd_timeoutalarm;
static DVDCallback __dvd_callback = NULL;

static u8 __dvd_buffer$15[48];
static dvdcmd __dvd_cmdlist[3];
static dvdcmd2 __dvd_prev,__dvd_curr;

static u8 __dvd_patchcode[] = {
	0x00,0x85,0x02,
	0x0c,0xea,0x00,0xfc,0xb4,0x00,0xf3,0xfe,0xc0,0xda,0xfc,0x00,0x02,
	0x0c,0xea,0x15,0xc8,0x0e,0xfc,0xd8,0x54,0xe9,0x03,0xfd,0x86,0x00,
	0x0c,0xc8,0x0e,0xfc,0xf4,0x74,0x77,0x0a,0x08,0xf0,0x00,0xfe,0xf5,
	0x0c,0xd8,0xda,0xfc,0xf5,0xd8,0x44,0xfc,0xf2,0x7c,0xd0,0x04,0xcc,
	0x0c,0x5b,0x80,0xd8,0x01,0xe9,0x02,0x7c,0x04,0xf4,0x75,0x42,0x85,
	0x0c,0x00,0x51,0x20,0xfe,0xf5,0x5e,0x00,0xf5,0x5f,0x04,0x5d,0x08,
	0x0c,0x5e,0x0c,0x82,0xf2,0xf3,0xf2,0x3e,0xd2,0x02,0xf4,0x75,0x24,
	0x0c,0x00,0x40,0x31,0xd9,0x00,0xe9,0x02,0x8a,0x08,0xf7,0x10,0xff,
	0x0c,0xf7,0x21,0xf4,0x79,0x00,0xf0,0x00,0xe9,0x0a,0x80,0x00,0xf5,
	0x0c,0x10,0x09,0x80,0x00,0xc0,0x20,0x82,0x21,0xd9,0x06,0xe9,0x06,
	0x0c,0x61,0x06,0xd5,0x06,0x41,0x06,0xf4,0x74,0x50,0xb1,0x08,0xf4,
	0x0c,0xc8,0xba,0x85,0x00,0xd8,0x29,0xe9,0x05,0xf4,0x74,0x33,0xae,
	0x0c,0x08,0xf7,0x48,0x84,0x00,0xe9,0x05,0xf4,0x74,0x80,0xae,0x08,
	0x0c,0xf0,0x00,0xc8,0x20,0x82,0xd4,0x01,0xc0,0x20,0x82,0xf7,0x48,
	0x0c,0x00,0x06,0xe9,0x0b,0x80,0x00,0xc0,0x20,0x82,0xf8,0x05,0x02,
	0x0c,0xc0,0x44,0x80,0xfe,0x00,0xd3,0xfc,0x0e,0xf4,0xc8,0x03,0x0a,
	0x0c,0x08,0xf4,0x44,0xba,0x85,0x00,0xf4,0x74,0x07,0x85,0x00,0xf7,
	0x0c,0x20,0x4c,0x80,0xf4,0xc0,0x16,0xeb,0x40,0xf7,0x00,0xff,0xef,
	0x0c,0xfd,0xd0,0x00,0xf8,0x0d,0x7f,0xfd,0x97,0x00,0xf8,0x0e,0x7f,
	0x0c,0xfd,0x91,0x00,0xf8,0x0b,0x7f,0xfd,0x8b,0x00,0xf8,0x00,0x01,
	0x0c,0xfd,0x73,0x00,0xf8,0x0b,0x7f,0xfd,0x7f,0x00,0xf8,0x0a,0x7f,
	0x0c,0xfd,0x79,0x00,0xf8,0x0d,0x7f,0xfd,0x73,0x00,0xf8,0x80,0x00,
	0x0c,0xfd,0x5b,0x00,0xf4,0xc0,0x16,0xeb,0x40,0xf7,0x40,0x00,0x10,
	0x0c,0xfd,0x94,0x00,0x80,0x0c,0xf4,0xca,0xba,0x85,0x00,0xdc,0x8e,
	0x0c,0x81,0xda,0x29,0xe9,0x03,0xdc,0x9a,0x81,0xf7,0x4a,0x84,0x00,
	0x0c,0xe9,0x03,0xdc,0x96,0x81,0x10,0xf4,0x70,0x30,0xb1,0x00,0xf4,
	0x0c,0xca,0xba,0x85,0x00,0xda,0x29,0xe9,0x05,0xf4,0x70,0x13,0xae,
	0x0c,0x00,0xf7,0x4a,0x84,0x00,0xe9,0x05,0xf4,0x70,0x60,0xae,0x00,
	0x0c,0xc0,0xd2,0xfc,0x80,0x08,0xc0,0xd4,0xfc,0x80,0x0c,0xc4,0xda,
	0x0c,0xfc,0x80,0x00,0xc0,0x20,0x82,0x2e,0xd3,0x04,0xfe,0xf4,0x71,
	0x0c,0xff,0xff,0xff,0xf7,0x1d,0x01,0x00,0xe9,0xfa,0xf7,0x1c,0x01,
	0x0c,0x00,0xe9,0xef,0xfe,0xd3,0xfc,0xf9,0x00,0x10,0x0d,0x4c,0x02,
	0x0c,0xf2,0x7c,0x80,0x02,0xf4,0xca,0xba,0x85,0x00,0xf7,0x4a,0xc9,
	0x0c,0x00,0xe9,0x05,0xf4,0xe1,0x90,0x2a,0x08,0xda,0x29,0xe9,0x05,
	0x0c,0xf4,0xe1,0x6a,0x27,0x08,0xf7,0x4a,0x84,0x00,0xe9,0x05,0xf4,
	0x0c,0xe1,0xac,0x27,0x08,0xd3,0x04,0xfe,0xf4,0x40,0x16,0xeb,0x40,
	0x0c,0xa5,0xf4,0xca,0xba,0x85,0x00,0xf7,0x4a,0xc9,0x00,0xe9,0x05,
	0x0c,0xf4,0xe1,0x0d,0x49,0x08,0xda,0x29,0xe9,0x05,0xf4,0xe1,0x7e,
	0x0c,0x49,0x08,0xf7,0x4a,0x84,0x00,0xe9,0x05,0xf4,0xe1,0x6e,0x49,
	0x06,0x08,0xfe,0xfe,0x74,0x08,0x06,
	0x00,0x80,0x4c,
	0x03,0x02,0x85,0x00,
	0x63
};

static vu32* const _piReg = (u32*)0xCC003000;
static vu32* const _diReg = (u32*)0xCC006000;

static u8 __dvd_unlockcmd$221[12] = {0xff,0x01,'M','A','T','S','H','I','T','A',0x02,0x00};
static u8 __dvd_unlockcmd$222[12] = {0xff,0x00,'D','V','D','-','G','A','M','E',0x03,0x00};

extern void udelay(int us);

extern void __MaskIrq(u32);
extern void __UnmaskIrq(u32);

static void __dvd_timeouthandler(sysalarm *alarm)
{
	DVDCallback cb;

	__MaskIrq(IRQMASK(IRQ_PI_DI));
	cb = __dvd_callback;
	if(cb) cb(0x10);
}

static void __SetupTimeoutAlarm(const struct timespec *tp)
{
	SYS_CreateAlarm(&__dvd_timeoutalarm);
	SYS_SetAlarm(&__dvd_timeoutalarm,tp,__dvd_timeouthandler);
}

static void __Read(void *buffer,u32 len,u32 offset,DVDCallback cb)
{
	u32 val;
	struct timespec tb;
	
	__dvd_callback = cb;
	__dvd_stopnextint = 0;
	__dvd_lastcmdwasread = 1;
	
	_diReg[2] = DVD_READSECTOR;
	_diReg[3] = offset>>2;
	_diReg[4] = len;
	_diReg[5] = (u32)buffer;
	_diReg[6] = len;

	__dvd_lastlen = len;

	_diReg[7] = (DVD_DI_DMA|DVD_DI_START);
	val = _diReg[7];
	if(val>0x00a00000) {
		tb.tv_sec = 20;
		tb.tv_nsec = 0;
		__SetupTimeoutAlarm(&tb);
	} else {
		tb.tv_sec = 10;
		tb.tv_nsec = 0;
		__SetupTimeoutAlarm(&tb);
	}
}

static void __DoRead(void *buffer,u32 len,u32 offset,DVDCallback cb)
{
	__dvd_nextcmdnum = 0;
	__dvd_cmdlist[0].cmd = -1;
	__Read(buffer,len,offset,cb);
}

static u32 __ProcessNextCmd()
{
	u32 cmd_num;
	
	cmd_num = __dvd_nextcmdnum;
	if(__dvd_cmdlist[cmd_num].cmd==0x0001) {
		__dvd_nextcmdnum++;
		__Read(__dvd_cmdlist[cmd_num].buffer,__dvd_cmdlist[cmd_num].len,__dvd_cmdlist[cmd_num].offset,__dvd_cmdlist[cmd_num].cb);
		return 1;
	}

	if(__dvd_cmdlist[cmd_num].cmd==0x0002) {
		__dvd_nextcmdnum++;
		return 1;
	}
	return 0;
}

static void __DVDLowWATypeSet(u32 workaround,u32 workaroundseek)
{
	u32 level;

	_CPU_ISR_Disable(level);
	__dvd_workaround = workaround;
	__dvd_workaroundseek = workaroundseek;
	_CPU_ISR_Restore(level);
}

static void __DVDInitWA()
{
	__dvd_nextcmdnum = 0;
	__DVDLowWATypeSet(0,0);	
}

static void __DVDInterruptHandler(u32 nIrq,void *pCtx)
{
	u32 status,ir,irm,irmm;
	DVDCallback cb;

	SYS_CancelAlarm(&__dvd_timeoutalarm);

	irmm = 0;
	if(__dvd_lastcmdwasread) {
		__dvd_prev.buffer = __dvd_curr.buffer;
		__dvd_prev.len = __dvd_curr.len;
		__dvd_prev.offset = __dvd_curr.offset;
		if(__dvd_stopnextint) irmm |= 0x0008;		
	}
	__dvd_lastcmdwasread = 0;
	__dvd_stopnextint = 0;

	status = _diReg[0];
	irm = (status&(DVD_DE_MSK|DVD_TC_MSK|DVD_BRK_MSK));
	ir = ((status&(DVD_DE_INT|DVD_TC_INT|DVD_BRK_INT))&(irm<<1));


	if(ir&DVD_BRK_INT) irmm |= 0x0008;
	if(ir&DVD_TC_INT) irmm |= 0x0001;
	if(ir&DVD_DE_INT) irmm |= 0x0002;

	if(irmm) __dvd_resetoccured = 0;
	
	_diReg[0] = (ir|irm);

	if(__dvd_resetoccured) {
	}

	if(__dvd_waitcoverclose) {
		status = _diReg[1];
		irm = status&DVD_CVR_MSK;
		ir = (status&DVD_CVR_INT)&(irm<<1);
		if(ir) irmm |= 0x0004;
	}

	_diReg[1] = 0;
	
	if(irmm&0x0008) {
		if(!__dvd_breaking) irmm &= ~0x0008;
	}

	if(irmm&0x0001) {
		if(__ProcessNextCmd()) return;
	} else {
		__dvd_cmdlist[0].cmd = -1;
		__dvd_nextcmdnum = 0;
	}

	cb = __dvd_callback;
	__dvd_callback = NULL;
	if(cb) cb(irmm);

	__dvd_breaking = 0;
}

static void __DVDUnlockDrive()
{
	u32 i;

	_diReg[0] |= (DVD_TC_INT|DVD_DE_INT);
	_diReg[1] = 0;

	for(i=0;i<3;i++) _diReg[2+i] = ((u32*)__dvd_unlockcmd$221)[i];
	_diReg[7] = DVD_DI_START;
	while(!(_diReg[0]&(DVD_DE_INT|DVD_TC_INT)));
	
	for(i=0;i<3;i++) _diReg[2+i] = ((u32*)__dvd_unlockcmd$222)[i];
	_diReg[7] = DVD_DI_START;
	while(!(_diReg[0]&(DVD_DE_INT|DVD_TC_INT)));
}

static void __DVDPatchDriveCode()
{
	u32 i,nPos,cmd;
	u32 drv_address;

	nPos=0;
	drv_address = 0;
	while(1) {
		cmd = __dvd_patchcode[nPos++];
		if(!cmd) {
			drv_address = _SHIFTL(__dvd_patchcode[nPos],8,8)|__dvd_patchcode[nPos+1];
			nPos += 2;
			continue;
		} else if(cmd==0x63) 
			break;
		
		_diReg[0] |= (DVD_TC_INT|DVD_DE_INT);
		_diReg[1] = 0;
		_diReg[2] = 0xfe010100;
		_diReg[3] = drv_address;
		_diReg[4] = _SHIFTL(cmd,16,16);
		_diReg[7] = (DVD_DI_DMA|DVD_DI_START);
		
		while(!(_diReg[0]&(DVD_DE_INT|DVD_TC_INT)));

		i = 0;
		_diReg[0] |= (DVD_TC_INT|DVD_DE_INT);
		_diReg[1] = 0;
		while(i<cmd/sizeof(u32)) {
			_diReg[2+i]	= ((u32*)&(__dvd_patchcode[nPos]))[i];
			i++;
		}
		_diReg[7] = (DVD_DI_DMA|DVD_DI_START);
		
		while(!(_diReg[0]&(DVD_DE_INT|DVD_TC_INT)));

		drv_address += cmd;
		nPos += cmd;
	}
}

static void __fst_cb(u32 result)
{
}

static void __DVDInitFST()
{
	u32 idtmp;

	__dvd_idtmp = &idtmp;
	
	DVD_Reset();

	__DVDUnlockDrive();
	__DVDPatchDriveCode();
	
	DVD_ReadId(__dvd_buffer$15,__dvd_idtmp,__fst_cb);
}

s32 DVD_LowReadId(void *buffer,DVDCallback cb)
{
	struct timespec tb;

	__dvd_callback = cb;
	__dvd_stopnextint = 0;
	
	_diReg[2] = DVD_READDISKID;
	_diReg[3] = 0;
	_diReg[4] = DVD_DISKIDSIZE;
	_diReg[5] = (u32)buffer;
	_diReg[6] = DVD_DISKIDSIZE;
	_diReg[7] = (DVD_DI_DMA|DVD_DI_START);

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);
	
	return 1;
}


void DVD_LowReset()
{
	u32 val;
	
	_diReg[1] = DVD_CVR_MSK;
	val = _piReg[9];
	_piReg[9] = ((val&~0x0004)|0x0001);

	udelay(12);

	val |= 0x0004;
	val |= 0x0001;
	_piReg[9] = val;
}


s32 DVD_ReadId(void *block,u32 *id,DVDCallback cb)
{
	u32 *iblock = (u32*)block;

	iblock[2] = 5;
	
	return 0;
}

s32 DVD_Read(void *pDst,s32 nLen,u32 nOffset)
{
	vs32 *pDI = (vs32*)0xCC006000;
	
	pDI[0] = 0x2E;
	pDI[1] = 0;
	pDI[2] = 0xA8000000;
	pDI[3] = nOffset>>2;
	pDI[4] = nLen;
	pDI[5] = (u32)pDst;
	pDI[6] = nLen;
	pDI[7] = 3;
	
	if((((s32)pDst)&0xC0000000)==0x80000000) DCInvalidateRange(pDst,nLen);
	while(1) {
		if(pDI[0]&0x04)
			return 0;
		if(!pDI[6])
			return 1;
	}
}

void DVD_Init()
{
	if(__dvd_initflag) return;
	__dvd_initflag = 1;

	__DVDInitWA();

	IRQ_Request(IRQ_PI_DI,__DVDInterruptHandler,NULL);
	__UnmaskIrq(IRQMASK(IRQ_PI_DI));

	LWP_InitQueue(&__dvd_wait_queue);

	__DVDInitFST();
}

void DVD_Reset()
{
	DVD_LowReset();

	_diReg[0] = (DVD_DE_MSK|DVD_TC_MSK|DVD_BRK_MSK);
	_diReg[1] = _diReg[1];
}
