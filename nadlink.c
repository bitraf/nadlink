#define _XOPEN_SOURCE 500
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

const int parallel_port = 0x378;

int high = 0x00;
int low = 0xff;

void busleep(unsigned int usecs)
{
  struct timeval start, end;
  gettimeofday(&start, 0);

  for(;;)
  {
  	gettimeofday(&end, 0);
  	if(end.tv_usec - start.tv_usec + (end.tv_sec - start.tv_sec) * 1000000 > usecs)
  		return;
  }
}

static void send_one()
{
  outb(high, parallel_port);
  busleep(563);
  outb(low, parallel_port);
  busleep(1687);
}

static void send_zero()
{
  outb(high, parallel_port);
  busleep(563);
  outb(low, parallel_port);
  busleep(562);
}

static void send_byte(int b)
{
  int i;

  for(i = 0; i < 8; ++i)
  {
  	if(b & (1 << i))
  		send_one();
  	else
  		send_zero();
  }
}

static void send_repeat()
{
  outb(high, parallel_port);
  busleep(9000);
  outb(low, parallel_port);
  busleep(2250);
  outb(high, parallel_port);
  busleep(563);
  outb(low, parallel_port);
  busleep(110000 - 563 - 9000 - 2250);
}

static void send_command(int command)
{
  int i;

  outb(high, parallel_port);
  busleep(9000);
  outb(low, parallel_port);
  busleep(4500);

  send_one();
  send_one();
  send_one();
  send_zero();
  send_zero();
  send_zero();
  send_zero();
  send_one();

  send_zero();
  send_zero();
  send_one();
  send_one();
  send_one();
  send_one();
  send_one();
  send_zero();

  send_byte(command);
  send_byte(~command);

  outb(high, parallel_port);
  busleep(563);
  outb(low, parallel_port);

  busleep(110000 - 563 - 9000 - 4500 - 32 * 563 - 16 * 563 - 16 * 1687);
}

#define NAD_POWER_TOGGLE 0x80
#define NAD_VOLUME_UP    0x88
#define NAD_VOLUME_DOWN  0x8C
#define NAD_MUTE         0x94

int main(int argc, char **argv)
{
  int listenfd;

  sched_setscheduler(getpid(), SCHED_FIFO, 0);

  if (ioperm(parallel_port, 1, 1) == -1)
  {
  	fprintf(stderr, "ioperm on port %#x failed: %s\n", parallel_port, strerror(errno));

  	return EXIT_FAILURE;
  }

  if(-1 == (listenfd = socket(PF_INET, SOCK_STREAM, 0)))
  {
  	perror("socket(PF_INET, SOCK_STREAM, 0)");

  	return EXIT_FAILURE;
  }

  struct sockaddr_in address;

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(0xBABE);

  int one = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  if(-1 == bind(listenfd, (struct sockaddr*) &address, sizeof(address)))
  {
    perror("bind(...)");

    return EXIT_FAILURE;
  }

  if(-1 == listen(listenfd, 16))
  {
    perror("listen(...)");

    return EXIT_FAILURE;
  }

  daemon(0, 0);

  fd_set all_sockets;
  FD_ZERO(&all_sockets);
  FD_SET(listenfd, &all_sockets);

  for(;;)
  {
    int i;
    int res;
    int clientfd;
    int command = -1;
    fd_set readset;
    struct timeval tv;

    readset = all_sockets;

    res = select(1024, &readset, 0, 0, 0);

    if(res == -1)
    {
      if(errno == EBUSY || errno == EAGAIN)
        continue;
    }

repeat:

    if(FD_ISSET(listenfd, &readset))
    {
      struct sockaddr addr;
      socklen_t addrlen;

      clientfd = accept(listenfd, &addr, &addrlen);

      FD_SET(clientfd, &all_sockets);
    }

    for(i = 3; i < 1024; ++i)
    {
      if(i == listenfd)
        continue;

      if(FD_ISSET(i, &readset))
      {
        char buf;

        res = read(i, &buf, 1);

        if(res <= 0)
        {
          close(i);
          FD_CLR(i, &all_sockets);
        }
        else
        {
          switch(buf)
          {
          case '+': command = NAD_VOLUME_UP; break;
          case '-': command = NAD_VOLUME_DOWN; break;
          case 'm': command = NAD_MUTE; break;
          }
        }
      }
    }

    /* Read all pending commands to avoid buffer pileup */
    if(command != -1)
    {
      tv.tv_sec = 0;
      tv.tv_usec = 0;

      readset = all_sockets;

      res = select(1024, &readset, 0, 0, &tv);

      if(res > 0)
        goto repeat;

      send_command(command);
      send_repeat();
    }
  }

  return EXIT_SUCCESS;
}

// vim: et ts=2 sw=2
