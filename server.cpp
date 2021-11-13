/*
 * server.cpp
 *
 *  Created on: 2021Äê11ÔÂ13ÈÕ
 *      Author: LENOVO
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <pthread.h>

#define MAXLNE 4096

void *client_routine(void *arg)
{ //
	int connfd = *(int *)arg;
	char buff[MAXLNE];
	//printf("========waiting for client's request ========\n");
	while (1)
	{
		int n = recv(connfd, buff, MAXLNE, 0);
        if (n > 0)
        {
            buff[n] = '\0';
            printf("recv msg from client: %s\n", buff);

	    	send(connfd, buff, n, 0);
        }
        else if (n == 0)
        {
            close(connfd);
			printf("client %d is disconnect\n",connfd);
			break;
        }
	}
	return NULL;
}

int main(int argc,char **argv)
{
	int listenfd,connfd,n;
	struct sockaddr_in servaddr;
	char buff[MAXLNE];

	if((listenfd = socket(AF_INET,SOCK_STREAM,0)) == -1)
	{
		printf("create socket error: %s(errno: %d)\n", strerror(errno), errno);
		return 0;
	}

	memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(8888);

    if(bind(listenfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) == -1)
    {
        printf("bind socket error: %s(errno: %d)\n", strerror(errno), errno);
        return 0;
    }
    if (listen(listenfd, 10) == -1)
    {
        printf("listen socket error: %s(errno: %d)\n", strerror(errno), errno);
        return 0;
    }

	while (1)
	{
		struct sockaddr_in client;
		socklen_t len = sizeof(client);
		printf("========waiting for client connect ========\n");
		if ((connfd = accept(listenfd, (struct sockaddr *)&client, &len)) == -1)
		{
			printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
			return 0;
		}
		pthread_t threadid;
		pthread_create(&threadid, NULL, client_routine, (void*)&connfd);
	}
}



