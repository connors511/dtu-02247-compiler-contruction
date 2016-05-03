#include <stdio.h>

int add(int a, int b, int c)
{
  return a+b+c;
}

int main( )
{
  printf("Hello World!\n");
  int a = 2;
  int b = 3;
  add(a+a, a, a);
  add(1, a, a);
  //int cond = 1;
  if (a + b == 1) {
    printf("Hello World!\n");
  } else {
    printf("Hello World!\n");
  }
  return 0;
}