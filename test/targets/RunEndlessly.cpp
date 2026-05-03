#include <thread>

int main() {
  while (true) {
    std::this_thread::yield();
  }
  return 0;
}
