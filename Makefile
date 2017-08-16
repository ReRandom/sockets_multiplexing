all: client server
client: client_time.c
	gcc client_time.c -o client -g -Wall -lpthread
server: server_time.c
	gcc server_time.c -o server -g -Wall -lpthread