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
  add(a, a, a);
  if (a + b == 1) {
    printf("Hello World!!\n");
    add(a, a, b);
  }
  return 0;
}