#include "DDB/Error.h"

#include <cerrno>
#include <cstring>

void DDB::Error::send(const std::string &what) { throw Error(what); }

void DDB::Error::sendErrno(const std::string &prefix) {
  throw Error(prefix + ": " + std::strerror(errno));
}
