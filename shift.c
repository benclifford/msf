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

char buffer[BUFSIZE];
int bufoff=0;

long oldtenths=0;

int definitelyInsaneCount = BUFSIZE;

void main() {
  printf("msf shift.c\n");

  printf("entering infinite loop.");
  while(1==1) {
    struct timeval tv;
    struct timezone tz;
    
    gettimeofday(&tv, &tz);

    long newtenths;
    newtenths = tv.tv_sec * RPS + tv.tv_usec/(1000000/RPS);

    if(newtenths != oldtenths) {
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

      if(definitelyInsaneCount > 0) {
        definitelyInsaneCount--;
        if(definitelyInsaneCount == 0) {
          printf("Collected one minutes worth of data so can analyse\n");
        }
      } else {
        #define HALFSEC (RPS / 2)

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
          printf("matched, numOnesInFirst = %d, numZeroesInSecond = %d\n", numOnesInFirst, numZeroesInSecond);
        }
      }
    }
  }
}
