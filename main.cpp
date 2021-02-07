#include "ThreadManager.h"
#include <iostream>
#include <Windows.h>

using namespace concurrency;
// #include <memory>

/**
 * 线程管理器测试
 * 
 * 1. 数据库查询线程
*/

class DbSqlQuery : public Runnable
{
public:
    DbSqlQuery(const std::string &sql) : m_sql(sql), m_exec_times(100) {}

    virtual void run() override
    {
        std::cout << std::this_thread::get_id() << " exec sql " << m_sql.data() << std::endl;
        // _sleep(1000);
        Sleep(100);
    }

    ~DbSqlQuery() override = default;

private:
    std::string m_sql;
    const int m_exec_times;
};

class LoggerWriter : public Runnable
{
public:
    LoggerWriter(const std::string &log) : m_log(log) {}

    virtual void run() override
    {
        std::cout << std::this_thread::get_id() << " log : " << m_log.data() << std::endl;
        Sleep(100);
    }

private:
    const std::string m_log;
};

int main()
{
    std::shared_ptr<ThreadManager> _threadManager = ThreadManager::newSimpleThreadManager(10, 10);
    std::shared_ptr<ThreadFactory> _threadFactory = std::make_shared<ThreadFactory>(false);
    _threadManager->threadFactory(_threadFactory);

    auto t1 = std::make_shared<DbSqlQuery>("select * from t1;");
    auto t2 = std::make_shared<LoggerWriter>("this is test log");

    std::cout << __LINE__ << std::endl;
    _threadManager->start();
    std::cout << __LINE__ << std::endl;

    for (int i = 0; i < 100; i++)
    {
        _threadManager->add(t1, 100000, 100000);
        _threadManager->add(t2, 100000, 100000);
    }

    std::cout << __LINE__ << std::endl;

    Sleep(1000 * 1000 * 10);

    _threadManager->stop();
}