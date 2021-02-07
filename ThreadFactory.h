#include <memory>
#include "Thread.h"

namespace concurrency {
/**
 * Factory to create thread object and bind them to Runnable
 * object for execution
 * 
 * 线程工厂：创建一个绑定Runnable对象的Thread线程对象
 */
class ThreadFactory final {
public:
  /**
   * detached ： 是否线程运行后分离线程
   * 
   * All threads created by a factory are reference-counted
   * via std::shared_ptr.  The factory guarantees that threads and the Runnable tasks
   * they host will be properly cleaned up once the last strong reference
   * to both is given up.
   *
   * By default threads are not joinable.
   */
  ThreadFactory(bool detached = true) : detached_(detached) { }

  ~ThreadFactory() = default;

  /**
   * Gets current detached mode
   */
  bool isDetached() const { return detached_; }

  /**
   * Sets the detached disposition of newly created threads.
   */
  void setDetached(bool detached) { detached_ = detached; }

  /**
   * Create a new thread.
   * 
   * 创建一个绑定Runnable的线程对象
   */
  std::shared_ptr<Thread> newThread(std::shared_ptr<Runnable> runnable) const;

  /**
   * Gets the current thread id or unknown_thread_id if the current thread is not a thrift thread
   */
  Thread::id_t getCurrentThreadId() const;

private:
  bool detached_;
};
}