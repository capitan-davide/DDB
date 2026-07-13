#include "DDB/Pipe.h"
#include "DDB/Error.h"

#include <cstddef>
#include <utility>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

DDB::Pipe::Pipe(bool CloseOnExec) {
  if (pipe2(PipeFD, CloseOnExec ? O_CLOEXEC : 0) == -1) {
    Error::sendErrno("pipe2");
  }
}

DDB::Pipe::~Pipe() {
  closeRead();
  closeWrite();
}

int DDB::Pipe::releaseRead() { return std::exchange(PipeFD[ReadFD], -1); }

int DDB::Pipe::releaseWrite() { return std::exchange(PipeFD[WriteFD], -1); }

void DDB::Pipe::closeRead() {
  if (PipeFD[ReadFD] != -1) {
    close(PipeFD[ReadFD]);
    PipeFD[ReadFD] = -1;
  }
}

void DDB::Pipe::closeWrite() {
  if (PipeFD[WriteFD] != -1) {
    close(PipeFD[WriteFD]);
    PipeFD[WriteFD] = -1;
  }
}

std::vector<std::byte> DDB::Pipe::read() {
  std::byte Bytes[1024];
  ssize_t NumRead;
  if ((NumRead = ::read(PipeFD[ReadFD], Bytes, sizeof(Bytes))) == -1) {
    Error::sendErrno("read");
  }
  return std::vector<std::byte>(Bytes, Bytes + NumRead);
}

void DDB::Pipe::write(std::byte *Bytes, std::size_t NumBytes) {
  if (::write(PipeFD[WriteFD], Bytes, NumBytes) == -1) {
    Error::sendErrno("write");
  }
}
