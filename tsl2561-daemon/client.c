/* example client for server
   http://www.thegeekstuff.com/2011/12/c-socket-programming/  */

 /* select examples: http://developerweb.net/viewtopic.php?id=2933 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> 
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>


int sockfd = 0;
char recvBuff[1024];

/*
   Params:
      fd       -  (int) socket file descriptor
      buffer - (char*) buffer to hold data
      len     - (int) maximum number of bytes to recv()
      flags   - (int) flags (as the fourth param to recv() )
      to       - (int) timeout in milliseconds
   Results:
      int      - The same as recv, but -2 == TIMEOUT
   Notes:
      You can only use it on file descriptors that are sockets!
      'to' must be different to 0
      'buffer' must not be NULL and must point to enough memory to hold at least 'len' bytes
      I WILL mix the C and C++ commenting styles...
*/
int recv_to(int fd, char *buffer, int len, int flags, int to) {

  fd_set readset,tempset;
   int result, iof = -1;
   struct timeval tv;

   // Initialize the set
   FD_ZERO(&readset);
   FD_SET(fd, &readset);
   
   // Initialize time out struct
   tv.tv_sec = 0;
   tv.tv_usec = to * 1000;
   // select()
   result = select(fd+1, &tempset, NULL, NULL, &tv);

   // Check status
   if (result < 0)
      return -1;
   else if (result > 0 && FD_ISSET(fd, &tempset)) {
      // Set non-blocking mode
      if ((iof = fcntl(fd, F_GETFL, 0)) != -1)
         fcntl(fd, F_SETFL, iof | O_NONBLOCK);
      // receive
      result = recv(fd, buffer, len, flags);
      // set as before
      if (iof != -1)
         fcntl(fd, F_SETFL, iof);
      return result;
   }
   return -2;
}

int open_socket(char *ipaddress) {

  struct sockaddr_in serv_addr; 

    
    memset(recvBuff, '0',sizeof(recvBuff));
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Error : Could not create socket \n");
        return 1;
    } 

    memset(&serv_addr, '0', sizeof(serv_addr)); 

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(5000); 

    if(inet_pton(AF_INET, ipaddress, &serv_addr.sin_addr)<=0)
    {
        printf("\n inet_pton error occured\n");
        return 1;
    } 

    if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
       printf("\n Error : Connect Failed \n");
       return 1;
    } 

}

int get_brightness(char *ipaddress) {

  int n = 0;
  int broadband, ir, lux;

    n = recv_to (sockfd,recvBuff,1024,MSG_DONTWAIT,450);
    if (n > 0 ) {
      recvBuff[n] = 0;
      sscanf(recvBuff, "RC: 0(Success), broadband: %d, ir: %d, lux: %d", &broadband, &ir, &lux);
      printf("Broadband: %d, ir: %d, lux: %d\n",broadband, ir, lux);
      //      if (fputs(recvBuff, stdout) == EOF) {
      //    printf("\n Error : Fputs error\n");
      //  }
    } else {
      switch (n) {
      case 0:
          printf("\n read 0 bytes \n");
          break;        
      case -1:
          printf("\n returned -1, read again later \n");
          break;        
      case -2:
          printf("\n timed out \n");
          break;        
      default:
        printf("got something we shouldn't have  - aborting\n");
        exit(EXIT_FAILURE);
      }

    return 0;
    }
}

void sigint_handler(int sig)
{
  /*do something*/
  printf("killing process %d\n",getpid());
  printf("Closing socket\n");
  close(sockfd);
  exit(0);
}


int main(int argc, char *argv[])
{

  signal(SIGINT, sigint_handler);
  
  if(argc != 2) {
    printf("\n Usage: %s <ip of server> \n",argv[0]);
    return 1;
  }

  
  int x;
  for ( x = 0; x < 1000; x++ ) {
    open_socket(argv[1]);
  
    get_brightness(argv[1]);

    close(sockfd);
    sleep(1);
  }
}

/*
# Local Variables:
# compile-command: "gcc -g -std=c99 client.c -o client"
# End:
*/
