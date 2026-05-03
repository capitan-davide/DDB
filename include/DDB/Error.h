#ifndef DBB_ERROR_H
#define DBB_ERROR_H

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
} // namespace DDB

#endif // DBB_ERROR_H
