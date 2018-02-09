// RUN: %llvmgcc -I../../../include -emit-llvm -g -c %s -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t.bc > %t.log
// RUN: grep "a: p" %t1 
// RUN: grep "a: d" %t1 


#include <stdio.h>
#include <stdlib.h>

int main() {
  int t, y;
  char *a = malloc(5);
  char *b = malloc(5);
  char *c;
   a = malloc(50);

  klee_make_symbolic(&t, sizeof(t), "t");
  klee_make_symbolic(&y, sizeof(y), "y");

  if (t) {
      a[0] = 'p'; a[1] = '\0';
      c = malloc(6);
      printf("a: %s\n",a);
      a =c;
      return 0;
  }
  else {
      a = malloc(50);
      *a = 'd';
      if(y) c = malloc(7); 
      a[3] = '\0';

      printf("%s\n", a);

  }
  return 0;

}
