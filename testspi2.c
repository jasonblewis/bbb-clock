#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#define tlc5947_count 2
#define tlc5947_channels 24
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


uint16_t buf[channels];
int file;
uint32_t speed = 1000000;
uint8_t bpw = BPW;
uint8_t mode = 0;


void print_usage(void) {
  printf("Usage: testspi2 [w|-c channel -g greyscale ]\n");
    }

int walk(void) {
      int loopcounter = 0;
      while (loopcounter < 1000) {  

      int numbytes = sizeof(buf[0]) * channels;
      printf("sending %d bytes\n",sizeof(buf[0]) * numbytes);
      //printf("sending %d bytes, %05x, %05x, %05x, %05x, %05x, %05x\n",sizeof(buf[0]) * numbytes, buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
      
      if(write(file,&buf, numbytes) != numbytes) {
        perror("Error writing spi:");
        //        fprintf(stderr, "There was an error writing to the spi device\n");
        return 1; }
    
    // walk the bit
    printf("loopcounter: %d mod 32+16: %d\n",loopcounter , (loopcounter % 32)+16);
    buf[(loopcounter % 32)+16] = 0;
    buf[((loopcounter+1) % 32)+16 ] = 0x055;
    loopcounter++;
    usleep(50000);
  }
      return 0;
}

int write_led_buffer(void) {
      int numbytes = sizeof(buf[0]) * channels;

      if(write(file,&buf, numbytes) != numbytes)
      {
        perror("Error writing spi:");
        return 1; }
    return 0;
}

int spi_init(void) {
  int i;
  for (i = 0; i < channels ; i++) {
    buf[i] = 0x0000;  }
  
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

  /* if (ioctl(file,SPI_IOC_WR_MODE,&mode) < 0) { */
  /* 	perror ("Error setting mode"); */
  /* }  */
  if (ioctl(file,SPI_IOC_RD_MODE,&mode) < 0) {
    perror ("Error reading mode"); }
  else {
    printf("mode is %d\n",mode);
  }
}

int main(int argc, char *argv[])
{ 
  uint32_t speed = 1000000;
  uint8_t bpw = BPW;
  uint8_t mode = 0;

  int c = 0;
  int cvalue = -1; // channel to set
  int gvalue = -1; // to this greyscale value

  opterr = 0;
     
  while ((c = getopt (argc, argv, "wc:g:")) != -1) {
    switch (c) {
    case 'w': // walk
      printf("got w\n");
      spi_init();
      walk();
      exit(0);
      break;
    case 'c':
      cvalue = atoi(optarg);
      break;
    case 'g':
      gvalue = atoi(optarg);
      break;
    default:
      print_usage();
      exit(EXIT_FAILURE);
    }
  }
  
  spi_init();
      buf[cvalue] = gvalue;
      write_led_buffer();
      
      close(file);
}
/*
# Local Variables:
# compile-command: "gcc -g testspi2.c -o testspi2"
# End:
*/
