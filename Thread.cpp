#include "Thread.h"

namespace concurrency {
void Thread::threadMain(std::shared_ptr<Thread> thread) {
  thread->setState(started);
  thread->runnable()->run();

  if (thread->getState() != stopping && thread->getState() != stopped) {
    thread->setState(stopping);
  }
}
}