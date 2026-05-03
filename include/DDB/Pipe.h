#ifndef DDB_PIPE_H
#define DDB_PIPE_H

#include <cstddef>
#include <vector>

namespace DDB {
class Pipe {
public:
  explicit Pipe(bool closeOnExec);
  ~Pipe();

  int getRead() const { return m_pipeFd[readFd]; }
  int getWrite() const { return m_pipeFd[writeFd]; }
  int releaseRead();
  int releaseWrite();
  void closeRead();
  void closeWrite();

  std::vector<std::byte> read();
  void write(std::byte *bytes, std::size_t nBytes);

private:
  static constexpr unsigned readFd = 0;
  static constexpr unsigned writeFd = 1;

  int m_pipeFd[2];
};
} // namespace DDB

#endif // DDB_PIPE_H
