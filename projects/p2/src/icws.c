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

#define BUFSIZE 8192
#define DATESIZE 30

typedef struct sockaddr SA;

#define YYERROR_VERBOSE
#ifdef YACCDEBUG
#define YPRINTF(...) printf(_VA_ARGS_)
#else
#define YPRINTF(...)
#endif
void respond_404(int connFd){
	char *msg = "<h1>404 NOT FOUND</h1>";
	char buf[BUFSIZE];
	sprintf(buf, "HTTP/1.1 404 Not Found\r\n"
	"Server: Icws\r\n"
	"Content-length: %lu\r\n"
	"Connection: close\r\n"
	"Content-type: text/html\r\n\r\n",strlen(msg));
	write_all(connFd, buf, strlen(buf));
	write_all(connFd, msg, strlen(msg));
}

void respond_501(int connFd){
	char *msg = "<h1>501 METHOD NOT IMPLEMENTED</h1>";
	char buf[BUFSIZE];
	sprintf(buf, "HTTP/1.1 501 method not implemented\r\n"
	"Server: Icws\r\n"
	"Content-length: %lu\r\n"
	"Connection: close\r\n"
	"Content-type: text/html\r\n\r\n",strlen(msg));
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

void respond(int connFd,char *root,char *object,int key){
	char filename[BUFSIZE];
	get_filename(filename, root, object);
	int inFd = open(filename, O_RDONLY);
	if(inFd < 0){
		fprintf(stderr,"open file error\n");
		respond_404(connFd);
		if(inFd){
			close(inFd);
		}
		return;
	}
	
        struct stat statbuf;
	char get[BUFSIZE];
	int readNum;
	char buf[BUFSIZE];
	stat(filename,&statbuf);
	
	char *ext = get_Extension(filename);
	char *mime;
	mime = mime_type(ext);
	
	char date[DATESIZE];
	char last_modified[DATESIZE];
	server_date(date);
	server_last_modified(last_modified,statbuf);
	
	sprintf(buf, "HTTP/1.1 200 OK\r\n"
	"Date: %s\r\n"
	"Server: Icws\r\n"
	"Content-length: %lu\r\n"
	"Connection: close\r\n"
	"Content-type: %s\r\n"
	"Last-Modified:%s\r\n\r\n",date,statbuf.st_size,mime,last_modified);
	
	write_all(connFd, buf, strlen(buf));
	
	if(key == 1){
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

void serve_http(int connFd, char* root){
   char buf[BUFSIZE];
   
   if(!read_line(connFd,buf,BUFSIZE)){
    	return;
    }
   
   char line[BUFSIZE];
   
   if(!read_line(connFd,line,BUFSIZE)){
    	return;
    }
    
   while(read_line(connFd, line, BUFSIZE) > 0){
   	strcat(buf, line);
   	if(strcmp(line,"\r\n") == 0){
   		break;
   	}
   }
   printf("%s\n",buf);
   Request *request = parse(buf,BUFSIZE,connFd);
   if(strcmp(request->http_method, "GET") == 0){
   	respond(connFd,root,request->http_uri,1);
   }
   else if(strcmp(request->http_method, "HEAD") == 0){
   	respond(connFd,root,request->http_uri,0);
   }
   else{
   	respond_501(connFd);
   }
   
   free(request->headers);
   free(request);
  
}	
  
//./icws --port <portnumber> --root <folderName>

int main(int argc, char* argv[]) {
    if(argc < 4){
    	printf("Usage: ./icws --port <ListenPort> --root <rootFolder>\n");
    	exit(-1);
    }
    
    int opt;
    int option_index;
    char port[BUFSIZE];
    char root[BUFSIZE];
    
    struct option long_options[] = {
    	{"port",1,NULL,'a'},
    	{"root",1,NULL,'b'}
    };
    
    while((opt = getopt_long(argc,argv,"a:b:",long_options,&option_index)) != -1){
    	switch(opt){
    		case 'a':
    			strcpy(port, optarg);
    			break;
    		case 'b':
    			strcpy(root, optarg);
    			break;
    		case '?':
    			break;
    		default:
    			printf("optarg return %d\n", opt);
    	}
    }
    
    int listenFd = open_listenfd(port);
    
    for (;;) {
        struct sockaddr_storage clientAddr; 
        socklen_t clientLen = sizeof(struct sockaddr_storage); 

        int connFd = accept(listenFd, (SA *) &clientAddr, &clientLen);
        
        char hostBuf[BUFSIZE], svcBuf[BUFSIZE];
        if (getnameinfo((SA *) &clientAddr, clientLen, hostBuf, BUFSIZE, svcBuf, BUFSIZE, 0) == 0){
        	printf("Connection from %s:%s\n", hostBuf, svcBuf); 
        }
        else{
        	printf("Connection from UNKNOWN.");
        }
            
        serve_http(connFd,root);
        close(connFd);
        printf("Activate\n");
    }
    
    return 0;
}
