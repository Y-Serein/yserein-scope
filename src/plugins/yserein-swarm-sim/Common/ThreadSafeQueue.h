#pragma once

#include <queue>
#include <mutex>
#include <memory>

template<typename T>
class ThreadSafeQueue
{
public:
	ThreadSafeQueue() {}

	void push(T new_value)
	{
		std::lock_guard<std::mutex> lk(mut);
		data_queue.push(std::move(new_value));
		data_cond.notify_one();
	}

	void push_bulk(const std::vector<T>& values)
	{
		if ( values.empty() ) return ;

		std::lock_guard<std::mutex> lk(mut);
		for (const auto& val : values) {
			data_queue.push(val);
		}
		data_cond.notify_one();
	}

	void push_bulk(std::vector<T>&& values)
	{
		if (values.empty()) return;

		std::lock_guard<std::mutex> lk(mut);
		for (auto& val : values) {
			data_queue.push(std::move(val));
		}
		data_cond.notify_all();
	}

	void wait_and_pop(T& value)
	{
		std::unique_lock<std::mutex> lk(mut);
		data_cond.wait(lk, [this] {return !data_queue.empty(); });
		value = std::move(data_queue.front());
		data_queue.pop();
	}

	std::shared_ptr<T> wait_and_pop()
	{
		std::unique_lock<std::mutex> lk(mut);
		data_cond.wait(lk, [this] {return !data_queue.empty(); });

		std::shared_ptr<T> res(
			std::make_shared<T>(std::move(data_queue.front()))
		);
		data_queue.pop();

		return res;
	}

	void wait_and_pop_bulk(std::vector<T>& out, size_t max_count = 1024)
	{
		out.clear();
		std::unique_lock<std::mutex> lk(mut);
		data_cond.wait(lk, [this] { return !data_queue.empty(); });

		size_t count = 0;
		while (!data_queue.empty() && count < max_count) {
			out.push_back(std::move(data_queue.front()));
			data_queue.pop();
			++count;
		}
	}


	bool try_pop(T& value)
	{
		std::lock_guard<std::mutex> lk(mut);
		if (data_queue.empty()) {
			return false;
		}

		value = std::move(data_queue.front());
		data_queue.pop();

		return true;
	}

	std::shared_ptr<T> try_pop()
	{
		std::lock_guard<std::mutex> lk(mut);
		if (data_queue.empty()) {
			return std::shared_ptr<T>();
		}

		std::shared_ptr<T>  res(
			std::make_shared<T>(std::move(data_queue.front()))
		);
		data_queue.pop();
		return res;
	}

	bool try_pop_bulk(std::vector<T>& out, size_t max_count)
	{
		std::lock_guard<std::mutex> lk(mut);
		if (data_queue.empty()) return false;

		out.clear();
		size_t count = 0;
		while (!data_queue.empty() && count < max_count) {
			out.push_back(std::move(data_queue.front()));
			data_queue.pop();
			++count;
		}
		return true;
	}

	bool empty() const
	{
		std::lock_guard<std::mutex> lk(mut);
		return data_queue.empty();
	}

	void notify_all()
	{
		std::unique_lock<std::mutex> lk(mut);
		data_cond.notify_all();
	}

private:
	mutable std::mutex mut;
	std::queue<T> data_queue;
	std::condition_variable data_cond;
};