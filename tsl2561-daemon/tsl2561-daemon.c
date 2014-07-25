/* socket server example taken from
   http://www.thegeekstuff.com/2011/12/c-socket-programming/ */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>

#include "tsl2561/TSL2561.h"

int rc;
uint16_t broadband, ir;
uint32_t lux=0;
// prepare the sensor
// (the first parameter is the raspberry pi i2c master controller attached to the TSL2561, the second is the i2c selection jumper)
// The i2c selection address can be one of: TSL2561_ADDR_LOW, TSL2561_ADDR_FLOAT or TSL2561_ADDR_HIGH
TSL2561 light1 = TSL2561_INIT(1, TSL2561_ADDR_FLOAT);
	
int listenfd = 0, connfd = 0;
 

int init_tsl2561(void) {
  rc = TSL2561_OPEN(&light1);
  if(rc != 0) {
    fprintf(stderr, "Error initializing TSL2561 sensor (%s). Check your i2c bus (es. i2cdetect)\n", strerror(light1.lasterr));
    // you don't need to TSL2561_CLOSE() if TSL2561_OPEN() failed, but it's safe doing it.
    TSL2561_CLOSE(&light1);
    return 1;
  }
	
  // set the gain to 1X (it can be TSL2561_GAIN_1X or TSL2561_GAIN_16X)
  // use 16X gain to get more precision in dark ambients, or enable auto gain below
  rc = TSL2561_SETGAIN(&light1, TSL2561_GAIN_1X);
}

void sigint_handler(int sig)
{
  /*do something*/
  printf("killing process %d\n",getpid());
  printf("Closing socket\n");
  close(listenfd);
  exit(0);
}

int main(int argc, char *argv[])
{
  signal(SIGINT, sigint_handler);
  
  struct sockaddr_in serv_addr;
  
  char sendBuff[1025];
  time_t ticks; 
  
  
  init_tsl2561();
  
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  // see http://ubuntuforums.org/showthread.php?t=1351359 about
  // reusing addresses - http://alas.matf.bg.ac.rs/manuals/lspe/snode=104.html
  int opt = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
  
  memset(&serv_addr, '0', sizeof(serv_addr));
  memset(sendBuff, '0', sizeof(sendBuff)); 
  
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(5000); 
  
  if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0 ) {
    printf("\n Error : Could not bind socket. Error: %s\n", strerror(errno));
    return 1;
  }
  
  
  
  listen(listenfd, 10); 
  
  while(1)
    {
      connfd = accept(listenfd, (struct sockaddr*)NULL, NULL); 
      
      ticks = time(NULL);
      
      rc = TSL2561_SETINTEGRATIONTIME(&light1, TSL2561_INTEGRATIONTIME_101MS);
      
      // sense the luminosity from the sensor (lux is the luminosity taken in "lux" measure units)
      // the last parameter can be 1 to enable library auto gain, or 0 to disable it
      rc = TSL2561_SENSELIGHT(&light1, &broadband, &ir, &lux, 1);
      sprintf(sendBuff, "RC: %i(%s), broadband: %i, ir: %i, lux: %i\n", rc, strerror(light1.lasterr), broadband, ir, lux);
      
      
      //snprintf(sendBuff, sizeof(sendBuff), "%.24s\r\n", ctime(&ticks));
      write(connfd, sendBuff, strlen(sendBuff)); 
      
      close(connfd);
      sleep(1);
    }
}

/*
# Local Variables:
# compile-command: "gcc -g -std=c99 tsl2561-daemon.c tsl2561/TSL2561.c -o tsl2561-daemon"
# End:
*/
