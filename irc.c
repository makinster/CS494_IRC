#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>


#define MAX_CLIENTS 50
#define BUFFER_SZ 2048

static unsigned int client_count = 0;   //number of clients in server
static int uid = 100;                   // user id number
static int roomid = 1;                  //default room id

// Client struct
typedef struct {
    struct sockaddr_in addr;    // Client remote address 
    int connfd;                 // Connection file descriptor 
    int uid;                    // Client unique identifier 
    int roomid;                 // Client room id
    char name[32];              // Client name 
} client_t;

client_t *clients[MAX_CLIENTS];                             //init client_t struct
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;  //init threading

//function prototyping
void *handle_client(void *arg);
void queue_add(client_t *cl);
void queue_delete(int uid);
void message(char *s, int uid, int room_id);
void message_all(char *s);
void message_self(const char *s, int connfd);
void message_client(char *s, int uid);
void active_clients(int connfd);
void strip_newline(char *s);
void *handle_client(void *arg);



int main(int argc, char *argv[])
{
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    //set up socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(6667);
    signal(SIGPIPE, SIG_IGN);

    //bind socket
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) 
    {
        perror("Socket binding failed");
        return EXIT_FAILURE;
    }

    //listen
    if (listen(listenfd, 10) < 0) {
        perror("Socket listening failed");
        return EXIT_FAILURE;
    }
    printf("> Server started!\n");

    //accept and handle clients
    while (1) 
    {
        socklen_t clientlength = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clientlength);

        if ((client_count + 1) == MAX_CLIENTS)     //don't go past max number of clients
        {
            printf("> ERROR: max clients reached\n");
            close(connfd);
            continue;
        }

        //initialize client details
        client_t *my_client = (client_t *)malloc(sizeof(client_t));
        my_client ->addr = cli_addr;    //set address
        my_client ->roomid = roomid;    //set to default value of 1
        my_client ->connfd = connfd;    //unique fd for each client
        my_client ->uid = uid++;        //increment and store uid
        sprintf(my_client ->name, "%d", my_client ->uid);

        //Add client to the queue and fork thread
        queue_add(my_client);
        pthread_create(&tid, NULL, &handle_client, (void*)my_client);

        sleep(1);   //performance optimization
    }

    return EXIT_SUCCESS;
}


// Add client to queue
void queue_add(client_t *cl)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) 
    {
        if (!clients[i])           // = null
        {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

//Delete the client from queue 
void queue_delete(int uid)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) 
    {
        if (clients[i])     //is valid
        {
            if (clients[i]->uid == uid) {
                clients[i] = NULL;
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

//message all but the sender who are in same room
void message(char *s, int uid, int room_id)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) 
    {
        if (clients[i])                         //if valid client
        {
            if (clients[i]->roomid == room_id)  //if in same room
            {
                if (clients[i]->uid != uid)     //if not self
                {
                    if (write(clients[i]->connfd, s, strlen(s)) < 0)
                    {
                        perror("Write to descriptor failed");   //error checking
                        break;
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

//message all
void message_all(char *s)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i <MAX_CLIENTS; i++)
    {
        if (clients[i])     //if valid client
        {
            if (write(clients[i]->connfd, s, strlen(s)) < 0) 
            {
                perror("Write to descriptor failed");
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// message sender
void message_self(const char *s, int connfd)
{
    if (write(connfd, s, strlen(s)) < 0) 
    {
        perror("Write to descriptor failed");
        exit(-1);
    }
}

//Message select client
void message_client(char *s, int uid)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i])                     //if valid client
        { 
            if (clients[i]->uid == uid)     //if correct uid
            {
                if (write(clients[i]->connfd, s, strlen(s))<0) 
                {
                    perror("Write to descriptor failed");
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

//List all active clients
void active_clients(int connfd)
{
    char s[64];
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i]) 
        {
            sprintf(s, "[%d] %s - room: %d\r\n", clients[i]->uid, clients[i]->name,clients[i]->roomid);
            message_self(s, connfd);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

//Parse string of characters for correct formatting
void strip_newline(char *s)
{
    while (*s != '\0') 
    {
        if (*s == '\r' || *s == '\n') 
        {
            *s = '\0';
        }
        s++;
    }
}

//Handle all client messaging and "/" commands
void *handle_client(void *arg)
{
    char buff_out[BUFFER_SZ];
    char buff_in[BUFFER_SZ / 2];
    int rlen;                           //read length

    client_count++;
    client_t *my_client = (client_t *)arg;

    printf("Client number [%d] has joined\n", my_client ->uid);         //server info
    sprintf(buff_out, "[%s] has joined\r\n", my_client ->name);         //create message broadcast to all
    message_all(buff_out);                                              //broadcast to all clients
    message_self("> see /help for a list of commands\r\n>", my_client ->connfd);

    //endless loop for each client that recieves all inputs
    while ((rlen = read(my_client ->connfd, buff_in, sizeof(buff_in) - 1)) > 0) 
    {
        buff_in[rlen] = '\0';   //initialize
        buff_out[0] = '\0';     
        strip_newline(buff_in);

        // Ignore input if empty
        if (!strlen(buff_in)) 
        {
            continue;
        }

        // "/" character read, check for command
        if (buff_in[0] == '/') 
        {
            char *command, *param;
            command = strtok(buff_in," ");
            if (!strcmp(command, "/quit"))      //user wants to quit
            {
                break;
            } 
            else if (!strcmp(command, "/test")) //test server
            {
                message_self("> *boop*\r\n> ", my_client ->connfd);
            } 
            else if (!strcmp(command, "/nick")) //change nickname
            {
                param = strtok(NULL, " ");
                if (param) {
                    char *old_name = strdup(my_client ->name);
                    if (!old_name) {
                        perror("Cannot allocate memory");
                        continue;
                    }
                    strcpy(my_client ->name, param);
                    sprintf(buff_out, "> user [%s] is now known as [%s]\r\n", old_name, my_client ->name);
                    free(old_name);
                    message_all(buff_out);
                } 
                else 
                {
                    message_self("> name cannot be empty\r\n", my_client ->connfd);
                }
            } 
            else if (!strcmp(command, "/room"))     //change room (between 1 and 5)
            {
                param = strtok(NULL, " ");
                if (param) 
                {
                    int num = atoi(param);
                    if (num > 0 && num < 6)
                    {
                        my_client ->roomid = num;
                        if (!my_client ->name)
                        {
                            sprintf(buff_out, "> [%d] is now in room number %d\r\n", my_client ->uid, my_client ->roomid);
                            message_all(buff_out);
                        }
                        else
                        {
                            sprintf(buff_out, "> [%s] is now in room number %d\r\n", my_client ->name, my_client ->roomid);
                            message_all(buff_out);
                        }
                    }
                    else 
                    {
                        message_self("> invalid room, pick a number between 1 and 5.\r\n", my_client ->connfd);
                    }
                } 
                else 
                {
                    message_self("> invalid room, pick a number between 1 and 5.\r\n", my_client ->connfd);
                }
            } 
            else if (!strcmp(command, "/whisper"))  //send a private message to a select client
            {
                param = strtok(NULL, " ");
                if (param) 
                {
                    int uid = atoi(param);
                    param = strtok(NULL, " ");
                    if (param) 
                    {
                        sprintf(buff_out, "> [%s][whisper]", my_client ->name);
                        while (param != NULL) 
                        {
                            strcat(buff_out, " ");
                            strcat(buff_out, param);
                            param = strtok(NULL, " ");
                        }
                        strcat(buff_out, "\r\n");
                        message_client(buff_out, uid);
                    } 
                    else 
                    {
                        message_self("> message cannot be null\r\n", my_client ->connfd);
                    }

                } 
                else 
                {
                    message_self("> message cannot be null\r\n", my_client ->connfd);
                }
            } 
            else if(!strcmp(command, "/list"))      //view all active client memebers and their room id
            {
                sprintf(buff_out, "=============================\nClients in server: %d\r\n", client_count);
                message_self(buff_out, my_client ->connfd);
                active_clients(my_client ->connfd);
                sprintf(buff_out, "=============================\r\n");
                message_self(buff_out, my_client ->connfd);
            } 
            else if (!strcmp(command, "/help"))     //display all commands
            {
                strcat(buff_out, "===============================================================\r\n");
                strcat(buff_out, ">  /quit     Quit chatroom                                    <\r\n");
                strcat(buff_out, ">  /test     Server test                                      <\r\n");
                strcat(buff_out, ">  /room     <room number> change chat room                   <\r\n");
                strcat(buff_out, ">  /nick     <name> Change nickname                           <\r\n");
                strcat(buff_out, ">  /whisper  <user id> <message> Send private message         <\r\n");
                strcat(buff_out, ">  /list     Show active clients                              <\r\n");
                strcat(buff_out, ">  /help     Show help                                        <\r\n");
                strcat(buff_out, "===============================================================\r\n> ");
                message_self(buff_out, my_client ->connfd);
            } 
            else 
            {
                message_self("> unknown command\r\n", my_client ->connfd);
            }
        } 
        else    //user just wants to send a normal message
        {
            snprintf(buff_out, sizeof(buff_out), "> [%s] %s\r\n", my_client ->name, buff_in);
            message(buff_out, my_client ->uid,my_client ->roomid);
        }
    }

    //close connection
    sprintf(buff_out, "[%s] has left\r\n", my_client ->name);
    message_all(buff_out);
    close(my_client ->connfd);

    //delete client from queue and free resources
    queue_delete(my_client ->uid);
    printf("Client number [%d] has left the chat\n", my_client ->uid);
    free(my_client);
    client_count--;
    pthread_detach(pthread_self());

    return NULL;
}
