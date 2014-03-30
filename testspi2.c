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
#define DIGITS 4 // 4 7-segment displays attached
#define SEGMENTS 7
/* brown - p9_22 - clock
   green - p9_17 - /cs - chip select, latch
   grey -  p9_18 - data out of BBB */


/* pinout taken from http://derekmolloy.ie/beaglebone/beaglebone-gpio-programming-on-arm-embedded-linux/

   SPI0_cs0  p9_17   /cs green
   SPI0_d1   p9_18   MISO (master in slave out) data out grey
   SPI0_d0   p9_21   MOSI (master out slave in) data in nc
   SPI0_sclk p9_22   clock brown
*/

const unsigned int PWMTable[] = {
  0,    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 13,
  16,   19, 23, 26, 29, 32,  35,  39,  43,  47,  51,  55, 60, 66,
  71,   77, 84, 91, 98, 106, 114, 123, 133, 143, 154, 166,
  179,  192, 207, 222, 239, 257, 276, 296, 317, 341, 366,
  392,  421, 451, 483, 518, 555, 595, 638, 684, 732, 784,
  840,  900, 964, 1032, 1105, 1184, 1267, 1357, 1453, 1555,
  1665, 1782, 1907, 2042, 2185, 2339, 2503, 2679, 2867, 3069,
  3284, 3514, 3761, 4024, 4095};

typedef struct digit_t {uint8_t segment[SEGMENTS];} digit_t;

                   ///segment  A  B  C  D  E  F  G
digit_t const digit0 = { .segment = {47,46,45,44,43,42,41}};
digit_t const digit1 = { .segment = {39,38,37,36,35,34,33}};
digit_t const digit2 = { .segment = {31,30,29,28,27,26,25}};
digit_t const digit3 = { .segment = {23,22,21,20,19,18,17}};

// alternative way to declare segments
int segments[DIGITS][SEGMENTS] = {{47,46,45,44,43,42,41}, {39,38,37,36,35,34,33},
                      {31,30,29,28,27,26,25}, {23,22,21,20,19,18,17}};

int dp0 = 40;
int dp1 = 32;
int dp2 = 24;
int dp3 = 16;

///segment  A  B  C  D  E  F  G
/* int const f0[] = {1, 1, 1, 1, 1, 1, 0}; */
/* int const f1[] = {0, 1, 1, 0, 0, 0, 0}; */
/* int const f2[] = {1, 1, 0, 1, 1, 0, 1}; */
/* int const f3[] = {1, 1, 1, 1, 0, 0, 1}; */
/* int const f4[] = {0, 1, 1, 0, 0, 1, 1}; */
/* int const f5[] = {1, 0, 1, 1, 0, 1, 1}; */
/* int const f6[] = {1, 1, 1, 1, 1, 0, 1}; */
/* int const f7[] = {1, 1, 1, 0, 0, 0, 0}; */
/* int const f8[] = {1, 1, 1, 1, 1, 1, 1}; */
/* int const f9[] = {1, 1, 1, 1, 0, 1, 1}; */

uint16_t const f[10][10] = {
  {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0x000},
  {0x000, 0xFFF, 0xFFF, 0x000, 0x000, 0x000, 0x000},
  {0xFFF, 0xFFF, 0x000, 0xFFF, 0xFFF, 0x000, 0xFFF},
  {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0x000, 0x000, 0xFFF},
  {0x000, 0xFFF, 0xFFF, 0x000, 0x000, 0xFFF, 0xFFF},
  {0xFFF, 0x000, 0xFFF, 0xFFF, 0x000, 0xFFF, 0xFFF},
  {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0x000, 0xFFF},
  {0xFFF, 0xFFF, 0xFFF, 0x000, 0x000, 0x000, 0x000},
  {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF},
  {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0x000, 0xFFF, 0xFFF}
};

uint16_t buf[channels];
int file;
uint32_t speed = 1000000;
uint8_t bpw = BPW;
uint8_t mode = 0;

int c = 0;
int cvalue = -1; // channel to set
int gvalue = -1; // to this greyscale value
int dvalue = -1; // digit to set
int vvalue = -1; // value to set digit to

 
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

int set_digit(int digit, int val, uint16_t greyscale) {
  if ((val < 0) || (val > 9)) {
    printf("Error: val must be in the range 0-9\n");
    exit(EXIT_FAILURE);}
  if ((digit < 0) || (digit > DIGITS-1)) {
    printf("Error: digit  must be in the range 0-%d\n", DIGITS - 1 );
    exit(EXIT_FAILURE);}
  int i;
  for (i = 0; i < SEGMENTS ; i++) {
    //buf[digit.segment[i]] = f[val][i] && greyscale;
    buf[segments[digit][i]] = f[val][i] && greyscale;
  }

}

int main(int argc, char *argv[])
{ 
  uint32_t speed = 1000000;
  uint8_t bpw = BPW;
  uint8_t mode = 0;

  opterr = 0;
     
  while ((c = getopt (argc, argv, "wc:g:d:v:")) != -1) {
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
    case 'd':
      dvalue = atoi(optarg);
      break;
    case 'v':
      vvalue = atoi(optarg);
      break;
    default:
      print_usage();
      exit(EXIT_FAILURE);
    }
  }
  
  spi_init();

  if (dvalue > -1) {
    // setting a digit to a value
    if ((vvalue < 0) || (gvalue < 0)) {
      printf("Error: -g and -v must both be set to use the digit option");
      exit(EXIT_FAILURE);
    }
    set_digit(dvalue,vvalue,gvalue);
  } else {
    /// assuming we are setting individual segments
    buf[cvalue] = gvalue;
    write_led_buffer();
  }
  close(file);

}
/*
# Local Variables:
# compile-command: "gcc -g testspi2.c -o testspi2"
# End:
*/
