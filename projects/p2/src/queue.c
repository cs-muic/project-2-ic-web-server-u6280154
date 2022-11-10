#include "queue.h"
#include <stdlib.h>

node_t* head = NULL;
node_t* tail = NULL;

void enqueue(int *client_socket){
	node_t *newnode = malloc(sizeof(node_t));
	newnode->client_socket = client_socket;
	newnode->next = NULL;
	switch(tail){
		case NULL:
			head = newnode;
			break;
		default:
			tail->next = newnode;
			break;
	}
	tail = newnode;
}

int* dequeue(){
	switch(head){
		case NULL: return NULL;
		default:
			int *result = head->
			
	
	}
	


}


  
