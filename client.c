#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

void irc_free(int sockfd);
int irc_connect(char *host, char *port);
struct addrinfo *servinfo;
static int MAX_MESSAGE_SIZE = 512;

int main(int argc, char *argv[])
{
        int status;     
        char *host = "127.0.0.1";           //default ip
        char *port = "6667";                //same port as server
        int sock = irc_connect(host, port); //set up socket
        if (sock == -1) 
        {
                perror("socket");
                return -1;
        }

        int fd = STDOUT_FILENO; 
        if (sock > fd) 
        {
                fd = sock;
        }
        fd++;

        int select_fd;
        while (1) 
        {
                fd_set rfds;    //read fd
                fd_set wfds;    //write fd
        
                FD_SET(STDIN_FILENO, &rfds);                    //set read fd
                FD_SET(sock, &rfds);
                FD_SET(STDOUT_FILENO, &wfds);                   //set write fd
                FD_SET(sock, &wfds);

                select_fd = select(fd, &rfds, &wfds, NULL, NULL);     //monitor read and write file descriptors and update select_fd
                if (select_fd == -1) 
                {
                        perror("ERROR: error with select()");
                        irc_free(sock);
                        return -1;
                }

                if (FD_ISSET(STDIN_FILENO, &rfds))  //read input
                {
                        ssize_t read_buf;
                        char inbuf[MAX_MESSAGE_SIZE + 2];
                        memset(&inbuf, 0, sizeof inbuf);        //allocate resources for read
                        read_buf = read(STDOUT_FILENO, inbuf, MAX_MESSAGE_SIZE);
                        if (read_buf < 1) 
                        {
                                perror("ERROR: read error");
                                irc_free(sock);
                                return -1;
                        }
                        if (inbuf[read_buf-1] == '\n')  //formatting message
                        {
                                inbuf[read_buf-1] = '\r';
                                inbuf[read_buf] = '\n';
                        }

                        //make sure that socket can be written to
                        if (!FD_ISSET(sock, &wfds)) 
                        {
                                //select on sock for writing
                                fd_set wfds_sock;
                                FD_SET(sock, &wfds_sock);
                                select_fd = select(sock + 1, NULL, &wfds_sock, NULL, NULL); //monitor write file descriptors and update select_fd
                                if (select_fd == -1) 
                                {
                                        perror("error with select()");
                                        irc_free(sock);
                                        return -1;
                                }
                        }

                        //write to the socket
                        read_buf = send(sock, inbuf, read_buf+1, 0);
                        if (read_buf == -1) 
                        {
                                perror("ERROR: error while sending");
                                irc_free(sock);
                                return -1;
                        }
                }
                if (FD_ISSET(sock, &rfds))  //if file descriptor is set
                {
                        //read the socket
                        ssize_t read_buf;
                        char buf[MAX_MESSAGE_SIZE + 1];
                        memset(&buf, 0, sizeof buf);
                        read_buf = recv(sock, (void *) &buf, MAX_MESSAGE_SIZE, 0);
                        if (read_buf < 1) 
                        {
                                perror("Exiting");
                                irc_free(sock);
                                return -1;
                        }
                        if (buf[MAX_MESSAGE_SIZE] != '\0')          //message was too large
                        {
                                char overbuf[MAX_MESSAGE_SIZE];
                                while (read_buf == MAX_MESSAGE_SIZE)
                                {
                                        read_buf = recv(sock, (void *) &overbuf, MAX_MESSAGE_SIZE, 0);  //recieve the rest of the message
                                        if (read_buf < 1) 
                                        {
                                                perror("ERROR: error with recv() overbuf");
                                                irc_free(sock);
                                                return -1;
                                        }
                                }
                        }
                        //check to see if we can write to stdout
                        if (FD_ISSET(STDOUT_FILENO, &wfds)) //if write fd is set
                        {
                                //select on stdout for writing 
                                fd_set wfds_stdout;
                                FD_SET(STDOUT_FILENO, &wfds_stdout);
                                select_fd = select(STDOUT_FILENO + 1, NULL, &wfds_stdout, NULL, NULL);
                                if (select_fd == -1) 
                                {
                                        perror("select");
                                        return -1;
                                }
                        }
                        //write to stdout
                        printf("%s", buf);
                }
        }         
        irc_free(sock); //free resources
        return 0;
}

//connect to the irc server
int irc_connect(char *host, char *port)
{
        //set up socket
        struct addrinfo irc_server;
        memset(&irc_server, 0, sizeof irc_server);  
        irc_server.ai_family = AF_INET;
        irc_server.ai_socktype = SOCK_STREAM;
        int status = getaddrinfo(host, port, &irc_server, &servinfo);
        if (status == -1) 
        {
                perror("ERROR: error getting address info");
                return -1;
        }
        
        int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == -1) 
        {
                freeaddrinfo(servinfo);
                perror("ERRIR: error with socket()");
                return -1;
        }
        status = connect(sock, servinfo->ai_addr, servinfo->ai_addrlen);
        if (status == -1) 
        {
                perror("ERROR: error with connect()");
                irc_free(sock);
                return -1;
        }  
        return sock;
}

//close connection and free resources
void irc_free(int sockfd)
{
        freeaddrinfo(servinfo);
        if (close(sockfd) == -1) 
        {
                perror("ERROR: error with close()");
        }        
}