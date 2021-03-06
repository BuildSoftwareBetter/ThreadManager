#ifndef _CONCURRENCY_MONITOR_H_
#define _CONCURRENCY_MONITOR_H_ 1

#include <chrono>
#include "Mutex.h"
namespace concurrency {
/**
 * A monitor is a combination mutex and condition-event.  Waiting and
 * notifying condition events requires that the caller own the mutex.  Mutex
 * lock and unlock operations can be performed independently of condition
 * events.  This is more or less analogous to java.lang.Object multi-thread
 * operations.
 *
 * Note the Monitor can create a new, internal mutex; alternatively, a
 * separate Mutex can be passed in and the Monitor will re-use it without
 * taking ownership.  It's the user's responsibility to make sure that the
 * Mutex is not deallocated before the Monitor.
 *
 * Note that all methods are const.  Monitors implement logical constness, not
 * bit constness.  This allows const methods to call monitor methods without
 * needing to cast away constness or change to non-const signatures.
 *
 * @version $Id:$
 */
class Monitor {
public:
  /** Creates a new mutex, and takes ownership of it. */
  Monitor();

  /** Uses the provided mutex without taking ownership. */
  explicit Monitor(Mutex* mutex);

  /** Uses the mutex inside the provided Monitor without taking ownership. */
  explicit Monitor(Monitor* monitor);

  /** Deallocates the mutex only if we own it. */
  virtual ~Monitor();

  Mutex& mutex() const;

  virtual void lock() const;

  virtual void unlock() const;

  /**
   * Waits a maximum of the specified timeout in milliseconds for the condition
   * to occur, or waits forever if timeout is zero.
   *
   * Returns 0 if condition occurs, THRIFT_ETIMEDOUT on timeout, or an error code.
   */
  int waitForTimeRelative(const std::chrono::milliseconds &timeout) const;

  int waitForTimeRelative(uint64_t timeout_ms) const { return waitForTimeRelative(std::chrono::milliseconds(timeout_ms)); }

  /**
   * Waits until the absolute time specified by abstime.
   * Returns 0 if condition occurs, THRIFT_ETIMEDOUT on timeout, or an error code.
   */
  int waitForTime(const std::chrono::time_point<std::chrono::steady_clock>& abstime) const;

  /**
   * Waits forever until the condition occurs.
   * Returns 0 if condition occurs, or an error code otherwise.
   */
  int waitForever() const;

  /**
   * Exception-throwing version of waitForTimeRelative(), called simply
   * wait(std::chrono::milliseconds) for historical reasons.  Timeout is in milliseconds.
   *
   * If the condition occurs, this function returns cleanly; on timeout or
   * error an exception is thrown.
   */
  void wait(const std::chrono::milliseconds &timeout) const;

  void wait(uint64_t timeout_ms = 0ULL) const { this->wait(std::chrono::milliseconds(timeout_ms)); }

  /** Wakes up one thread waiting on this monitor. */
  virtual void notify() const;

  /** Wakes up all waiting threads on this monitor. */
  virtual void notifyAll() const;

private:
  class Impl;

  Impl* impl_;
};

class Synchronized {
public:
  Synchronized(const Monitor* monitor) : g(monitor->mutex()) {}
  Synchronized(const Monitor& monitor) : g(monitor.mutex()) {}

private:
  Guard g;
};
}
#endif