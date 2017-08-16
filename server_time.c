#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <string.h>

void* sender(void* arg)
{
	int efd = epoll_create(5);
	if(efd < 0)
		perror("epoll_create");
	struct epoll_event tcp_connector;
	tcp_connector.events =  EPOLLIN;
	tcp_connector.data.fd = *(((int**)arg)[1]);

	struct epoll_event udp;
	udp.events = EPOLLIN;
	udp.data.fd = *(((int**)arg)[0]);

	epoll_ctl(efd, EPOLL_CTL_ADD, *(((int**)arg)[1]), &tcp_connector);
	epoll_ctl(efd, EPOLL_CTL_ADD, *(((int**)arg)[0]), &udp);

	struct epoll_event events[5];

	struct epoll_event* clients = NULL;
	size_t client_count = 0;
	while(1)
	{
		int ret = epoll_wait(efd, events, 5, 1000);
		if(ret == -1)
		{
			perror("epoll_wait");
			break;
		}
		else if(ret != 0)
		{
			for(size_t i = 0; i < ret; ++i)
			{
				//udp
				if(events[i].data.fd == *(((int**)arg)[0]))
				{
					char buf[1024];
					struct sockaddr addr;
					socklen_t socklen = sizeof(addr);
					ssize_t bytes_read = recvfrom(*(((int**)arg)[0]), buf, 1024, 0, &addr, &socklen); 
					if(bytes_read <= 0)
					{
						perror("recvfrom");
						continue;
					}
					if(bytes_read < 4 || strncmp("time", buf, 4) != 0)
						continue;
					time_t *b = (time_t*) malloc(sizeof(time_t));
					*b = time(NULL);
					if(sendto(*(((int**)arg)[0]), b, sizeof(time_t), 0, &addr, socklen) == -1)
					{
						perror("sendto");
						continue;
					}
					free(b);
				}
				//tcp connecter
				else if(events[i].data.fd == *(((int**)arg)[1]))
				{
					int new_client = accept(events[i].data.fd, NULL, NULL);
					if(clients == NULL)
					{
						clients = (struct epoll_event*)malloc(sizeof(struct epoll_event));
						clients->events = EPOLLIN;
						clients->data.fd = new_client;
						client_count = 1;
					}
					else
					{
						clients = (struct epoll_event*)realloc(clients, sizeof(struct epoll_event)*(client_count+1));
						clients[client_count].events = EPOLLIN;
						clients[client_count++].data.fd = new_client;
					}
					epoll_ctl(efd, EPOLL_CTL_ADD, new_client, &clients[client_count-1]);
				}
				//tcp clients
				else
				{
					char buf[1024];
					ssize_t bytes_read = recv(events[i].data.fd, buf, 1024, 0);
					if(bytes_read < 0)
					{
						size_t j;
						for(j = 0; j < client_count; ++j)
							if(clients[j].data.fd == events[i].data.fd)
								break;
						epoll_ctl(efd, EPOLL_CTL_DEL, clients[j].data.fd, &clients[j]);
						if(close(events[i].data.fd))
							perror("close");
						struct epoll_event *temp = (struct epoll_event*) malloc(sizeof(struct epoll_event)*(client_count-1));
						size_t t;
						for(t = 0; t < client_count-1; ++t)
							if(t < j)
								temp[t] = clients[t];
							else if(t > j)
								temp[t] = clients[t+1];
						free(clients);
						clients = temp;
					}
					if(bytes_read < 4 || strncmp("time", buf, 4) != 0)
						continue;
					time_t *b = (time_t*) malloc(sizeof(time_t));
					*buf = time(NULL);
					if(send(events[i].data.fd, b, sizeof(time_t), 0) == -1)
					{
						perror("send");
						continue;
					}
					free(b);
				}
			}
		}
	}
	return NULL;
}

int main(int argc, char* argv[])
{
	int sock_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
	if(sock_udp == -1 || sock_tcp == -1)
	{
		perror("socket");
		return 1;
	}

	//Помечаем сокеты неблокируемым
    //if(fcntl(sock_udp, F_SETFL, O_NONBLOCK) == -1) perror("fcntl");
    //if(fcntl(sock_tcp, F_SETFL, O_NONBLOCK) == -1) perror("fcntl");

	struct sockaddr_in addr_udp;
	addr_udp.sin_family = AF_INET;
	addr_udp.sin_port = htons(7777);
	addr_udp.sin_addr.s_addr = htonl(INADDR_ANY);
	struct sockaddr_in addr_tcp = { AF_INET, htons(7778), htonl(INADDR_ANY)};

	if(bind(sock_udp, (struct sockaddr*)&addr_udp, sizeof(addr_udp)) == -1)
	{
		perror("bind udp");
		if(close(sock_tcp) == -1 || close(sock_udp) == -1)
			perror("close");
		return 1;
	}
	if(bind(sock_tcp, (struct sockaddr*)&addr_tcp, sizeof(addr_tcp)) == -1)
	{
		perror("bind tcp");
		if(close(sock_tcp) == -1 || close(sock_udp) == -1)
			perror("close");
		return 1;
	}

	if (listen(sock_tcp, SOMAXCONN) != 0)
    {
    	perror("listen");
        if(close(sock_tcp) == -1 || close(sock_udp) == -1)
			perror("close");
		return 1;
    }

	void* arg = malloc(sizeof(void*)*2);
	if(arg == NULL)
	{
		fprintf(stderr, "Failed to allocate memory\n");
		if(close(sock_tcp) == -1 || close(sock_udp) == -1)
			perror("close");
		return 1;
	}
	((void**)arg)[0] = &sock_udp;
	((void**)arg)[1] = &sock_tcp;
	pthread_t thread;
	if(pthread_create(&thread, NULL, sender, arg) != 0)
	{
		fprintf(stderr, "Failed to create thread\n");
		free(arg);
		if(close(sock_tcp) == -1 || close(sock_udp) == -1)
			perror("close");
		return 1;
	}

	if(pthread_join(thread, NULL) != 0)
	{
		fprintf(stderr, "Failed to join thread\n");
		free(arg);
		if(close(sock_tcp) == -1 || close(sock_udp) == -1)
			perror("close");
		return 1;
	}
	free(arg);	
	if(close(sock_tcp) == -1 || close(sock_udp) == -1)
	{
		perror("close");
		return 1;
	}
	return 0;
}