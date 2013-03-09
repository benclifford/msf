#include <stdio.h>
#include <sys/time.h>

// readings per second
const int div = 80;

void main() {
  printf("msf a.c\n");
  int oldsec=0;
  int oldtenths=0;
  while(1==1) {

    struct timeval tv;
    struct timezone tz;
    
    gettimeofday(&tv, &tz);

    long newtenths;
    newtenths = tv.tv_sec * div + tv.tv_usec/(1000000/div);

    if(newtenths != oldtenths) {
      oldtenths = newtenths;
      FILE *fp;
      fp = fopen("/sys/class/gpio/gpio25/value","r");
      char c = fgetc(fp);
      fclose(fp);
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

  }
}
