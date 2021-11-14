#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/poll.h>
#include <sys/epoll.h>


#include <pthread.h>
 
#define MAXLNE  4096

#define POLL_SIZE	1024

#define BUFFER_LENGTH		1024
#define MAX_EPOLL_EVENT		1024

#define NOSET_CB	0
#define READ_CB		1
#define WRITE_CB	2
#define ACCEPT_CB	3


typedef int NCALLBACK(int fd, int event, void *arg);

//对应每一个客户端fd的各种信息
struct nitem
{ // fd

	int fd; //新的客户端的fd

	int status;
	int events;
	void *arg;
#if 0
	NCALLBACK callback;
#else
	NCALLBACK *readcb;   // epollin
	NCALLBACK *writecb;  // epollout
	NCALLBACK *acceptcb; // epollin
#endif
	unsigned char sbuffer[BUFFER_LENGTH]; //
	int slength;

	unsigned char rbuffer[BUFFER_LENGTH];
	int rlength;
	
};

struct itemblock {
	//多个客户端组成的集合
	struct itemblock *next;
	struct nitem *items;

};

struct reactor {

	int epfd; //epoll_creat的fd
	struct itemblock *head; 

};

int init_reactor(struct reactor *r);

int read_callback(int fd, int event, void *arg);

int write_callback(int fd, int event, void *arg);

int accept_callback(int fd, int event, void *arg);


struct reactor *instance = NULL;

struct reactor *getInstance(void) { //singleton

	if (instance == NULL) {

		instance = malloc(sizeof(struct reactor));
		if (instance == NULL) return NULL;
		memset(instance, 0, sizeof(struct reactor));

		if (0 > init_reactor(instance)) {
			free(instance);
			return NULL;
		}

	}

	return instance;
}


//将当前fd交由epoll管理
int nreactor_set_event(int fd, NCALLBACK cb, int event, void *arg) {

	struct reactor *r = getInstance();
	
	struct epoll_event ev = {0};
	
	//判断是要设置成哪种事件
	if (event == READ_CB) {
		r->head->items[fd].fd = fd;
		r->head->items[fd].readcb = cb;
		r->head->items[fd].arg = arg;

		ev.events = EPOLLIN;
		
	} else if (event == WRITE_CB) {
		r->head->items[fd].fd = fd;
		r->head->items[fd].writecb = cb;
		r->head->items[fd].arg = arg;

		ev.events = EPOLLOUT;
	} else if (event == ACCEPT_CB) {
		r->head->items[fd].fd = fd;
		r->head->items[fd].acceptcb = cb;
		r->head->items[fd].arg = arg;

		ev.events = EPOLLIN;
	}

	ev.data.ptr = &r->head->items[fd];

	
	if (r->head->items[fd].events == NOSET_CB) {
		if (epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
			printf("epoll_ctl EPOLL_CTL_ADD failed, %d\n", errno);
			return -1;
		}
		r->head->items[fd].events = event;
	}
	else if (r->head->items[fd].events != event) {
		if (epoll_ctl(r->epfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
			printf("epoll_ctl EPOLL_CTL_MOD failed\n");
			return -1;
		}
		r->head->items[fd].events = event;
	}
	return 0;
}
//从epoll中删除指定的fd
int nreactor_del_event(int fd, NCALLBACK cb, int event, void *arg) {

	struct reactor *r = getInstance();
	
	struct epoll_event ev = {0};
	ev.data.ptr = arg;

	epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, &ev);
	r->head->items[fd].events = 0;

	return 0;
}

int write_callback(int fd, int event, void *arg) {

	struct reactor *R = getInstance();
	
	unsigned char *sbuffer = R->head->items[fd].sbuffer;
	int length = R->head->items[fd].slength;

	int ret = send(fd, sbuffer, length, 0);

	if (ret < length) {
		nreactor_set_event(fd, write_callback, WRITE_CB, NULL);
	} else {
		nreactor_set_event(fd, read_callback, READ_CB, NULL);
	}
	return 0;
}

// 5k qps

int read_callback(int fd, int event, void *arg) {

	struct reactor *R = getInstance();

	unsigned char *buffer = R->head->items[fd].rbuffer;

	int ret = recv(fd, buffer, BUFFER_LENGTH, 0);
	if (ret == 0) { // fin   ret == 0代表对端关闭了  不是读数据为0
		
		nreactor_del_event(fd, NULL, 0, NULL);//先删除再关闭
		close(fd);
		
	} else if (ret > 0) {
		//先将接收到的数据做一个拷贝，用来回环转发
		unsigned char *sbuffer = R->head->items[fd].sbuffer;
		memcpy(sbuffer, buffer, ret);
		R->head->items[fd].slength = ret;

		printf("readcb: %s\n", sbuffer);
		nreactor_set_event(fd, write_callback, WRITE_CB, NULL);
	}
}


// web server 
// ET / LT
int accept_callback(int fd, int event, void *arg) {

	int connfd;
	struct sockaddr_in client;
    socklen_t len = sizeof(client);
    if ((connfd = accept(fd, (struct sockaddr *)&client, &len)) == -1) {
        printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
        return 0;
    }

	nreactor_set_event(connfd, read_callback, READ_CB, NULL);

}

int init_server(int port) {

	int listenfd;
    struct sockaddr_in servaddr;
    char buff[MAXLNE];
 
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("create socket error: %s(errno: %d)\n", strerror(errno), errno);
        return 0;
    }
 
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
 
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        printf("bind socket error: %s(errno: %d)\n", strerror(errno), errno);
        return 0;
    }
 
    if (listen(listenfd, 10) == -1) {
        printf("listen socket error: %s(errno: %d)\n", strerror(errno), errno);
        return 0;
    }
	return listenfd;

}

int init_reactor(struct reactor *r) {

	if (r == NULL) return -1;

	int epfd = epoll_create(1); //int size
	r->epfd = epfd;

	// fd --> item
	r->head = (struct itemblock*)malloc(sizeof(struct itemblock));
	if (r->head == NULL) {
		close(epfd);
		return -2;
	} 
	memset(r->head, 0, sizeof(struct itemblock));

	r->head->items = malloc(MAX_EPOLL_EVENT * sizeof(struct nitem));
	if (r->head->items == NULL) {
		free(r->head);
		close(epfd);
		return -2;
	}
	memset(r->head->items, 0, (MAX_EPOLL_EVENT * sizeof(struct nitem)));
	
	r->head->next = NULL;
	
	return 0;
}

// accept --> EPOLL
int reactor_loop(int listenfd) {

	struct reactor *R = getInstance();
	
	struct epoll_event events[POLL_SIZE] = {0};//监听的最大客户端个数集合
	while (1)
	{
		int nready = epoll_wait(R->epfd, events, POLL_SIZE, 5);
		if (nready == -1) {
			continue;
		}

		int i = 0;
		for (i = 0;i < nready;i ++)
		{
			struct nitem *item = (struct nitem *)events[i].data.ptr;
			int connfd = item->fd;

			if (connfd == listenfd)//发生的IO事件是有新连接
			{ //
				item->acceptcb(listenfd, 0, NULL);
			}
			else
			{
				if (events[i].events & EPOLLIN)
				{ //
					item->readcb(connfd, 0, NULL);
				} 
				if (events[i].events & EPOLLOUT)
				{
					item->writecb(connfd, 0, NULL);
				}
			}
		}
	}
	return 0;
}

int main(int argc, char **argv) 
{
    
 	int  connfd, n;

	int listenfd = init_server(8888);

	//将监听的端口设置到事件中
	nreactor_set_event(listenfd, accept_callback, ACCEPT_CB, NULL);

	//nreactor_set_event(listenfd, accept_callback, read_callback, write_callback);
	
	reactor_loop(listenfd);
	 
    return 0;
}

