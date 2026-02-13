#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#define MAX_CLIENTS = 10;



volatile int keep_running = 1;
//pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; NOT NEEDED


void report(const char *msg, int code){
	perror(msg);
	exit(code);

}
void *receive_messages(void *arg){
	int sockfd = *(int *)arg;
	char buffer[1024];
	while(keep_running){
		memset(buffer, 0, sizeof(buffer));
		int r_bytes = recv(sockfd, buffer, sizeof(buffer), 0);
		if (r_bytes <= 0){
			printf("\nDisconnected from server\n");
			keep_running = 0; //thread cleanup
			break;}
	
		//pthread_mutex_lock(&lock);
		printf("%s\n", buffer);
		//pthread_mutex_unlock(&lock);}
			
	}
	return NULL;
	
}


void *send_messages(void *arg){
	int sockfd = *(int *)arg;
	char buffer[1024];

	while (keep_running){
		memset(buffer, 0, sizeof(buffer));
		fgets(buffer, sizeof(buffer), stdin);
		buffer[strcspn(buffer, "\n")] = '\0';

		//pthread_mutex_lock(&lock);
		send(sockfd, buffer, strlen(buffer), 0);
		printf("You: %s\n", buffer); 
		//pthread_mutex_unlock(&lock);

		if (strncmp(buffer, "exit", 4) == 0){
			keep_running = 0;//for thread cleanup
			break;
		}
	
	}
	return NULL;

}


int main(int argc, char *argv[]){
	//defining localhost, portnum, and sockfd
	char *Host  = "127.0.0.1";
	int sockfd;
	struct sockaddr_in saddr;
	char buffer[1024];
	char username[50];
	struct hostent *hptr;
	

	//getting port
	if (argc !=2){
		printf("Enter: %s <port>\n", argv[0]);
		return 1;
	}
	int PortNumber = atoi(argv[1]);


	//return a pointer to a hostent struct for the host name specified on the call
	hptr = gethostbyname(Host);
	if (!hptr)
		report("gethostbyname", 1);
	if (hptr->h_addrtype != AF_INET)
		report("bad address family", 2);
	

	//creating socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd<0)
		report("Error creating socket", 4);


	/*
	 *struct sockaddr_in{
	 	short 	sin_family; the address family
		unsigned short 	sin_port; 16-bit TCP or UDP port 
		struct in_addr	sin_addr; 32-bit IPv4 address
		char 	sin_zero[8]; unused padding field
	 
	 }
	 */

	//setting up socket
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr = *((struct in_addr *)hptr->h_addr_list[0]); 
	saddr.sin_port = htons(PortNumber);

	
	//if connection fails
	if (connect(sockfd, (struct sockaddr*) &saddr, sizeof(saddr)) <0)
	       report("failed to connect", 4);
	

	//reading welcome message
	memset(buffer, 0, sizeof(buffer));
	recv(sockfd, buffer, sizeof(buffer), 0);
	printf("%s", buffer);


	//send username
	fgets(username, sizeof(username), stdin);
	username[strcspn(username, "\n")] = 0;
	send(sockfd, username, strlen(username), 0);
	
	//username confirmation
	memset(buffer, 0, sizeof(buffer));
	recv(sockfd, buffer, sizeof(buffer), 0);
	printf("%s", buffer);

	//read prompt message
	memset(buffer, 0, sizeof(buffer));
	recv(sockfd, buffer, sizeof(buffer), 0);
	printf("%s", buffer);


	//creating thread
	pthread_t receive_thread, send_thread;
	pthread_create(&receive_thread, NULL, receive_messages, &sockfd);
	pthread_create(&send_thread, NULL, send_messages, &sockfd);
		

	
	//keep_running = 0;
	//join threads
	pthread_join(receive_thread, NULL);
	pthread_join(send_thread, NULL);
	//close socket
	close(sockfd);

	//destroy thread
	//pthread_mutex_destroy(&lock);
	return 0; 

}

