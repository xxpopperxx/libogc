#include <config.h>
#include <_ansi.h>
#include <_syslist.h>
#include <stdio.h>
#ifdef REENTRANT_SYSCALLS_PROVIDED
#include <reent.h>
#endif
#include <errno.h>
#undef errno
extern int errno;

#include "iosupp.h"

#ifdef REENTRANT_SYSCALLS_PROVIDED
int _DEFUN(_close_r,(ptr,fildes),
		   struct reent *ptr _AND
           int fildes)
{
	int ret = -1;
	unsigned int dev = 0;
	unsigned int fd = -1;

	if(fildes!=-1) {
		dev = fildes;
		if(fildes&0xf000) {
			dev = _SHIFTR(fildes,12,4);
			fd = fildes&0x0fff;
		}
		if(devoptab_list[dev]->close_r)
			ret = devoptab_list[dev]->close_r(ptr,fd);
	}
	return ret;
}
#else
int _DEFUN(close,(fildes),
           int fildes)
{
	int ret = -1;
	unsigned int dev = 0;
	unsigned int fd = -1;

	printf("close(%04x)\n",fildes);
	
	if(fildes!=-1) {
		dev = fildes;
		if(fildes&0xf000) {
			dev = _SHIFTR(fildes,12,4);
			fd = fildes&0x0fff;
		}

		if(devoptab_list[dev]->close_r)
			ret = devoptab_list[dev]->close_r(0,fd);
	}
	printf("close(%d)\n",ret);
	return ret;
}
#endif
