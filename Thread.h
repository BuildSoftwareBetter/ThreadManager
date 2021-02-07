#include <memory>
#include <thread>
#include "Monitor.h"

namespace concurrency {
class Thread;

/**
 * Minimal runnable class.  More or less analogous to java.lang.Runnable.
 * 
 * 封装的执行单元，可独立的调用run运行
 *
 * @version $Id:$
 */
class Runnable {

public:
  virtual ~Runnable() = default;
  virtual void run() = 0;

  /**
   * Gets the thread object that is hosting this runnable object  - can return
   * an empty boost::shared pointer if no references remain on that thread object
   */
  virtual std::shared_ptr<Thread> thread() { return thread_.lock(); }

  /**
   * Sets the thread that is executing this object.  This is only meant for
   * use by concrete implementations of Thread.
   */
  virtual void thread(std::shared_ptr<Thread> value) { thread_ = value; }

private:
  std::weak_ptr<Thread> thread_;
};

/**
 * 线程封装，创建线程执行Runnable::run()
 * 
 * Minimal thread class. Returned by thread factory bound to a Runnable object
 * and ready to start execution.  More or less analogous to java.lang.Thread
 * (minus all the thread group, priority, mode and other baggage, since that
 * is difficult to abstract across platforms and is left for platform-specific
 * ThreadFactory implemtations to deal with
 *
 * @see apache::thrift::concurrency::ThreadFactory)
 */
class Thread final : public std::enable_shared_from_this<Thread> {

public:
  typedef std::thread::id id_t;

  enum STATE { uninitialized, starting, started, stopping, stopped };

  static void threadMain(std::shared_ptr<Thread> thread);

  static inline bool is_current(id_t t) { return t == std::this_thread::get_id(); }
  static inline id_t get_current() { return std::this_thread::get_id(); }

  Thread(bool detached, std::shared_ptr<Runnable> runnable)
    : state_(uninitialized), detached_(detached) {
    this->_runnable = runnable;
  }

  ~Thread() {
    if (!detached_ && thread_->joinable()) {
      try {
        join();
      } catch (...) {
        // We're really hosed.
      }
    }
  }

  STATE getState() const
  {
    Synchronized sync(monitor_);
    return state_;
  }

  void setState(STATE newState)
  {
    Synchronized sync(monitor_);
    state_ = newState;

    // unblock start() with the knowledge that the thread has actually
    // started running, which avoids a race in detached threads.
    if (newState == started) {
	  monitor_.notify();
    }
  }

  /**
   * Starts the thread. Does platform specific thread creation and
   * configuration then invokes the run method of the Runnable object bound
   * to this thread.
   */
  void start() {
    if (getState() != uninitialized) {
      return;
    }

    std::shared_ptr<Thread> selfRef = shared_from_this();
    setState(starting);


    // 创建线程，调用threadMain函数，参数为this
    Synchronized sync(monitor_);
    thread_ = std::unique_ptr<std::thread>(new std::thread(threadMain, selfRef));

    if (detached_)
      thread_->detach();
    
    // Wait for the thread to start and get far enough to grab everything
    // that it needs from the calling context, thus absolving the caller
    // from being required to hold on to runnable indefinitely.
    monitor_.wait();
  }

  /**
   * Join this thread. If this thread is joinable, the calling thread blocks
   * until this thread completes.  If the target thread is not joinable, then
   * nothing happens.
   */
  void join() {
    if (!detached_ && state_ != uninitialized) {
      thread_->join();
    }
  }

  /**
   * Gets the thread's platform-specific ID
   */
  Thread::id_t getId() const { return thread_.get() ? thread_->get_id() : std::thread::id(); }

  /**
   * Gets the runnable object this thread is hosting
   */
  std::shared_ptr<Runnable> runnable() const { return _runnable; }

private:
  std::shared_ptr<Runnable> _runnable;
  std::unique_ptr<std::thread> thread_;
  Monitor monitor_;
  STATE state_;
  // 是否在线程运行后分离线程，分离后则当前std::thread变量与运行的线程无关
  bool detached_;
};
}