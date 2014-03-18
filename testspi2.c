

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#define tlc5947_count 1
#define tlc5947_channels 1
#define channels (tlc5947_count * tlc5947_channels)
#define BPW 12 // bits per word

/* brown - p9_22 - clock
   green - p9_17 - /cs - chip select, latch
   grey -  p9_18 - data out of BBB */

int main(int argc, char **argv)
{ 
  int file;
  uint16_t buf[channels];
  uint32_t speed = 1000000;
  uint8_t bpw = BPW;

  buf[0] = 0xF0F0;
  buf[1] = 0x0000;
  buf[2] = 0x0001;
  buf[3] = 0xFFF;
  buf[4] = 0xFFF;
  buf[5] = 0xFFF;
  
  /* int i; */
  /* for (i = 0; i < channels ; i++) { */
  /*   buf[i] = 0xF0F0;  */
  /* } */

    file = open("/dev/spidev1.0",O_WRONLY); //dev
      if(file < 0) {
        perror ("Error:");
      return 1;
    }
      if (ioctl(file,SPI_IOC_WR_MAX_SPEED_HZ,&speed) < 0) {
	perror ("Error setting speed");
      }
      if (ioctl(file,SPI_IOC_RD_MAX_SPEED_HZ,&speed) < 0) {
	perror ("Error reading speed");
      } else {
	printf("speed is %ul\n",speed);
      }
      if (ioctl(file,SPI_IOC_WR_BITS_PER_WORD,&bpw) < 0) {
	perror ("Error setting bpw");
      } 
      if (ioctl(file,SPI_IOC_RD_BITS_PER_WORD,&bpw) < 0) {
	perror ("Error reading bpw");
      } else {
	printf("bpw is %d\n",bpw);
      }
      
      int loopcounter = 0;
    while (loopcounter++ < 1) {  

    int numbytes = 6;
    printf("sending %d bytes\n",numbytes);
    
    if(write(file,&buf[0], numbytes) != numbytes)
      {
        perror("Error writing spi:");
          //        fprintf(stderr, "There was an error writing to the spi device\n");
        return 1;
      }
    
    //printf("before return 0");
    //return 0;
    
    usleep(500000);
  }
    close(file);
}
/*
# Local Variables:
# compile-command: "gcc -g testspi2.c -o testspi2"
# End:
*/
