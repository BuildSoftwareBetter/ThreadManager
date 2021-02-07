#include "Monitor.h"
#include <assert.h>

#include <condition_variable>
#include <chrono>
#include <thread>
#include <mutex>

#define THRIFT_ETIMEDOUT 10060L

namespace concurrency {
/**
 * Monitor implementation using the std thread library
 *
 * @version $Id:$
 */
class Monitor::Impl {

public:
  Impl() : ownedMutex_(new Mutex()), conditionVariable_(), mutex_(nullptr) { init(ownedMutex_.get()); }

  Impl(Mutex* mutex) : ownedMutex_(), conditionVariable_(), mutex_(nullptr) { init(mutex); }

  Impl(Monitor* monitor) : ownedMutex_(), conditionVariable_(), mutex_(nullptr) {
    init(&(monitor->mutex()));
  }

  Mutex& mutex() { return *mutex_; }
  void lock() { mutex_->lock(); }
  void unlock() { mutex_->unlock(); }

  /**
   * Exception-throwing version of waitForTimeRelative(), called simply
   * wait(int64) for historical reasons.  Timeout is in milliseconds.
   *
   * If the condition occurs,  this function returns cleanly; on timeout or
   * error an exception is thrown.
   */
  void wait(const std::chrono::milliseconds &timeout) {
    int result = waitForTimeRelative(timeout);
    if (result == THRIFT_ETIMEDOUT) {
      throw std::exception();
    } else if (result != 0) {
      throw std::exception("Monitor::wait() failed");
    }
  }

  /**
   * Waits until the specified timeout in milliseconds for the condition to
   * occur, or waits forever if timeout is zero.
   *
   * Returns 0 if condition occurs, THRIFT_ETIMEDOUT on timeout, or an error code.
   */
  int waitForTimeRelative(const std::chrono::milliseconds &timeout) {
    if (timeout.count() == 0) {
      return waitForever();
    }

    assert(mutex_);
    auto* mutexImpl = static_cast<std::timed_mutex*>(mutex_->getUnderlyingImpl());
    assert(mutexImpl);

    std::unique_lock<std::timed_mutex> lock(*mutexImpl, std::adopt_lock);
    bool timedout = (conditionVariable_.wait_for(lock, timeout)
                     == std::cv_status::timeout);
    lock.release();
    return (timedout ? THRIFT_ETIMEDOUT : 0);
  }

  /**
   * Waits until the absolute time specified by abstime.
   * Returns 0 if condition occurs, THRIFT_ETIMEDOUT on timeout, or an error code.
   */
  int waitForTime(const std::chrono::time_point<std::chrono::steady_clock>& abstime) {
    assert(mutex_);
    auto* mutexImpl = static_cast<std::timed_mutex*>(mutex_->getUnderlyingImpl());
    assert(mutexImpl);

    std::unique_lock<std::timed_mutex> lock(*mutexImpl, std::adopt_lock);
    bool timedout = (conditionVariable_.wait_until(lock, abstime)
                     == std::cv_status::timeout);
    lock.release();
    return (timedout ? THRIFT_ETIMEDOUT : 0);
  }

  /**
   * Waits forever until the condition occurs.
   * Returns 0 if condition occurs, or an error code otherwise.
   */
  int waitForever() {
    assert(mutex_);
    auto* mutexImpl = static_cast<std::timed_mutex*>(mutex_->getUnderlyingImpl());
    assert(mutexImpl);

    std::unique_lock<std::timed_mutex> lock(*mutexImpl, std::adopt_lock);
    conditionVariable_.wait(lock);
    lock.release();
    return 0;
  }

  void notify() { conditionVariable_.notify_one(); }

  void notifyAll() { conditionVariable_.notify_all(); }

private:
  void init(Mutex* mutex) { mutex_ = mutex; }

  const std::unique_ptr<Mutex> ownedMutex_;
  std::condition_variable_any conditionVariable_;
  Mutex* mutex_;
};

Monitor::Monitor() : impl_(new Monitor::Impl()) {
}
Monitor::Monitor(Mutex* mutex) : impl_(new Monitor::Impl(mutex)) {
}
Monitor::Monitor(Monitor* monitor) : impl_(new Monitor::Impl(monitor)) {
}

Monitor::~Monitor() {
  delete impl_;
}

Mutex& Monitor::mutex() const {
  return const_cast<Monitor::Impl*>(impl_)->mutex();
}

void Monitor::lock() const {
  const_cast<Monitor::Impl*>(impl_)->lock();
}

void Monitor::unlock() const {
  const_cast<Monitor::Impl*>(impl_)->unlock();
}

void Monitor::wait(const std::chrono::milliseconds &timeout) const {
  const_cast<Monitor::Impl*>(impl_)->wait(timeout);
}

int Monitor::waitForTime(const std::chrono::time_point<std::chrono::steady_clock>& abstime) const {
  return const_cast<Monitor::Impl*>(impl_)->waitForTime(abstime);
}

int Monitor::waitForTimeRelative(const std::chrono::milliseconds &timeout) const {
  return const_cast<Monitor::Impl*>(impl_)->waitForTimeRelative(timeout);
}

int Monitor::waitForever() const {
  return const_cast<Monitor::Impl*>(impl_)->waitForever();
}

void Monitor::notify() const {
  const_cast<Monitor::Impl*>(impl_)->notify();
}

void Monitor::notifyAll() const {
  const_cast<Monitor::Impl*>(impl_)->notifyAll();
}
}