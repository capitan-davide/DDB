#include "DDB/Error.h"

#include <cerrno>
#include <cstring>

void DDB::Error::send(const std::string &What) { throw Error(What); }

void DDB::Error::sendErrno(const std::string &Prefix) {
  throw Error(Prefix + ": " + std::strerror(errno));
}

void DDB::UnreachableError::send(const std::string &What) {
  throw UnreachableError(What);
}
