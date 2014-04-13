#define _BSD_SOURCE
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
#include <time.h>


#define debug_print(...) do { if (DEBUG) fprintf(stderr, __VA_ARGS__); } while (0)
#define DEBUG 1

#define tlc5947_count 2
#define tlc5947_channels 24
#define EXTRA_LEDS 4
#define channels (tlc5947_count * tlc5947_channels)
#define BPW 12 // bits per word
#define DIGITS 4 // 4 7-segment displays attached
#define SEGMENTS 7
#define connected_leds ((SEGMENTS * DIGITS) + DIGITS + EXTRA_LEDS)
#define start_channel (channels - connected_leds) - 1

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
/* digit_t const digit0 = { .segment = {47,46,45,44,43,42,41}}; */
/* digit_t const digit1 = { .segment = {39,38,37,36,35,34,33}}; */
/* digit_t const digit2 = { .segment = {31,30,29,28,27,26,25}}; */
/* digit_t const digit3 = { .segment = {23,22,21,20,19,18,17}}; */

// alternative way to declare segments
int const segments[DIGITS][SEGMENTS] = {{47,46,45,44,43,42,41}, {39,38,37,36,35,34,33},
                      {31,30,29,28,27,26,25}, {23,22,21,20,19,18,17}};

/* int dp0 = 40; */
/* int dp1 = 32; */
/* int dp2 = 24; */
/* int dp3 = 16; */

int const decimalpoint[DIGITS] = {40,32,24,16};
int const colon[2][2] = {{13,12},{15,14}};

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

uint16_t const f[11][SEGMENTS] = {
  {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0x000}, // 0
  {0x000, 0xFFF, 0xFFF, 0x000, 0x000, 0x000, 0x000}, // 1
  {0xFFF, 0xFFF, 0x000, 0xFFF, 0xFFF, 0x000, 0xFFF}, // 2
  {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0x000, 0x000, 0xFFF}, // 3
  {0x000, 0xFFF, 0xFFF, 0x000, 0x000, 0xFFF, 0xFFF}, // 4
  {0xFFF, 0x000, 0xFFF, 0xFFF, 0x000, 0xFFF, 0xFFF}, // 5
  {0xFFF, 0x000, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF}, // 6
  {0xFFF, 0xFFF, 0xFFF, 0x000, 0x000, 0x000, 0x000}, // 7
  {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF}, // 8
  {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0x000, 0xFFF, 0xFFF}, // 9
  {0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000}  // all off (10)
};
#define BLANK 10

uint16_t buf[channels];
int file;
uint32_t speed = 1000000; /* max speed to run the SPI bus at in MHz */
uint8_t bpw = BPW;
uint8_t mode = 0;

int c = 0;
int cvalue = -1; // channel to set
uint16_t gvalue = -1; // to this greyscale value
int dvalue = -1; // digit to set
int vvalue = -1; // value to set digit to
int walk_option = 0; //by default don't run the walk
int clock_option = 0; //
int help_option = 0;


int set_digit(int digit, int val, uint16_t greyscale);
int write_led_buffer(void);

void usage(void) {
  debug_print("Usage: testspi2 [w|-c channel -g greyscale ]\n");
  debug_print("                 -h    show this help message\n");
  debug_print("                 -t    show the time\n");
  debug_print("                 -d <d> -v <v> -g <g>    set digit d to show value v at greyscale g\n");
    }

int nthdigit(int x, int n)
{
    static int powersof10[] = {1, 10, 100, 1000};
    return ((x / powersof10[n]) % 10);
}

void clockfn() {
  debug_print("in clock function\n");
  time_t t = time(NULL);
  struct tm tm;
  int current_hour;
  while(1) {
    t = time(NULL);
    tm = *localtime(&t);

    // convert to 12 hour clock
    current_hour = ((tm.tm_hour + 11) % 12 + 1);
    
    debug_print("h: %d m: %d s: %d\n",tm.tm_hour, tm.tm_min, tm.tm_sec);
    debug_print("nthdigit(tm.tm_hour,2): %d\n", nthdigit(tm.tm_hour,2));

    // if the leading digit of the hour is 0, display it as blank
    if (nthdigit(current_hour,1) == 0) {
      set_digit (3,BLANK,1000);
    } else {
      set_digit (3,nthdigit(current_hour,1),1000);
    }
    set_digit(2,nthdigit(current_hour,0),1000);
    set_digit(1,nthdigit(tm.tm_min,1),1000);
    set_digit(0,nthdigit(tm.tm_min,0),1000);
    buf[decimalpoint[0]] = (tm.tm_sec % 2) * 1000;
    buf[colon[0][0]] = (tm.tm_sec % 2) * 1000;
    buf[colon[0][1]] = ((tm.tm_sec+1) % 2) * 1000;
    buf[colon[1][0]] = (tm.tm_sec % 2) * 1000;
    buf[colon[1][1]] = ((tm.tm_sec+1) % 2) * 1000;
    write_led_buffer();
    usleep(500000);
  }

}

int walk(void) {
      int loopcounter = 0;
      while (loopcounter < 1000) {  

      int numbytes = sizeof(buf[0]) * channels;
      //debug_print("sending %d bytes\n",sizeof(buf[0]) * numbytes);
      //debug_print("sending %d bytes, %05x, %05x, %05x, %05x, %05x, %05x\n",sizeof(buf[0]) * numbytes, buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
      
      if(write(file,&buf, numbytes) != numbytes) {
        perror("Error writing spi:");
        //        fdebug_print(stderr, "There was an error writing to the spi device\n");
        return 1; }
    
      // walk the bit
      debug_print("connected_leds: %d,", connected_leds );
      debug_print("start_channel: %d,", start_channel );
      debug_print("channels: %d\n", channels );
      debug_print("loopcounter: %d mod %d + %d: %d, gvalue: %d\n",loopcounter,connected_leds,start_channel, (loopcounter % connected_leds )+ start_channel,gvalue);
    buf[(loopcounter % connected_leds ) + start_channel] = 0;
    buf[((loopcounter+1) % connected_leds )+ start_channel ] = gvalue;
    loopcounter++;
    usleep(100000);
  }
      return 0;
}

int write_led_buffer(void) {
      int numbytes = sizeof(buf[0]) * channels;
      int byteswritten = 0;

      byteswritten = write(file,&buf, numbytes);
      if(byteswritten != numbytes)
      {
        
        debug_print("Error writing spi: tried to write %d bytes but only %d bytes were written\n",numbytes,byteswritten);
        perror("Error writing spi");
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
    debug_print("speed is %ul\n",speed);
  }
  if (ioctl(file,SPI_IOC_WR_BITS_PER_WORD,&bpw) < 0) {
    perror ("Error setting bpw");
  } 
  if (ioctl(file,SPI_IOC_RD_BITS_PER_WORD,&bpw) < 0) {
    perror ("Error reading bpw");
  } else {
    debug_print("bpw is %d\n",bpw);
  }

  /* if (ioctl(file,SPI_IOC_WR_MODE,&mode) < 0) { */
  /* 	perror ("Error setting mode"); */
  /* }  */
  if (ioctl(file,SPI_IOC_RD_MODE,&mode) < 0) {
    perror ("Error reading mode"); }
  else {
    debug_print("mode is %d\n",mode);
  }
}

int set_digit(int digit, int val, uint16_t greyscale) {
  debug_print("greyscale: %d\n",greyscale);
  if ((val < 0) || (val > BLANK)) {
    debug_print("Error: val was %d but must be in the range 0-9.\n", val);
    exit(EXIT_FAILURE);}
  if ((digit < 0) || (digit > DIGITS-1)) {
    debug_print("Error: digit  must be in the range 0-%d\n", DIGITS - 1 );
    exit(EXIT_FAILURE);}
  int i;
  for (i = 0; i < SEGMENTS ; i++) {
    //buf[digit.segment[i]] = f[val][i] & greyscale;
    buf[segments[digit][i]] = f[val][i] & greyscale;
    debug_print("digit: %d segment: %d f[%d][%d]: %d & greyscale: %d = %d\n", digit,i,val,i,f[val][i], greyscale,f[val][i] & greyscale  );
  }
}

int main(int argc, char *argv[])
{ 
  uint32_t speed = 1000000;
  uint8_t bpw = BPW;
  uint8_t mode = 0;

  opterr = 0;
     
  while ((c = getopt (argc, argv, "hwc:g:d:v:t")) != -1) {
    switch (c) {
    case 'h': // show usage message
      help_option = 1;
      break;
    case 'w': // walk
       walk_option = 1;
      break;
    case 'c':
      cvalue = atoi(optarg);
      break;
    case 't': // show the time
      clock_option = 1;
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
      usage();
      exit(EXIT_FAILURE);
    }
  }

  
  debug_print("cvalue: %d, gvalue: %d, dvalue: %d, vvalue: %d\n",cvalue,gvalue,dvalue,vvalue);
  spi_init();


  if (help_option == 1) {
    usage();
    exit(0);
  }

  if (walk_option == 1) {
    walk();
    exit(0);
  }

  if (clock_option == 1) {
    clockfn();
    exit(0);
  }

  if (dvalue >= 0) {
    // setting a digit to a value
    if ((vvalue < 0) || (gvalue < 0)) {
      debug_print("Error: -g and -v must both be set to use the digit option");
      exit(EXIT_FAILURE);
    }
    set_digit(dvalue,vvalue,gvalue);
    write_led_buffer();
  } else {
    /// assuming we are setting individual segments
    buf[cvalue] = gvalue;
    write_led_buffer();
  }
  close(file);

}

/*
# Local Variables:
# compile-command: "gcc -g -std=c99 testspi2.c -o testspi2"
# End:
*/