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
  Stoppoint &push(std::unique_ptr<Stoppoint> sp);

  bool containsId(typename Stoppoint::IdType id) const;
  bool containsAddr(VirtAddr addr) const;
  bool enabledAtAddr(VirtAddr addr) const;

  Stoppoint &getById(typename Stoppoint::IdType id);
  const Stoppoint &getById(typename Stoppoint::IdType id) const;
  Stoppoint &getByAddr(VirtAddr addr);
  const Stoppoint &getByAddr(VirtAddr addr) const;

  void removeById(typename Stoppoint::IdType id);
  void removeByAddr(VirtAddr addr);

  template <class F> void forEach(F f);
  template <class F> void forEach(F f) const;

  std::size_t size() const { return m_stoppoints.size(); }
  bool empty() const { return m_stoppoints.empty(); }

private:
  using Stoppoints = std::vector<std::unique_ptr<Stoppoint>>;

  typename Stoppoints::iterator findById(typename Stoppoint::IdType id);
  typename Stoppoints::const_iterator
  findById(typename Stoppoint::IdType id) const;
  typename Stoppoints::iterator findByAddr(VirtAddr addr);
  typename Stoppoints::const_iterator findByAddr(VirtAddr addr) const;

  Stoppoints m_stoppoints;
};

template <class Stoppoint>
Stoppoint &StoppointCollection<Stoppoint>::push(std::unique_ptr<Stoppoint> sp) {
  m_stoppoints.push_back(std::move(sp));
  return *m_stoppoints.back();
}

template <class Stoppoint>
bool StoppointCollection<Stoppoint>::containsId(
    typename Stoppoint::IdType id) const {
  return findById(id) != std::end(m_stoppoints);
}

template <class Stoppoint>
bool StoppointCollection<Stoppoint>::containsAddr(VirtAddr addr) const {
  return findByAddr(addr) != std::end(m_stoppoints);
}

template <class Stoppoint>
bool StoppointCollection<Stoppoint>::enabledAtAddr(VirtAddr addr) const {
  return containsAddr(addr) && getByAddr(addr).isEnabled();
}

template <class Stoppoint>
Stoppoint &
StoppointCollection<Stoppoint>::getById(typename Stoppoint::IdType id) {
  auto it = findById(id);
  if (it == std::end(m_stoppoints)) {
    Error::send("Invalid stoppoint id");
  }
  return **it;
}

template <class Stoppoint>
const Stoppoint &
StoppointCollection<Stoppoint>::getById(typename Stoppoint::IdType id) const {
  return const_cast<StoppointCollection *>(this)->getById(id);
}

template <class Stoppoint>
Stoppoint &StoppointCollection<Stoppoint>::getByAddr(VirtAddr addr) {
  auto it = findByAddr(addr);
  if (it == std::end(m_stoppoints)) {
    Error::send("Stoppoint with given address not found");
  }
  return **it;
}

template <class Stoppoint>
const Stoppoint &
StoppointCollection<Stoppoint>::getByAddr(VirtAddr addr) const {
  return const_cast<StoppointCollection *>(this)->getByAddr(addr);
}

template <class Stoppoint>
void StoppointCollection<Stoppoint>::removeById(typename Stoppoint::IdType id) {
  auto it = findById(id);
  (**it).disable();
  m_stoppoints.erase(it);
}

template <class Stoppoint>
void StoppointCollection<Stoppoint>::removeByAddr(VirtAddr addr) {
  auto it = findByAddr(addr);
  (**it).disable();
  m_stoppoints.erase(it);
}

template <class Stoppoint>
template <class F>
void StoppointCollection<Stoppoint>::forEach(F f) {
  for (auto &sp : m_stoppoints) {
    f(*sp);
  }
}

template <class Stoppoint>
template <class F>
void StoppointCollection<Stoppoint>::forEach(F f) const {
  for (const auto &sp : m_stoppoints) {
    f(*sp);
  }
}

template <class Stoppoint>
auto StoppointCollection<Stoppoint>::findById(typename Stoppoint::IdType id) ->
    typename Stoppoints::iterator {
  return std::find_if(std::begin(m_stoppoints), std::end(m_stoppoints),
                      [=](auto &sp) { return sp->id() == id; });
}

template <class Stoppoint>
auto StoppointCollection<Stoppoint>::findById(typename Stoppoint::IdType id)
    const -> typename Stoppoints::const_iterator {
  return const_cast<StoppointCollection *>(this)->findById(id);
}

template <class Stoppoint>
auto StoppointCollection<Stoppoint>::findByAddr(VirtAddr addr) ->
    typename Stoppoints::iterator {
  return std::find_if(std::begin(m_stoppoints), std::end(m_stoppoints),
                      [=](auto &sp) { return sp->atAddr(addr); });
}

template <class Stoppoint>
auto StoppointCollection<Stoppoint>::findByAddr(VirtAddr addr) const ->
    typename Stoppoints::const_iterator {
  return const_cast<StoppointCollection *>(this)->findByAddr(addr);
}
} // namespace DDB

#endif // DDB_STOPPOINT_COLLECTION_H
