#include "DDB/Pipe.h"
#include "DDB/Error.h"

#include <cstddef>
#include <utility>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

DDB::Pipe::Pipe(bool closeOnExec) {
  if (pipe2(m_pipeFd, closeOnExec ? O_CLOEXEC : 0) == -1) {
    Error::sendErrno("pipe2");
  }
}

DDB::Pipe::~Pipe() {
  closeRead();
  closeWrite();
}

int DDB::Pipe::releaseRead() { return std::exchange(m_pipeFd[readFd], -1); }

int DDB::Pipe::releaseWrite() { return std::exchange(m_pipeFd[writeFd], -1); }

void DDB::Pipe::closeRead() {
  if (m_pipeFd[readFd] != -1) {
    close(m_pipeFd[readFd]);
    m_pipeFd[readFd] = -1;
  }
}

void DDB::Pipe::closeWrite() {
  if (m_pipeFd[writeFd] != -1) {
    close(m_pipeFd[writeFd]);
    m_pipeFd[writeFd] = -1;
  }
}

std::vector<std::byte> DDB::Pipe::read() {
  std::byte bytes[1024];
  ssize_t nRead;
  if ((nRead = ::read(m_pipeFd[readFd], bytes, sizeof(bytes))) == -1) {
    Error::sendErrno("read");
  }
  return std::vector<std::byte>(bytes, bytes + nRead);
}

void DDB::Pipe::write(std::byte *bytes, std::size_t nBytes) {
  if (::write(m_pipeFd[writeFd], bytes, nBytes) == -1) {
    Error::sendErrno("write");
  }
}
