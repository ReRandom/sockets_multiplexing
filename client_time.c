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

int main(int argc, char* argv[])
{
	if(argc == 2 && strncmp("-u", argv[1], 2) == 0)
	{
		int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(7777);
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		struct sockaddr_in addr1;
		addr1.sin_family = AF_INET;
		addr1.sin_port = htons(13000);
		addr1.sin_addr.s_addr = htonl(INADDR_ANY);
		if(bind(sock,(struct sockaddr*)&addr1, sizeof(addr1)) == -1)
			perror("bind");
		//connect(sock, (struct sockaddr*)&addr, sizeof(addr));
		sendto(sock, "time", 5, 0, (struct sockaddr*)&addr, sizeof(addr));
		time_t buf = 0;
		recvfrom(sock, &buf, sizeof(buf), 0, NULL, NULL);
		struct tm* tim = localtime(&buf);
		printf("%s\n", asctime(tim));
	}
}