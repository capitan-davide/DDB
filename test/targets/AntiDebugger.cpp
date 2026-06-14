// Malware software modify their own code while running, rendering software
// breakpoint useless. The software can just scribble over the 0xcc byte (i.e.,
// int3) that the debugger puts into memory to trigger the break. Other kind of
// makware detect whether they're being traced by a debugger and change their
// activities to hide their evil deeds.

#include <csignal>
#include <cstdio>
#include <numeric>

#include <unistd.h>

void anEvilFunction() { std::puts("Doing something evil..."); }
void anEvilFunctionEnd() {}

void anInnocentFunction() { std::puts("Doing something innocent..."); }

// To detect if the function was modified (e.g., by a breakpoint) we calculate
// the checksum of the function code. See "section hashing" for more
// information.
int checksum() {
  auto begin = reinterpret_cast<volatile const char *>(&anEvilFunction);
  auto end = reinterpret_cast<volatile const char *>(&anEvilFunctionEnd);
  return std::accumulate(begin, end, 0);
}

int main() {
  const int safe = checksum();

  // For testing purpose, print the function address to stdout so that the
  // debugger can read it.
  auto ptr = reinterpret_cast<void *>(&anEvilFunction);
  write(STDOUT_FILENO, &ptr, sizeof(void *));
  fflush(stdout);
  raise(SIGTRAP);

  while (true) {
    if (checksum() == safe) {
      anEvilFunction();
    } else {
      anInnocentFunction();
    }
    fflush(stdout);
    raise(SIGTRAP);
  }
  return 0;
}

// clang-format off
// 1) To find the load address of 'anEvilFunction' first, find the PID of the running process:
//
// ❯ ps aux | grep anti-debugger
// davide      9969  0.0  0.0   7580  3736 pts/1    S<   10:37   0:00 ./build/Debug/test/targets/anti-debugger
//
// => the PID is 9969
// 
// 2) Then, find the offset of 'anEvilFunction' in the ELF file:
//
// ❯ objdump -wD build/Debug/test/targets/anti-debugger | grep anEvilFunction
// 00000000000011a0 <_Z14anEvilFunctionv>:
// 00000000000011c0 <_Z17anEvilFunctionEndv>:
//     11f8:	48 8d 05 a1 ff ff ff 	lea    -0x5f(%rip),%rax        # 11a0 <_Z14anEvilFunctionv>
//     1203:	48 8d 05 b6 ff ff ff 	lea    -0x4a(%rip),%rax        # 11c0 <_Z17anEvilFunctionEndv>
//     1251:	e8 4a ff ff ff       	call   11a0 <_Z14anEvilFunctionv>
//     113a:	76 73                	jbe    11af <_Z14anEvilFunctionv+0xf>
//     113c:	77 73                	ja     11b1 <_Z14anEvilFunctionv+0x11>
//     1145:	70 72                	jo     11b9 <_Z14anEvilFunctionv+0x19>
//     1155:	76 77                	jbe    11ce <_Z17anEvilFunctionEndv+0xe>
//     1157:	73 63                	jae    11bc <_Z14anEvilFunctionv+0x1c>
//     1167:	73 63                	jae    11cc <_Z17anEvilFunctionEndv+0xc
//
// => the offset is 0x11a0
//
// 3) Then, the load address is:
//
// ❯ cat /proc/9969/maps | grep anti-debugger | grep 'r-xp'
// 556227ca0000-556227ca1000 r-xp 00001000 00:23 954206                     /home/davide/Code/DDB/build/Debug/test/targets/anti-debugger
//
// => the load address is 0x556227ca000 - 0x1000 + 0x11a0 = 0x556227ca01a
