#include "ui_task_runtime.h"

#include <chrono>
#include <QDebug>

bool UiTaskTracker::try_begin()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_.load(std::memory_order_acquire))
        return false;
    ++pending_;
    return true;
}

bool UiTaskTracker::accepting() const
{
    return accepting_.load(std::memory_order_acquire);
}

void UiTaskTracker::finish()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_ > 0)
        --pending_;
    if (pending_ == 0)
        condition_.notify_all();
}

void UiTaskTracker::close_and_wait()
{
    std::unique_lock<std::mutex> lock(mutex_);
    accepting_.store(false, std::memory_order_release);
    while (pending_ != 0)
    {
        if (condition_.wait_for(lock, std::chrono::seconds(1)) == std::cv_status::timeout)
            qWarning() << "[Shutdown] Waiting for UI tasks; pending:" << pending_;
    }
}

UiTaskRuntime& UiTaskRuntime::instance()
{
    static UiTaskRuntime runtime;
    return runtime;
}

UiTaskRuntime::UiTaskRuntime()
    : work_guard_(asio::make_work_guard(io_context_))
    , thread_pool_(3)
    , io_thread_([this]
    {
        io_context_.run();
    })
{
}

UiTaskRuntime::~UiTaskRuntime()
{
    qInfo() << "[Shutdown] Stopping UI task runtime";
    work_guard_.reset();
    if (io_thread_.joinable())
    {
        qInfo() << "[Shutdown] Joining UI I/O thread";
        io_thread_.join();
    }
    qInfo() << "[Shutdown] Joining UI thread pool";
    thread_pool_.join();
    qInfo() << "[Shutdown] UI task runtime stopped";
}

asio::io_context& UiTaskRuntime::io_context()
{
    return io_context_;
}

asio::thread_pool::executor_type UiTaskRuntime::thread_pool_executor()
{
    return thread_pool_.get_executor();
}
