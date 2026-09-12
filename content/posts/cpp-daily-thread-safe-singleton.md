---
{
  "title": "C++ 每日一题：手撕线程安全的单例模式",
  "slug": "cpp-daily-thread-safe-singleton",
  "date": "2026-09-12",
  "updated": "2026-09-12",
  "excerpt": "使用 C++11 函数内静态对象实现线程安全的懒汉单例，并通过多线程测试验证所有线程取得同一个实例。",
  "category": { "name": "C++每日一题", "slug": "cpp-daily" },
  "tags": [
    { "name": "C++", "slug": "cpp" },
    { "name": "每日一题", "slug": "daily-question" },
    { "name": "单例模式", "slug": "singleton" },
    { "name": "多线程", "slug": "multithreading" }
  ],
  "featured": false,
  "draft": false,
  "seoDescription": "C++ 每日一题：手撕线程安全的懒汉单例，理解 C++11 局部静态变量初始化、禁用拷贝移动以及对象方法的并发安全。"
}
---

## 问：如何手写一个线程安全的单例模式？

要求：

1. 第一次使用时才创建对象，实现懒加载。
2. 多个线程同时调用时，只能构造一个实例。
3. 禁止通过拷贝或移动产生第二个对象。
4. 写一段多线程代码验证所有线程取得的是同一个实例。

## 答

在 C++11 及以后，最推荐的写法是 **Meyers Singleton**：把唯一实例定义成成员函数中的局部静态对象。

C++11 标准保证函数内静态变量的初始化是线程安全的：如果多个线程第一次同时执行到初始化语句，只有一个线程负责构造对象，其他线程会等待初始化完成。

### 完整代码

```cpp
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

class Singleton
{
public:
    // C++11 起，函数内静态对象的初始化由语言保证线程安全。
    static Singleton& getInstance()
    {
        static Singleton instance;
        return instance;
    }

    // 禁止拷贝和移动，防止产生第二个 Singleton 对象。
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

    void visit() noexcept
    {
        visit_count_.fetch_add(1, std::memory_order_relaxed);
    }

    int visitCount() const noexcept
    {
        return visit_count_.load(std::memory_order_relaxed);
    }

private:
    Singleton() = default;
    ~Singleton() = default;

    std::atomic<int> visit_count_{0};
};

int main()
{
    constexpr std::size_t thread_count = 8;
    constexpr int visits_per_thread = 100000;

    std::vector<const Singleton*> addresses(thread_count, nullptr);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t i = 0; i < thread_count; ++i)
    {
        threads.emplace_back([i, &addresses] {
            Singleton& singleton = Singleton::getInstance();
            addresses[i] = &singleton;

            for (int j = 0; j < visits_per_thread; ++j)
            {
                singleton.visit();
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    const Singleton* first = addresses.front();
    const bool same_instance = std::all_of(
        addresses.begin(), addresses.end(),
        [first](const Singleton* address) {
            return address == first;
        });

    const int expected =
        static_cast<int>(thread_count) * visits_per_thread;

    std::cout << std::boolalpha
              << "same instance: " << same_instance << '\n'
              << "instance address: " << first << '\n'
              << "visit count: "
              << Singleton::getInstance().visitCount() << '\n'
              << "expected count: " << expected << '\n';

    return same_instance &&
                   Singleton::getInstance().visitCount() == expected
               ? 0
               : 1;
}
```

Linux / macOS 编译：

```bash
g++ -std=c++11 -O2 -pthread singleton.cpp -o singleton
./singleton
```

运行结果类似：

```text
same instance: true
instance address: 0x55a22df681d8
visit count: 800000
expected count: 800000
```

对象地址每次运行可能不同，但所有线程保存的地址必须相同，最终计数也必须等于 `8 × 100000`。

### 为什么这段代码线程安全？

关键代码只有两行：

```cpp
static Singleton instance;
return instance;
```

它同时解决了三个问题：

- **懒加载**：程序第一次调用 `getInstance()` 时才构造对象。
- **只初始化一次**：C++11 保证局部静态对象的初始化只成功完成一次。
- **自动管理生命周期**：对象通常在程序正常退出时自动析构，不需要手动 `new` 和 `delete`。

初始化完成后，后续调用只返回已经存在的对象，不会重复构造。

### 为什么返回引用？

单例一定存在，因此没有必要用可能为空的指针表达返回值。返回引用还可以避免调用者误以为自己需要释放对象：

```cpp
Singleton& singleton = Singleton::getInstance();
```

也可以返回指针，但绝对不能让调用者对它执行 `delete`。

### 为什么要删除拷贝和移动？

如果不禁止拷贝，调用者可能写出：

```cpp
auto another = Singleton::getInstance();
```

这样会尝试复制出另一个对象，破坏“全局只有一个实例”的约束。因此需要删除拷贝构造、拷贝赋值、移动构造和移动赋值。

正确使用引用：

```cpp
auto& singleton = Singleton::getInstance();
```

### 单例初始化安全，不代表所有成员函数都安全

C++11 保证的是 `instance` 的**构造过程**线程安全，不会自动保护对象内部的数据。

如果多个线程会修改普通成员变量，仍然会发生数据竞争。示例中的访问次数使用 `std::atomic<int>`，所以多个线程可以安全递增：

```cpp
std::atomic<int> visit_count_{0};
```

如果单例管理的是容器、连接池或多项关联状态，通常应该在成员函数内部使用 `std::mutex` 保护共享数据。

### 为什么不推荐手写双重检查锁？

传统懒汉式单例经常写成“双重检查锁”：先判断指针，再加锁，再判断一次，最后创建对象。

这种写法代码更长，容易在内存可见性、指令重排和对象生命周期上犯错。现代 C++ 已经直接保证局部静态对象初始化安全，面试没有额外限制时，优先写 Meyers Singleton。

如果面试官明确要求使用 `std::call_once`，可以使用 `std::once_flag` 保证初始化函数只执行一次，但还需要额外处理对象的存储和释放，通常没有局部静态对象简洁。

### 还要注意哪些问题？

单例虽然写起来简单，但会引入全局状态：

- 隐藏模块之间的依赖关系。
- 单元测试时不容易替换实现或重置状态。
- 多个静态对象互相依赖时，可能遇到析构顺序问题。
- 单例内部如果承担太多职责，容易逐渐变成难以维护的“万能对象”。

工程中可以通过依赖注入传递对象时，通常比到处调用单例更容易测试和维护。

### 面试总结

手撕线程安全单例时记住四点：

1. 构造函数和析构函数私有化。
2. `getInstance()` 中定义函数内静态对象。
3. 删除拷贝和移动相关函数。
4. 区分“单例初始化线程安全”和“单例成员操作线程安全”。

核心答案：

```cpp
static Singleton& getInstance()
{
    static Singleton instance;
    return instance;
}
```

这就是现代 C++ 中最简洁、最稳妥的线程安全懒汉单例。
