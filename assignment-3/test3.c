#include <stdio.h>
int main( )
{
  printf("Hello World!\n");
  printf("Hello World!\n");
  printf("Hello World2!\n");
  if (someMethod()) {
    printf("Hello World!\n");
    someMethod(1,2);
    printf("Hello World!\n");
    anotherMethod(1);
  } else {
    anotherMethod();
  }
  return 0;
}

int someMethod()
{
  return 1;
}

int anotherMethod( )
{
  printf("Hello World!\n");
  printf("Hello World!\n");
  return 0;
}