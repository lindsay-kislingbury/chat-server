#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024


volatile sig_atomic_t shutdown_flag = 0;
volatile sig_atomic_t server_shutdown = 0;
int server_socket_fd;

// protects access to client_sockets array and client_count
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// protects access to chat_history file
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// how many clients are currently connected
int client_count = 0;

// client data
typedef struct {
    int socket_fd;
    char username[50];
    pthread_t thread_id;
    int active;
}client;

// array of client data
client client_array[MAX_CLIENTS];

// functions
int setup_server_socket(int port);
void *handle_client(void *arg);
void broadcast_message(char *message, int sender_fd);
void write_to_history(char *message);
void handle_shutdown(int sig);
void cleanup_server();
int add_client(client *new_client);
void remove_client(int socket_fd);

int setup_server_socket(int port){
    int server_fd; // socket
    struct sockaddr_in address; // ipv4 socket address
    int opt = 1;


    // create socket fd
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0){
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // set SO_RESUSEADDR to help with dev restarting server a lot.
    // prevents "address already in use"
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))){
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // clear address struct
    memset(&address, 0, sizeof(address));

    // setup address struct
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // bind socket to port
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0){
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    //listen for connections
    if(listen(server_fd, 3) < 0){
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return server_fd;
}

// add a new client, returns the index in teh clients array or -1 if full
int add_client(client *new_client){
    pthread_mutex_lock(&clients_mutex);

    if(client_count >= MAX_CLIENTS){
        pthread_mutex_unlock(&clients_mutex);
        return -1;
    }

    for(int i = 0; i < MAX_CLIENTS; i++){
        if(!client_array[i].active){
            client_array[i] = *new_client;
            client_array[i].active = 1;
            client_count++;
            pthread_mutex_unlock(&clients_mutex);
            return i;
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
    return -1;

}


void remove_client(int socket_fd){
    pthread_mutex_lock(&clients_mutex);

    for(int i = 0; i < MAX_CLIENTS; i++){
        if(client_array[i].active && client_array[i].socket_fd == socket_fd){
            client_array[i].active = 0;
            client_count--;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}



// get client username, store client data,
// get messages call broadcast message
void *handle_client(void *client_index_ptr){
    int client_index = *((int *)client_index_ptr);
    free(client_index_ptr);

    client *client_data = &client_array[client_index];
    int socket_fd = client_data->socket_fd;
    char buffer[BUFFER_SIZE];

    // send welcome message
    char *welcome = "Welcome to chat server! <(^.^)>\nPlease enter your username: ";
    write(socket_fd, welcome, strlen(welcome));

    // read username
    int bytes_read = read(socket_fd, buffer, sizeof(buffer) - 1);
    if(bytes_read <= 0){
        perror("error reading username");
        close(socket_fd);
        return NULL;
    }

    // handle newline and add null terminator
    if(bytes_read > 0 && buffer[bytes_read - 1] == '\n'){
        buffer[bytes_read - 1] = '\0';
    }
    else{
        buffer[bytes_read] = '\0';
    }

    // store username
    strncpy(client_data->username, buffer, sizeof(client_data->username) - 1);
    client_data->username[sizeof(client_data->username) - 1] = '\0';
    printf("Client Connected: %s\n", client_data->username);

    char username_confirmation[100];
    sprintf(username_confirmation,
        "Username '%s' recieved. You are now connected\n", client_data->username);
    if(write(socket_fd, username_confirmation, strlen(username_confirmation)) < 0){
        perror("Failed to send username confirmation");
    };


    char *msg_prompt = "Enter message (or 'exit' to quit): \n";
    if(write(socket_fd, msg_prompt, strlen(msg_prompt)) < 0){
        perror("Failed to send message prompt");
       };

    // start continuous message reading loop
    memset(buffer, 0, BUFFER_SIZE); 
    while(1){
        memset(buffer, 0, BUFFER_SIZE); 

        // read message from client
        int bytes_read = read(socket_fd, buffer, sizeof(buffer) - 1);
        if(bytes_read == 1 && buffer[0] == '\n'){
            continue;
        }

        // check if client disconnected unexpectedly
        if(bytes_read <= 0){
            break;
        }

        buffer[bytes_read] = '\0';

       // check for 'exit'
        if(strncmp(buffer, "exit", 4) == 0){
            break;
        }

        char formatted_msg[BUFFER_SIZE + 70];
        sprintf(formatted_msg, "%s: %s\n", client_data->username, buffer);
        
        // broadcast message and write to history
        broadcast_message(formatted_msg, socket_fd);
        write_to_history(formatted_msg);

    }


    // close connection
    if(bytes_read <= 0){
        printf("Client %s disconnected\n", client_data->username);
    } else if(strncmp(buffer, "exit", 4) == 0){
        printf("Client %s is exiting\n", client_data->username);
        char *goodbye = "Closing connection...\n";
        write(socket_fd, goodbye, strlen(goodbye));
    }

    if(socket_fd >= 0 && !server_shutdown){
        if(close(socket_fd) < 0){
            perror("close failed");
        }
        else{
            printf("Connection for '%s' closed succesfully\n", client_data->username);
        }     
   }

    // remove client
    remove_client(socket_fd);

    return NULL;
}



void broadcast_message(char *message, int sender_fd){
    printf("Broadcasting message: %s", message);

    // lock client data
    pthread_mutex_lock(&clients_mutex);

    // send message to all connected clients except the sender
    for(int i = 0; i < MAX_CLIENTS; i++){
        // skip the sender
        if(client_array[i].active && client_array[i].socket_fd != sender_fd){
            if(write(client_array[i].socket_fd, message, strlen(message)) < 0){
                perror("broadcast write failed.");
            }
        }
    }

    //unlock client mutex
    pthread_mutex_unlock(&clients_mutex);
}



void write_to_history(char *message){
    // get current time
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[30];

    strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", t);


    // open file in append mode
    pthread_mutex_lock(&file_mutex);
    FILE *history_file = fopen("chat_history", "a");
    if(history_file == NULL){
        perror("Error opening chat history file");
        pthread_mutex_unlock(&file_mutex);
        return;
    }

    // write timestamp and message
    fprintf(history_file, "%s %s", timestamp, message);
    fflush(history_file); // make sure its written to disk
    fclose(history_file);

    // unlock file
    pthread_mutex_unlock(&file_mutex);
    printf("Message written to history: %s\n", message);
}



void handle_shutdown(int sig){
    printf("\nRecieved signal %d. shutting down server...\n", sig);
    server_shutdown = 1;
    shutdown_flag = 1; // causes accept to return with an error when socket is closed
    
    if(server_socket_fd > 0){
        close(server_socket_fd);
        server_socket_fd = -1;
    }

    pthread_mutex_lock(&clients_mutex);
    pthread_mutex_unlock(&clients_mutex);
}



void cleanup_server(){
    printf("Doing server cleanup..\n");

    // close client connections and join threads
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENTS; i++){
        if(client_array[i].active){
            char *goodbye = "Server is shutting down. Goodbye!\n";
            write(client_array[i].socket_fd, goodbye, strlen(goodbye));
            close(client_array[i].socket_fd);
        
            pthread_join(client_array[i].thread_id, NULL);
            client_array[i].active = 0;
        }
    }

    client_count = 0;

    // destroy mutexes
    pthread_mutex_unlock(&clients_mutex);
    pthread_mutex_destroy(&clients_mutex);
    pthread_mutex_destroy(&file_mutex);

    printf("Server shutdown complete\n");
}




int main(int argc, char *argv[]){
    if(argc != 2){
        printf("Enter: %s <port>\n", argv[0]);
        return 1;
    }

    for(int i = 0; i< MAX_CLIENTS; i++){
        client_array[i].active = 0;
    }

    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);

    int port = atoi(argv[1]);
    server_socket_fd = setup_server_socket(port);

    printf("Server started on port %d\n", port);
    printf("Press Ctrl+C to shutdown server gracefully\n");

    while(!shutdown_flag){
        // accept new connections
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_socket_fd, (struct sockaddr*)&client_addr,
            &client_addr_len);

        if(client_fd < 0){
            if(shutdown_flag || errno == EINTR || errno == EBADF){
                printf("Accept interrupted by shutdown\n");
                break;
            }
            else{
                perror("accept");
                continue; // skip and try again
            }
        }

        // new client
        client new_client;
        memset(&new_client, 0, sizeof(client));
        new_client.socket_fd = client_fd;
        new_client.active = 1;

        int client_index = add_client(&new_client);

        if(client_index == -1){
            char *msg = "Server is full. Try again later\n";
            write(client_fd, msg, strlen(msg));
            close(client_fd);
            continue;
        }

        int *client_index_ptr = malloc(sizeof(int));
        if(client_index_ptr == NULL){
            perror("malloc failed");
            remove_client(client_fd);
            close(client_fd);
            continue;
        }
        *client_index_ptr = client_index;
        
        // new thread for client
        if(pthread_create(&client_array[client_index].thread_id, NULL, handle_client, client_index_ptr) != 0){
            perror("pthread_create failed");
            remove_client(client_fd);
            close(client_fd);
            free(client_index_ptr);
            continue;
        }

    }

    cleanup_server();

    return 0;
}
