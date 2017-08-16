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

void* work(void* flag_udp_ptr)
{
	int* ret_value = (int*)malloc(sizeof(int));
	*ret_value = 0;
	int sock = socket(AF_INET, (*(int*)flag_udp_ptr)? SOCK_DGRAM : SOCK_STREAM, 0);
	if(sock == -1)
		perror("socket");

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons((*(int*)flag_udp_ptr)? SERVER_PORT_UDP : SERVER_PORT_TCP);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	time_t buf = 0;

	if(*(int*)flag_udp_ptr)
	{	
		struct sockaddr_in addr_my;
		addr_my.sin_family = AF_INET;
		addr_my.sin_port = htons(MY_PORT);
		addr_my.sin_addr.s_addr = htonl(INADDR_ANY);

		if(bind(sock,(struct sockaddr*)&addr_my, sizeof(addr_my)) == -1)
			perror("bind");
		GET_TIME(sock, sendto(sock, REQUEST, strlen(REQUEST)+1, 0, (struct sockaddr*)&addr, sizeof(addr)), strlen(REQUEST)+1,
					recvfrom(sock, &buf, sizeof(buf), 0, NULL, NULL), sizeof(buf), ret_value)
	}
	else
	{
		if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1)
			perror("conect");
		GET_TIME(sock, send(sock, REQUEST, strlen(REQUEST)+1, 0), strlen(REQUEST)+1,
					recv(sock, &buf, sizeof(buf), 0), sizeof(buf), ret_value)
	}
	close(sock);
	struct tm* tim = localtime(&buf);
	printf("%s\n", asctime(tim));
	return ret_value;
}


int main(int argc, char* argv[])
{
	int udp = 0;
	if(argc == 2 && strncmp("-u", argv[1], 2) == 0)
	{
		udp = 1;
	}
	pthread_t thread;
	pthread_create(&thread, NULL, work, &udp);
	int* ret;
	pthread_join(thread, (void**)&ret);
	if(*ret == 0)
	{
		free(ret);
		return 0;
	}
	else
	{
		free(ret);
		return 1;
	}
	
}