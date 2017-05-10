/* $Id: socket.c 1.1 1995/01/01 07:11:14 cthuang Exp $
 *
 * This module has been modified by Radim Kolar for OS/2 emx
 */

/***********************************************************************
  module:       socket.c
  program:      popclient
  SCCS ID:      @(#)socket.c    1.5  4/1/94
  programmer:   Virginia Tech Computing Center
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3 
  description:  UNIX sockets code.
 ***********************************************************************/
 
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

int set_sock_time(int fd, int read_sec, int write_sec)；

int Socket(const char *host, int clientPort)
{
    int sock;
    unsigned long inaddr;
    struct sockaddr_in ad;
    struct hostent *hp;
    
    memset(&ad, 0, sizeof(ad));
    ad.sin_family = AF_INET;

    inaddr = inet_addr(host);
    if (inaddr != INADDR_NONE)
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    else
    {
        hp = gethostbyname(host);
        if (hp == NULL)
            return -1;
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }
    ad.sin_port = htons(clientPort);
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    set_sock_time(sock,300,300);//五分钟的超时时间
    if (sock < 0)
        return sock;
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0)
        return -1;
    return sock;
}

int set_sock_time(int fd, int read_sec, int write_sec)
{
    struct timeval send_timeval;
    struct timeval recv_timeval;
    if(fd <= 0) return -1;
    send_timeval.tv_sec = write_sec<0?0:write_sec;
    send_timeval.tv_usec = 0;
    recv_timeval.tv_sec = read_sec<0?0:read_sec;;
    recv_timeval.tv_usec = 0;
    if(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeval, sizeof(send_timeval)) == -1)
    {
        return -1;
    }
    if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeval, sizeof(recv_timeval)) == -1)
    {
        return -1;
    }
    return 0;
}

