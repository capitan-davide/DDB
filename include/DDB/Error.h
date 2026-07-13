#ifndef DBB_ERROR_H
#define DBB_ERROR_H

#include <sstream>
#include <stdexcept>
#include <string>

namespace DDB {
class Error : public std::runtime_error {
public:
  [[noreturn]] static void send(const std::string &What);
  [[noreturn]] static void sendErrno(const std::string &Prefix);

private:
  Error(const std::string &What) : std::runtime_error(What) {}
};

class UnreachableError : public std::logic_error {
public:
  [[noreturn]] static void send(const std::string &What);

private:
  UnreachableError(const std::string &What) : std::logic_error(What) {}
};

[[noreturn]] inline void unreachableInternal(const char *Msg, const char *File,
                                             unsigned Line) {
  std::ostringstream OSS;
  OSS << "[DDB_UNREACHABLE] " << File << ":" << Line << ": " << Msg;
  UnreachableError::send(OSS.str());
}

} // namespace DDB

#ifdef NDEBUG
#define DDB_UNREACHABLE(msg) __builtin_unreachable()
#else
#define DDB_UNREACHABLE(msg) DDB::unreachableInternal(msg, __FILE__, __LINE__)
#endif

#endif // DBB_ERROR_H
