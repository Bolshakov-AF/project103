#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
using namespace std;

class ThreadPool;

typedef packaged_task<void()> task_type;
typedef future<void> res_type;
typedef void (*FuncType) (vector<int>&, int, int, bool, ThreadPool&, int);

template<typename T>
class BlockedQueue
{
public:
	void push(T& item)
	{
		lock_guard<mutex> l(m_locker);
		m_task_queue.push(move(item));
		m_notifier.notify_one();
	}

	void pop(T& item)
	{
		unique_lock<mutex> l(m_locker);
		if (m_task_queue.empty())
		{
			m_notifier.wait(l, [this]() {return !m_task_queue.empty(); });
		}
		item = move(m_task_queue.front());
		m_task_queue.pop();
	}

	bool fast_pop(T& item)
	{
		lock_guard<mutex> l(m_locker);
		if (m_task_queue.empty())
		{
			return false;
		}
		item = move(m_task_queue.front());
		m_task_queue.pop();
		return true;
	}
private:
	queue<T> m_task_queue;
	mutex m_locker;
	condition_variable m_notifier;
};

class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	void start();
	void stop();
	res_type push_task(FuncType f, vector<int>& arr, int l, int r, bool enable,
		ThreadPool& tp, int multi_size);
	void thread_func(int qindex);
	void run_pending_task();
private:
	int m_thread_count;
	vector<thread> m_threads;
	vector<BlockedQueue<task_type>> m_queues;
	int m_index;
};

ThreadPool::ThreadPool() : m_thread_count(thread::hardware_concurrency() != 0 ?
	thread::hardware_concurrency() : 2),
	m_queues(m_thread_count),
	m_index(0)
{
	start();
}

ThreadPool::~ThreadPool()
{
	stop();
}

void ThreadPool::start()
{
	for (int i = 0; i < m_thread_count; ++i)
	{
		m_threads.emplace_back(&ThreadPool::thread_func, this, i);
	}
}

void ThreadPool::thread_func(int qindex)
{
	while (1)
	{
		task_type task_to_do;
		bool res;
		int i = 0;
		for (; i < m_thread_count; ++i)
		{
			res = m_queues[(qindex + i) % m_thread_count].fast_pop(task_to_do);
			if (res) break;
		}

		if (!res)
		{
			m_queues[qindex].pop(task_to_do);
		}
		else if (!task_to_do.valid())
		{
			m_queues[(qindex + i) % m_thread_count].push(task_to_do);
		}

		if (!task_to_do.valid()) return;

		task_to_do();
	}
}

void ThreadPool::stop()
{
	for (int i = 0; i < m_thread_count; ++i)
	{
		task_type empty_task;
		m_queues[i].push(empty_task);
	}

	for (auto& t : m_threads)
	{
		if (t.joinable()) t.join();
	}
}

res_type ThreadPool::push_task(FuncType f, vector<int>& arr, int l, int r, bool
	enable, ThreadPool& tp, int multi_size)
{
	int queue_to_push = m_index++ % m_thread_count;
	task_type task([=, &arr, &tp]() {f(arr, l, r, enable, tp, multi_size); });
	auto res = task.get_future();
	m_queues[queue_to_push].push(task);
	return res;
}

void ThreadPool::run_pending_task()
{
	task_type task_to_do;
	bool res;
	int i = 0;
	for (; i < m_thread_count; ++i)
	{
		res = m_queues[i % m_thread_count].fast_pop(task_to_do);
		if (res) break;
	}

	if (!task_to_do.valid())
	{
		this_thread::yield();
	}
	else
	{
		task_to_do();
	}
}

void fill_random(vector<int>& v, int size, int seed)
{
	srand(seed);
	v.clear();
	v.resize(size);

	for (int i = 0; i < size; ++i)
	{
		v[i] = rand() % 10000;
	}
}

void print_v(vector<int>& v, int l, int r)
{
	for (int i = l; i <= r; i++)
	{
		cout << v[i] << " ";
	}
	cout << "\n";
}

bool check_v(vector<int>& v, int l, int r)
{
	for (int i = l + 1; i <= r; ++i)
	{
		if (v[i - 1] > v[i]) return false;
	}
	return true;
}

int partition(vector<int>& arr, int l, int r)
{
	int i = l;
	int j = r - 1;
	int p = r;
	while ((i < r - 1) && (j > l))
	{
		while (arr[i] <= arr[p])
		{
			++i;
			if (i >= j) break;
		}
		while (arr[j] >= arr[p])
		{
			--j;
			if (i >= j) break;
		}
		if (i >= j) break;
		swap(arr[i], arr[j]);
		++i;
		--j;
	}

	if (arr[i] > arr[p]) swap(arr[i], arr[p]);
	return i;
}

void quicksort_single(vector<int>& arr, int l, int r)
{
	if (l >= r) return;
	int m = partition(arr, l, r);
	quicksort_single(arr, l, m);
	quicksort_single(arr, m + 1, r);
}

void quicksort_multithread_nopool(vector<int>& arr, int l, int r, bool enable)
{
	if (l >= r) return;
	int size = r - l;
	int m = partition(arr, l, r);
	if ((size >= 1000) && enable)
	{
		auto f = async(launch::async, [&]() {
			quicksort_multithread_nopool(arr, l, m, true);
			});
		quicksort_multithread_nopool(arr, m + 1, r, true);
	}
	else
	{
		quicksort_multithread_nopool(arr, l, m, false);
		quicksort_multithread_nopool(arr, m + 1, r, false);
	}
}

void quicksort_threadpool(vector<int>& arr, int l, int r, bool enable, ThreadPool&
	tp, int multi_size = 1000)
{
	if (l >= r) return;
	int size = r - l;
	int m = partition(arr, l, r);
	if ((size >= multi_size) && enable)
	{
		auto f = tp.push_task(quicksort_threadpool, ref<vector<int>>(arr), l, m, enable,
			ref<ThreadPool>(tp), multi_size);
		quicksort_threadpool(arr, m + 1, r, true, tp, multi_size);

		while (f.wait_for(chrono::seconds(0)) == future_status::timeout)
		{
			tp.run_pending_task();
		}
	}
	else
	{
		quicksort_threadpool(arr, l, m, false, tp, multi_size);
		quicksort_threadpool(arr, m + 1, r, false, tp, multi_size);
	}
}

int main()
{
	srand(0);

	ThreadPool tp;

	vector<int> v;
	const int num = 10;
	const int size = 100000;

	double sum_time1 = 0;
	for (int i = 0; i < num; ++i)
	{
		fill_random(v, size, i);

		auto start1 = chrono::high_resolution_clock::now();
		quicksort_single(v, 0, v.size() - 1);
		auto stop1 = chrono::high_resolution_clock::now();

		cout << check_v(v, 0, v.size() - 1) << "\n";

		chrono::duration<double> elapsed_time1 = stop1 - start1;
		sum_time1 = sum_time1 + elapsed_time1.count();
		if (!check_v(v, 0, v.size() - 1)) cout << "FAIL!" << "\n";
	}
	cout << "SINGLE THREAD DONE. Elapsed time: " << sum_time1 / num << "\n";

	double sum_time2 = 0;
	for (int i = 0; i < num; ++i)
	{
		fill_random(v, size, i);

		auto start2 = chrono::high_resolution_clock::now();
		quicksort_multithread_nopool(v, 0, v.size() - 1, true);
		auto stop2 = chrono::high_resolution_clock::now();

		cout << check_v(v, 0, v.size() - 1) << "\n";

		chrono::duration<double> elapsed_time2 = stop2 - start2;
		sum_time2 = sum_time2 + elapsed_time2.count();
		if (!check_v(v, 0, v.size() - 1)) cout << "FAIL!" << "\n";
	}
	cout << "MULTIPLE THREAD WITH NO THREADPOOL DONE. Elapsed time: " <<
		sum_time2 / num << "\n";

	double sum_time3 = 0;
	for (int i = 0; i < num; ++i)
	{
		fill_random(v, size, i);

		auto start3 = chrono::high_resolution_clock::now();
		quicksort_threadpool(v, 0, v.size() - 1, true, tp, 1000);
		auto stop3 = chrono::high_resolution_clock::now();

		cout << check_v(v, 0, v.size() - 1) << "\n";

		chrono::duration<double> elapsed_time3 = stop3 - start3;
		sum_time3 = sum_time3 + elapsed_time3.count();
		if (!check_v(v, 0, v.size() - 1)) cout << "FAIL!" << "\n";
	}
	cout << "MULTIPLE THREAD WITH THREADPOOL DONE. Elapsed time: " << sum_time3
		/ num << "\n";

	return 0;
}