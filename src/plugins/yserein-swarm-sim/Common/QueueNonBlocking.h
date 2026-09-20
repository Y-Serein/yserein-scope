#pragma once

#include <queue>
#include <mutex>
#include <memory>

/// 非阻塞线程安全队列：支持并发 push / try_pop，不阻塞线程
template<typename T>
class QueueNonBlocking
{
public:
    QueueNonBlocking() = default;
    QueueNonBlocking(const QueueNonBlocking&) = delete;
    QueueNonBlocking& operator=(const QueueNonBlocking&) = delete;

    /// 压入元素（线程安全）
    void push(T newValue)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(std::move(newValue));
    }

    /// 尝试弹出一个元素（赋值给 value）
    bool try_pop(T& value)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) return false;

        value = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    /// 尝试弹出一个元素（返回 shared_ptr），为空则返回 nullptr
    std::shared_ptr<T> try_pop()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) return nullptr;

        auto result = std::make_shared<T>(std::move(_queue.front()));
        _queue.pop();
        return result;
    }

    /// 判断队列是否为空
    bool empty() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

    /// 获取当前队列大小
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }

private:
    mutable std::mutex _mutex;
    std::queue<T> _queue;
};
