#define _DEFAULT_SOURCE
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
#include <math.h>


// includes for reading tsl2561-daemon
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h> 
#include <sys/select.h>

#include <signal.h>

#define debug_print(...) do { if (DEBUG) fprintf(stderr, __VA_ARGS__); } while (0)
#define DEBUG 0

#define tlc5947_count 2
#define tlc5947_channels 24
#define EXTRA_LEDS 4
#define channels (tlc5947_count * tlc5947_channels)
#define BPW 12 // bits per word
#define DIGITS 4 // 4 7-segment displays attached
#define SEGMENTS 7
#define connected_leds ((SEGMENTS * DIGITS) + DIGITS + EXTRA_LEDS)
#define start_channel (channels - connected_leds) - 1
#define default_brightness 1000
//#define BRIGHTNESS_FACTOR 1.055484602
#define BRIGHTNESS_FACTOR 1.040810774


static int sockfd = 0;
static char recvBuff[1024];
static int current_ambient;
static char *tsl2561_address = "127.0.0.1"; // ip address for tsl2561
                                            // brightness daemon
#define moving_ave_period  10
static int brightness_buffer[moving_ave_period];
static float current_ambient_average;
static uint16_t brightness_samples = 0;
static int gpio20fd;


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
   0,     1,  2,  3,  4,  5,  6,  7,  8,  9,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,
  20,    22,  24,  26,  28,  30,  32,  34,  36,  38,  40,  42,  44,  46,  48,  50,  53,
  56,    59,  62,  65,  69,  73,  78,  83,  88,  93,  98,  103,  109,  115,  121,  127,
 133,   141,  149,  157,  164,  171,  178,  186,  196,  206,  216,  226,  235,  247,
 259,   270,  284,  298,  305,  319,  333,  347,  361,  375,  389,  417,  445,  473,
 487,   515,  542,  569,  569,  596,  623,  650,  676,  711,  763,  792,  792,  843,
 868,   918,  971,  1024,  1071,  1118,  1176,  1267,  1355,  1440,  1522,  1623,
1730,  1849,  1974,  2116,  2298,  2468,  2651,  2813,  3017,  3258,  3449,
3638,  3894,  4095};
/* 8x5 + 7x5 + 10 = 40 + 35 + 10 = 85 elements */

typedef struct digit_t {uint8_t segment[SEGMENTS];} digit_t;

///segment  A  B  C  D  E  F  G
/* digit_t const digit0 = { .segment = {47,46,45,44,43,42,41}}; */
/* digit_t const digit1 = { .segment = {39,38,37,36,35,34,33}}; */
/* digit_t const digit2 = { .segment = {31,30,29,28,27,26,25}}; */
/* digit_t const digit3 = { .segment = {23,22,21,20,19,18,17}}; */

// alternative way to declare segments
//int const segments[DIGITS][SEGMENTS] = {{47,46,45,44,43,42,41}, {39,38,37,36,35,34,33},
//                      {31,30,29,28,27,26,25}, {23,22,21,20,19,18,17}};


int const segments[DIGITS][SEGMENTS] = {{13,12,10,9,8,14,15},
                                        {17,16,6,5,4,18,19},
                                        {21,20,2,1,0,22,23},
                                        {45,44,26,25,24,46,47}};

/* int dp0 = 40; */
/* int dp1 = 32; */
/* int dp2 = 24; */
/* int dp3 = 16; */

int const decimalpoint[DIGITS] = {11,7,3,27};
int const colon[2][2] = {{39,40},{43,41}};

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

int cvalue = -1; // channel to set
//uint16_t gvalue = 0; // to this greyscale value
float gvaluef = 0;
int gvalue_set = 0; // flag to determine if we have set a gvalue or not
int dvalue = -1; // digit to set
int vvalue = -1; // value to set digit to
int walk_option = 0; //by default don't run the walk
int clock_option = 0; //
int help_option = 0;
int brightness_option = 0;
int quiet = 0;
int dynamic_brightness = 1;

int set_digit(int digit, int val, uint16_t greyscale);
int write_led_buffer(void);

void usage(void) {
  printf("Usage: clock [-w|-c channel -g greyscale ]\n");
  printf("                 -h    show this help message\n");
  printf("                 -b    show the current brightness detected by sensor\n");
  printf("                 -t    show the time\n");
  printf("                 -d <d> -v <v> -g <g>    set digit d to show value v at greyscale g\n");
  printf("\n\n");
  printf("  if greyscale is set then LED brightness will be fixed at greyscale\n");
}

uint16_t brightness_map(float brightness) {

  //  return(round( ( (float) brightness * 34.9) + 1));
  //  y=9.692 * x - 1.266
  uint16_t b;
  b = lrint(( 9.692 * brightness) - 1.266) ;
  if ( b >= 4015 ) {
    return 4015;
  } else if (b <= 5) {
    return 5;
  } else {
    return ( (b < 5) ? 5 : b);
  }
  return 0;
}


void update_average_brightness(void) {

  uint16_t sum = 0;
  for (uint16_t x = 0; x < brightness_samples ; x++) {
    sum = sum + brightness_buffer[x];
  }
  current_ambient_average =  (float) sum / (float) brightness_samples;
  printf("current_ambient_average: %.2f, map: %d\n",current_ambient_average, brightness_map(current_ambient_average));
  
}


int nthdigit(int x, int n)
{
  static int powersof10[] = {1, 10, 100, 1000};
  return ((x / powersof10[n]) % 10);
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
    debug_print("loopcounter: %d mod %d + %d: %d, gvalue: %ld\n",loopcounter,connected_leds,start_channel, (loopcounter % connected_leds )+ start_channel,lrint(gvaluef));
    buf[(loopcounter % connected_leds ) + start_channel] = 0;
    buf[((loopcounter+1) % connected_leds )+ start_channel ] = round(gvaluef);
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

// initialise gpio20 ready for blanking output on tlc5947 chips
int gpio20_init(void) {
    // see https://www.ics.com/blog/how-control-gpio-hardware-c-or-c

    // Export the desired pin by writing to /sys/class/gpio/export

    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd == -1) {
        perror("Unable to open /sys/class/gpio/export");
        exit(1);
    }

    if (write(fd, "20", 2) != 2) {
        perror("Error writing to /sys/class/gpio/export");
        exit(1);
    }

    close(fd);

    // Set the pin to be an output by writing "out" to /sys/class/gpio/gpio24/direction

    fd = open("/sys/class/gpio/gpio20/direction", O_WRONLY);
    if (fd == -1) {
        perror("Unable to open /sys/class/gpio/gpio20/direction");
        exit(1);
    }

    if (write(fd, "out", 3) != 3) {
        perror("Error writing to /sys/class/gpio/gpio20/direction");
        exit(1);
    }

    close(fd);
    // open the gpio ready for writing
    gpio20fd = open("/sys/class/gpio/gpio20/value", O_WRONLY);
    if (fd == -1) {
        perror("Unable to open /sys/class/gpio/gpio20/value");
        exit(1);
    } 
  return 0;  
}

int spi_init(void) {
  int i;
  for (i = 0; i < channels ; i++) {
    buf[i] = 0x0000;  }
  
  file = open("/dev/spidev1.0",O_WRONLY); //dev
  if(file < 0) {
    perror ("Error opening spidev1.0");
    exit(EXIT_FAILURE);
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
  // if we get here, we should return 0 as no errors detected
  // avoid the error: control reaches end of non-void function [-Werror=return-type] error
  return 0;
}

int set_digit(int digit, int val, uint16_t greyscale) {
  //debug_print("greyscale: %d\n",greyscale);
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
    //debug_print("digit: %d segment: %d f[%d][%d]: %d & greyscale: %d = %d\n", digit,i,val,i,f[val][i], greyscale,f[val][i] & greyscale  );
  }
  // if we get here, we should return 0 as no errors detected
  // avoid the error: control reaches end of non-void function [-Werror=return-type] error
  return 0;
}


void sigint_handler(int sig)
{
  /*do something*/
  debug_print("SIGINT %d received",sig);
  printf("killing process %d\n",getpid());
  printf("Closing socket\n");
  close(sockfd);
  close(gpio20fd);
  exit(0);
}


int recv_to(int fd, char *buffer, int len, int flags, int time_out) {

  fd_set readset;
  int result,recv_result, iof = -1;
  struct timeval tv;

  // Initialize the set
  FD_ZERO(&readset);
  FD_SET(fd, &readset);
   
  // Initialize time out struct
  tv.tv_sec = 0;
  tv.tv_usec = time_out * 1000;
  // select()
  result = select(fd+1, &readset, NULL, NULL, &tv);
  debug_print("select return result: %d\n",result);
     
  // Check status
  if (result < 0) {
    return -1;
  } else {
    if (result > 0 && FD_ISSET(fd, &readset)) {
      // Set non-blocking mode
      if ((iof = fcntl(fd, F_GETFL, 0)) != -1) {
        fcntl(fd, F_SETFL, iof | O_NONBLOCK);
      } else {
        printf("failed to set non-blocking mode\n");
      }
      // receive
      recv_result = recv(fd, buffer, len, flags);
      debug_print("number of bytes received: %d\n",recv_result);
      if (recv_result == 0) printf("recv result 0, TCP connection was closed\n");
      // set as before
      if (iof != -1)
        fcntl(fd, F_SETFL, iof);
      return recv_result;
    }
  }
  debug_print("result should be 0: %d\n",result);
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
  return 0;
}

void add_brightness_to_buffer(int cb) {
  static int16_t current_position = -1;

  current_position = (current_position + 1) % moving_ave_period ;
  brightness_buffer[current_position] = cb;
  brightness_samples++;
  if (brightness_samples > moving_ave_period) {
    brightness_samples = moving_ave_period;
  }
};

float min(float a, float b) {
  if (a <= b)
    return a;
  else return b;
}

float max(float a, float b) {
  if (a >= b)
    return a;
  else return b;
}


int get_brightness(char *ipaddress) {
  static int socket_open = 0;

  debug_print("in get_brightness\n");
  int n = 0;
  int broadband, ir, lux;
  if (socket_open == 0) { // open the socket if its not already open
    debug_print("socket not open, opening it\n");
    if (open_socket (tsl2561_address) != 0) {
      printf("failed to open socket\n");
    } else {
      debug_print("socket successfully opened\n");
      socket_open = 1;
    }
  } else {
    debug_print("socket already open, on with trying to read from it\n");
  }
  if ( socket_open !=0 ) { // now if its open, get the brightness
    debug_print("about to call recv_to\n");
    n = recv_to (sockfd,recvBuff,1024,MSG_DONTWAIT,450);
    if (n > 0 ) {
      recvBuff[n] = 0; //null terminate the string
      sscanf(recvBuff, "Test. RC: 0(Success), broadband: %d, ir: %d, lux: %d", &broadband, &ir, &lux);
      debug_print("broadband: %d, ir: %d, lux: %d\n",broadband,ir,lux);
      current_ambient = broadband;
      add_brightness_to_buffer(current_ambient);
      update_average_brightness();
      if (dynamic_brightness) {
        uint16_t map = brightness_map(current_ambient_average);
        float ngval;
        if (map < gvaluef) {
          // descending in brightness
          ngval = gvaluef / BRIGHTNESS_FACTOR;
          printf("descending: gvaluef %.2f map %d ngval %.2f\n",gvaluef,map,ngval);
          gvaluef = max(map, ngval);
          
        } else if (map > gvaluef) {
          // ascending brightness
          if (gvaluef <= 0) { // if gvaluef is 0 it will never
                              // increase so just change it to 1
            gvaluef = 1;
          }
          ngval = gvaluef * BRIGHTNESS_FACTOR;
          printf("ascending: gvaluef %.2f map %d ngval %.2f\n",gvaluef,map,ngval);
          gvaluef = min(map,ngval);
        } else {
          printf("map = gvaluef: %d %.2f\n",map,gvaluef);
        };
      };
      if (brightness_option) {
            printf("broadband brightness: %d\n",current_ambient);
      };
      
    } else {
      switch (n) {
      case 0:
        debug_print("recv_to result: read 0 bytes\n");
        break;        
      case -1:
        debug_print("recv_to result: returned -1, read again later\n");
        break;        
      case -2:
        debug_print("recv_to result: timed out\n");
        break;        
      default:
        printf("got something we shouldn't have  - aborting\n");
        exit(EXIT_FAILURE);
      }

      return 0;
    }
  }
  socket_open = 0;
  close(sockfd);
  // if we get here, we should return 0 as no errors detected
  // avoid the error: control reaches end of non-void function [-Werror=return-type] error
  return 0;
}

void clockfn() {
  
  if (!gvalue_set) { gvaluef = 0; }
  debug_print("in clock function\n");
  time_t t = time(NULL);
  struct tm tm;
  int current_hour;
  while(1) {
    t = time(NULL);
    tm = *localtime(&t);
    
    get_brightness ( tsl2561_address);
    
    // convert to 12 hour clock
    current_hour = ((tm.tm_hour + 11) % 12 + 1);
    
    debug_print("h: %d m: %d s: %d\n",tm.tm_hour, tm.tm_min, tm.tm_sec);
    debug_print("nthdigit(tm.tm_hour,2): %d\n", nthdigit(tm.tm_hour,2));
    
    // if the leading digit of the hour is 0, display it as blank
    if (nthdigit(current_hour,1) == 0) {
      set_digit (3,BLANK,round(gvaluef));
    } else {
      set_digit (3,nthdigit(current_hour,1),round(gvaluef));
    }
    set_digit(2,nthdigit(current_hour,0),round(gvaluef));
    set_digit(1,nthdigit(tm.tm_min,1),round(gvaluef));
    set_digit(0,nthdigit(tm.tm_min,0),round(gvaluef));
    //buf[decimalpoint[0]] = (tm.tm_sec % 2) * gvalue;
    // buf[colon[0][0]] = 1 * gvalue; // left top
    // buf[colon[0][1]] = 1 * gvalue; // left bottom
    buf[colon[1][0]] = 1 * round(gvaluef); // 
    buf[colon[1][1]] = 1 * round(gvaluef); //
    if (tm.tm_hour >= 12) { // set colon digit for hour
      buf[colon[0][0]] = 1 * round(gvaluef);
    } else {
      buf[colon[0][0]] = 0;
    };

    //root@clock:/sys/class/gpio/gpio20# echo 0 > /sys/class/gpio/gpio20/value 
        // blank by pulling gpio20 low
        if (write(gpio20fd, "0", 1) != 1) {
          debug_print("Error writing to /sys/class/gpio/gpio24/value");
          exit(1);
        }
        
    write_led_buffer();
    //root@clock:/sys/class/gpio/gpio20# echo 1 > /sys/class/gpio/gpio20/value
    // unblank by pulling gpio20 high
        if (write(gpio20fd, "1", 1) != 1) {
            debug_print("Error writing to /sys/class/gpio/gpio24/value");
            exit(1);
        }

    usleep(500000); 
  }
}


  int main(int argc, char *argv[])
  { 
    // set up ctrl-c signal handler
    signal(SIGINT, sigint_handler);

    //uint32_t speed = 1000000;
    //    uint8_t bpw = BPW;
    //    uint8_t mode = 0;

    opterr = 0;
    int c;
    while ((c = getopt (argc, argv, "hwbc:g:d:v:t")) != -1) {
      switch (c) {
      case 'h': // show usage message
        debug_print("command line args got: %c\n",c);
        help_option = 1;
        break;
      case 'b': // read brightness from tsl2561 and print it out
        debug_print("command line args got: %c\n",c);
        brightness_option = 1;
        break;
      case 'w': // walk
        debug_print("command line args got: %c\n",c);
        walk_option = 1;
        break;
      case 'c':
        debug_print("command line args got: %c\n",c);
        cvalue = atoi(optarg);
        break;
      case 't': // show the time
        debug_print("command line args got: %c\n",c);
        clock_option = 1;
        break;
      case 'g': // grey scale
        debug_print("command line args got: %c\n",c);
        gvaluef = atoi(optarg);
        //gvalue = PWMTable[gvalue];
        gvalue_set = 1;
        dynamic_brightness = 0;
        break;
      case 'd':
        debug_print("command line args got: %c\n",c);
        dvalue = atoi(optarg);
        break;
      case 'v': // value to set digit to
        debug_print("command line args got: %c\n",c);
        vvalue = atoi(optarg);
        break;
      default:
        debug_print("command line arg didn't match anything: %c\n",c);
        usage();
        exit(EXIT_FAILURE);
      }
    }

    if (help_option == 1) {
      usage();
      exit(0);
    }

    debug_print("cvalue: %d, gvalue: %.2f, dvalue: %d, vvalue: %d\n",cvalue,gvaluef,dvalue,vvalue);
    gpio20_init();
    spi_init();

    /* if (brightness_option == 1) { */
    /*   while (1) { */
    /*     if (get_brightness ( tsl2561_address) != 0) { */
    /*       printf("broadband brightness: %d\n",current_ambient);}; */
    /*     usleep(1000000); */
    /*   } */
    /*   exit (0); */
    /* } */
    
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
      if ((vvalue < 0) || (gvaluef < 0)) {
        debug_print("Error: -g and -v must both be set to use the digit option");
        exit(EXIT_FAILURE);
      }
      set_digit(dvalue,vvalue,round(gvaluef));
      write_led_buffer();
    } else {
      /// assuming we are setting individual segments
      buf[cvalue] = round(gvaluef);
      write_led_buffer();
    }
    close(file);
  }

  /*
    # Local Variables:
    # compile-command: "make clock"
    # End:
  */
