#ifndef DDB_PIPE_H
#define DDB_PIPE_H

#include <cstddef>
#include <vector>

namespace DDB {
class Pipe {
public:
  explicit Pipe(bool CloseOnExec);
  ~Pipe();

  int getRead() const { return PipeFD[ReadFD]; }
  int getWrite() const { return PipeFD[WriteFD]; }
  int releaseRead();
  int releaseWrite();
  void closeRead();
  void closeWrite();

  std::vector<std::byte> read();
  void write(std::byte *Bytes, std::size_t NumBytes);

private:
  static constexpr unsigned ReadFD = 0;
  static constexpr unsigned WriteFD = 1;

  int PipeFD[2];
};
} // namespace DDB

#endif // DDB_PIPE_H
