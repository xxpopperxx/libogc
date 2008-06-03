/*-------------------------------------------------------------

network_wii.c -- Wii network subsystem

Copyright (C) 2008 bushing

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
distribution.

-------------------------------------------------------------*/

#if defined(HW_RVL)


#define MAX_IP_RETRIES		100
#define MAX_INIT_RETRIES	20

//#define DEBUG_NET

#ifdef DEBUG_NET
#define debug_printf(fmt, args...) \
	fprintf(stderr, "%s:%d:" fmt, __FUNCTION__, __LINE__, ##args)
#else
#define debug_printf(fmt, args...)
#endif // DEBUG_NET

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>

#include "ipc.h"
#include "processor.h"
#include "network.h"
#include "ogcsys.h"

#define NET_HEAP_SIZE				8192

#define IOS_O_NONBLOCK				(O_NONBLOCK >> 16)

#define IOCTL_NWC24_STARTUP			0x06

#define IOCTL_NCD_SETIFCONFIG3		0x03
#define IOCTL_NCD_SETIFCONFIG4		0x04
#define IOCTL_NCD_GETLINKSTATUS		0x07

#define NET_UNKNOWN_ERROR_OFFSET	-10000

enum {
	IOCTL_SO_ACCEPT	= 1,
	IOCTL_SO_BIND,   
	IOCTL_SO_CLOSE,	
	IOCTL_SO_CONNECT, 
	IOCTL_SO_FCNTL,
	IOCTL_SO_GETPEERNAME, // todo
	IOCTL_SO_GETSOCKNAME, // todo
	IOCTL_SO_GETSOCKOPT,  // todo    8
	IOCTL_SO_SETSOCKOPT,  
	IOCTL_SO_LISTEN,
	IOCTL_SO_POLL,        // todo    b
	IOCTLV_SO_RECVFROM,
	IOCTLV_SO_SENDTO,
	IOCTL_SO_SHUTDOWN,    // todo    e
	IOCTL_SO_SOCKET,
	IOCTL_SO_GETHOSTID,
	IOCTL_SO_GETHOSTBYNAME,
	IOCTL_SO_GETHOSTBYADDR,// todo
	IOCTLV_SO_GETNAMEINFO, // todo   13
	IOCTL_SO_UNK14,        // todo
	IOCTL_SO_INETATON,     // todo
	IOCTL_SO_INETPTON,     // todo
	IOCTL_SO_INETNTOP,     // todo
	IOCTLV_SO_GETADDRINFO, // todo
	IOCTL_SO_SOCKATMARK,   // todo
	IOCTLV_SO_UNK1A,       // todo
	IOCTLV_SO_UNK1B,       // todo
	IOCTLV_SO_GETINTERFACEOPT, // todo
	IOCTLV_SO_SETINTERFACEOPT, // todo
	IOCTL_SO_SETINTERFACE,     // todo
	IOCTL_SO_STARTUP,           // 0x1f
	IOCTL_SO_ICMPSOCKET =	0x30, // todo
	IOCTLV_SO_ICMPPING,         // todo
	IOCTL_SO_ICMPCANCEL,        // todo
	IOCTL_SO_ICMPCLOSE          // todo
};

struct bind_params {
	u32 socket;
	u32 has_name;
	u8 name[28];
};

struct connect_params {
	u32 socket;
	u32 has_addr;
	u8 addr[28];
};

struct sendto_params {
	u32 socket;
	u32 flags;
	u32 has_destaddr;
	u8 destaddr[28];
}; 

struct setsockopt_params {
	u32 socket;
	u32 level;
	u32 optname;
	u32 optlen;
	u8 optval[20];
};

// 0 means we don't know what this error code means
// I sense a pattern here...
static u8 _net_error_code_map[] = {
	0, // 0
	0, 
	0, 
	0,
	0, 
	0, // 5
	EAGAIN, 
	EALREADY,
	EBADFD,
	0,
	0, // 10
	0,
	0,
	0,
	0,
	0, // 15
	0,
	0,
	0,
	0,
	0, // 20
	0,
	0,
	0,
	0,
	0, // 25
	EINPROGRESS,
	0,
	0,
	0,
	EISCONN, // 30
	0,
	0,
	0,
	0,
	0, // 35
	0,
	0,
	0,
	ENETDOWN, //?
	0, // 40
	0,
	0,
	0,
	0,
	0, // 45
	0,
	0,
	0,
	0,
	0, // 50
	0,
	0,
	0,
	0,
	0, // 55
	0,
	0,
	0,
	0,
	0, // 60
};

static s32 net_ip_top_fd = -1;
static s32 __net_hid = -1;
static char __manage_fs[] ATTRIBUTE_ALIGN(32) = "/dev/net/ncd/manage";
static char __iptop_fs[] ATTRIBUTE_ALIGN(32) = "/dev/net/ip/top";
static char __kd_fs[] ATTRIBUTE_ALIGN(32) = "/dev/net/kd/request";

static s32 _net_convert_error(s32 ios_retval)
{
//	return ios_retval;
	if (ios_retval >= 0) return ios_retval;
	if (ios_retval < -sizeof(_net_error_code_map)
		|| !_net_error_code_map[-ios_retval])
			return NET_UNKNOWN_ERROR_OFFSET + ios_retval;
	return -_net_error_code_map[-ios_retval];
}

static s32 _open_manage_fd(void)
{
	s32 ncd_fd;

	do {
		ncd_fd = _net_convert_error(IOS_Open(__manage_fs, 0));
		if (ncd_fd < 0) usleep(100000);
	} while(ncd_fd == IPC_ENOENT);

	return ncd_fd;
}

s32 NCDGetLinkStatus(void) 
{
	s32 ret, ncd_fd;
	STACK_ALIGN(u8, linkinfo, 0x20, 32);
  
	ncd_fd = _open_manage_fd();
	if (ncd_fd < 0) return ncd_fd;
	
	ret = _net_convert_error(IOS_IoctlvFormat(__net_hid, ncd_fd, IOCTL_NCD_GETLINKSTATUS, ":d", linkinfo, 0x20));
	IOS_Close(ncd_fd);

  	if (ret < 0) debug_printf("NCDGetLinkStatus returned error %d\n", ret);

	return ret;
}

static s32 NWC24iStartupSocket(void)
{
	s32 kd_fd, ret;
	STACK_ALIGN(u8, kd_buf, 0x20, 32);
	
	kd_fd = _net_convert_error(IOS_Open(__kd_fs, 0));
	if (kd_fd < 0) {
		debug_printf("IOS_Open(%s) failed with code %d\n", __kd_fs, kd_fd);
		return kd_fd;
	}
  
	ret = _net_convert_error(IOS_Ioctl(kd_fd, IOCTL_NWC24_STARTUP, NULL, 0, kd_buf, 0x20));
	if (ret < 0) debug_printf("IOS_Ioctl(6)=%d\n", ret);
  	IOS_Close(kd_fd);
  	return ret;
}

u32 net_gethostip(void)
{
	u32 ip_addr=0;
	int retries;

	if (net_ip_top_fd < 0) return 0;
	for (retries=0, ip_addr=0; !ip_addr && retries < MAX_IP_RETRIES; retries++) {
		ip_addr = IOS_Ioctl(net_ip_top_fd, IOCTL_SO_GETHOSTID, 0, 0, 0, 0);
		debug_printf("."); fflush(stdout);
		if (!ip_addr) usleep(100000);
	}

	return ip_addr;
}

s32 net_init(void)
{
	s32 ret;
	u32 ip_addr = 0;

	if (net_ip_top_fd >= 0) return 0;
	
	ret = NCDGetLinkStatus();  // this must be called as part of initialization
	if (ret < 0) {
		debug_printf("NCDGetLinkStatus returned %d\n", ret);
		return ret;
	}
	
	if (__net_hid == -1) __net_hid = iosCreateHeap(NET_HEAP_SIZE);
	if (__net_hid < 0) return __net_hid;
	
	net_ip_top_fd = _net_convert_error(IOS_Open(__iptop_fs, 0));
	if (net_ip_top_fd < 0) {
		debug_printf("IOS_Open(/dev/net/ip/top)=%d\n", net_ip_top_fd);
		ret = net_ip_top_fd;
		goto done;
	}

	ret = NWC24iStartupSocket(); // this must also be called during init
	if (ret < 0) {
		debug_printf("NWC24iStartupSocket returned %d\n", ret);
		goto done;
	}

	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_STARTUP, 0, 0, 0, 0));
	if (ret < 0) {
		debug_printf("IOCTL_SO_STARTUP returned %d\n", ret);
		goto done;
	}

#ifdef DEBUG_NET
	u8 *octets = (u8 *) &ip_addr;
#endif
	debug_printf("Getting IP Address"); fflush(stdout);

	ip_addr=net_gethostip();

	if (!ip_addr) {
		debug_printf("Unable to obtain IP address\n");
		ret = -ETIMEDOUT;
		goto done;
	}

	debug_printf(" %d.%d.%d.%d\n", octets[0], octets[1], octets[2], octets[3]);

done:
	return ret;	
}


/* Returned value is a static buffer -- this function is not threadsafe! */
struct hostent * net_gethostbyname(char *addrString)
{
	s32 ret, len, i;
	u8 *params;
	struct hostent *ipData;
	u32 addrOffset;
	static u8 ipBuffer[0x460] ATTRIBUTE_ALIGN(32);

	memset(ipBuffer, 0, 0x460);

	if (net_ip_top_fd < 0) {
		errno = -ENXIO;
		return NULL;
	}

	len = strlen(addrString) + 1;
	params = iosAlloc(__net_hid, len);
	if (params==NULL) {
		errno = IPC_ENOMEM;
		return NULL;
	}

	memcpy(params, addrString, len);

	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_GETHOSTBYNAME, params, len, ipBuffer, 0x460));

	if(params!=NULL) iosFree(__net_hid, params);

	if (ret < 0) {
		errno = ret;
		return NULL;
	}

	ipData = ((struct hostent*)ipBuffer);
	addrOffset = (u32)MEM_PHYSICAL_TO_K0(ipData->h_name) - ((u32)ipBuffer + 0x10);

	ipData->h_name = MEM_PHYSICAL_TO_K0(ipData->h_name) - addrOffset;
	ipData->h_aliases = MEM_PHYSICAL_TO_K0(ipData->h_aliases) - addrOffset;

	for (i=0; (i < 0x40) && (ipData->h_aliases[i] != 0); i++) {
		ipData->h_aliases[i] = MEM_PHYSICAL_TO_K0(ipData->h_aliases[i]) - addrOffset;
	}

	ipData->h_addr_list = MEM_PHYSICAL_TO_K0(ipData->h_addr_list) - addrOffset;

	for (i=0; (i < 0x40) && (ipData->h_addr_list[i] != 0); i++) {
		ipData->h_addr_list[i] = MEM_PHYSICAL_TO_K0(ipData->h_addr_list[i]) - addrOffset;
	}

	errno = 0;
	return ipData;
}

s32 net_socket(u32 domain, u32 type, u32 protocol)
{
	s32 ret;
	STACK_ALIGN(u32, params, 3, 32);

	if (net_ip_top_fd < 0) return -ENXIO;
 
	params[0] = domain;
	params[1] = type;
	params[2] = protocol;

	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_SOCKET, params, 12, NULL, 0));
	debug_printf("net_socket(%d, %d, %d)=%d\n", domain, type, protocol, ret);
	return ret;		
}

s32 net_shutdown(s32 s, u32 how)
{
	s32 ret;
	STACK_ALIGN(u32, params, 2, 32);

	if (net_ip_top_fd < 0) return -ENXIO;

	params[0] = s;
	params[1] = how;
	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_SHUTDOWN, params, 8, NULL, 0));

	debug_printf("net_shutdown(%d, %d)=%d\n", s, how, ret);
	return ret;             
}

s32 net_bind(s32 s, struct sockaddr *name, socklen_t namelen)
{
	s32 ret;
	STACK_ALIGN(struct bind_params,params,1,32);

	if (net_ip_top_fd < 0) return -ENXIO;
	if (name->sa_family != AF_INET) return -EAFNOSUPPORT;

	name->sa_len = 8;

	memset(params, 0, sizeof(struct bind_params));
	params->socket = s;
	params->has_name = 1;
	memcpy(params->name, name, 8);

	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_BIND, params, sizeof (struct bind_params), NULL, 0));
	debug_printf("net_bind(%d, %p)=%d\n", s, name, ret);

	return ret;
}

s32 net_listen(s32 s, u32 backlog)
{
	s32 ret;
	STACK_ALIGN(u32, params, 2, 32);

	if (net_ip_top_fd < 0) return -ENXIO;

	params[0] = s;
	params[1] = backlog;

	debug_printf("calling ios_ioctl(%d, %d, %p, %d)\n", net_ip_top_fd, IOCTL_SO_SOCKET, params, 8);

	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_LISTEN, params, 8, NULL, 0));
  	debug_printf("net_listen(%d, %d)=%d\n", s, backlog, ret);
	return ret;	
}

s32 net_accept(s32 s, struct sockaddr *addr, socklen_t *addrlen)
{
	s32 ret;
	STACK_ALIGN(u32, _socket, 1, 32);

	debug_printf("net_accept()\n");

	if (net_ip_top_fd < 0) return -ENXIO;

	if (!addr) return -EINVAL;
	addr->sa_len = 8;
	addr->sa_family = AF_INET;

	if (!addrlen) return -EINVAL;

	if (*addrlen < 8) return -ENOMEM;

	*addrlen = 8;

	*_socket = s;
	debug_printf("calling ios_ioctl(%d, %d, %p, %d)\n", net_ip_top_fd, IOCTL_SO_ACCEPT, _socket, 4);
	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_ACCEPT, _socket, 4, addr, *addrlen));

	debug_printf("net_accept(%d, %p)=%d\n", s, addr, ret);
	return ret;
}

s32 net_connect(s32 s, struct sockaddr *addr, socklen_t addrlen)
{
	s32 ret;
	STACK_ALIGN(struct connect_params,params,1,32);
	
	if (net_ip_top_fd < 0) return -ENXIO;
	if (addr->sa_family != AF_INET) return -EAFNOSUPPORT;
	if (addrlen < 8) return -EINVAL;

	addr->sa_len = 8;

	memset(params, 0, sizeof(struct connect_params));
	params->socket = s;
	params->has_addr = 1;
	memcpy(&params->addr, addr, addrlen);

	ret = IOS_Ioctl(net_ip_top_fd, IOCTL_SO_CONNECT, params, sizeof(struct connect_params), NULL, 0);
	if (ret < 0) {
    	debug_printf("SOConnect(%d, %p)=%d\n", s, addr, ret);
	}

  	return ret;
}

s32 net_write(s32 s, const void *data, s32 size)
{
    return net_send(s, data, size, 0);
}

s32 net_send(s32 s, const void *data, s32 size, u32 flags)
{
	return net_sendto(s, data, size, flags, NULL, 0);
}

s32 net_sendto(s32 s, const void *data, s32 len, u32 flags, struct sockaddr *to, socklen_t tolen)
{
	s32 ret;
	u8 * message_buf = NULL;
	STACK_ALIGN(struct sendto_params,params,1,32);

	if (net_ip_top_fd < 0) return -ENXIO;
	if (tolen > 28) return -EOVERFLOW;

	message_buf = iosAlloc(__net_hid, len);
	if (message_buf == NULL) {
		debug_printf("net_send: failed to alloc %d bytes\n", len);
		return IPC_ENOMEM;
	}

	debug_printf("net_sendto(%d, %p, %d, %d, %p, %d)\n", s, data, len, flags, to, tolen);

	if (to && to->sa_len != tolen) {
		debug_printf("warning: to->sa_len was %d, setting to %d\n",	to->sa_len, tolen);
		to->sa_len = tolen;
	}
	
	memset(params, 0, sizeof(struct sendto_params));
	memcpy(message_buf, data, len);   // ensure message buf is aligned

	params->socket = s;  
	params->flags = flags;
	if (to) {
		params->has_destaddr = 1;
		memcpy(params->destaddr, to, to->sa_len);		
	} else {
		params->has_destaddr = 0;
	}

	ret = _net_convert_error(IOS_IoctlvFormat(__net_hid, net_ip_top_fd, IOCTLV_SO_SENDTO, "dd:", message_buf, len, params, sizeof(struct sendto_params)));
	debug_printf("net_send retuned %d\n", ret);

	return ret;
}

s32 net_recv(s32 s, void *mem, s32 len, u32 flags)
{
    return net_recvfrom(s, mem, len, 0, NULL, NULL);	
}

s32 net_recvfrom(s32 s, void *mem, s32 len, u32 flags, struct sockaddr *from, socklen_t *fromlen)
{
	s32 ret;
	u8* message_buf = NULL;
	STACK_ALIGN(u32, params, 2, 32);

	if (net_ip_top_fd < 0) return -ENXIO;
	if (len<=0) return -EINVAL;

	if (fromlen && from->sa_len != *fromlen) {
		debug_printf("warning: from->sa_len was %d, setting to %d\n",from->sa_len, *fromlen);
		from->sa_len = *fromlen;
	}
	
	message_buf = iosAlloc(__net_hid, len);
	if (message_buf == NULL) {
    	debug_printf("SORecv: failed to alloc %d bytes\n", len);
		return IPC_ENOMEM;
	}

	debug_printf("net_recvfrom(%d, '%s', %d, %d, %p, %d)\n", s, (char *)mem, len, flags, from, fromlen?*fromlen:0);

	memset(message_buf, 0, len);
	params[0] = s;
	params[1] = flags;

	ret = _net_convert_error(IOS_IoctlvFormat(__net_hid, net_ip_top_fd, IOCTLV_SO_RECVFROM,	"d:dd", params, 8, message_buf, len, from, (fromlen?*fromlen:0)));
	debug_printf("net_recvfrom returned %d\n", ret);

	if (ret > 0) {
		if (ret > len) {
			ret = -EOVERFLOW;
			goto done;
		}

		memcpy(mem, message_buf, ret);
	}

	if (fromlen && from) *fromlen = from->sa_len;
	
done:
	if(message_buf!=NULL) iosFree(__net_hid, message_buf);
	return ret;
}

s32 net_read(s32 s, void *mem, s32 len)
{
	return net_recvfrom(s, mem, len, 0, NULL, NULL);
}

s32 net_close(s32 s)
{
	s32 ret;
	STACK_ALIGN(u32, _socket, 1, 32);

	if (net_ip_top_fd < 0) return -ENXIO;

	*_socket = s;
	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_CLOSE, _socket, 4, NULL, 0));

	if (ret < 0) debug_printf("net_close(%d)=%d\n", s, ret);

	return ret;
}

s32 net_select(s32 maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)
{
	// not yet implemented
	return -EINVAL;
}

s32 net_setsockopt(s32 s, u32 level, u32 optname, const void *optval, socklen_t optlen)
{
	s32 ret;
	STACK_ALIGN(struct setsockopt_params,params,1,32);

	if (net_ip_top_fd < 0) return -ENXIO;
	if (optlen < 0 || optlen > 20) return -EINVAL;

	memset(params, 0, sizeof(struct setsockopt_params));
	params->socket = s;
	params->level = level;
	params->optname = optname;
	params->optlen = optlen;
	if (optval && optlen) memcpy (params->optval, optval, optlen);

	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_SETSOCKOPT, params, sizeof(struct setsockopt_params), NULL, 0));

	debug_printf("net_setsockopt(%d, %u, %u, %p, %d)=%d\n",	s, level, optname, optval, optlen, ret);
	return ret;
}

s32 net_ioctl(s32 s, u32 cmd, void *argp) 
{
	u32 flags;
	u32 *intp = (u32 *)argp;

	if (net_ip_top_fd < 0) return -ENXIO;
	if (!intp) return -EINVAL;

	switch (cmd) {
		case FIONBIO: 
			flags = net_fcntl(s, F_GETFL, 0);
			flags &= ~IOS_O_NONBLOCK;
			if (*intp) flags |= IOS_O_NONBLOCK;
			return net_fcntl(s, F_SETFL, flags);
		default:
			return -EINVAL;
	}
}

s32 net_fcntl(s32 s, u32 cmd, u32 flags)
{
	s32 ret;
	STACK_ALIGN(u32, params, 3, 32);

	if (net_ip_top_fd < 0) return -ENXIO;
	if (cmd != F_GETFL && cmd != F_SETFL) return -EINVAL;
	

	params[0] = s;
	params[1] = cmd;
	params[2] = flags;

	ret = _net_convert_error(IOS_Ioctl(net_ip_top_fd, IOCTL_SO_FCNTL, params, 12, NULL, 0));

	debug_printf("net_fcntl(%d, %d, %x)=%d\n", params[0], params[1], params[2], ret);

	return ret;
}


s32 if_config(char *local_ip, char *netmask, char *gateway,boolean use_dhcp)
{
	s32 i,ret;
	struct in_addr hostip;

	if ( use_dhcp != true ) return -EINVAL;
	
	for(i=0;i<MAX_INIT_RETRIES && (ret=net_init())==-EAGAIN;i++);
	if(ret<0) return ret;

	hostip.s_addr = net_gethostip();
	if ( local_ip!=NULL && hostip.s_addr ) {
		strcpy(local_ip, inet_ntoa(hostip));
	}
	
	return 0;
	
			
}
#endif /* defined(HW_RVL) */
