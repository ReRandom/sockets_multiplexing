#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/msg.h>
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
#include <sys/time.h>
#include <sys/resource.h>

#define MSGBUF_NEW_CLIENT_TYPE 		1
#define MSGBUF_NEED_NEW_THREAD_TYPE 2

int tcp_connector_sockfd;
int udp_sockfd;
int exit_flag;
size_t max_fd;
int a =1;

struct msgbuf_new_client
{
	long mtype;
	int fd;
};

void* worker(void* msqid_ptr)
{
	int efd = epoll_create(max_fd);
	if(efd < 0)
	{
		perror("epoll_create thread");
		return NULL;
	}
	struct epoll_event events[max_fd];
	struct epoll_event* clients = NULL;
	size_t size_clients = 0;
	{
		struct msgbuf_new_client buf;
		if(msgrcv(*(int*)msqid_ptr, &buf, sizeof(buf)-sizeof(long), MSGBUF_NEW_CLIENT_TYPE, 0) != -1)
		{
			clients = (struct epoll_event*)malloc(sizeof(struct epoll_event));
			clients->events = EPOLLIN;
			clients->data.fd = buf.fd;
			++size_clients;
			epoll_ctl(efd, EPOLL_CTL_ADD, buf.fd, clients);
		}
		else
		{
			perror("msgrcv thread");
			return NULL;
		}
	}
	while(1)
	{
		{
			struct msgbuf_new_client buf;
			if(msgrcv(*(int*)msqid_ptr, &buf, sizeof(buf)-sizeof(long), MSGBUF_NEW_CLIENT_TYPE, IPC_NOWAIT) != -1)
			{
				clients = (struct epoll_event*)realloc(clients, sizeof(struct epoll_event)*(size_clients+1));
				clients[size_clients].events = EPOLLIN;
				clients[size_clients].data.fd = buf.fd;
				epoll_ctl(efd, EPOLL_CTL_ADD, buf.fd, &clients[size_clients]);
				++size_clients;
				printf("clients: %ld\n", size_clients);
			}
			else if(errno != ENOMSG)
			{
				perror("msgrcv");
				break;
			}
		}
		int ret_epoll = epoll_wait(efd, events, max_fd, 50);
		if(ret_epoll == -1)
		{
			perror("epoll_wait");
			break;
		}
		else if(ret_epoll != 0)
		{
			for(int i = 0; i < ret_epoll; ++i)
			{
				char buf[128];
				ssize_t bytes_read = recv(events[i].data.fd, buf, 128, 0);
				if(bytes_read <= 0)
				{
					size_t j;
					for(j = 0; j < size_clients; ++j)
						if(clients[j].data.fd == events[i].data.fd)
							break;
					epoll_ctl(efd, EPOLL_CTL_DEL, clients[j].data.fd, &clients[j]);
					if(close(events[i].data.fd))
						perror("close");
					struct epoll_event *temp = (struct epoll_event*) malloc(sizeof(struct epoll_event)*(size_clients-1));
					size_t t;
					for(t = 0; t < size_clients-1; ++t)
						if(t < j)
							temp[t] = clients[t];
						else if(t > j)
							temp[t] = clients[t+1];
					--size_clients;
					printf("del clients: %ld\n", size_clients);
					free(clients);
					clients = temp;
					continue;
				}
				if(bytes_read < 4 || strncmp("time", buf, 4) != 0)
					continue;
				time_t b = time(NULL);
				if(send(events[i].data.fd, &b, sizeof(time_t), 0) == -1)
				{
					perror("send");
					continue;
				}
			}
		}
		if(exit_flag)
		{
			break;
		}
	}
	for(size_t i = 0; i < size_clients; ++i)
	{
		if(close(clients[i].data.fd) == -1)
			perror("close thread");
	}
	free(clients);
	return NULL;
}

void* manager(void* msqid_ptr)
{
	pthread_t *threads = NULL;
	size_t size_threads = 0;
	
	int efd = epoll_create(max_fd);
	if(efd < 0)
	{
		perror("epoll_create");
		free(threads);
		return NULL;
	}
	{
		struct epoll_event tcp_connector_event;
		tcp_connector_event.events =  EPOLLIN;
		tcp_connector_event.data.fd = tcp_connector_sockfd;
		epoll_ctl(efd, EPOLL_CTL_ADD, tcp_connector_sockfd, &tcp_connector_event);
	}
	{
		struct epoll_event udp_event;
		udp_event.events = EPOLLIN;
		udp_event.data.fd = udp_sockfd;
		epoll_ctl(efd, EPOLL_CTL_ADD, udp_sockfd, &udp_event);
	}
	struct epoll_event events[max_fd];
	struct epoll_event* clients = NULL;
	size_t size_clients = 0;
	while(1)
	{
		{
			struct msgbuf_new_client buf;
			if(msgrcv(*(int*)msqid_ptr, &buf, sizeof(buf)-sizeof(long), MSGBUF_NEED_NEW_THREAD_TYPE, IPC_NOWAIT) != -1)
			{
				threads = (pthread_t*)realloc(threads, sizeof(pthread_t)*(size_threads+1));
				if(pthread_create(&threads[size_threads++], NULL, worker, msqid_ptr) != 0)
				{
					fprintf(stderr, "pthread_create");
					exit_flag = 1;
					break;
				}
			}
			else if(errno != ENOMSG)
			{
				perror("msgrcv");
				exit_flag = 1;
				break;
			}
		}
		int ret_epoll = epoll_wait(efd, events, max_fd, 50);
		if(ret_epoll == -1)
		{
			perror("epoll_wait");
			break;
		}
		else if(ret_epoll != 0)
		{
			for(int i = 0; i < ret_epoll; ++i)
			{
				//udp
				if(events[i].data.fd == udp_sockfd)
				{
					char buf[128];
					struct sockaddr addr;
					socklen_t socklen = sizeof(addr);
					ssize_t bytes_read = recvfrom(udp_sockfd, buf, 128, 0, &addr, &socklen); 
					if(bytes_read <= 0)
					{
						perror("recvfrom");
						continue;
					}
					if(bytes_read < 4 || strncmp("time", buf, 4) != 0)
						continue;
					time_t b = time(NULL);
					if(sendto(udp_sockfd, &b, sizeof(time_t), 0, &addr, socklen) == -1)
					{
						perror("sendto");
						continue;
					}
				}
				//tcp connector
				else if(events[i].data.fd == tcp_connector_sockfd)
				{
					int new_client_sockfd = accept(events[i].data.fd, NULL, NULL);
					if(size_clients < max_fd-2)
					{
						if(clients == NULL)
						{
							clients = (struct epoll_event*)malloc(sizeof(struct epoll_event));
							clients->events = EPOLLIN;
							clients->data.fd = new_client_sockfd;
							size_clients = 1;
						}
						else
						{
							clients = (struct epoll_event*)realloc(clients, sizeof(struct epoll_event)*(size_clients+1));
							clients[size_clients].events = EPOLLIN;
							clients[size_clients++].data.fd = new_client_sockfd;
							printf("manager clients: %ld\n", size_clients);
						}
						epoll_ctl(efd, EPOLL_CTL_ADD, new_client_sockfd, &clients[size_clients-1]);
					}
					else
					{
						struct msgbuf_new_client buf_new_client;
						buf_new_client.mtype = MSGBUF_NEW_CLIENT_TYPE;
						buf_new_client.fd = new_client_sockfd;
						if(msgsnd(*(int*)msqid_ptr, &buf_new_client, sizeof(buf_new_client)-sizeof(long), 0) == -1)
							perror("msgsnd");
						if(a)
						{
							a = 0;
							buf_new_client.mtype = MSGBUF_NEED_NEW_THREAD_TYPE;
							if(msgsnd(*(int*)msqid_ptr, &buf_new_client, sizeof(buf_new_client)-sizeof(long), 0) == -1)
								perror("msgsnd");
						}
					}
				}
				//tcp clients
				else
				{
					char buf[128];
					ssize_t bytes_read = recv(events[i].data.fd, buf, 128, 0);
					if(bytes_read <= 0)
					{
						size_t j;
						for(j = 0; j < size_clients; ++j)
							if(clients[j].data.fd == events[i].data.fd)
								break;
						epoll_ctl(efd, EPOLL_CTL_DEL, clients[j].data.fd, &clients[j]);
						if(close(events[i].data.fd))
							perror("close");
						struct epoll_event *temp = (struct epoll_event*) malloc(sizeof(struct epoll_event)*(size_clients-1));
						size_t t;
						for(t = 0; t < size_clients-1; ++t)
							if(t < j)
								temp[t] = clients[t];
							else if(t > j)
								temp[t] = clients[t+1];
						free(clients);
						--size_clients;
						clients = temp;
						continue;
					}
					if(bytes_read < 4 || strncmp("time", buf, 4) != 0)
						continue;
					time_t b = time(NULL);
					if(send(events[i].data.fd, &b, sizeof(time_t), 0) == -1)
					{
						perror("send");
						continue;
					}
				}
			}
		}
	}
	for(size_t i = 0; i < size_clients; ++i)
		if(close(clients[i].data.fd) == -1)
			perror("close");
	free(clients);
	for(size_t i = 0; i < size_threads; ++i)
		if(pthread_join(threads[i], NULL) != 0)
			fprintf(stderr, "Faided to join thread\n");
	free(threads);
	return NULL;
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
	exit_flag = 0;
	max_fd = 4;
	udp_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	tcp_connector_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(udp_sockfd == -1 || tcp_connector_sockfd == -1)
	{
		perror("socket");
		return 1;
	}

	//Помечаем сокеты неблокируемым
	if(fcntl(udp_sockfd, F_SETFL, O_NONBLOCK) == -1) perror("fcntl");
	if(fcntl(tcp_connector_sockfd, F_SETFL, O_NONBLOCK) == -1) perror("fcntl");

	int msqid;
	{
		key_t key = ftok(argv[0], getpid());
		if(key == -1)
		{
			perror("ftok");
			if(close(tcp_connector_sockfd) == -1 || close(udp_sockfd) == -1)
				perror("close");
			return 1;
		}

		msqid = msgget(key, IPC_CREAT | IPC_EXCL | 0644);
		if(msqid == -1)
		{
			perror("bind udp");
			if(close(tcp_connector_sockfd) == -1 || close(udp_sockfd) == -1)
				perror("close");
			return 1;
		}
	}

	struct sockaddr_in addr_udp;
	addr_udp.sin_family = AF_INET;
	addr_udp.sin_port = htons(7777);
	addr_udp.sin_addr.s_addr = htonl(INADDR_ANY);
	struct sockaddr_in addr_tcp;
	addr_tcp.sin_family = AF_INET;
	addr_tcp.sin_port = htons(7778);
	addr_tcp.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(udp_sockfd, (struct sockaddr*)&addr_udp, sizeof(addr_udp)) == -1)
	{
		perror("bind udp");
		if(close(tcp_connector_sockfd) == -1 || close(udp_sockfd) == -1)
			perror("close");
		return 1;
	}
	if(bind(tcp_connector_sockfd, (struct sockaddr*)&addr_tcp, sizeof(addr_tcp)) == -1)
	{
		perror("bind tcp");
		if(close(tcp_connector_sockfd) == -1 || close(udp_sockfd) == -1)
			perror("close");
		return 1;
	}

	if (listen(tcp_connector_sockfd, SOMAXCONN) != 0)
	{
		perror("listen");
		if(close(tcp_connector_sockfd) == -1 || close(udp_sockfd) == -1)
			perror("close");
		return 1;
	}

	void* arg = malloc(sizeof(void*)*2);
	if(arg == NULL)
	{
		fprintf(stderr, "Failed to allocate memory\n");
		if(close(tcp_connector_sockfd) == -1 || close(udp_sockfd) == -1)
			perror("close");
		return 1;
	}
	
	pthread_t thread;
	if(pthread_create(&thread, NULL, manager, &msqid) != 0)
	{
		fprintf(stderr, "Failed to create thread\n");
		free(arg);
		if(close(tcp_connector_sockfd) == -1 || close(udp_sockfd) == -1)
			perror("close");
		return 1;
	}

	if(pthread_join(thread, NULL) != 0)
	{
		fprintf(stderr, "Failed to join thread\n");
		free(arg);
		if(close(tcp_connector_sockfd) == -1 || close(udp_sockfd) == -1)
			perror("close");
		return 1;
	}
	free(arg);	
	if(close(tcp_connector_sockfd) == -1 || close(udp_sockfd) == -1)
	{
		perror("close");
		return 1;
	}
	return 0;
}