#include <stdio.h>
#include <sys/time.h>

// readings per second
const int div = 1000;

void main() {
  printf("msf rl.c run length encoder\n");

  long oldfracs=0;
  char oldstate='x';

  while(1==1) {
    FILE *fp;
    fp = fopen("/sys/class/gpio/gpio25/value","r");
    char newstate = fgetc(fp);
    fclose(fp);

    if(newstate != oldstate) {


      struct timeval tv;
      struct timezone tz;
    
      gettimeofday(&tv, &tz);
      long newfracss;
      newfracs = tv.tv_sec * div + tv.tv_usec/(1000000/div);

      long fracdiff = newfracs - oldfracs;

      // state transition
      print("%c %d   ", oldstate, fracdiff);
      oldstate = newstate;
    }

/*
    struct timeval tv;
    struct timezone tz;
    
    gettimeofday(&tv, &tz);


    if(newtenths != oldtenths) {
      oldtenths = newtenths;
      FILE *fp;
      if(c=='1') {
        printf("*");
      } else {
        printf(".");
      }

      if(tv.tv_sec != oldsec) {
        oldsec=tv.tv_sec;
    
        printf("\n");
      }



    }
*/
  }
}
