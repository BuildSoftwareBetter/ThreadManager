#include "Mutex.h"
#include <chrono>
#include <mutex>

namespace concurrency {
/**
 * Implementation of Mutex class using C++11 std::timed_mutex
 *
 * Methods throw std::system_error on error.
 *
 * @version $Id:$
 */
class Mutex::impl : public std::timed_mutex {};

Mutex::Mutex() : impl_(new Mutex::impl()) {
}

void* Mutex::getUnderlyingImpl() const {
  return impl_.get();
}

void Mutex::lock() const {
  impl_->lock();
}

bool Mutex::trylock() const {
  return impl_->try_lock();
}

bool Mutex::timedlock(int64_t ms) const {
  return impl_->try_lock_for(std::chrono::milliseconds(ms));
}

void Mutex::unlock() const {
  impl_->unlock();
}
}