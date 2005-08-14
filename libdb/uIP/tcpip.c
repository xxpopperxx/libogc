#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#include "uip_ip.h"
#include "uip_arp.h"
#include "uip_tcp.h"
#include "uip_netif.h"
#include "bba.h"
#include "tcpip.h"
#include "memb.h"

#if UIP_LOGGING == 1
#include <stdio.h>
#define UIP_LOG(m) uip_log(__FILE__,__LINE__,m)
#else
#define UIP_LOG(m)
#endif /* UIP_LOGGING == 1 */

#if UIP_STATISTICS == 1
struct uip_stats uip_stat;
#define UIP_STAT(s) s
#else
#define UIP_STAT(s)
#endif /* UIP_STATISTICS == 1 */


struct tcpip_sock {
	struct tcpip_sock *next;
	struct uip_tcp_pcb *pcb;
	struct uip_pbuf *lastdata;
	s32 lastoffset,recvevt,sendevt,flags;
	s32 err,socket;
} tcpip_socks[UIP_TCPIP_SOCKS];

static s64 tcpip_time = 0;
static struct tcpip_sock *tcpip_accepted_sockets = NULL;

extern s64 gettime();
extern u32 diff_msec(s64 start,s64 end);

static s32_t tcpip_allocsocket(struct uip_tcp_pcb *pcb)
{
	s32_t i;

	for(i=0;i<UIP_TCPIP_SOCKS;i++) {
		if(tcpip_socks[i].pcb==NULL) {
			tcpip_socks[i].pcb = pcb;
			tcpip_socks[i].lastdata = NULL;
			tcpip_socks[i].lastoffset = 0;
			tcpip_socks[i].recvevt = 0;
			tcpip_socks[i].sendevt = 1;
			tcpip_socks[i].flags = 0;
			tcpip_socks[i].socket = i;
			tcpip_socks[i].err = UIP_ERR_OK;
			return i;
		}
	}
	return -1;
}

static struct tcpip_sock* tcpip_getsocket(s32_t s)
{
	struct tcpip_sock *sock;

	if(s<0 || s>=UIP_TCPIP_SOCKS) return NULL;

	sock = &tcpip_socks[s];
	if(!sock->pcb) return NULL;

	return sock;
}


static void tcpip_err(void *arg,s8_t err)
{
//	printf("tcpip_err: err_code(%d)\n",err);
}

static s8_t tcpip_poll(void *arg,struct uip_tcp_pcb *pcb)
{
	UIP_LOG("tcpip_poll()");
	return UIP_ERR_OK;
}

static s8_t tcpip_sent(void *arg,struct uip_tcp_pcb *pcb,u16_t space)
{
//	printf("tcpip_sent(%d)\n",space);
	return UIP_ERR_OK;
}

static s8_t tcpip_recved(void *arg,struct uip_tcp_pcb *pcb,struct uip_pbuf *p,s8_t err)
{
	u16_t len;
	struct tcpip_sock *sock = (struct tcpip_sock*)arg;

//	printf("tcpip_recved(%s (%d/%d))\n",(u8_t*)p->payload,p->len,p->tot_len);
	if(!sock) {
		uip_pbuf_free(p);
		return UIP_ERR_VAL;
	}

	if(p) {
		if(sock->lastdata==NULL) {
			sock->lastdata = p;
		} else {
			uip_pbuf_chain(sock->lastdata,p);
		}
		len = p->tot_len;
	} else
		len = 1;

	uip_tcp_recved(pcb,len);

	return UIP_ERR_OK;
}


static s8_t tcpip_accept_func(void *arg,struct uip_tcp_pcb *newpcb,s8_t err)
{
	s32_t s;
	struct tcpip_sock *ptr,*newsock = NULL;
	struct tcpip_sock *sock = (struct tcpip_sock*)arg;

	UIP_LOG("tcpip_accept_func()");
	if(!sock) return UIP_ERR_ABRT;

	s = tcpip_allocsocket(newpcb);
	if(s<0) {
		uip_tcp_close(newpcb);
		return UIP_ERR_ABRT;
	}
	
	newsock = tcpip_getsocket(s);
	
	ptr = tcpip_accepted_sockets;
	while(ptr && ptr->next) ptr = ptr->next;
	if(!ptr) tcpip_accepted_sockets = newsock;
	else ptr->next = newsock;
	
	uip_tcp_arg(newpcb,newsock);
	uip_tcp_recv(newpcb,tcpip_recved);
	uip_tcp_sent(newpcb,tcpip_sent);
	uip_tcp_err(newpcb,tcpip_err);
	uip_tcp_poll(newpcb,tcpip_poll,4);
	
	return UIP_ERR_OK;
}

static void __tcpip_poll()
{
	s64 now;

	if(uip_netif_default==NULL) return;

	bba_poll(uip_netif_default);
	
	if(tcpip_time && (uip_tcp_active_pcbs || uip_tcp_tw_pcbs)) {
		now = gettime();
		if(diff_msec(tcpip_time,now)>=UIP_TCP_TMR_INTERVAL) {
			uip_tcp_tmr();
			tcpip_time = gettime();
		}
	} else
		tcpip_time = 0;
}

void tcpip_tmr_needed()
{
	if(!tcpip_time && (uip_tcp_active_pcbs || uip_tcp_tw_pcbs)) {
		tcpip_time = gettime();
	}
}

void tcpip_init()
{
	uip_tcp_init();
	memset(tcpip_socks,0,(UIP_TCPIP_SOCKS*sizeof(struct tcpip_sock)));
}

s32_t tcpip_socket()
{
	s32_t s;
	struct tcpip_sock *sock;
	struct uip_tcp_pcb *pcb;

	pcb = uip_tcp_new();
	if(!pcb) return -1;

	s = tcpip_allocsocket(pcb);
	if(s<0) {
		uip_tcp_close(pcb);
		return -1;
	}
	
	sock = tcpip_getsocket(s);
	uip_tcp_arg(pcb,sock);
	
	return s;
}

s32_t tcpip_bind(s32_t s,struct sockaddr *name,socklen_t *namelen)
{
	struct tcpip_sock *sock;
	struct uip_ip_addr local_ip;
	u16_t local_port;
	s8_t err;
	
	sock = tcpip_getsocket(s);
	if(!sock) return -1;
	
	local_ip.addr = ((struct sockaddr_in*)name)->sin_addr.s_addr;
	local_port = ((struct sockaddr_in*)name)->sin_port;

	err = uip_tcp_bind(sock->pcb,&local_ip,local_port);
	
	return (s32_t)err;
}

s32_t tcpip_listen(s32_t s,u32_t backlog)
{
	struct tcpip_sock *sock;

	sock = tcpip_getsocket(s);
	if(!sock) return -1;

	sock->pcb = uip_tcp_listen(sock->pcb);
	if(sock->pcb==NULL) return -1;

	uip_tcp_accept(sock->pcb,tcpip_accept_func);

	return 0;
}

s32_t tcpip_accept(s32_t s)
{
	s32_t newsock = -1;
	struct tcpip_sock *sock;

	sock = tcpip_getsocket(s);
	if(sock==NULL) return -1;
	
	do {
		__tcpip_poll();
	} while(!tcpip_accepted_sockets);

	newsock = tcpip_accepted_sockets->socket;
	tcpip_accepted_sockets = tcpip_accepted_sockets->next;

	return newsock;
}

s32_t tcpip_read(s32_t s,void *buffer,u32_t len)
{
	u32_t off,copy,i;
	u8_t *ptr;
	struct uip_pbuf *p;
	struct tcpip_sock *sock;

	sock = tcpip_getsocket(s);
	if(!sock) return -1;
	
	do {
		__tcpip_poll();
	} while(sock->lastdata==NULL);
	
	if(sock->lastdata) {
		off = 0;
		ptr = buffer;
		while(len>0 && sock->lastdata) {
			p = sock->lastdata;

			if(len>p->len-sock->lastoffset) copy = (p->len-sock->lastoffset);
			else copy = len;
		
			for(i=0;i<copy;i++) ptr[off+i] = ((u8_t*)p->payload)[sock->lastoffset+i];

			off += copy;
			len -= copy;
			sock->lastoffset += copy;

			if(sock->lastoffset>=p->len) {
				sock->lastoffset = 0;
				sock->lastdata = uip_pbuf_dechain(p);
				uip_pbuf_free(p);
			}
		}
		return off;
	}
	return -1;
}

s32_t tcpip_write(s32_t s,const void *buffer,u32_t len)
{
	s8_t err;
	u16_t snd_buf,copy;
	struct tcpip_sock *sock;

	sock = tcpip_getsocket(s);
	if(!sock) return -1;

	while(len>0) {
		while((snd_buf=uip_tcp_sndbuf(sock->pcb))==0) {
			__tcpip_poll();
		}
		
		if(len>snd_buf) copy = snd_buf;
		else copy = len;
		
		err = uip_tcp_write(sock->pcb,buffer,copy,1);
		if(err==UIP_ERR_OK && (!sock->pcb->unacked || sock->pcb->flags&UIP_TF_NODELAY))
			uip_tcpoutput(sock->pcb);
		
		__tcpip_poll();

		buffer = buffer+copy;
		len -= copy;
	}
	return UIP_ERR_OK;
}

void tcpip_close(s32_t s)
{
	struct tcpip_sock *sock;

	sock = tcpip_getsocket(s);
	if(sock==NULL) return;

	uip_tcp_close(sock->pcb);
	sock->pcb = NULL;
}
