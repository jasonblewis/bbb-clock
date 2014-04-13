/*
    spidevlib.c - A user-space program to comunicate using spidev.
                                Gustavo Zamboni
*/
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <string.h>

#define tlc5947_count 1
#define tlc5947_channels 1
#define channels (tlc5947_count * tlc5947_channels)
#define bpw 12 // bits per word

uint16_t buf[channels];
int com_serial;
int failcount;
 
struct spi_ioc_transfer xfer;
//////////
// Init SPIdev
//////////
int spi_init(char filename[40])
{
  int   file;
  __u8 mode = 0;
  __u8 lsb = 1;
  __u8 bits = bpw;
  __u32 speed=2500000;
 
  if ((file = open(filename,O_RDWR)) < 0)
    {
      printf("Failed to open the bus.");
      /* ERROR HANDLING; you can check errno to see what went wrong */
      com_serial=0;
      exit(1);
    }
 
  ///////////////
  // Verifications
  ///////////////
  //possible modes: mode |= SPI_LOOP; mode |= SPI_CPHA; mode |= SPI_CPOL; mode |= SPI_LSB_FIRST; mode |= SPI_CS_HIGH; mode |= SPI_3WIRE; mode |= SPI_NO_CS; mode |= SPI_READY;
  //multiple possibilities using |
  /*
                        if (ioctl(file, SPI_IOC_WR_MODE, &mode)<0)       {
                                perror("can't set spi mode");
                                return;
                                }
  */
 
  if (ioctl(file, SPI_IOC_RD_MODE, &mode) < 0)
    {
      perror("SPI rd_mode");
      return;
    }
  if (ioctl(file, SPI_IOC_RD_LSB_FIRST, &lsb) < 0)
    {
      perror("SPI rd_lsb_fist");
      return;
    }
  //sunxi supports only 8 bits
  /*
                        if (ioctl(file, SPI_IOC_WR_BITS_PER_WORD, 8)<0)      
                                {
                                perror("can't set bits per word");
                                return;
                                }
  */
  if (ioctl(file, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0) 
    {
      perror("SPI bits_per_word");
      return;
    }
  /*
                        if (ioctl(file, SPI_IOC_WR_MAX_SPEED_HZ, &speed)<0)      
                                {
                                perror("can't set max speed hz");
                                return;
                                }
  */
  if (ioctl(file, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0) 
    {
      perror("SPI max_speed_hz");
      return;
    }
         
 
  printf("%s: spi mode %d, %d bits %sper word, %d Hz max\n",filename, mode, bits, lsb ? "(lsb first) " : "", speed);
 
  //xfer[0].tx_buf = (unsigned long)buf;
  xfer.len = sizeof buf; /* Length of  command to write*/
  xfer.cs_change = 0; /* Keep CS activated */
  xfer.delay_usecs = 0; //delay in us
  xfer.speed_hz = 2500000; //speed
  xfer.bits_per_word = bpw; // bites per word 8
  
  return file;
}

//////////
// Write n bytes to the spi bus
//////////
void spi_write(int nbytes,uint16_t value[channels],int file)
{
  uint16_t buf[channels];
  int status;
 
  memset(buf, 0, sizeof buf);
  buf[0] = value[0];
  /* buf[1] = value[1]; */
  /* buf[2] = value[2]; */
  /* buf[3] = value[3]; */
  /* buf[4] = value[4]; */
  /* buf[5] = value[5]; */
  /* buf[6] = value[6]; */
  /* buf[7] = value[7]; */
  /* buf[8] = value[8]; */
  /* buf[9] = value[9]; */
  /* buf[10] = value[10]; */
  /* buf[11] = value[11]; */
  /* buf[12] = value[12]; */
  /* buf[13] = value[13]; */
  /* buf[14] = value[14]; */
  /* buf[15] = value[15]; */
  /* buf[16] = value[16]; */
  /* buf[17] = value[17]; */
  /* buf[18] = value[18]; */
  /* buf[19] = value[19]; */
  /* buf[20] = value[20]; */
  /* buf[21] = value[21]; */
  /* buf[22] = value[22]; */
  /* buf[23] = value[23]; */
  /* buf[24] = value[24]; */
  xfer.tx_buf = (unsigned long)buf;
  //  xfer.len = sizeof buf; /* number of words to transfer */
  xfer.len = 1; /* number of words to transfer */
  status = ioctl(file, SPI_IOC_MESSAGE(1), xfer);
  if (status < 0)
    {
      perror("SPI_IOC_MESSAGE");
      return;
    }
  //printf("env: %02x %02x %02x\n", buf[0], buf[1], buf[2]);
 
  com_serial=1;
  failcount=0;
}


uint16_t buf[channels];

int main(int argc, char **argv)
{ 
  int file;
  file = spi_init("/dev/spidev1.0"); //dev
  
  buf[0] = 0x000;
  /* buf[1] = 0xFFF; */
  /* buf[2] = 0x000; */
  /* buf[3] = 0xFFF; */
  /* buf[4] = 0x000; */
  /* buf[5] = 0xFFF; */
  /* buf[6] = 0x000; */
  /* buf[7] = 0xFFF; */
  /* buf[8] = 0x000; */
  /* buf[9] = 0xFFF; */
  /* buf[10] = 0x000; */
  /* buf[11] = 0xFFF; */
  /* buf[12] = 0x000; */
  /* buf[13] = 0xFFF; */
  /* buf[14] = 0x000; */
  /* buf[15] = 0xFFF; */
  /* buf[16] = 0x000; */
  /* buf[17] = 0xFFF; */
  /* buf[18] = 0x000; */
  /* buf[19] = 0xFFF; */
  /* buf[20] = 0x000; */
  /* buf[21] = 0xFFF; */
  /* buf[22] = 0x000; */
  /* buf[23] = 0xFFF; */
  /* buf[24] = 0x000; */
  spi_write(channels,buf,file); //this will write the buffer
                                          //out to the spi bus
  
  close(file);
}
/*
# Local Variables:
# compile-command: "gcc spi.c -o spi"
# End:
*/
