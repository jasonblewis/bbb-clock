/* socket server example taken from
   http://www.thegeekstuff.com/2011/12/c-socket-programming/ */

#include <sys/socket.h>
#include <sys/select.h>
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
#include <fcntl.h>

#include "tsl2561/TSL2561.h"

/* How often the sensor is sampled into the cache, independent of client
   connection rate. The served value is at most this stale. ~200 ms is a safe
   default: it comfortably exceeds the 101 ms integration time and stays well
   above the effective sensor rate, while letting the daemon drain the clock's
   ~15 Hz reconnect burst (during a brightness ramp) between samples. See #21. */
#define SAMPLE_INTERVAL_MS 200

int rc;
uint16_t broadband, ir;
uint32_t lux=0;
// prepare the sensor
// (the first parameter is the raspberry pi i2c master controller attached to the TSL2561, the second is the i2c selection jumper)
// The i2c selection address can be one of: TSL2561_ADDR_LOW, TSL2561_ADDR_FLOAT or TSL2561_ADDR_HIGH
TSL2561 light1 = TSL2561_INIT(2, TSL2561_ADDR_FLOAT); // i2c bus 2 (/dev/i2c-2) on Debian 13 / kernel 6.x; was bus 1 on the old 3.8 kernel

int listenfd = 0, connfd = 0;

/* The most recent reading, formatted as the wire line served to every client.
   Refreshed only by sample_sensor() on the cadence below; each accept() writes
   this verbatim and returns immediately, so no sensor read sits on the serve
   path. Format is byte-for-byte unchanged so clock.c's sscanf and telemetry's
   regex keep parsing it. */
char cachedBuff[1025];


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
  return 0;
}

/* Read the sensor once (single I2C owner: only ever called from the main
   loop, never concurrently) and refresh the cached wire line. This is where
   the ~101 ms integration latency lives — off the connection-serving path. */
void sample_sensor(void)
{
  rc = TSL2561_SETINTEGRATIONTIME(&light1, TSL2561_INTEGRATIONTIME_101MS);

  // sense the luminosity from the sensor (lux is the luminosity taken in "lux" measure units)
  // the last parameter can be 1 to enable library auto gain, or 0 to disable it
  rc = TSL2561_SENSELIGHT(&light1, &broadband, &ir, &lux, 1);
  snprintf(cachedBuff, sizeof(cachedBuff), "RC: %i(%s), broadband: %i, ir: %i, lux: %i\n", rc, strerror(light1.lasterr), broadband, ir, lux);
}

/* Milliseconds from now to deadline (deadline - now), using a monotonic clock
   so it is immune to wall-clock steps (NTP, settimeofday). */
static long ms_until(const struct timespec *deadline, const struct timespec *now)
{
  return (deadline->tv_sec  - now->tv_sec)  * 1000L
       + (deadline->tv_nsec - now->tv_nsec) / 1000000L;
}

/* Advance a monotonic deadline by SAMPLE_INTERVAL_MS, normalising nsec. */
static void advance_deadline(struct timespec *t)
{
  t->tv_sec  += SAMPLE_INTERVAL_MS / 1000;
  t->tv_nsec += (SAMPLE_INTERVAL_MS % 1000) * 1000000L;
  if (t->tv_nsec >= 1000000000L) { t->tv_nsec -= 1000000000L; t->tv_sec += 1; }
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

  if (init_tsl2561() != 0) {
    // Sensor open failed: exit so systemd (Restart=on-failure) retries, rather
    // than staying up and serving RC!=0 error lines forever.
    return 1;
  }

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  // see http://ubuntuforums.org/showthread.php?t=1351359 about
  // reusing addresses - http://alas.matf.bg.ac.rs/manuals/lspe/snode=104.html
  int opt = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));

  // Non-blocking listen socket: we only accept() after select() reports the
  // socket readable, but a client that aborts (RST) in between would otherwise
  // make a blocking accept() hang and stall the whole cadence loop. With
  // O_NONBLOCK such an accept() returns EAGAIN and we just loop back.
  int flags = fcntl(listenfd, F_GETFL, 0);
  if (flags == -1 || fcntl(listenfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    printf("\n Error : Could not set listen socket non-blocking. Error: %s\n", strerror(errno));
    return 1;
  }

  memset(&serv_addr, '0', sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(5000);

  if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0 ) {
    printf("\n Error : Could not bind socket. Error: %s\n", strerror(errno));
    return 1;
  }

  // Deep backlog: the clock reconnects up to ~15 Hz during a ramp, so a handful
  // of connections can queue during one ~101 ms sample. Hold them rather than
  // drop SYNs, then drain them in a burst once the sample completes.
  listen(listenfd, 128);

  // Prime the cache with a first reading before serving anyone, so the very
  // first client gets a real value rather than an empty line.
  sample_sensor();
  struct timespec next_sample;
  clock_gettime(CLOCK_MONOTONIC, &next_sample);
  advance_deadline(&next_sample);

  while(1)
    {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      long wait_ms = ms_until(&next_sample, &now);

      if (wait_ms <= 0) {
        // Cadence due: take a fresh sample and schedule the next one. Advancing
        // from next_sample (not from now) keeps the cadence steady across the
        // ~101 ms the sample itself takes.
        sample_sensor();
        advance_deadline(&next_sample);
        // If we fell more than a full interval behind (a scheduling stall),
        // resync to now instead of firing a burst of back-to-back catch-up
        // samples that would leave queued clients unserved meanwhile.
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (ms_until(&next_sample, &now) <= 0) {
          next_sample = now;
          advance_deadline(&next_sample);
        }
        continue;
      }

      // Block until either a client connects or the next sample is due — no
      // busy-spin, so idle CPU stays flat.
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(listenfd, &rfds);
      struct timeval tv;
      tv.tv_sec  = wait_ms / 1000;
      tv.tv_usec = (wait_ms % 1000) * 1000;

      int r = select(listenfd + 1, &rfds, NULL, NULL, &tv);
      if (r < 0) {
        if (errno == EINTR) continue;
        printf("\n Error : select() failed. Error: %s\n", strerror(errno));
        continue;
      }
      if (r > 0 && FD_ISSET(listenfd, &rfds)) {
        // Serve the cached line immediately (sub-millisecond) and close.
        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
        if (connfd >= 0) {
          write(connfd, cachedBuff, strlen(cachedBuff));
          close(connfd);
        }
      }
      // r == 0 -> select timed out -> loop back and take the due sample.
    }
}

/*
# Local Variables:
# compile-command: "gcc -g -std=c99 tsl2561-daemon.c tsl2561/TSL2561.c -o tsl2561-daemon"
# End:
*/
