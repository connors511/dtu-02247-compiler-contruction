#include <stdio.h>

int add(int a, int b, int c)
{
  return a+b+c;
}

int main( )
{
  int a = 2;
  int b = 3;
  add(a, a, a);
  add(b, a, a);
  return 0;
}