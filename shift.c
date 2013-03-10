#include <stdio.h>
#include <sys/time.h>
#include <assert.h>

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
#define RPS 100

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

void decode();
void decodeBCD();

void main() {
  printf("msf shift.c\n");

  printf("entering infinite loop.\n");
  while(1==1) {
    struct timeval tv;
    struct timezone tz;
    
    gettimeofday(&tv, &tz);

    long long newtenths;
    newtenths = ((long long)tv.tv_sec) * RPS + ((long long)tv.tv_usec)/(1000000/RPS);

    if(newtenths != oldtenths) {
      if(newtenths != oldtenths+1 && oldtenths != -1) {
        printf("WARNING: sync error. oldtenths = %lld, newtenths = %lld, not oldtenths+1\n",
          oldtenths, newtenths);
        long long error = newtenths - oldtenths - 1; // -1 because we're going to read a bit anyway.
        bufoff = (bufoff + error) % BUFSIZE;
        // this shoudl resync us, and leave whatever was in the buffer previously. that might be
        // dangerous if we resync a huge distance and end up leaving a valid time from last time
        // round in the buffer. maybe could store "invalid" symbols there?
      }
      // printf("q"); fflush(stdout);
      oldtenths = newtenths;
      FILE *fp;
      fp = fopen("/sys/class/gpio/gpio25/value","r");
      char c = fgetc(fp);
      fclose(fp);
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

      if(inhibitDecodeFor > 0) {
        inhibitDecodeFor--;
        if(inhibitDecodeFor == 0) {
          printf("Decode inhibition ended.\n");
        }
      } else {

        // want to shift this down, so if we have a sync pulse approaching,
        // we defer until we get the leading edge at the very start
        if(buffer[bufoff] == '1') { 
          int numOnesInFirst = 0;
          int numZeroesInSecond = 0;
          int i;
          for(i=0;i<HALFSEC;i++) {
            int bp;
            bp = (bufoff + i) % BUFSIZE;
            numOnesInFirst += (buffer[bp] == '1') ? 1 : 0;
          }
          for(i=0;i<HALFSEC;i++) {
            int bp;
            bp = (HALFSEC + bufoff + i) % BUFSIZE;
            numZeroesInSecond += (buffer[bp] == '0') ? 1 : 0;
          }
          if(numOnesInFirst > (RPS * 9 / 20 )) { // 450ms worth of ones
            printf("matched num ones threshold, numOnesInFirst = %d, numZeroesInSecond = %d\n", numOnesInFirst, numZeroesInSecond);
            if(numZeroesInSecond > (RPS * 9 / 20)) {
              printf("Matched num zeroes threshold\n");
              decode();
              decodeBCD();
            }
          }
        }
      }
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
    printf("Processing second %d\n", sec);
    int i;
    int secbase = sec * RPS;
/*
    for(i=0; i<RPS; i++) {
      printf("%c",buffer[ (bufoff + secbase + i) % BUFSIZE]);
    }
    printf("\n");
*/
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
    printf("\n");
  }
  inhibitDecodeFor = RPS * 2;
}

/* checks 100ms block numbered by 'hundred' in specified
   second has value c, and outputs error % */
void checkbit(int secbase, int hundred, char c) {  
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
  printf("B%.1f ", pct);
  if(pct>0.5) {
    return 1;
  } else {
    return 0;
  }
}

void decodeBCD() {
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
 

  printf("year/month/day = %d/%d/%d\n", year, month, day);
  printf("day of week (0=sunday) = %d\n", dow);
  printf("hh:mm = %2.2d:%2.2d (%d %d)\n", hour, minute, hour, minute);

}

