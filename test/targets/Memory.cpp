#include <csignal>
#include <cstdio>
#include <unistd.h>

int main() {
  unsigned long long A = 0xcafebabe;
  auto AAddr = &A;

  write(STDOUT_FILENO, &AAddr, sizeof(void *));
  fflush(stdout);

  raise(SIGTRAP);

  char B[12] = {0};
  auto BAddr = &B;

  write(STDOUT_FILENO, &BAddr, sizeof(void *));
  fflush(stdout);

  raise(SIGTRAP);

  printf("%s", B);

  return 0;
}
