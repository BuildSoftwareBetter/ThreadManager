#include "ThreadManager.h"
#include "Monitor.h"
#include "Mutex.h"

#include <memory>

#include <map>
#include <deque>
#include <set>
#include <stdexcept>

using std::dynamic_pointer_cast;
using std::shared_ptr;
using std::unique_ptr;

namespace concurrency {
/**
 * ThreadManager class
 *
 * This class manages a pool of threads. It uses a ThreadFactory to create
 * threads.  It never actually creates or destroys worker threads, rather
 * it maintains statistics on number of idle threads, number of active threads,
 * task backlog, and average wait and service times.
 *
 * There are three different monitors used for signaling different conditions
 * however they all share the same mutex_.
 *
 * @version $Id:$
 */
class ThreadManager::Impl : public ThreadManager
{

public:
    Impl()
        : workerCount_(0),
          workerMaxCount_(0),
          idleCount_(0),
          pendingTaskCountMax_(0),
          expiredCount_(0),
          state_(ThreadManager::UNINITIALIZED),
          monitor_(&mutex_),
          maxMonitor_(&mutex_),
          workerMonitor_(&mutex_) {}

    ~Impl() override { stop(); }

    void start() override;
    void stop() override;

    ThreadManager::STATE state() const override { return state_; }

    shared_ptr<ThreadFactory> threadFactory() const override
    {
        Guard g(mutex_);
        return threadFactory_;
    }

    void threadFactory(shared_ptr<ThreadFactory> value) override
    {
        Guard g(mutex_);
        // 为什么必须两个ThreadFactory的Detached需要相等？？？
        if (threadFactory_ && threadFactory_->isDetached() != value->isDetached())
        {
            throw std::exception();
        }
        threadFactory_ = value;
    }

    void addWorker(size_t value) override;

    void removeWorker(size_t value) override;

    size_t idleWorkerCount() const override { return idleCount_; }

    size_t workerCount() const override
    {
        Guard g(mutex_);
        return workerCount_;
    }

    size_t pendingTaskCount() const override
    {
        Guard g(mutex_);
        return tasks_.size();
    }

    size_t totalTaskCount() const override
    {
        Guard g(mutex_);
        return tasks_.size() + workerCount_ - idleCount_;
    }

    size_t pendingTaskCountMax() const override
    {
        Guard g(mutex_);
        return pendingTaskCountMax_;
    }

    size_t expiredTaskCount() const override
    {
        Guard g(mutex_);
        return expiredCount_;
    }

    void pendingTaskCountMax(const size_t value)
    {
        Guard g(mutex_);
        pendingTaskCountMax_ = value;
    }

    void add(shared_ptr<Runnable> value, int64_t timeout, int64_t expiration) override;

    void remove(shared_ptr<Runnable> task) override;

    shared_ptr<Runnable> removeNextPending() override;

    void removeExpiredTasks() override { removeExpired(false); }

    void setExpireCallback(ExpireCallback expireCallback) override;

private:
    /**
   * Remove one or more expired tasks.
   * \param[in]  justOne  if true, try to remove just one task and return
   */
    void removeExpired(bool justOne);

    /**
   * 根据当前线程id判断是否可阻塞
   * \returns whether it is acceptable to block, depending on the current thread id
   */
    bool canSleep() const;

    /**
   * Lowers the maximum worker count and blocks until enough worker threads complete
   * to get to the new maximum worker limit.  The caller is responsible for acquiring
   * a lock on the class mutex_.
   * 
   * 移除指定数量的工作线程
   */
    void removeWorkersUnderLock(size_t value);

    size_t workerCount_;
    size_t workerMaxCount_;
    size_t idleCount_;
    size_t pendingTaskCountMax_;
    size_t expiredCount_;
    ExpireCallback expireCallback_;

    ThreadManager::STATE state_;
    shared_ptr<ThreadFactory> threadFactory_;

    friend class ThreadManager::Task;
    typedef std::deque<shared_ptr<Task>> TaskQueue;
    TaskQueue tasks_;
    Mutex mutex_;
    Monitor monitor_;

    /**
   * 任务队列最大监视器：通知任务队列中任务个数达到最大值或由最大值变为小于最大值
  */
    Monitor maxMonitor_;
    Monitor workerMonitor_; // used to synchronize changes in worker count；用于通知worker工作者线程数改变

    friend class ThreadManager::Worker;
    /**
   * 有效的工作线程集合（线程池）
  */
    std::set<shared_ptr<Thread>> workers_;

    /**
   * 死亡的工作线程集合
  */
    std::set<shared_ptr<Thread>> deadWorkers_;

    /**
   * 线程id和线程的键值型集合
   * 保存了所有wokers中的线程
  */
    std::map<const Thread::id_t, shared_ptr<Thread>> idMap_;
};

/**
 * 可执行的任务类
 *
 * 使用代理模式封装了可执行的Runnable任务对象，可通过expireTime_控制等待任务执行的超时时间
 */
class ThreadManager::Task : public Runnable
{

public:
    enum STATE
    {
        WAITING,
        EXECUTING,
        TIMEDOUT,
        COMPLETE
    };

    Task(shared_ptr<Runnable> runnable, uint64_t expiration = 0ULL)
        : runnable_(runnable), state_(WAITING)
    {
        if (expiration != 0ULL)
        {
            expireTime_.reset(new std::chrono::steady_clock::time_point(
                std::chrono::steady_clock::now() + std::chrono::milliseconds(expiration)));
        }
    }

    ~Task() override = default;

    // 只有在state_ == EXECUTING时才可被执行
    void run() override
    {
        if (state_ == EXECUTING)
        {
            runnable_->run();
            state_ = COMPLETE;
        }
    }

    shared_ptr<Runnable> getRunnable() { return runnable_; }

    /**
   * 获取任务超时时间
   */
    const unique_ptr<std::chrono::steady_clock::time_point> &getExpireTime() const
    {
        return expireTime_;
    }

private:
    shared_ptr<Runnable> runnable_;
    friend class ThreadManager::Worker;
    STATE state_;
    unique_ptr<std::chrono::steady_clock::time_point> expireTime_;
};

/**
 * 工作者线程
 * 负责从任务队列中获取队列，并执行任务
*/
class ThreadManager::Worker : public Runnable
{
    enum STATE
    {
        UNINITIALIZED,
        STARTING,
        STARTED,
        STOPPING,
        STOPPED
    };

public:
    Worker(ThreadManager::Impl *manager) : manager_(manager), state_(UNINITIALIZED) {}

    ~Worker() override = default;

private:
    /**
   * 判断工作者线程是否需要继续处于活跃状态
   * 1. 超过最大的工作者线程数了，需要减少工作者线程，则返回false；不需要处于活跃状态
   * 2. 线程管理器处于JOINING状态，且任务队列已经为空，则返回false
   * 3. 其他返回true
   * 
  */
    bool isActive() const
    {
        return (manager_->workerCount_ <= manager_->workerMaxCount_) || (manager_->state_ == JOINING && !manager_->tasks_.empty());
    }

public:
    /**
   * Worker工作者的入口
   * Worker entry point
   *
   * As long as worker thread is running, pull tasks off the task queue and
   * execute.
   */
    void run() override
    {
        Guard g(manager_->mutex_);

        /**
     * This method has three parts; one is to check for and account for
     * admitting a task which happens under a lock.  Then the lock is released
     * and the task itself is executed.  Finally we do some accounting
     * under lock again when the task completes.
     */

        /**
     * Admitting
     */

        /**
     * Increment worker semaphore and notify manager if worker count reached
     * desired max
     */
        bool active = manager_->workerCount_ < manager_->workerMaxCount_;
        if (active)
        {
            if (++manager_->workerCount_ == manager_->workerMaxCount_)
            {
                // 通知线程管理器，当前worker线程数量已改变
                manager_->workerMonitor_.notify();
            }
        }

        while (active)
        {
            /**
       * While holding manager monitor block for non-empty task queue (Also
       * check that the thread hasn't been requested to stop). Once the queue
       * is non-empty, dequeue a task, release monitor, and execute. If the
       * worker max count has been decremented such that we exceed it, mark
       * ourself inactive, decrement the worker count and notify the manager
       * (technically we're notifying the next blocked thread but eventually
       * the manager will see it.
       */
            active = isActive();

            while (active && manager_->tasks_.empty())
            {
                manager_->idleCount_++;
                manager_->monitor_.wait();
                active = isActive();
                manager_->idleCount_--;
            }

            shared_ptr<ThreadManager::Task> task;

            if (active)
            {
                if (!manager_->tasks_.empty())
                {
                    task = manager_->tasks_.front();
                    manager_->tasks_.pop_front();
                    if (task->state_ == ThreadManager::Task::WAITING)
                    {
                        // If the state is changed to anything other than EXECUTING or TIMEDOUT here
                        // then the execution loop needs to be changed below.
                        task->state_ = (task->getExpireTime() && *(task->getExpireTime()) < std::chrono::steady_clock::now())
                                           ? ThreadManager::Task::TIMEDOUT
                                           : ThreadManager::Task::EXECUTING;
                    }
                }

                /* If we have a pending task max and we just dropped below it, wakeup any
            thread that might be blocked on add. */
                if (manager_->pendingTaskCountMax_ != 0 && manager_->tasks_.size() <= manager_->pendingTaskCountMax_ - 1)
                {
                    manager_->maxMonitor_.notify();
                }
            }

            /**
       * Execution - not holding a lock
       */
            if (task)
            {
                // task是否超时
                if (task->state_ == ThreadManager::Task::EXECUTING)
                {

                    // Release the lock so we can run the task without blocking the thread manager
                    manager_->mutex_.unlock();

                    try
                    {
                        task->run();
                    }
                    catch (const std::exception &e)
                    {
                        printf("[ERROR] task->run() raised an exception: %s", e.what());
                    }
                    catch (...)
                    {
                        printf("[ERROR] task->run() raised an unknown exception");
                    }

                    // Re-acquire the lock to proceed in the thread manager
                    manager_->mutex_.lock();
                }
                else if (manager_->expireCallback_)
                {
                    // The only other state the task could have been in is TIMEDOUT (see above)
                    manager_->expireCallback_(task->getRunnable());
                    manager_->expiredCount_++;
                }
            }
        }

        /**
     * 回收worker的线程
     * 
     * Final accounting for the worker thread that is done working
     */
        manager_->deadWorkers_.insert(this->thread());
        if (--manager_->workerCount_ == manager_->workerMaxCount_)
        {
            manager_->workerMonitor_.notify();
        }
    }

private:
    ThreadManager::Impl *manager_;
    friend class ThreadManager::Impl;
    STATE state_;
};

/**
 * 增加工作线程数量（增加线程池中线程数）
*/
void ThreadManager::Impl::addWorker(size_t value)
{
    std::set<shared_ptr<Thread>> newThreads;
    // 创建worker，并把worker关联到thread对象
    for (size_t ix = 0; ix < value; ix++)
    {
        shared_ptr<ThreadManager::Worker> worker = std::make_shared<ThreadManager::Worker>(this);
        newThreads.insert(threadFactory_->newThread(worker));
    }

    Guard g(mutex_);
    workerMaxCount_ += value;
    workers_.insert(newThreads.begin(), newThreads.end());

    for (const auto &newThread : newThreads)
    {
        shared_ptr<ThreadManager::Worker> worker = dynamic_pointer_cast<ThreadManager::Worker, Runnable>(newThread->runnable());
        worker->state_ = ThreadManager::Worker::STARTING;
        // 启动worker线程
        newThread->start();
        idMap_.insert(
            std::pair<const Thread::id_t, shared_ptr<Thread>>(newThread->getId(), newThread));
    }

    // 等待全部工作线程进入状态（执行run函数）
    while (workerCount_ != workerMaxCount_)
    {
        workerMonitor_.wait();
    }
}

void ThreadManager::Impl::start()
{
    Guard g(mutex_);
    if (state_ == ThreadManager::STOPPED)
    {
        return;
    }

    if (state_ == ThreadManager::UNINITIALIZED)
    {
        if (!threadFactory_)
        {
            throw std::exception();
        }
        state_ = ThreadManager::STARTED;
        monitor_.notifyAll();
    }

    while (state_ == STARTING)
    {
        monitor_.wait();
    }
}

void ThreadManager::Impl::stop()
{
    Guard g(mutex_);
    bool doStop = false;

    if (state_ != ThreadManager::STOPPING && state_ != ThreadManager::JOINING && state_ != ThreadManager::STOPPED)
    {
        doStop = true;
        state_ = ThreadManager::JOINING;
    }

    if (doStop)
    {
        removeWorkersUnderLock(workerCount_);
    }

    state_ = ThreadManager::STOPPED;
}

void ThreadManager::Impl::removeWorker(size_t value)
{
    Guard g(mutex_);
    removeWorkersUnderLock(value);
}

void ThreadManager::Impl::removeWorkersUnderLock(size_t value)
{
    if (value > workerMaxCount_)
    {
        throw std::exception();
    }

    workerMaxCount_ -= value;

    if (idleCount_ > value)
    {
        // There are more idle workers than we need to remove,
        // so notify enough of them so they can terminate.
        for (size_t ix = 0; ix < value; ix++)
        {
            monitor_.notify();
        }
    }
    else
    {
        // There are as many or less idle workers than we need to remove,
        // so just notify them all so they can terminate.
        monitor_.notifyAll();
    }

    // 等待工作线程减少到workerMaxCount_个数
    while (workerCount_ != workerMaxCount_)
    {
        workerMonitor_.wait();
    }

    /**
   * 从死亡工作线程集合deadWorkers_中移除所有的元素，并清空deadWorkers_
  */
    for (const auto &deadWorker : deadWorkers_)
    {

        // when used with a joinable thread factory, we join the threads as we remove them
        if (!threadFactory_->isDetached())
        {
            deadWorker->join();
        }

        idMap_.erase(deadWorker->getId());
        workers_.erase(deadWorker);
    }

    deadWorkers_.clear();
}

bool ThreadManager::Impl::canSleep() const
{
    const Thread::id_t id = threadFactory_->getCurrentThreadId();
    return idMap_.find(id) == idMap_.end();
}

void ThreadManager::Impl::add(shared_ptr<Runnable> value, int64_t timeout, int64_t expiration)
{
    Guard g(mutex_, timeout);

    if (!g)
    {
        throw std::exception();
    }

    if (state_ != ThreadManager::STARTED)
    {
        throw std::exception(
            "ThreadManager::Impl::add ThreadManager "
            "not started");
    }

    // if we're at a limit, remove an expired task to see if the limit clears
    if (pendingTaskCountMax_ > 0 && (tasks_.size() >= pendingTaskCountMax_))
    {
        removeExpired(true);
    }

    if (pendingTaskCountMax_ > 0 && (tasks_.size() >= pendingTaskCountMax_))
    {
        if (canSleep() && timeout >= 0)
        { // 带添加等待超时时间的task需先判断是否能等待，否则在等待时可能造成死锁
            while (pendingTaskCountMax_ > 0 && tasks_.size() >= pendingTaskCountMax_)
            {
                // This is thread safe because the mutex is shared between monitors.
                maxMonitor_.wait(timeout);
            }
        }
        else
        {
            throw std::exception();
        }
    }

    tasks_.push_back(std::make_shared<ThreadManager::Task>(value, expiration));

    // If idle thread is available notify it, otherwise all worker threads are
    // running and will get around to this task in time.
    if (idleCount_ > 0)
    {
        monitor_.notify();
    }
}

void ThreadManager::Impl::remove(shared_ptr<Runnable> task)
{
    Guard g(mutex_);
    if (state_ != ThreadManager::STARTED)
    {
        throw std::exception(
            "ThreadManager::Impl::remove ThreadManager not "
            "started");
    }

    for (auto it = tasks_.begin(); it != tasks_.end(); ++it)
    {
        if ((*it)->getRunnable() == task)
        {
            tasks_.erase(it);
            return;
        }
    }
}

std::shared_ptr<Runnable> ThreadManager::Impl::removeNextPending()
{
    Guard g(mutex_);
    if (state_ != ThreadManager::STARTED)
    {
        throw std::exception(
            "ThreadManager::Impl::removeNextPending "
            "ThreadManager not started");
    }

    if (tasks_.empty())
    {
        return std::shared_ptr<Runnable>();
    }

    shared_ptr<ThreadManager::Task> task = tasks_.front();
    tasks_.pop_front();

    return task->getRunnable();
}

void ThreadManager::Impl::removeExpired(bool justOne)
{
    // this is always called under a lock
    if (tasks_.empty())
    {
        return;
    }
    auto now = std::chrono::steady_clock::now();

    for (auto it = tasks_.begin(); it != tasks_.end();)
    {
        if ((*it)->getExpireTime() && *((*it)->getExpireTime()) < now)
        {
            if (expireCallback_)
            {
                expireCallback_((*it)->getRunnable());
            }
            it = tasks_.erase(it);
            ++expiredCount_;
            if (justOne)
            {
                return;
            }
        }
        else
        {
            ++it;
        }
    }
}

void ThreadManager::Impl::setExpireCallback(ExpireCallback expireCallback)
{
    Guard g(mutex_);
    expireCallback_ = expireCallback;
}

/**
 * 一个简单的线程管理器实现
 */
class SimpleThreadManager : public ThreadManager::Impl
{

public:
    SimpleThreadManager(size_t workerCount = 4, size_t pendingTaskCountMax = 0)
        : workerCount_(workerCount), pendingTaskCountMax_(pendingTaskCountMax) {}

    void start() override
    {
        ThreadManager::Impl::pendingTaskCountMax(pendingTaskCountMax_);
        ThreadManager::Impl::start();
        addWorker(workerCount_);
    }

private:
    const size_t workerCount_;
    const size_t pendingTaskCountMax_;
};

shared_ptr<ThreadManager> ThreadManager::newThreadManager()
{
    return shared_ptr<ThreadManager>(new ThreadManager::Impl());
}

shared_ptr<ThreadManager> ThreadManager::newSimpleThreadManager(size_t count,
                                                                size_t pendingTaskCountMax)
{
    return shared_ptr<ThreadManager>(new SimpleThreadManager(count, pendingTaskCountMax));
}
}