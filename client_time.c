#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

#define REQUEST "time"
#define SERVER_PORT_UDP 7777
#define SERVER_PORT_TCP 7778
#define MY_PORT 13000

#define GET_TIME(SOCKET, SEND_FUNCTION, SEND_SIZE, RECV_FUNCTION, RECV_SIZE, RET_VAL_PTR) \
		ssize_t bytes_write = 0; \
		while(bytes_write < SEND_SIZE) \
		{ \
			ssize_t ret = SEND_FUNCTION; \
			if(ret == -1) \
			{ \
				perror("send function"); \
				if(close(SOCKET) == -1) \
					perror("close"); \
				*RET_VAL_PTR = 1; \
				return RET_VAL_PTR; \
			} \
			bytes_write += ret; \
		} \
		{ \
			int sel_ret = 0; \
			while(sel_ret == 0) \
			{ \
				fd_set set; \
				struct timeval tv; \
				FD_ZERO(&set); \
				FD_SET(SOCKET, &set); \
				tv.tv_sec = 1; \
				tv.tv_usec = 0; \
				sel_ret = select(SOCKET+1, &set, NULL, NULL, &tv); \
		    } \
		    if(sel_ret == -1) \
		    { \
		    	perror("select"); \
				if(close(SOCKET) == -1) \
					perror("close"); \
				*RET_VAL_PTR = 1; \
				return RET_VAL_PTR; \
		    } \
		} \
		{ \
			ssize_t bytes_read = 0; \
			while(bytes_read < RECV_SIZE) \
			{ \
				ssize_t ret = RECV_FUNCTION; \
				if(ret == -1) \
				{ \
					perror("recv function"); \
					if(close(SOCKET) == -1) \
						perror("close"); \
					*RET_VAL_PTR = 1; \
					return RET_VAL_PTR; \
				} \
				bytes_read += ret; \
			} \
		} \

struct param
{
	int flag_udp;
	long num;
};

void* work(void* arg)
{
	int* ret_value = (int*)malloc(sizeof(int));
	*ret_value = 0;
	int sock = socket(AF_INET, (((struct param*)arg)->flag_udp)? SOCK_DGRAM : SOCK_STREAM, 0);
	if(sock == -1)
	{
		printf("%ld", ((struct param*)arg)->num);
		perror("socket");
		*ret_value = 1;
		return ret_value;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons((((struct param*)arg)->flag_udp)? SERVER_PORT_UDP : SERVER_PORT_TCP);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	time_t buf = 0;
	if(((struct param*)arg)->flag_udp)
	{	
		struct sockaddr_in addr_my;
		addr_my.sin_family = AF_INET;
		addr_my.sin_port = htons(MY_PORT);
		addr_my.sin_addr.s_addr = htonl(INADDR_ANY);

		if(bind(sock,(struct sockaddr*)&addr_my, sizeof(addr_my)) == -1)
		{
			printf("%ld", ((struct param*)arg)->num);
			perror("bind");
		}
		GET_TIME(sock, sendto(sock, REQUEST, strlen(REQUEST)+1, 0, (struct sockaddr*)&addr, sizeof(addr)), strlen(REQUEST)+1,
					recvfrom(sock, &buf, sizeof(buf), 0, NULL, NULL), sizeof(buf), ret_value)
		struct tm* tim = localtime(&buf);
		printf("%s\n", asctime(tim));
	}
	else
	{
		if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1)
		{
			printf("%ld", ((struct param*)arg)->num);
			perror("conect");
		}
		while(1)
		{
			GET_TIME(sock, send(sock, REQUEST, strlen(REQUEST)+1, 0), strlen(REQUEST)+1,
						recv(sock, &buf, sizeof(buf), 0), sizeof(buf), ret_value)
			struct tm* tim = localtime(&buf);
			printf("%ld:%s\n", ((struct param*)arg)->num, asctime(tim));
			sleep(1);
		}
	}
	close(sock);
	return ret_value;
}


int main(int argc, char* argv[])
{
	{
		struct rlimit lim;

		// зададим текущий лимит на кол-во открытых дискриптеров
		lim.rlim_cur = 700000;
		// зададим максимальный лимит на кол-во открытых дискриптеров
		lim.rlim_max = 700000;

		// установим указанное кол-во
		if(setrlimit(RLIMIT_NOFILE, &lim) == -1)
		{
			perror("setrlimit");
		}
	}
	int udp = 0;
	if(argc > 1 && strncmp("-u", argv[1], 2) == 0)
	{
		udp = 1;
	}
	if(argc == 1)
		return 1;
	long size = atol(argv[argc-1]);
	pthread_t thread[size];
	struct param arg[size];
	for(long i = 0; i < size; ++i)
	{
		arg[i].flag_udp = udp;
		arg[i].num = i;
		pthread_create(&thread[i], NULL, work, &arg[i]);
	}
	for(long i = 0; i < size; ++i)
		pthread_join(thread[i],NULL);
	
}