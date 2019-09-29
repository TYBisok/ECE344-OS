#include "common.h"
#include <math.h>

int factorial(int num);

int main(int argc, char *argv[]) {
 if(argc > 1) {
  int num = atoi(argv[1]); // gets input as an integer
  float fl = atof(argv[1]); // gets the input as a float

  if(argc > 1 && num > 12) {
    printf("Overflow\n");
    return 0;
  }
  else if(argc > 1 && (num > 0 && num <= 12) && num == fl) 
    printf("%d\n", factorial(num));
  else 
    printf("Huh?\n");
 }
 else
    printf("Huh?\n");
	return 0;
}

int factorial(int num) {
  int temp;
  
  if(num == 1)
    return 1;
  else {
    temp = num - 1; 
  }

  return num * factorial(temp);
}
