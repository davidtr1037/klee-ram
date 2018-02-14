// RUN: %llvmgcc -I../../../include -emit-llvm -g -c %s -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t.bc > %t.log
// RUN: grep "a: p" %t.log
// RUN: grep "a: d" %t.log


#include <stdio.h>
#include <stdlib.h>

char* mk_sym(int size) {
  char *a = malloc(size);
  return a;
}

int main() {
  int t;
  char *a, *b;

  klee_make_symbolic(&t, sizeof(t), "t");
  a = mk_sym(1);

  if (t) {
      a = mk_sym(1);
      *a = 'p';
  }
  else {
      a = mk_sym(1);
      *a = 'd';
  }
  if(*a == 'p')
    printf("a: p\n");
  else if(*a == 'd')
    printf("a: d\n");
  else
    assert(0 && "Shouldn't be anything else");

  return 0;
}
