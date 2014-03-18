

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
#define tlc5947_channels 48
#define channels (tlc5947_count * tlc5947_channels)
#define BPW 12 // bits per word

/* brown - p9_22 - clock
   green - p9_17 - /cs - chip select, latch
   grey -  p9_18 - data out of BBB */


const unsigned int PWMTable[] = {
  0,    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 13,
  16,   19, 23, 26, 29, 32,  35,  39,  43,  47,  51,  55, 60, 66,
  71,   77, 84, 91, 98, 106, 114, 123, 133, 143, 154, 166,
  179,  192, 207, 222, 239, 257, 276, 296, 317, 341, 366,
  392,  421, 451, 483, 518, 555, 595, 638, 684, 732, 784,
  840,  900, 964, 1032, 1105, 1184, 1267, 1357, 1453, 1555,
  1665, 1782, 1907, 2042, 2185, 2339, 2503, 2679, 2867, 3069,
  3284, 3514, 3761, 4024, 4095};


int main(int argc, char **argv)
{ 
  int file;
  uint16_t buf[channels];
  uint32_t speed = 1000000;
  uint8_t bpw = BPW;

  /* buf[0] = 0xF0F0; */
  /* buf[1] = 0x0000; */
  /* buf[2] = 0x0001; */
  /* buf[3] = 0x0FFF; */
  /* buf[4] = 0x000F; */
  /* buf[5] = 0x00FF; */
  
  /* int i; */
  /* for (i = 0; i < channels ; i++) { */
  /*   buf[i] = PWMTable[i+1]; */
  /* } */

  int i;
  for (i = 0; i < channels ; i++) {
    buf[i] = 0x0000;
  }

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
    while (1) {  

      int numbytes = sizeof(buf[0]) * 48;
      //printf("sending %d bytes\n",numbytes);
    
    if(write(file,&buf[0], numbytes) != numbytes)
      {
        perror("Error writing spi:");
          //        fprintf(stderr, "There was an error writing to the spi device\n");
        return 1;
      }
    
    // walk the bit
    //printf("loopcounter: %d mod 32+16: %d\n",loopcounter , (loopcounter % 32)+16);
    buf[(loopcounter % 32)+16] = 0;
    buf[((loopcounter+1) % 32)+16 ] = 0x5555;
    loopcounter++;
    usleep(30000);
  }
    close(file);
}
/*
# Local Variables:
# compile-command: "gcc -g testspi2.c -o testspi2"
# End:
*/
