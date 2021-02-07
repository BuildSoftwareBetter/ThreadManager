#include "ThreadFactory.h"
namespace concurrency {
std::shared_ptr<Thread> ThreadFactory::newThread(std::shared_ptr<Runnable> runnable) const {
  std::shared_ptr<Thread> result = std::make_shared<Thread>(isDetached(), runnable);
  runnable->thread(result);
  return result;
}

/**
 * \return 获取当前线程id
*/
Thread::id_t ThreadFactory::getCurrentThreadId() const {
  return std::this_thread::get_id();
}
}