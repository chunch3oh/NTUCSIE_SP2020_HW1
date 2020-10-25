#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include<stdbool.h>

#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
    int id;
    int wait_for_write;  // used by handle_read to know if the header is read or not.
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

typedef struct {
    int id; //customer id
    int adultMask;
    int childrenMask;
} Order;

int handle_read(request* reqP) {
    char buf[512];
    read(reqP->conn_fd, buf, sizeof(buf));
    memcpy(reqP->buf, buf, strlen(buf));
    return 0;
}

int main(int argc, char** argv) {

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }
    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));
    fd_set original_set, current_set;
    FD_ZERO(&original_set);
    FD_ZERO(&current_set);
    FD_SET(svr.listen_fd,&original_set);
    //Get fd table size and initialize request table
    maxfd=getdtablesize();
    requestP=(request*)malloc(sizeof(request)*maxfd);
    if(requestP==NULL)
        ERR_EXIT("out of memory allocating all requests");
    for(int i=0;i<maxfd;i++)
        init_request(&requestP[i]);
    requestP[svr.listen_fd].conn_fd=svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host,svr.hostname);
    file_fd=open("preorderRecord",O_RDWR);
    if(file_fd<0)
        fprintf(stderr,"no suchfile name");
    int read_lock[21]={0};
    bool write_lock[21]={false};
    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);
    fcntl(svr.listen_fd,F_SETFL,O_NONBLOCK);
    while (1) {
        // TODO: Add IO multiplexing
        current_set=original_set;
        if(select(maxfd, &current_set, NULL, NULL, NULL) <1)
            continue;
        if(FD_ISSET(svr.listen_fd,&current_set))
        {
            // Check new connection
            clilen = sizeof(cliaddr);
            conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
            if (conn_fd < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;  // try again
                if (errno == ENFILE) {
                    (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                    continue;
                }
                ERR_EXIT("accept");
            }
            char str[80];
            sprintf(str,"Please enter the id (to check how many masks can be ordered):\n");
            write(conn_fd, str, strlen(str));
            FD_SET(conn_fd,&original_set);
            continue;
        }
        else
        {
            conn_fd=-1;
            for(int i=3;i<maxfd;i++)
                if(i!=svr.listen_fd&&FD_ISSET(i,&current_set))
                {
                    conn_fd=i;
                    break;
                }
             if(conn_fd==-1)
                    continue;
        }
        requestP[conn_fd].conn_fd = conn_fd;
        strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
        fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
        int ret = handle_read(&requestP[conn_fd]); // parse data from client to requestP[conn_fd].buf
        if (ret < 0) {
            fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
            continue;
        }
	struct flock lock;
    // TODO: handle requests from clients
#ifdef READ_SERVER
        Order order;
        order.id=atoi(requestP[conn_fd].buf);
        requestP[conn_fd].id=order.id-902000;
        lock.l_type=F_RDLCK;
        lock.l_start=sizeof(Order)*(requestP[conn_fd].id-1);
        lock.l_whence=SEEK_SET;
        lock.l_len=sizeof(Order);
        lock.l_pid=getpid();
        if(!write_lock[requestP[conn_fd].id]&&order.id>=902001&&order.id<=902020&&fcntl(file_fd,F_SETLK,&lock)!=-1)
        {
            ++read_lock[requestP[conn_fd].id];
            lseek(file_fd,sizeof(Order)*(requestP[conn_fd].id),SEEK_SET);
            read(file_fd,&order,sizeof(Order));
            sprintf(buf,"You can order %d adult mask(s) and %d children mask(s).\n",order.adultMask,order.childrenMask);
            write(requestP[conn_fd].conn_fd, buf, strlen(buf));
            lock.l_type=F_UNLCK;
            lock.l_start=sizeof(Order)*(requestP[conn_fd].id-1);
            lock.l_whence=SEEK_SET;
            lock.l_len=sizeof(Order);
            fcntl(file_fd,F_SETLK,&lock);
            --read_lock[requestP[conn_fd].id];
        }
        else if((write_lock[requestP[conn_fd].id]||fcntl(file_fd,F_SETLK,&lock)==-1)&&order.id>=902001&&order.id<=902020)
        {
            sprintf(buf,"Locked.\n");
            write(requestP[conn_fd].conn_fd, buf, strlen(buf));
        }
        else
        {
            sprintf(buf,"Operation failed.\n");
            write(requestP[conn_fd].conn_fd, buf, strlen(buf));
        }
#else
        Order order;
        if(requestP[conn_fd].id==0)
        {
            order.id=atoi(requestP[conn_fd].buf);
            requestP[conn_fd].id=order.id-902000;
            lock.l_type=F_WRLCK;
            lock.l_start=sizeof(Order)*(requestP[conn_fd].id-1);
            lock.l_whence=SEEK_SET;
            lock.l_len=sizeof(Order);
            lock.l_pid=getpid();
            if(!write_lock[requestP[conn_fd].id]&&read_lock[requestP[conn_fd].id]==0&&fcntl(file_fd,F_SETLK,&lock)!=-1&&order.id>=902001&&order.id<=902020)
            {
                write_lock[requestP[conn_fd].id]=true;
                lseek(file_fd,sizeof(Order)*(requestP[conn_fd].id-1),SEEK_SET);
                read(file_fd,&order,sizeof(Order));
                sprintf(buf,"You can order %d adult mask(s) and %d children mask(s).\nPlease enter the mask type (adult or children) and number of mask you would like to order:\n",order.adultMask,order.childrenMask);
                write(requestP[conn_fd].conn_fd, buf, strlen(buf));
                continue;
            }
            else if((write_lock[requestP[conn_fd].id]||fcntl(file_fd,F_SETLK,&lock)==-1||read_lock[requestP[conn_fd].id]>0)&&order.id>=902001&&order.id<=902020)
            {
                sprintf(buf,"Locked.\n");
                write(requestP[conn_fd].conn_fd, buf, strlen(buf));
            }
            else
            {
                sprintf(buf,"Operation failed.\n");
                write(requestP[conn_fd].conn_fd, buf, strlen(buf));
            }
        }
        else
        {
            char type[10],num[10];
            int number_of_order;
            sscanf(requestP[conn_fd].buf,"%s %s",type,num);
            number_of_order=atoi(num);
            if(!strncmp(type,"adult",5))
            {
            	if(number_of_order>0&&number_of_order<=order.adultMask)
                {
                    sprintf(buf,"Pre-order for %d successed, %d adult mask(s) ordered.\n",order.id,number_of_order);
                    write(requestP[conn_fd].conn_fd, buf, strlen(buf));
                    lseek(file_fd,sizeof(Order)*(requestP[conn_fd].id-1),SEEK_SET);
                    read(file_fd,&order,sizeof(Order));
                    order.adultMask-=number_of_order;
                    lseek(file_fd,sizeof(Order)*(requestP[conn_fd].id-1),SEEK_SET);
                    write(file_fd,&order,sizeof(Order));
                }
                else
                {
                    sprintf(buf,"Operation failed.\n");
                    write(requestP[conn_fd].conn_fd, buf, strlen(buf));
                }
            }
            else if(!strncmp(type,"children",8))
            {
            	if(number_of_order>0&&number_of_order<=order.childrenMask)
                {
                    sprintf(buf,"Pre-order for %d successed, %d children mask(s) ordered.\n",order.id,number_of_order);
                    write(requestP[conn_fd].conn_fd, buf, strlen(buf));
                    lseek(file_fd,sizeof(Order)*(requestP[conn_fd].id-1),SEEK_SET);
                    read(file_fd,&order,sizeof(Order));
                    order.childrenMask-=number_of_order;
                    lseek(file_fd,sizeof(Order)*(requestP[conn_fd].id-1),SEEK_SET);
                    write(file_fd,&order,sizeof(Order));
                }
                else
                {
                    sprintf(buf,"Operation failed.\n");
                    write(requestP[conn_fd].conn_fd, buf, strlen(buf));
                }
            }
            else
                {
                    sprintf(buf,"Operation failed.\n");
                    write(requestP[conn_fd].conn_fd, buf, strlen(buf));
                }
            lock.l_type=F_UNLCK;
            lock.l_start=sizeof(Order)*(requestP[conn_fd].id-1);
            lock.l_whence=SEEK_SET;
            lock.l_len=sizeof(Order);
            fcntl(file_fd,F_SETLK,&lock);
            write_lock[requestP[conn_fd].id]=false;
        }

#endif
        close(requestP[conn_fd].conn_fd);
        free_request(&requestP[conn_fd]);
        FD_CLR(conn_fd,&original_set);
    }
    free(requestP);
    close(file_fd);
    return 0;
}
// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->id = 0;
}

static void free_request(request* reqP) {
    /*if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }*/
    init_request(reqP);
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}
