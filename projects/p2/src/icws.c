#define _GNU_SOURCE

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
#define ENVSIZE 21
#define infinite for(;;)

#define YYERROR_VERBOSE
#ifdef YACCDEBUG
#define YPRINTF(...) printf(_VA_ARGS_)
#else
#define YPRINTF(...)
#endif

typedef struct sockaddr SA;

char* dirName,*cgi_dirName,*port,*address;
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
        printf("ext is %s\n",ext);
	if(!strcmp(ext, "html")){
		return "text/html";
	}
	else if(!strcmp(ext, "css")){
		return "text/css";
	}
	else if(!strcmp(ext, "txt")){
		return "text/plain";
	}
	else if(!strcmp(ext, "js") || !strcmp(ext, "mjs")){
		return "text/javascript";
	}
	else if(!strcmp(ext, "jpg") || !strcmp(ext, "jpeg")){
		return "image/jpeg";
	}
	else if(!strcmp(ext, "png")){
		return "image/png";
	}
	else if(!strcmp(ext, "gif")){
		return "image/gif";
	}
	else{
		return NULL;
	}
}

void get_filename(char* temp,char* root,char* req){
	strcpy(temp, root);
	if(!strcmp(req,"/")){
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

        if(key == 2){
        	printf("Sorry, I havent do post yet please forgive me\n");
        	return;
        }
        
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

void fail_exit(char *msg) { fprintf(stderr, "%s\n", msg); exit(-1); }

int piper(int connFd,Request *request){
	char* args[2];
	args[0] = cgi_dirName;
	args[1] = NULL;
	
	char* header_name,*header_value;
	
	for(int i = 0;i < request->header_count;i++){
		header_name = request->headers[i].header_name;
		header_value = request->headers[i].header_value;
		if(!strcasecmp(header_name,"CONNECTION")){
			setenv("HTTP_CONNECTION",header_value,1);
		}
		else if(!strcasecmp(header_name,"ACCEPT")){
			setenv("HTTP_ACCEPT",header_value,1);
		}
		else if(!strcasecmp(header_name,"REFERER")){		
			setenv("HTTP_REFERER",header_value,1);
		}
		else if(!strcasecmp(header_name,"ACCEPT-ENCODING")){
			setenv("HTTP_ACCEPT_ENCODING",header_value,1);
		}
		else if(!strcasecmp(header_name,"ACCEPT-LANGUAGE")){
			setenv("HTTP_ACCEPT_LANGUAGE",header_value,1);
		}
		else if(!strcasecmp(header_name,"COTENT-LENGTH")){
			setenv("CONTENT_LENGTH",header_value,1);
		}
		else if(!strcasecmp(header_name,"USER-AGENT")){
			setenv("HTTP_USER_AGENT",header_value,1);
		}
		else if(!strcasecmp(header_name,"ACCEPT-COOKIE")){
			setenv("HTTP_COOKIE",header_value,1);
		}
		else if(!strcasecmp(header_name,"ACCEPT-CHARSET")){
			setenv("HTTP_ACCEPT_CHARSET",header_value,1);
		}
		else if(!strcasecmp(header_name,"HOST")){
			setenv("HTTP_HOST",header_value,1);
		}
		else if(!strcasecmp(header_name,"CONTENT-TYPE")){
			setenv("CONTENT_TYPE",header_value,1);
		}
	}
	//Query & path info
	char** tokenList[BUFSIZE];
	char* token = strtok(request->http_uri,"?");
	tokenList[0] = token;
	//printf("%s\n",tokenList[0]);
	token = strtok(NULL,"");

	//Remote address
	char *char_addr,addr[20];
	sprintf(addr,"%d",connFd); 
	char_addr = addr;
	
	setenv("SERVER_SOFTWARE","icws",1);
	setenv("GATE_INTERFACE","CGI/1.1",1);
	setenv("REQUEST_METHOD",request->http_method,1);
	setenv("REQUEST_URI",request->http_uri,1);
	setenv("SERVER_PROTOCOL","HTTP/1.1",1);
	setenv("QUERY_STRING",token,1);
	setenv("REMOTE_ADDR",char_addr,1);
	setenv("PATH_INFO",tokenList[0],1);
	setenv("SERVER_PORT",port,1);
	
	pid_t pid = 0;
	int pipefd[2];
	
	pipe(pipefd);
	pid = fork();
	if(!pid){
		close(pipefd[0]);
		dup2(pipefd[1],STDOUT_FILENO);
		execv(args[0],args);
	}
	close(pipefd[1]);

	int w;
	waitpid(pid,&w,WNOHANG);
	
	char buf[BUFSIZE];
	ssize_t numRead;
	while((numRead = read(pipefd[0],buf,BUFSIZE)) > 0){
		write_all(connFd,buf,numRead);
	}
	
	return 0;
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
   	else if(!pollret){
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
   
   printf("%s\n",buf);
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
   	if(!strcmp(request->headers[i].header_name,"Connection")){
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
   
   char cgi_checker[BUFSIZE];
   strncpy(cgi_checker,request->http_uri,5);
  
   
   if(strcmp(request->http_version,"HTTP/1.1") != 0){
   	respond_with_number(505,connFd);
   	free(request->headers);
        free(request);
        memset(buf,0,BUFSIZE);
   	return connection;
   }
   
   if((strcasecmp(request->http_method, "GET") == 0 || strcasecmp(request->http_method, "HEAD") == 0 || || strcasecmp(request->http_method, "POST") == 0) && strcmp(cgi_checker,"/cgi/") == 0){
        printf("Pipe activate--------\n");
   	piper(connFd,request);
   }
   else if(!strcmp(request->http_method, "GET")){
   	respond(connFd,root,request->http_uri,1,connection_type);
   }
   else if(!strcmp(request->http_method, "HEAD")){
   	respond(connFd,root,request->http_uri,0,connection_type);
   }
   else if(!strcmp(request->http_method, "POST")){
   	respond(connFd,root,request->http_uri,2,connection_type);
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
		while(!taskCount){
			pthread_cond_wait(&condition_var,&mutex_q);
		}
		
		task = taskQ[0];
		for(int i = 0;i < taskCount-1;i++){
			taskQ[i] = taskQ[i+1];
		}
		
		
		taskCount--;
		pthread_mutex_unlock(&mutex_q);
		
		conn_handler(&task);
		
		if(task.connFd < 0){
			break;
		}
	}
}



void signalHandler(int sig){

	struct survival_bag *overdose = (struct survival_bag *) malloc(sizeof(struct survival_bag));
	overdose->connFd = -1;
	
	for(int i = 0;i < thread_number; i++){
	 	pthread_mutex_lock(&mutex_q);
       		taskQ[taskCount] = *overdose;
       		taskCount++;
       		pthread_mutex_unlock(&mutex_q);
       		pthread_cond_signal(&condition_var);
	}

}
//./icws --port <portnumber> --root <folderName> --numThreads <nThread> --timeout <ntime>
int main(int argc, char* argv[]) {
    if(argc < 7){
    	printf("Usage: ./icws --port <ListenPort> --root <rootFolder> --numThreads <nThread> --timeout <ntime>\n");
    	exit(-1);
    }
    
    int opt,option_index;
    char port_temp[BUFSIZE],root[BUFSIZE],numThreads[BUFSIZE],time_out[BUFSIZE],cgi_temp[BUFSIZE];
    
    struct option long_options[] = {
    	{"port",1,NULL,'a'},
    	{"root",1,NULL,'b'},
    	{"numThreads",1,NULL,'c'},
    	{"timeout",1,NULL,'d'},
    	{"cgiHandler",1,NULL,'e'}
   };
    
    while((opt = getopt_long(argc,argv,"a:b:c:d:e:",long_options,&option_index)) != -1){
    	switch(opt){
    		case 'a':
    			strcpy(port_temp, optarg);
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
    		case 'e':
    			strcpy(cgi_temp, optarg);
    			break;
    		case '?':
    			break;
    		default:
    			printf("optarg return %d\n", opt);
    	}
    }
    
    port = port_temp;
    cgi_dirName = cgi_temp;
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
        	fprintf(stderr, "Failed to accept\n");
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
        
        address = hostBuf;
        
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
    
    pthread_mutex_destroy(&mutex_q);
    pthread_mutex_destroy(&mutex_parse);
    pthread_cond_destroy(&condition_var);
    return 0;
}
