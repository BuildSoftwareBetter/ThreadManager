#ifndef _CONCURRENCY_MUTEX_H_
#define _CONCURRENCY_MUTEX_H_ 1

#include <memory>

namespace concurrency {
/**
 * 带超时时间的互斥量，底层封装了std::timed_mutex
 * A simple mutex class 
 *
 * @version $Id:$
 */
class Mutex
{
public:
    Mutex();
    virtual ~Mutex() = default;

    virtual void lock() const;
    virtual bool trylock() const;
    virtual bool timedlock(int64_t milliseconds) const;
    virtual void unlock() const;

    void *getUnderlyingImpl() const;

private:
    class impl;
    std::shared_ptr<impl> impl_;
};

/**
 * Mutex互斥量监视器
 * Mutex的RAII管理器，构造时获得Mutex并lock加锁，析构时unlock解锁
*/
class Guard
{
public:
    Guard(const Mutex &value, int64_t timeout = 0) : mutex_(&value)
    {
        if (timeout == 0)
        {
            value.lock();
        }
        else if (timeout < 0)
        {
            if (!value.trylock())
            {
                mutex_ = nullptr;
            }
        }
        else
        {
            if (!value.timedlock(timeout))
            {
                mutex_ = nullptr;
            }
        }
    }
    ~Guard()
    {
        if (mutex_)
        {
            mutex_->unlock();
        }
    }

    operator bool() const { return (mutex_ != nullptr); }

private:
    const Mutex *mutex_;
};
}

#endif