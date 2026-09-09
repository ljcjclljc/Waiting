---
{
  "title": "C++ 每日一题：面试手撕一个简单定时器",
  "slug": "cpp-daily-handwritten-timer",
  "date": "2026-09-09",
  "updated": "2026-09-09",
  "excerpt": "使用 steady_clock、小顶堆和懒删除实现一个精简的 C++ 定时器，支持添加任务、取消任务和事件循环 tick。",
  "category": { "name": "C++每日一题", "slug": "cpp-daily" },
  "tags": [
    { "name": "C++", "slug": "cpp" },
    { "name": "每日一题", "slug": "daily-question" },
    { "name": "定时器", "slug": "timer" },
    { "name": "数据结构", "slug": "data-structure" }
  ],
  "featured": false,
  "draft": false,
  "seoDescription": "C++ 每日一题：面试手撕精简定时器，掌握小顶堆、懒删除、steady_clock 和 tick 事件循环。"
}
---

## 问：如何用 C++ 手写一个简单定时器？

要求实现三个基础功能：

1. 添加一个延时任务。
2. 取消尚未执行的任务。
3. 通过 `tick()` 执行已经到期的任务。

## 答

核心思路非常简单：

- 用小顶堆保存任务，堆顶永远是最早到期的任务。
- 添加任务时计算绝对到期时间，然后放入堆中。
- 取消任务时只设置 `canceled` 标记，暂时不修改堆。
- 每次调用 `tick()`，不断取出已经到期的任务。

这种取消方式叫作**懒删除**，实现简单，也是面试中的重点。

### 完整代码

```cpp
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

using TimePoint = std::uint64_t;
using TimerCallback = std::function<void()>;

struct TimerTask
{
    TimePoint expire_time;
    TimerCallback callback;
    bool canceled = false;

    TimerTask(TimePoint time, TimerCallback cb)
        : expire_time(time),
          callback(std::move(cb))
    {
    }
};

using TimerHandle = std::shared_ptr<TimerTask>;

// priority_queue 默认是大顶堆，反向比较后得到小顶堆。
struct TaskCompare
{
    bool operator()(const TimerHandle& left,
                    const TimerHandle& right) const
    {
        return left->expire_time > right->expire_time;
    }
};

class Timer
{
public:
    // 添加定时器，返回的句柄可用于取消任务。
    TimerHandle add_timer(std::uint64_t delay_ms,
                          TimerCallback callback)
    {
        const auto expire = now_ms() + delay_ms;
        auto task = std::make_shared<TimerTask>(
            expire, std::move(callback));

        std::lock_guard<std::mutex> lock(mutex_);
        heap_.push(task);
        return task;
    }

    // 懒删除：只标记，不立即从堆中查找和删除。
    void cancel(const TimerHandle& task)
    {
        if (!task)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        task->canceled = true;
    }

    // 由事件循环周期调用，执行所有已经到期的任务。
    void tick()
    {
        while (true)
        {
            TimerHandle task;

            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (heap_.empty() ||
                    heap_.top()->expire_time > now_ms())
                {
                    return;
                }

                task = heap_.top();
                heap_.pop();

                if (task->canceled)
                {
                    continue;
                }
            }

            // 回调必须在锁外执行，避免阻塞 add_timer 和 cancel。
            task->callback();
        }
    }

private:
    static TimePoint now_ms()
    {
        const auto now =
            std::chrono::steady_clock::now();

        return std::chrono::duration_cast<
                   std::chrono::milliseconds>(
                   now.time_since_epoch())
            .count();
    }

    std::mutex mutex_;

    std::priority_queue<
        TimerHandle,
        std::vector<TimerHandle>,
        TaskCompare>
        heap_;
};
```

### 测试代码

```cpp
int main()
{
    using namespace std::chrono_literals;

    Timer timer;

    timer.add_timer(2000, [] {
        std::cout << "timer 2s triggered!\n";
    });

    timer.add_timer(1000, [] {
        std::cout << "timer 1s triggered!\n";
    });

    auto canceled_task = timer.add_timer(1500, [] {
        std::cout << "this should not run\n";
    });
    timer.cancel(canceled_task);

    // 模拟事件循环，每 100 毫秒调用一次 tick。
    for (int i = 0; i < 30; ++i)
    {
        timer.tick();
        std::this_thread::sleep_for(100ms);
    }
}
```

运行结果：

```text
timer 1s triggered!
timer 2s triggered!
```

虽然 2 秒定时器先加入，但小顶堆会保证 1 秒定时器先执行。被取消的 1.5 秒任务到期后会被弹出，但不会执行回调。

### 为什么使用小顶堆？

定时器最重要的操作是找到“最近到期的任务”。

`priority_queue` 的堆顶可以在 `O(1)` 时间内取得最近任务，添加和弹出任务的复杂度都是 `O(log n)`。

如果使用普通数组，每次寻找最早到期任务需要遍历全部任务，复杂度是 `O(n)`。

### 为什么取消采用懒删除？

`priority_queue` 只能方便地删除堆顶，不能根据指针直接删除堆中间的元素。

为了取消一个任务而遍历并重建堆，会让代码变复杂。因此取消时只设置：

```cpp
task->canceled = true;
```

等任务到达堆顶后，再由 `tick()` 将它弹出并跳过回调。

### 为什么使用 shared_ptr？

如果直接把裸指针作为取消句柄，任务执行并被 `delete` 后，调用者手中的指针就会变成悬空指针，再调用 `cancel()` 会产生未定义行为。

`shared_ptr` 可以自动管理任务生命周期，既省去了析构函数中手动释放堆节点，也让取消句柄更安全。

### 为什么使用 steady_clock？

定时器计算的是“经过多长时间”，应该使用单调递增的 `steady_clock`。

`system_clock` 可能因为系统校时而向前或向后跳变，从而导致任务提前执行或延迟执行。

### 为什么不能持锁执行回调？

回调是外部传入的代码，可能执行很久，也可能继续调用 `add_timer()` 或 `cancel()`。

如果持有互斥锁执行回调，其他操作会被阻塞，回调再次操作定时器时还可能发生死锁。因此，代码只在操作堆时加锁，弹出任务后立即释放锁，再执行回调。

### 这个版本的边界

这个定时器没有创建后台线程，必须由外部事件循环不断调用 `tick()`。

`tick()` 的调用间隔决定了触发精度。例如每 100 毫秒调用一次，任务最多可能晚约 100 毫秒执行。

如果面试官要求定时器自动唤醒，可以在这个版本上增加一个工作线程和 `condition_variable`，让线程直接等待到堆顶任务的到期时间。

### 面试总结

手撕时记住四个步骤：

1. 用 `steady_clock` 计算任务到期时间。
2. 用小顶堆保存任务，堆顶是最近到期任务。
3. 取消时设置标记，采用懒删除。
4. `tick()` 弹出到期任务，释放锁后执行回调。

这个版本结构清晰、代码量小，同时保留了添加、取消、到期执行和线程安全这些基础功能。
