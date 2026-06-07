#ifndef DBB_ERROR_H
#define DBB_ERROR_H

#include <sstream>
#include <stdexcept>
#include <string>

namespace DDB {
class Error : public std::runtime_error {
public:
  [[noreturn]] static void send(const std::string &what);
  [[noreturn]] static void sendErrno(const std::string &prefix);

private:
  Error(const std::string &what) : std::runtime_error(what) {}
};

class UnreachableError : public std::logic_error {
public:
  [[noreturn]] static void send(const std::string &what);

private:
  UnreachableError(const std::string &what) : std::logic_error(what) {}
};

[[noreturn]] inline void unreachableInternal(const char *msg, const char *file,
                                             unsigned line) {
  std::ostringstream oss;
  oss << "[DDB_UNREACHABLE] " << file << ":" << line << ": " << msg;
  UnreachableError::send(oss.str());
}

} // namespace DDB

#ifdef NDEBUG
#define DDB_UNREACHABLE(msg) __builtin_unreachable()
#else
#define DDB_UNREACHABLE(msg) DDB::unreachableInternal(msg, __FILE__, __LINE__)
#endif

#endif // DBB_ERROR_H
