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

#define BUFSIZE 1024

typedef struct sockaddr SA;

#define VYERROR_VERBOSE
#ifdef YACCDEBUG
#define YPRINTF(...) printf(_VA_ARGS_)
#else
#define YPRINTF(...)
#endif

char* get_Extension(char *filename){
	char* extension = filename;
	while(strrchr(extension,'.') != NULL){
		extension = strrchr(extension,'.');
		extension += 1;
	}
	return extension;
}

void respond_with_error(int connFd){
	char *msg = "<h1>404 NOT FOUND<h1>";
	char buf[BUFSIZE];
	sprintf(buf, "HTTP/1.1 404 NOT FOUND\r\n"
	"Server: Micro\r\n"
	"Content-length: %lu\r\n"
	"Connection: close\r\n"
	"Content-type: text/html\r\n\r\n",strlen(msg));
	write_all(connFd, buf, strlen(buf));
	write_all(connFd, buf, strlen(msg));
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

void respond_get(int connFd,char *root,char *temp,char *mime){
	char path[BUFSIZE];

	sprintf(path,"%s%s",root,temp);

	int inFd = open(path, O_RDONLY);
	if(inFd < 0){
		fprintf(stderr,"open file error\n");
		respond_with_error(connFd);
		return;
	}
	
        struct stat statbuf;
	char get[BUFSIZE];
	int readNum,fileNum;
	int retVal = 2;
	char buf[BUFSIZE];
	retVal = stat(path,&statbuf);
	
	sprintf(buf, "HTTP/1.1 200 OK\r\n"
	"Server: Icws\r\n"
	"Content-length: %lu\r\n"
	"Connection: close\r\n"
	"Content-type: %s\r\n\r\n",statbuf.st_size,mime);
	
	write_all(connFd, buf, strlen(buf));
	
	while((readNum = read(inFd,get,BUFSIZE)) > 0){
		write_all(connFd,get,readNum);
	}
	
	if(readNum == -1){
		fprintf(stderr,"read file error\n");
		return;
	}
}

void respond_head(int connFd,char *root,char *temp,char *mime){
	char path[BUFSIZE];

	sprintf(path,"%s%s",root,temp);

	int inFd = open(path, O_RDONLY);
	if(inFd < 0){
		fprintf(stderr,"open file error\n");
		respond_with_error(connFd);
		return;
	}
	
        struct stat statbuf;
	int retVal = 2;
	char buf[BUFSIZE];
	retVal = stat(path,&statbuf);
	
	sprintf(buf, "HTTP/1.1 200 OK\r\n"
	"Server: Icws\r\n"
	"Content-length: %lu\r\n"
	"Connection: close\r\n"
	"Content-type: %s\r\n\r\n",statbuf.st_size,mime);
	
	write_all(connFd, buf, strlen(buf));
	
	if(close(inFd) < 0){
		printf("Failed to close\n");
	}

}

void serve_http(int connFd, char* root){
    char buf[BUFSIZE];
    
    if(!read_line(connFd,buf,BUFSIZE)){
    	return;
    }
    
    char method[BUFSIZE],obj[BUFSIZE],version[BUFSIZE];
    sscanf(buf,"%s %s %s",method,obj,version);
    
    while(read_line(connFd,buf,BUFSIZE) > 0){
    	if(strcmp(buf,"\r\n") == 0){
    		break;
    	}
    }
    
    char* temp;
    char* mime;
    if(strcmp(method, "GET") == 0){
        temp = get_Extension(obj);
        if(strcmp(obj,"/") == 0){
                mime = mime_type("html");
        	respond_get(connFd,root,"/index.html",mime);
        }
        else{
                mime = mime_type(temp);
        	respond_get(connFd,root,obj,mime);
        }
     }
     else if(strcmp(method, "HEAD") == 0){
        temp = get_Extension(obj);
        if(strcmp(obj,"/") == 0){
                mime = mime_type("html");
        	respond_get(connFd,root,"/index.html",mime);
        }
        else{
                mime = mime_type(temp);
        	respond_get(connFd,root,obj,mime);
        }
     }
  }	
  
  
int main(int argc, char* argv[]) {
    int listenFd = open_listenfd(argv[1]);
    
    if(argv[2] == NULL){
    	fprintf(stderr,"root not provided\n");
    	exit(-1);
    }
    
    for (;;) {
        struct sockaddr_storage clientAddr; 
        socklen_t clientLen = sizeof(struct sockaddr_storage); 

        int connFd = accept(listenFd, (SA *) &clientAddr, &clientLen);
        
        char hostBuf[BUFSIZE], svcBuf[BUFSIZE];
        if (getnameinfo((SA *) &clientAddr, clientLen, hostBuf, BUFSIZE, svcBuf, BUFSIZE, 0) == 0) 
            printf("Connection from %s:%s\n", hostBuf, svcBuf); 
        else 
            printf("Connection from UNKNOWN.");
       
        serve_http(connFd,argv[2]); 
        close(connFd);
    }
    
    return 0;
}
