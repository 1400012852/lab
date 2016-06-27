//1400012852 haolin_li
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_URL 350

static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept1 = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection = "Connection: close\r\n";
static const char *proxy_connection = "Proxy-Connection: close\r\n\r\n";

struct thread_argv
{
    int connfd, curtime;
};

struct cache_block
{
    char data[MAX_OBJECT_SIZE], url[MAX_URL];
    int  time;

    sem_t mutex_data,   mutex_url,   mutex_time;
    sem_t w_data,       w_url,       w_time;
    int   readcnt_data, readcnt_url, readcnt_time;
};
struct cache_block cache[10];


void cache_write(char *src, char *url, int time, int i);
void cache_init(int i);
void cache_read_data(char *dst, int i, int time);
void cache_read_url(char *dst, int i);
void cache_read_time(int i, int *time);
void read_requesthdrs(rio_t *rp);
void read_responsehdrs(rio_t *rp, int *len, char *data, int connfd);
void parse_url(char *ori_url, char *hostname, char *uri, int *port);
void *thread(void *vargp);

void cache_write(char *src, char *url, int time, int i)
{
    struct cache_block *c = &cache[i];
    P(&(c->w_data));
    P(&(c->w_url));
    P(&(c->w_time));

    strcpy(c->data, src);
    strcpy(c->url, url);
    c->time = time;

    V(&(c->w_time));
    V(&(c->w_url));
    V(&(c->w_data));
    return;

}

void cache_init(int i)
{
    cache[i].data[0] = 0;
    cache[i].url[0] = 0;
    cache[i].time = 0;
    cache[i].readcnt_data = 0;
    cache[i].readcnt_url = 0;
    cache[i].readcnt_time = 0;
    Sem_init(&cache[i].mutex_data, 0, 1);
    Sem_init(&cache[i].mutex_url, 0, 1);
    Sem_init(&cache[i].mutex_time, 0, 1);
    Sem_init(&cache[i].w_data, 0, 1);
    Sem_init(&cache[i].w_url, 0, 1);
    Sem_init(&cache[i].w_time, 0, 1);
    return;
}

void cache_read_data(char *dst, int i, int time)
{
    struct cache_block *c = &cache[i];
    P(&(c->mutex_data));
    ++(c->readcnt_data);
    if(c->readcnt_data == 1)
	P(&(c->w_data));
    V(&(c->mutex_data));

    P(&(c->w_time));
    c->time = time;
    strcpy(dst, c->data);
    V(&(c->w_time));

    P(&(c->mutex_data));
    --(c->readcnt_data);
    if(c->readcnt_data == 0)
	V(&(c->w_data));
    V(&(c->mutex_data));
    return;
}

void cache_read_url(char *dst, int i)
{
    struct cache_block *c = &cache[i];
    P(&(c->mutex_url));
    ++(c->readcnt_url);
    if(c->readcnt_url == 1)
	P(&(c->w_url));
    V(&(c->mutex_url));

    strcpy(dst, c->url);

    P(&(c->mutex_url));
    --(c->readcnt_url);
    if(c->readcnt_url == 0)
	V(&(c->w_url));
    V(&(c->mutex_url));
    return;
}

void cache_read_time(int i, int *time)
{
    struct cache_block *c = &cache[i];
    P(&(c->mutex_time));
    ++(c->readcnt_time);
    if(c->readcnt_time == 1)
	P(&(c->w_time));
    V(&(c->mutex_time));

    *time = c->time;

    P(&(c->mutex_time));
    --(c->readcnt_time);
    if(c->readcnt_time == 0)
	V(&(c->w_time));
    V(&(c->mutex_time));
    return;
}

int main(int argc, char **argv)
{
    Signal(SIGPIPE, SIG_IGN);
    int listenfd, connfd, port, clientlen, curtime = 1;
    struct sockaddr_in clientaddr;
    pthread_t tid;
    struct thread_argv *val;
    
    int i;
    for(i = 0; i < 10; ++i)
	cache_init(i);

    if(argc != 2)
    {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(0);
    }
    port = atoi(argv[1]);

    listenfd = Open_listenfd(port);
    while(1)
    {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
	
	printf("connfd : %d\r\n", connfd);
	val = (struct thread_argv *)Malloc(sizeof(struct thread_argv));
	val->connfd = connfd;
	val->curtime = curtime++;
	Pthread_create(&tid, NULL, thread, (void *)val);
    }
    return 0;
}

void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE - 1);
    while(strcmp(buf, "\r\n"))
	Rio_readlineb(rp, buf, MAXLINE - 1);
    return;
}

void read_responsehdrs(rio_t *rp, int *len, char *data, int connfd)
{
    char buf[MAXLINE];
    int line_size;
    do
    {
	line_size = Rio_readlineb(rp, buf, MAXLINE - 1);
	if(line_size <= 0)
	    break;
	strncat(data, buf, line_size);	//append cache save data
	if(strstr(buf, "Content-length"))
            sscanf(buf, "Content-length: %d\r\n", len);
        Rio_writen(connfd, buf, line_size);	//forward info
    }while(strcmp(buf, "\r\n"));
    return;
}   


void parse_url(char *ori_url, char *hostname, char *uri, int *port)
{

//printf("parse_url...\r\n");
//printf("ori_url : %s\r\n", ori_url);

    char url[MAX_URL];	//to be modified
    url[0] = hostname[0] = uri[0] = 0;
    *port = 80;		//default
    strcat(url, ori_url);

//printf("url : %s\r\n", url);

    char *p1, *p2;
    p1 = strstr(url, "//");
    if(p1)		//find the start of hostname
    	p1 += 2;
    else		//no http://
	p1 = url;

    p2 = strstr(p1, ":");
    if(p2)	//start from hostname
    {
	*p2 = 0;
	sscanf(p1, "%s", hostname);
	sscanf(p2 + 1, "%d%s", port, uri);	//offered port
    }
    else
    {
	p2 = strstr(p1, "/");
	if(p2)
	{
	    *p2 = 0;
	    sscanf(p1, "%s", hostname);
	    *p2 = '/';
	    sscanf(p2, "%s", uri);
	}
	else			//using the default path
	    sscanf(p1, "%s", hostname);
    }
    if(strlen(uri) < 2)		//0 or only '/'
	strcpy(uri, "/index.html");	//default path
//printf("hostname : %s\r\n", hostname);
//printf("uri : %s\r\n", uri);
//printf("port : %d\r\n", *port);
    return;
}

void *thread(void *vargp)
{
    Pthread_detach(Pthread_self());
    int connfd = ((struct thread_argv *)vargp)->connfd;
    int time = ((struct thread_argv *)vargp)->curtime;
    Free(vargp);

    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], uri[MAXLINE];
    int port, clientfd;
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);	//read request line
    sscanf(buf, "%s %s %s", method, url, version);

//printf("%s\r\n", buf);
    if(strcasecmp(method, "GET"))
    {
//printf("Not GET\r\n");
	Close(connfd); 
	return NULL;
    }
    //read_requesthdrs(&rio);	//ignore headers
//printf("read request header...\r\n");
    do 
    {
        Rio_readlineb(&rio, buf, MAXLINE - 1);
    }while(strcmp(buf, "\r\n"));

    //search in cache
    int i;
    for(i = 0; i < 10; ++i)
    {
//printf("Cache search : %d\r\n", i);
	char tmp[MAX_URL];
	cache_read_url(tmp, i);
	if(!strcmp(url, tmp))	//got a match
	    break;
    }

//printf("Malloc data\r\n");
    char *data = (char *)Malloc(MAX_OBJECT_SIZE);
    data[0] = 0;

    if(i != 10)		//hit
    {
//printf("Cache hit : %d", i);
	cache_read_data(data, i, time);
	Rio_writen(connfd, data, strlen(data));
	Free(data);
	Close(connfd);
	return NULL;
    }

    //server connection
    parse_url(url, hostname, uri, &port);

    clientfd = open_clientfd(hostname, port);
//printf("clientfd : %d\r\n", clientfd);
    if(clientfd < 0)	//fail
    {
	Free(data);
	Close(connfd);
	return NULL;
    }
    Rio_readinitb(&rio, clientfd);

    sprintf(buf,"GET %s HTTP/1.0\r\n", uri);
    Rio_writen(clientfd, buf, strlen(buf));
    //headers
    sprintf(buf,"Host: %s\r\n", hostname);
    Rio_writen(clientfd, (void *)buf, strlen(buf));
    Rio_writen(clientfd, (void *)user_agent, strlen(user_agent));
    Rio_writen(clientfd, (void *)accept1, strlen(accept1));
    Rio_writen(clientfd, (void *)accept_encoding, strlen(accept_encoding));
    Rio_writen(clientfd, (void *)connection, strlen(connection));
    Rio_writen(clientfd, (void *)proxy_connection, strlen(proxy_connection));
    
    //read response from server
    int content_len = 0;
    //response header
    read_responsehdrs(&rio, &content_len, data, connfd);
    
    int sum_size = strlen(data) + content_len;
    if(sum_size >= MAX_OBJECT_SIZE)	//too large
    {
	while(content_len > 0) 
	{
            int line_size = rio_readnb(&rio , buf, (content_len < (MAXLINE - 1)) ? content_len : (MAXLINE - 1));
            if(line_size <=0)
                break;
            Rio_writen(connfd, buf, line_size);	//forward info
            content_len -= line_size;
        }
    }
    else
    {
	while(content_len > 0) 
	{
            int line_size = rio_readnb(&rio , buf, (content_len < (MAXLINE - 1)) ? content_len : (MAXLINE - 1));
            if(line_size <=0)
                continue;
            strncat(data, buf, line_size);	//append cache data
            Rio_writen(connfd, buf, line_size);	//forward info
            content_len -= line_size;
        }

	int lru = 0;
	for(i = 0; i < 10; ++i)	//eliminate least recent one
	{
	    int t1, t2;
	    cache_read_time(i, &t1);
	    cache_read_time(lru, &t2);
	    if(t1 < t2)
		lru = i;
	}
	cache_write(data, url, time, lru);	//save new block
    }
    Free(data);
    Close(clientfd);
    Close(connfd);
    return NULL;
}























