#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <errno.h>
#include <time.h>
#include <poll.h>


// Dave_H off #a&a suggested the shift register
// approach.

// build a minute long shift register.
// wait until the start of it looks like the
// start-second (500ms 1, 500ms 0, approx)

// maybe when we match that mostly-1s then
// mostly-0s, i'll need a fine-tuning step
// to get the first 1 bit lined up at the
// start.

// then treat it as a valid buffer and decode.

// BUG: I need one full minute of data before
// attempting to do the decode, if I'm matching
// the start of the buffer.

// readings per second
#define RPS 500

// this will be a shift register implemented
// as a cyclic buffer.

#define BUFSIZE (RPS*60)

#define HALFSEC (RPS / 2)
#define HUNDREDMS (RPS / 10)

char buffer[BUFSIZE];
int bufoff=0;

char bits[120];

long long oldtenths=-1;

// initially inhibit decoding until we
// have a minute worth of data
// after that, inhibit for a couple of
// seconds whenever we do a decode so that
// it only happens once per cycle.
int inhibitDecodeFor = BUFSIZE;

// exit after this many successful time readings
int exitAfter = -1; // -1 to not exit

void checkdecode(struct timeval *, struct timezone *);
void decode();
void decodeBCD(struct timeval *tv, struct timezone *tz);
void tellNTP(int year, int month, int day, int hour, int minute, struct timeval *tv, struct timezone *tz, int summertime);
static volatile struct shmTime *getShmTime(int unit);


// this struct def from ntpd via gpsd
struct shmTime
{
    int mode;			/* 0 - if valid set
				 *       use values, 
				 *       clear valid
				 * 1 - if valid set 
				 *       if count before and after read of values is equal,
				 *         use values 
				 *       clear valid
				 */
    int count;
    time_t clockTimeStampSec;
    int clockTimeStampUSec;
    time_t receiveTimeStampSec;
    int receiveTimeStampUSec;
    int leap;
    int precision;
    int nsamples;
    int valid;
    int pad[10];
};


volatile struct shmTime *ntpmem;

void main() {
  printf("msf shift.c\n");

  printf("getting NTP shm\n");
  ntpmem = getShmTime(2);
  assert(ntpmem!=0);

  printf("Key: \n");
  printf("  P = main loop\n");
  printf("  x = poll timeout without input\n");
  printf("  C = Starting decode\n");
  printf(" X1 = Decode failed: insufficient ones in scan zone\n");
  printf(" X0 = Decode failed: insufficient zeroes in scan zone\n");
  printf("  * = Decode inhibition ended\n");

  printf("entering infinite loop.\n");

/*
  struct timeval old_tv;
  old_tv.tv_sec = 0;
  int old_level = 0;
*/

  int oldc='X';
  assert(oldc == 'X' || oldc == '1' || oldc == '0');

  FILE *fp;
  fp = fopen("/sys/class/gpio/gpio25/value","r");

  int fd;
  fd=fileno(fp);
 
  while(1==1) {
    long long newtenths = oldtenths;
    struct timeval tv;
    struct timezone tz;

#define POLLWAIT
#ifdef POLLHARD
    while(newtenths == oldtenths) {
      // this time gets used here to determine if
      // we should tick, and also later when we decode
      // because its the time we took the reading for
      // the last reading of the buffer, which often
      // (but not in leap seconds cases) will be the
      // start of the minute just described.
    
      gettimeofday(&tv, &tz);

      newtenths = ((long long)tv.tv_sec) * RPS + ((long long)tv.tv_usec)/(1000000/RPS);
    }
#endif
#ifdef POLLWAIT
    int ret = 0;
    printf("P");fflush(stdout);
    while(ret == 0) {
      struct pollfd fds;
      fds.fd = fd;
      fds.events = POLLPRI;
      fds.revents=0;
      ret = poll(&fds, 1, 5000);
      gettimeofday(&tv, &tz); // get this time stamp as soon after poll returns as possible
      if(ret == 0) printf("x");fflush(stdout);
    }

    newtenths = ((long long)tv.tv_sec) * RPS + ((long long)tv.tv_usec)/(1000000/RPS);
#endif

    if(newtenths != oldtenths) {
      if(newtenths != oldtenths+1 && oldtenths != -1) {
#ifdef POLLHARD
        printf("WARNING: skipped multiple steps. oldtenths = %lld, newtenths = %lld\n",
          oldtenths, newtenths);
#endif
// this expected normal operation in the case of POLLWAIT mode so don't print warnings.

        long long error = newtenths - oldtenths - 1; // -1 because we're going to read a bit anyway.
        int i;
        for(i = 0; i<error; i++)
        {
          assert(oldc == 'X' || oldc == '1' || oldc == '0');
          // printf("filling slot %d with old value %c at bufoff %d\n", i, oldc, bufoff);
          // printf("F%d=%c ",bufoff,oldc);fflush(stdout);
          buffer[bufoff] = oldc; 
          bufoff=(bufoff+1) % BUFSIZE;
        }
        checkdecode(&tv,&tz);
        // bufoff = (bufoff + error) % BUFSIZE;
        // this shoudl resync us, and leave whatever was in the buffer previously. that might be
        // dangerous if we resync a huge distance and end up leaving a valid time from last time
        // round in the buffer. maybe could store "invalid" symbols there?
        if(inhibitDecodeFor > 0) inhibitDecodeFor-=error;
      }
      // printf("q"); fflush(stdout);
      oldtenths = newtenths;
      int rsize;
      char c;
      rsize = read(fd, &c, 1);
      assert(rsize == 1);
      oldc=c;
      assert(oldc == 'X' || oldc == '1' || oldc == '0');
      fseek(fp, 0, SEEK_SET);
 
/*
      if(c=='1') {
        printf("*");
      } else {
        printf(".");
      }

*/
      assert(bufoff < BUFSIZE);
      buffer[bufoff] = c;
      bufoff ++;
      bufoff %= BUFSIZE;

      if(inhibitDecodeFor != 0) {
        inhibitDecodeFor--;
        if(inhibitDecodeFor <= 0) {
          printf("*"); fflush(stdout);
          inhibitDecodeFor = 0; // it might have been made negative elsewhere
        }
      } else {
        checkdecode(&tv, &tz);
      }
    }
  }
}

void checkdecode(struct timeval *tv, struct timezone *tz) {
        assert(tv!=NULL);
        // want to shift this down, so if we have a sync pulse approaching,
        // we defer until we get the leading edge at the very start
        if(inhibitDecodeFor <= 0 && buffer[bufoff] == '1') { 
          // printf("c"); fflush(stdout);
          int numOnesInFirst = 0;
          int numZeroesInSecond = 0;
          int i;

// do a quick scan first over the whole time range at 10 points.
          int bad=0;
          for(i=0; i<10; i++) {
            int pos = (bufoff + i*(HALFSEC/10)) % BUFSIZE;
            bad+= (buffer[pos] !='1' ? 1:0);
          }
          // 20% wrong?
          if(bad >= 2) return;
          printf("C"); fflush(stdout);
          for(i=0;i<HALFSEC;i++) {
            int bp;
            bp = (bufoff + i) % BUFSIZE;
            numOnesInFirst += (buffer[bp] == '1') ? 1 : 0;
          }
          if(numOnesInFirst > (RPS * 9 / 20 )) { // 450ms worth of ones
            printf("\nmatched num ones threshold, numOnesInFirst = %d\n", numOnesInFirst);
            for(i=0;i<HALFSEC;i++) {
              int bp;
              bp = (HALFSEC + bufoff + i) % BUFSIZE;
              numZeroesInSecond += (buffer[bp] == '0') ? 1 : 0;
            }
            printf("numZeroesInSecond = %d\n", numZeroesInSecond);
            if(numZeroesInSecond > (RPS * 9 / 20)) {
              printf("Matched num zeroes threshold\n");
              decode();
              assert(tv!=NULL);
              decodeBCD(tv, tz);
            } else {
              printf("X0 ");
            }
          } else {
            printf("X1 ");
          }
        }
}

void checkbit(int secbase, int hundred, char c);
int getbit(int secbase, int hundred);

// this function assumes that the buffer contains a minute
// worth of signal lined up with the start of second zero
// at the buf offset.
// its going to decode into 120 bits, roughly. (2 per sec)
void decode() {
  printf("Decoding");
  int sec;
  for(sec = 0; sec < 60; sec++) {
#ifdef VERBOSE
    printf("Processing second %d\n", sec);
#endif
    int i;
    int secbase = sec * RPS;
#ifdef SUPERVERBOSE
    for(i=0; i<RPS; i++) {
      printf("%c",buffer[ (bufoff + secbase + i) % BUFSIZE]);
    }
    printf("\n");
#endif
    checkbit(secbase,0, '1');
    int bitA = getbit(secbase,1);
    int bitB = getbit(secbase,2);
    checkbit(secbase,3, '0');
    checkbit(secbase,4, '0');
    checkbit(secbase,5, '0');
    checkbit(secbase,6, '0');
    checkbit(secbase,7, '0');
    checkbit(secbase,8, '0');
    checkbit(secbase,9, '0');
    bits[sec*2] = bitA;
    bits[sec*2+1] = bitB;
#ifdef VERBOSE
    printf("\n");
#endif
  }
  // if we've been successful, we won't expect to find a
  // result for another minute, so wait 'till just before
  // then before we start looking in the buffer again.
  inhibitDecodeFor = RPS * 55;
}

/* checks 100ms block numbered by 'hundred' in specified
   second has value c, and outputs error % */
void checkbit(int secbase, int hundred, char c) {  
#ifdef VERBOSE
  int v=0;
  int hundredbase;
  int i;
  hundredbase = HUNDREDMS * hundred;
  for(i=0; i<HUNDREDMS; i++) {
    int p = (bufoff + secbase + hundredbase + i) % BUFSIZE;
    if(buffer[p] == c) v++;
  }
  float pct = ((float)v)/((float)HUNDREDMS);
  printf("C%.1f ", pct);
#endif
}

int getbit(int secbase, int hundred) {
  int v=0;
  int hundredbase;
  int i;
  hundredbase = HUNDREDMS * hundred;
  for(i=0; i<HUNDREDMS; i++) {
    int p = (bufoff + secbase + hundredbase + i) % BUFSIZE;
    if(buffer[p] == '1') v++;
  }
  float pct = ((float)v)/((float)HUNDREDMS);
#ifdef VERBOSE
  printf("B%.1f ", pct);
#endif
  if(pct>0.5) {
    return 1;
  } else {
    return 0;
  }
}

void decodeBCD(struct timeval *tv, struct timezone *tz) {
  assert(tv!=NULL);
  printf("Decoding BCD\n");
  // BCD year
  int year = 80 * bits[17*2 + 0]
           + 40 * bits[18*2 + 0]
           + 20 * bits[19*2 + 0]
           + 10 * bits[20*2 + 0]
           +  8 * bits[21*2 + 0]
           +  4 * bits[22*2 + 0]
           +  2 * bits[23*2 + 0]
           +  1 * bits[24*2 + 0];

  // BCD year
  int month = 10 * bits[25*2 + 0]
            +  8 * bits[26*2 + 0]
            +  4 * bits[27*2 + 0]
            +  2 * bits[28*2 + 0]
            +  1 * bits[29*2 + 0];

  int day = 20 * bits[30*2 + 0]
          + 10 * bits[31*2 + 0]
          +  8 * bits[32*2 + 0]
          +  4 * bits[33*2 + 0]
          +  2 * bits[34*2 + 0]
          +  1 * bits[35*2 + 0];

  int dow = 4 * bits[36*2 + 0]
          + 2 * bits[37*2 + 0]
          + 1 * bits[38*2 + 0];

  int hour = 20 * bits[39*2 + 0]
           + 10 * bits[40*2 + 0]
           +  8 * bits[41*2 + 0]
           +  4 * bits[42*2 + 0]
           +  2 * bits[43*2 + 0]
           +  1 * bits[44*2 + 0];

  int minute = 40 * bits[45*2 + 0]
             + 20 * bits[46*2 + 0]
             + 10 * bits[47*2 + 0]
             +  8 * bits[48*2 + 0]
             +  4 * bits[49*2 + 0]
             +  2 * bits[50*2 + 0]
             +  1 * bits[51*2 + 0];

  int summertime = bits[58*2 + 1]; 

  printf("Time now is '%d/%2.2d/%2.2d %2.2d:%2.2d %s\n", year, month, day, hour, minute, summertime == 0 ? "GMT" : "BST");
/*
  printf("day of week (0=sunday) = %d\n", dow);
  printf("hh:mm = %2.2d:%2.2d (%d %d)\n", hour, minute, hour, minute);
*/

  if(exitAfter>0) exitAfter--;
  if(exitAfter == 0) {
    printf("exitAfter counter reached zero - exiting\n");
    exit(0);
  }

  tellNTP(year, month, day, hour, minute, tv, tz, summertime);
}

void tellNTP(int year, int month, int day, int hour, int minute, struct timeval *tv, struct timezone *tz, int summertime) {
  printf("Telling NTP\n");
  assert(ntpmem != 0);

  printf("ntpmem is %p\n", ntpmem);
  printf("ntpmem->mode = %d\n", ntpmem->mode); 

  assert(ntpmem->mode == 0);
  // ntpd appears to create the segment and put it in
  // mode 0. I do not know if I can change that or not
  // so I'll just stick with mode 0 for now.

  // BUG: we will have skipped a bunch of ms so
  // ideally I would like a time stamp for "time of
  // last reading" or something like that, so that
  // I can communicate that rather than the "now" that
  // is now when this function is called.

  if(ntpmem->valid != 0) {
    printf("There is still a valid result in ntp struct. Skipping update.\n");
  } else {
    printf("ntp struct contains no valid result, so we can populate it.\n");

    // i need to convert MSF time into unix time in seconds. perhaps using mktime

    struct tm clocktime;
    clocktime.tm_sec = 0;
    clocktime.tm_min = minute;
    clocktime.tm_hour = hour;
    clocktime.tm_mday = day;
    clocktime.tm_mon = month-1;
    clocktime.tm_year = 100+year;
    clocktime.tm_isdst = 0; // this info is extractable from time signal
    time_t clocksec = mktime(&clocktime);
    if(summertime == 1) {
      clocksec -= 3600;
    }
    printf("time_t clocksec (from MSF) = %lld seconds\n",(long long)clocksec);
    printf("time_t clocksec (local)    = %lld . %6.6lld seconds\n",(long long)tv->tv_sec, (long long)tv->tv_usec);

    //       struct tm {
     //          int tm_sec;         /* seconds */
      //         int tm_min;         /* minutes */
        //       int tm_hour;        /* hours */
     //          int tm_mday;        /* day of the month */
       //        int tm_mon;         /* month */
         //      int tm_year;        /* year */
           //    int tm_wday;        /* day of the week */
//               int tm_yday;        /* day in the year */
  //             int tm_isdst;       /* daylight saving time */
    //       };

    ntpmem->clockTimeStampSec = clocksec;
    ntpmem->clockTimeStampUSec = 0;

// TODO: we'll need these earlier on for the receive time
//   and need to construct clocktime from the MSF signal
//   but for now, use this time for everything...
    
    gettimeofday(tv, tz);

    ntpmem->receiveTimeStampSec = tv->tv_sec;
    ntpmem->receiveTimeStampUSec = tv->tv_usec;
    ntpmem->valid = 1;
  }
}



// following function based on BSD-licensed code from gpsd
static volatile struct shmTime *getShmTime(int unit)
{
#define NTPD_BASE 0x4e545030
    int shmid;
    unsigned int perms;
    volatile struct shmTime *p;
    // set the SHM perms the way ntpd does
    if (unit < 2) {
	// we are root, be careful
	perms = 0600;
    } else {
	// we are not root, try to work anyway
	perms = 0666;
    }

    shmid = shmget((key_t) (NTPD_BASE + unit),
		   sizeof(struct shmTime), (int)(IPC_CREAT | perms));
    if (shmid == -1) {
	printf( "NTPD shmget(%ld, %zd, %o) fail: %s\n",
		    (long int)(NTPD_BASE + unit), sizeof(struct shmTime),
		    (int)perms, strerror(errno));
	return NULL;
    } 
    p = (struct shmTime *)shmat(shmid, 0, 0);
    /*@ -mustfreefresh */
    if ((int)(long)p == -1) {
	printf("NTPD shmat failed: %s\n",
		    strerror(errno));
	return NULL;
    }
    return p;
    /*@ +mustfreefresh */
}

