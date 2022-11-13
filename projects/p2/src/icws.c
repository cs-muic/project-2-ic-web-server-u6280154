#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "parse.h"
#include "pcsa_net.h"
#include <getopt.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <stdbool.h>

#define BUFSIZE 8192
#define DATESIZE 30
#define MAXTHREAD 256
#define PERSISTENT 1
#define CLOSE 0
#define infinite for(;;)

#define YYERROR_VERBOSE
#ifdef YACCDEBUG
#define YPRINTF(...) printf(_VA_ARGS_)
#else
#define YPRINTF(...)
#endif

typedef struct sockaddr SA;

char* dirName;
int thread_number,timeout,taskCount = 0;

pthread_t thread_pool[MAXTHREAD];
pthread_mutex_t mutex_q = PTHREAD_MUTEX_INITIALIZER,mutex_parse = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;

void respond_with_number(int status,int connFd){
        char *response;
	switch(status){
		case 501:
		   response = "HTTP/1.1 501 Method Not Implemented\r\n\r\n";
		   break;
		case 505:
		   response = "HTTP/1.1 505 Bad Version Number\r\n\r\n";
		   break;
		case 400:
		   response = "HTTP/1.1 400 Parsing Failed\r\n\r\n";
		   break;
		case 411:
		   response = "HTTP/1.1 411 File size missing\r\n\r\n";
		   break;
	}
	write_all(connFd,response,strlen(response));
}

void respond_404(int connFd,char* type){
	char *msg = "<h1>404 NOT FOUND</h1>";
	char buf[BUFSIZE];
	memset(buf,0,BUFSIZE);
	sprintf(buf, "HTTP/1.1 404 Not Found\r\n"
	"Server: Icws\r\n"
	"Content-length: %lu\r\n"
	"Connection: %s\r\n"
	"Content-type: text/html\r\n\r\n",strlen(msg),type);
	write_all(connFd, buf, strlen(buf));
	write_all(connFd, msg, strlen(msg));
}

char* get_Extension(char *filename){
	char* extension = filename;
	while(strrchr(extension,'.') != NULL){
		extension = strrchr(extension,'.');
		extension += 1;
	}
	return extension;
}

char* mime_type(char *ext){
	if(strcmp(ext, "html") == 0){
		return "text/html";
	}
	else if(strcmp(ext, "css") == 0){
		return "text/css";
	}
	else if(strcmp(ext, "txt") == 0 ){
		return "text/plain";
	}
	else if(strcmp(ext, "js") == 0  ||strcmp(ext, "mjs") == 0){
		return "text/javascript";
	}
	else if(strcmp(ext, "jpg") == 0 ||strcmp(ext, "jpeg") == 0){
		return "image/jpeg";
	}
	else if(strcmp(ext, "png") == 0){
		return "image/png";
	}
	else if(strcmp(ext, "gif") == 0){
		return "image/gif";
	}
	else{
		return NULL;
	}
}

void get_filename(char* temp,char* root,char* req){
	strcpy(temp, root);
	if(strcmp(req,"/") == 0){
		req = "/index.html";
	}
	else if(req[0] != '/'){
		strcat(temp,"/");
	}
	strcat(temp, req);	
}

void server_date(char* date){
	time_t current = time(0);
	struct tm local = *gmtime(&current);
	strftime(date,DATESIZE,"%a, %d %b %Y %H:%M:%S %Z",&local);
}

void server_last_modified(char* last_modified,struct stat statbuf){
	struct tm local = *gmtime(&statbuf.st_mtime);
	strftime(last_modified,DATESIZE,"%a, %d %b %Y %H:%M:%S %Z",&local);
}

void respond(int connFd,char *root,char *object,int key,char* type){
	char filename[BUFSIZE];
	get_filename(filename, root, object);
	int inFd = open(filename, O_RDONLY);
	
	if(inFd < 0){
		fprintf(stderr,"open file error\n");
		respond_404(connFd,type);
		if(inFd){
			close(inFd);
		}
		printf("File error warning\n");
		return;
	}
	
        struct stat statbuf;
	int readNum;
	char date[DATESIZE],last_modified[DATESIZE],buf[BUFSIZE];
	stat(filename,&statbuf);
	
	if(statbuf.st_size < 0){
		respond_with_number(411,connFd);
		return;
	}
	
	char *ext = get_Extension(filename), *mime;
	mime = mime_type(ext);
	server_date(date);
	server_last_modified(last_modified,statbuf);
	sprintf(buf, "HTTP/1.1 200 OK\r\n"
	"Date: %s\r\n"
	"Server: Icws\r\n"
	"Content-length: %lu\r\n"
	"Connection: %s\r\n"
	"Content-type: %s\r\n"
	"Last-Modified:%s\r\n\r\n",
	date,statbuf.st_size,type,mime,last_modified);
	write_all(connFd, buf, strlen(buf));
	if(key == 1){
	        char get[BUFSIZE];
		while((readNum = read(inFd,get,BUFSIZE)) > 0){
			write_all(connFd,get,readNum);
	        }
	}
	if(readNum == -1){
		fprintf(stderr,"read file error\n");
		return;
	}
	if(inFd){
		close(inFd);
	}
}

int serve_http(int connFd, char* root){   
   char buf[BUFSIZE],line[BUFSIZE];
   struct pollfd fds[1];
   int readline;  
   infinite{
   	fds[0].fd = connFd;
   	fds[0].events = POLLIN;
   	int pollret = poll(fds,1,timeout*1000);
   	if(pollret < 0){
   		return CLOSE;
   	}
   	else if(pollret == 0){
   		return CLOSE;
   	}
   	else{
   		while((readline = read(connFd,line,BUFSIZE)) > 0){
   			strcat(buf,line);
   			if(strstr(line,"\r\n\r\n") != NULL){
   			        memset(line,'\0',BUFSIZE);
   				break;
   			}
   			memset(line,'\0',BUFSIZE);
   		}
   		break;
   	}
   }
   pthread_mutex_lock(&mutex_parse);
   Request *request = parse(buf,BUFSIZE,connFd);
   pthread_mutex_unlock(&mutex_parse);
   int connection = PERSISTENT;
   char* connection_type;
    if(request == NULL){
    	respond_with_number(400,connFd);
    	memset(buf,0,BUFSIZE);
    	return connection;
   }
   for(int i = 0;i < request->header_count;i++){
   	if(strcmp(request->headers[i].header_name,"Connection") == 0){
   		if(strcmp(request->headers[i].header_value,"close")){
   		        connection = CLOSE;
   			connection_type = "close";
   		}
   		else{
   			connection_type = "keep-alive";
   		}
   		break;
   	}
   }
   if(strcmp(request->http_version,"HTTP/1.1") != 0){
   	respond_with_number(505,connFd);
   	free(request->headers);
        free(request);
        memset(buf,0,BUFSIZE);
   	return connection;
   }
   if(strcmp(request->http_method, "GET") == 0){
   	respond(connFd,root,request->http_uri,1,connection_type);
   }
   else if(strcmp(request->http_method, "HEAD") == 0){
   	respond(connFd,root,request->http_uri,0,connection_type);
   }
   else{
   	respond_with_number(501,connFd);
   }   
   
   free(request->headers);
   free(request);
   memset(buf,0,BUFSIZE);
   return connection;
}	

struct survival_bag{
	struct sockaddr_storage clientAddr;
	int connFd;
};

struct survival_bag taskQ[256];

void* conn_handler(struct survival_bag* task) {
	int connection = PERSISTENT;
	while(connection == PERSISTENT){
		connection = serve_http(task->connFd, dirName);
	}
	close(task->connFd);
	return NULL;
}  

void* thread_function(void *args){
	infinite
	{
		struct survival_bag task;
		pthread_mutex_lock(&mutex_q);
		while(taskCount == 0){
			pthread_cond_wait(&condition_var,&mutex_q);
		}
		for(int i = 0;i < taskCount-1;i++){
			taskQ[i] = taskQ[i+1];
		}
		taskCount--;
		pthread_mutex_unlock(&mutex_q);
		conn_handler(&taskQ[0]);
	}
}

//./icws --port <portnumber> --root <folderName> --numThreads <nThread> --timeout <ntime>
int main(int argc, char* argv[]) {
    if(argc < 7){
    	printf("Usage: ./icws --port <ListenPort> --root <rootFolder> --numThreads <nThread> --timeout <ntime>\n");
    	exit(-1);
    }
    
    int opt,option_index;
    char port[BUFSIZE],root[BUFSIZE],numThreads[BUFSIZE],time_out[BUFSIZE];
    
    struct option long_options[] = {
    	{"port",1,NULL,'a'},
    	{"root",1,NULL,'b'},
    	{"numThreads",1,NULL,'c'},
    	{"timeout",1,NULL,'d'}
    };
    
    while((opt = getopt_long(argc,argv,"a:b:",long_options,&option_index)) != -1){
    	switch(opt){
    		case 'a':
    			strcpy(port, optarg);
    			break;
    		case 'b':
    			strcpy(root, optarg);
    			break;
    		case 'c':
    			strcpy(numThreads, optarg);
    			break;
    		case 'd':
    			strcpy(time_out, optarg);
    			break;
    		case '?':
    			break;
    		default:
    			printf("optarg return %d\n", opt);
    	}
    }
    
    int listenFd = open_listenfd(port);
    thread_number = atoi(numThreads);
    timeout = atoi(time_out);
    dirName = root;
    
    for(int i = 0;i < thread_number;i++){
    	if(pthread_create(&thread_pool[i],NULL,thread_function,NULL) != 0){
    		printf("Failed to create thread");
    	}
    }

    infinite
    {
        struct sockaddr_storage clientAddr; 
        socklen_t clientLen = sizeof(struct sockaddr_storage); 
        int connFd = accept(listenFd, (SA *) &clientAddr, &clientLen);
        if(connFd < 0){
        	fprintf(stderr, "Failed to accpet\n");
        	continue;
        }
        
        struct survival_bag *context = (struct survival_bag *) malloc(sizeof(struct survival_bag));
        context->connFd = connFd;
        memcpy(&context->clientAddr,&clientAddr,sizeof(struct sockaddr_storage));
        
        char hostBuf[BUFSIZE], svcBuf[BUFSIZE];
        if (getnameinfo((SA *) &clientAddr, clientLen, hostBuf, BUFSIZE, svcBuf, BUFSIZE, 0) == 0){
        	 printf("Connection from %s:%s\n", hostBuf, svcBuf); 
        } 
        else{
        	 printf("Connection from UNKNOWN.");
        } 
       	pthread_mutex_lock(&mutex_q);
       
       	taskQ[taskCount] = *context;
       	taskCount++;
       
       	pthread_mutex_unlock(&mutex_q);
       	pthread_cond_signal(&condition_var);
    }
    
    for(int i = 0;i < thread_number;i++){
    	if(pthread_join(thread_pool[i],NULL) != 0){
    		printf("Failed to join thread");
    	}
    }
    return 0;
}
