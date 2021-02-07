#include <memory>
#include <functional>
#include "ThreadFactory.h"

namespace concurrency {
class ThreadManager
{
protected:
    ThreadManager() = default;

public:
    typedef std::function<void(std::shared_ptr<Runnable>)> ExpireCallback;

    virtual ~ThreadManager() = default;

    /**
   * Starts the thread manager. Verifies all attributes have been properly
   * initialized, then allocates necessary resources to begin operation
   */
    virtual void start() = 0;

    /**
   * Stops the thread manager. Aborts all remaining unprocessed task, shuts
   * down all created worker threads, and releases all allocated resources.
   * This method blocks for all worker threads to complete, thus it can
   * potentially block forever if a worker thread is running a task that
   * won't terminate.
   *
   * Worker threads will be joined depending on the threadFactory's detached
   * disposition.
   */
    virtual void stop() = 0;

    enum STATE
    {
        UNINITIALIZED,
        STARTING,
        STARTED,
        JOINING,
        STOPPING,
        STOPPED
    };

    virtual STATE state() const = 0;

    /**
   * \returns the current thread factory
   */
    virtual std::shared_ptr<ThreadFactory> threadFactory() const = 0;

    /**
   * Set the thread factory.
   * \throws InvalidArgumentException if the new thread factory has a different
   *                                  detached disposition than the one replacing it
   */
    virtual void threadFactory(std::shared_ptr<ThreadFactory> value) = 0;

    /**
   * Adds worker thread(s).
   * 
   * 增加指定个数工作线程
   */
    virtual void addWorker(size_t value = 1) = 0;

    /**
   * Removes worker thread(s).
   * Threads are joined if the thread factory detached disposition allows it.
   * Blocks until the number of worker threads reaches the new limit.
   * \param[in]  value  the number to remove
   * \throws InvalidArgumentException if the value is greater than the number
   *                                  of workers
   * 
   * 减少指定个数工作线程
   */
    virtual void removeWorker(size_t value = 1) = 0;

    /**
   * Gets the current number of idle worker threads
   * 
   * 获取闲置线程个数
   */
    virtual size_t idleWorkerCount() const = 0;

    /**
   * Gets the current number of total worker threads
   * 
   * 获取总线程个数
   */
    virtual size_t workerCount() const = 0;

    /**
   * Gets the current number of pending tasks
   * 
   * 获取挂起的任务（队列中等待执行的任务）个数
   */
    virtual size_t pendingTaskCount() const = 0;

    /**
   * Gets the current number of pending and executing tasks
   * 
   * 获取总任务个数（等待执行和已经执行的任务总和）
   */
    virtual size_t totalTaskCount() const = 0;

    /**
   * Gets the maximum pending task count.  0 indicates no maximum
   * 
   * 获取允许挂起任务的最大个数
   */
    virtual size_t pendingTaskCountMax() const = 0;

    /**
   * Gets the number of tasks which have been expired without being run
   * since start() was called.
   * 
   * 获取过期（超时未执行）的任务个数
   */
    virtual size_t expiredTaskCount() const = 0;

    /**
   * Adds a task to be executed at some time in the future by a worker thread.
   * 
   * 添加待执行的任务
   *
   * This method will block if pendingTaskCountMax() in not zero and pendingTaskCount()
   * is greater than or equalt to pendingTaskCountMax().  If this method is called in the
   * context of a ThreadManager worker thread it will throw a
   * TooManyPendingTasksException
   *
   * @param task  The task to queue for execution
   *
   * @param timeout Time to wait in milliseconds to add a task when a pending-task-count
   * is specified. Specific cases:
   * timeout = 0  : Wait forever to queue task.
   * timeout = -1 : Return immediately if pending task count exceeds specified max
   * 
   * 等待添加的超时时间
   * 
   * @param expiration when nonzero, the number of milliseconds the task is valid
   * to be run; if exceeded, the task will be dropped off the queue and not run.
   * 
   * 任务等待执行的超时时间
   *
   * @throws TooManyPendingTasksException Pending task count exceeds max pending task count
   */
    virtual void add(std::shared_ptr<Runnable> task,
                     int64_t timeout = 0LL,
                     int64_t expiration = 0LL) = 0;

    /**
   * Removes a pending task
   * 
   * 移除一个挂起的任务
   */
    virtual void remove(std::shared_ptr<Runnable> task) = 0;

    /**
   * Remove the next pending task which would be run.
   * 
   * 移除下一个将要执行的任务
   *
   * @return the task removed.
   */
    virtual std::shared_ptr<Runnable> removeNextPending() = 0;

    /**
   * Remove tasks from front of task queue that have expired.
   * 
   * 从任务队列头部开始移除超时的任务
   */
    virtual void removeExpiredTasks() = 0;

    /**
   * Set a callback to be called when a task is expired and not run.
   * 
   * 设置超时回调函数
   *
   * @param expireCallback a function called with the shared_ptr<Runnable> for
   * the expired task.
   */
    virtual void setExpireCallback(ExpireCallback expireCallback) = 0;

    static std::shared_ptr<ThreadManager> newThreadManager();

    /**
   * Creates a simple thread manager the uses count number of worker threads and has
   * a pendingTaskCountMax maximum pending tasks. The default, 0, specified no limit
   * on pending tasks
   * 
   * \param count worker threads（工作线程）个数
   * @param pendingTaskCountMax 最大空闲任务个数（空闲任务队列最大长度），0 不限制
   */
    static std::shared_ptr<ThreadManager> newSimpleThreadManager(size_t count = 4,
                                                                 size_t pendingTaskCountMax = 0);

    // 任务
    class Task;

    // 工作线程
    class Worker;

    // ThreadManager Implement 线程管理器接口的实现，ThreadManager 是一个接口
    class Impl;
};
}