#include <csignal>
#include <cstdio>
#include <unistd.h>

int main() {
  unsigned long long a = 0xcafebabe;
  auto aAddr = &a;

  write(STDOUT_FILENO, &aAddr, sizeof(void *));
  fflush(stdout);

  raise(SIGTRAP);

  char b[12] = {0};
  auto bAddr = &b;

  write(STDOUT_FILENO, &bAddr, sizeof(void *));
  fflush(stdout);

  raise(SIGTRAP);

  printf("%s", b);

  return 0;
}
