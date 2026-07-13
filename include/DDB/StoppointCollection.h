#ifndef DDB_STOPPOINT_COLLECTION_H
#define DDB_STOPPOINT_COLLECTION_H

#include "DDB/Error.h"
#include "DDB/Types.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace DDB {
template <class Stoppoint> class StoppointCollection {
public:
  Stoppoint &push(std::unique_ptr<Stoppoint> SP);

  bool containsId(typename Stoppoint::IdType Id) const;
  bool containsAddr(VirtAddr Addr) const;
  bool enabledAtAddr(VirtAddr Addr) const;

  Stoppoint &getById(typename Stoppoint::IdType Id);
  const Stoppoint &getById(typename Stoppoint::IdType Id) const;
  Stoppoint &getByAddr(VirtAddr Addr);
  const Stoppoint &getByAddr(VirtAddr Addr) const;
  std::vector<Stoppoint *> getInRegion(VirtAddr Low, VirtAddr High) const;

  void removeById(typename Stoppoint::IdType Id);
  void removeByAddr(VirtAddr Addr);

  template <class F> void forEach(F Fn);
  template <class F> void forEach(F Fn) const;

  std::size_t size() const { return TheStoppoints.size(); }
  bool empty() const { return TheStoppoints.empty(); }

private:
  using Stoppoints = std::vector<std::unique_ptr<Stoppoint>>;

  typename Stoppoints::iterator findById(typename Stoppoint::IdType Id);
  typename Stoppoints::const_iterator
  findById(typename Stoppoint::IdType Id) const;
  typename Stoppoints::iterator findByAddr(VirtAddr Addr);
  typename Stoppoints::const_iterator findByAddr(VirtAddr Addr) const;

  Stoppoints TheStoppoints;
};

template <class Stoppoint>
Stoppoint &StoppointCollection<Stoppoint>::push(std::unique_ptr<Stoppoint> SP) {
  TheStoppoints.push_back(std::move(SP));
  return *TheStoppoints.back();
}

template <class Stoppoint>
bool StoppointCollection<Stoppoint>::containsId(
    typename Stoppoint::IdType Id) const {
  return findById(Id) != std::end(TheStoppoints);
}

template <class Stoppoint>
bool StoppointCollection<Stoppoint>::containsAddr(VirtAddr Addr) const {
  return findByAddr(Addr) != std::end(TheStoppoints);
}

template <class Stoppoint>
bool StoppointCollection<Stoppoint>::enabledAtAddr(VirtAddr Addr) const {
  return containsAddr(Addr) && getByAddr(Addr).isEnabled();
}

template <class Stoppoint>
Stoppoint &
StoppointCollection<Stoppoint>::getById(typename Stoppoint::IdType Id) {
  auto It = findById(Id);
  if (It == std::end(TheStoppoints)) {
    Error::send("Invalid stoppoint id");
  }
  return **It;
}

template <class Stoppoint>
const Stoppoint &
StoppointCollection<Stoppoint>::getById(typename Stoppoint::IdType Id) const {
  return const_cast<StoppointCollection *>(this)->getById(Id);
}

template <class Stoppoint>
Stoppoint &StoppointCollection<Stoppoint>::getByAddr(VirtAddr Addr) {
  auto It = findByAddr(Addr);
  if (It == std::end(TheStoppoints)) {
    Error::send("Stoppoint with given address not found");
  }
  return **It;
}

template <class Stoppoint>
const Stoppoint &
StoppointCollection<Stoppoint>::getByAddr(VirtAddr Addr) const {
  return const_cast<StoppointCollection *>(this)->getByAddr(Addr);
}

template <class Stoppoint>
std::vector<Stoppoint *>
StoppointCollection<Stoppoint>::getInRegion(VirtAddr Low, VirtAddr High) const {
  std::vector<Stoppoint *> Ret;
  for (auto &SP : TheStoppoints) {
    if (SP->inRange(Low, High)) {
      Ret.push_back(&*SP);
    }
  }
  return Ret;
}

template <class Stoppoint>
void StoppointCollection<Stoppoint>::removeById(typename Stoppoint::IdType Id) {
  auto It = findById(Id);
  (**It).disable();
  TheStoppoints.erase(It);
}

template <class Stoppoint>
void StoppointCollection<Stoppoint>::removeByAddr(VirtAddr Addr) {
  auto It = findByAddr(Addr);
  (**It).disable();
  TheStoppoints.erase(It);
}

template <class Stoppoint>
template <class F>
void StoppointCollection<Stoppoint>::forEach(F Fn) {
  for (auto &SP : TheStoppoints) {
    Fn(*SP);
  }
}

template <class Stoppoint>
template <class F>
void StoppointCollection<Stoppoint>::forEach(F Fn) const {
  for (const auto &SP : TheStoppoints) {
    Fn(*SP);
  }
}

template <class Stoppoint>
auto StoppointCollection<Stoppoint>::findById(typename Stoppoint::IdType Id) ->
    typename Stoppoints::iterator {
  return std::find_if(std::begin(TheStoppoints), std::end(TheStoppoints),
                      [=](auto &SP) { return SP->id() == Id; });
}

template <class Stoppoint>
auto StoppointCollection<Stoppoint>::findById(typename Stoppoint::IdType Id)
    const -> typename Stoppoints::const_iterator {
  return const_cast<StoppointCollection *>(this)->findById(Id);
}

template <class Stoppoint>
auto StoppointCollection<Stoppoint>::findByAddr(VirtAddr Addr) ->
    typename Stoppoints::iterator {
  return std::find_if(std::begin(TheStoppoints), std::end(TheStoppoints),
                      [=](auto &SP) { return SP->atAddr(Addr); });
}

template <class Stoppoint>
auto StoppointCollection<Stoppoint>::findByAddr(VirtAddr Addr) const ->
    typename Stoppoints::const_iterator {
  return const_cast<StoppointCollection *>(this)->findByAddr(Addr);
}
} // namespace DDB

#endif // DDB_STOPPOINT_COLLECTION_H
