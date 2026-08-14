---
{
  "title": "C++ 面试复盘：关联容器、线程交替打印与对象生命周期",
  "slug": "cpp-interview-maps-thread-order-shared-ptr",
  "date": "2026-08-15",
  "updated": "2026-08-15",
  "excerpt": "复盘 map 与 unordered_map、自定义结构体键、三个线程有序打印、条件变量唤醒策略、shared_ptr 双重释放和基类虚析构等高频 C++ 面试题。",
  "category": { "name": "C++ 实践", "slug": "cpp" },
  "tags": [
    { "name": "C++", "slug": "cpp" },
    { "name": "面试复盘", "slug": "interview" },
    { "name": "并发编程", "slug": "concurrency" },
    { "name": "智能指针", "slug": "smart-pointer" }
  ],
  "featured": false,
  "draft": false,
  "seoDescription": "C++ 面试题复盘，覆盖 map 与 unordered_map、自定义 key、条件变量、三线程顺序打印、shared_ptr 所有权和虚析构函数。"
}
---

今天遇到的几道题看起来分别属于容器、并发和对象生命周期，实际都在考同一件事：能否说清楚 C++ 类型所依赖的约束，以及一个对象或线程状态究竟由谁管理。

## 一、`map` 和 `unordered_map` 的区别

| 对比项 | `std::map` | `std::unordered_map` |
| --- | --- | --- |
| 典型实现 | 红黑树等平衡搜索树 | 哈希表 |
| 元素顺序 | 按 key 有序 | 不保证顺序 |
| 查找、插入、删除 | 通常为 `O(log n)` | 平均 `O(1)`，最坏 `O(n)` |
| 范围查询 | 支持，适合 `lower_bound`、`upper_bound` | 不适合 |
| key 的要求 | 能建立严格弱序 | 能计算哈希值并判断相等 |
| 迭代器失效 | 擦除当前元素时，该元素的迭代器失效 | rehash 可能使所有迭代器失效 |
| 内存特点 | 每个节点包含指针和颜色等信息 | 需要 bucket 数组，负载因子影响空间和性能 |

### 1. 如何选择

- 需要按 key 排序、范围查询，或者希望最坏时间复杂度稳定时，使用 `std::map`。
- 只需要精确查找，并且 key 的哈希质量可靠时，优先考虑 `std::unordered_map`。
- 数据量很小时，两者的理论复杂度未必能直接代表实际性能，应结合内存布局和基准测试判断。

`unordered_map` 的平均 `O(1)` 并不是无条件保证。哈希冲突严重时，大量 key 会落到同一个 bucket，操作可能退化为 `O(n)`。因此，自定义 key 的哈希函数必须让不同输入尽量均匀分布。

## 二、自定义结构体作为 `map` 的 key

`std::map` 必须知道两个 key 的先后关系。默认比较器是 `std::less<Key>`，因此可以提供 `operator<`：

```cpp
#include <map>
#include <string>

struct UserKey {
    int departmentId{};
    int userId{};

    bool operator<(const UserKey& other) const {
        if (departmentId != other.departmentId) {
            return departmentId < other.departmentId;
        }
        return userId < other.userId;
    }
};

std::map<UserKey, std::string> users;
```

这里的比较必须满足严格弱序，至少要保证：

- `comp(a, a)` 永远为 `false`；
- 如果 `a < b`，就不能同时有 `b < a`；
- 比较关系具有传递性；
- 比较过程中使用的字段不能在 key 放入容器后被随意改变。

对于 `std::map`，不要求必须重载 `operator==`。当 `!comp(a, b) && !comp(b, a)` 时，容器就把两个 key 视为等价。

### 1. 使用自定义比较器

不修改结构体也可以把比较规则交给模板参数：

```cpp
struct UserKeyCompare {
    bool operator()(const UserKey& left, const UserKey& right) const {
        return std::tie(left.departmentId, left.userId) <
               std::tie(right.departmentId, right.userId);
    }
};

std::map<UserKey, std::string, UserKeyCompare> users;
```

这段代码需要包含 `<tuple>`。自定义比较器适合“同一种 key 在不同容器中需要不同排序规则”的场景。

### 2. C++20 的三路比较

字段都支持比较时，可以使用默认的 `operator<=>`：

```cpp
#include <compare>

struct UserKey {
    int departmentId{};
    int userId{};

    auto operator<=>(const UserKey&) const = default;
};
```

编译器会按照成员声明顺序生成字典序比较，并同时生成相等比较。

## 三、自定义结构体作为 `unordered_map` 的 key

`unordered_map` 不关心大小关系，但需要两个能力：

1. 通过哈希函数得到 bucket 位置；
2. 哈希冲突后，通过相等比较确认是否为同一个 key。

```cpp
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

struct UserKey {
    int departmentId{};
    int userId{};

    bool operator==(const UserKey& other) const {
        return departmentId == other.departmentId &&
               userId == other.userId;
    }
};

struct UserKeyHash {
    std::size_t operator()(const UserKey& key) const noexcept {
        const auto first = std::hash<int>{}(key.departmentId);
        const auto second = std::hash<int>{}(key.userId);
        return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
    }
};

std::unordered_map<UserKey, std::string, UserKeyHash> users;
```

最重要的约束是：

> 如果 `a == b`，那么 `hash(a)` 必须等于 `hash(b)`。

反过来不成立。两个不相等的 key 可以得到相同哈希值，容器会再用相等比较区分它们，只是冲突过多会影响性能。

面试简答：

> 自定义结构体作为 `map` 的 key，需要提供严格弱序，可以重载 `operator<`、提供比较器，或者在 C++20 中提供三路比较。作为 `unordered_map` 的 key，则需要哈希函数和相等比较，通常是自定义 Hasher 加 `operator==`。

## 四、三个线程按照固定顺序输出 `0~100`

假设三个线程编号分别为 `0`、`1`、`2`，要求它们轮流打印：

```text
线程 0 打印 0
线程 1 打印 1
线程 2 打印 2
线程 0 打印 3
线程 1 打印 4
...
```

共享状态包括当前数字 `next` 和应该运行的线程 `turn`。二者都由同一把互斥锁保护：

```cpp
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

class OrderedPrinter {
public:
    void run(int id) {
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this, id] {
                return next_ > 100 || turn_ == id;
            });

            if (next_ > 100) {
                lock.unlock();
                cv_.notify_all();
                return;
            }

            std::cout << "thread " << id << ": " << next_ << '\n';
            ++next_;
            turn_ = (turn_ + 1) % 3;

            lock.unlock();
            cv_.notify_all();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int next_{0};
    int turn_{0};
};

int main() {
    OrderedPrinter printer;

    std::thread first([&printer] { printer.run(0); });
    std::thread second([&printer] { printer.run(1); });
    std::thread third([&printer] { printer.run(2); });

    first.join();
    second.join();
    third.join();
}
```

### 1. 为什么必须使用带谓词的 `wait`

条件变量允许虚假唤醒，被唤醒也不表示当前线程一定获得了执行资格。因此不能写成：

```cpp
cv_.wait(lock);
```

而应该始终重新检查业务条件：

```cpp
cv_.wait(lock, [this, id] {
    return next_ > 100 || turn_ == id;
});
```

`wait(lock, predicate)` 等价于用 `while` 循环反复检查谓词。它会原子地释放互斥锁并进入等待，被唤醒后先重新获得锁，再检查条件。

### 2. 为什么先解锁再通知

在锁内调用 `notify_all()` 也是正确的，但被唤醒的线程会立刻竞争同一把锁，却发现当前线程尚未释放它。先更新共享状态，再解锁和通知，可以减少一次无意义的竞争：

```cpp
turn_ = (turn_ + 1) % 3;
lock.unlock();
cv_.notify_all();
```

正确性的关键仍然是：修改 `next_` 和 `turn_` 时必须持有锁。

## 五、`notify_one` 和 `notify_all` 的区别

### 1. `notify_one`

`notify_one()` 唤醒至少一个正在等待该条件变量的线程。具体唤醒哪一个线程并不由调用者指定，也不保证公平性。

适合的场景是：

- 任意一个等待线程都可以处理新任务；
- 所有等待线程使用相同或等价的谓词；
- 典型例子是线程池中向任务队列加入一个任务。

```cpp
{
    std::lock_guard<std::mutex> lock(mutex);
    queue.push(task);
}
cv.notify_one();
```

### 2. `notify_all`

`notify_all()` 唤醒当前等待该条件变量的所有线程。每个线程醒来后仍需重新获取互斥锁并检查自己的谓词，只有条件成立的线程继续执行，其余线程重新等待。

适合的场景是：

- 多个线程等待不同条件；
- 一次状态变化可能让多个线程同时满足条件；
- 程序退出时，需要让所有等待线程检查停止标志并退出。

代价是可能出现“惊群”：大量线程同时醒来竞争锁，但最终只有少数线程能够继续。

## 六、为什么上面的单条件变量方案不能直接改成 `notify_one`

三个线程等待的是不同谓词：

```cpp
turn_ == 0
turn_ == 1
turn_ == 2
```

假设线程 0 打印完成，把 `turn_` 改为 `1`。此时必须唤醒线程 1。但 `notify_one()` 不能指定目标线程，它可能唤醒线程 2：

1. 线程 2 被唤醒；
2. 线程 2 发现 `turn_ != 2`，重新等待；
3. 真正应该运行的线程 1 仍在睡眠；
4. 没有线程能够继续打印，也不会再产生下一次通知；
5. 程序死锁。

所以，在“一个条件变量承载多个不同谓词”的实现中，应使用 `notify_all()`，让所有线程都重新检查条件。

### 1. `notify_one` 并不是永远不能用

可以为每个线程准备一个条件变量，当前线程只通知明确的下一个线程：

```cpp
#include <array>
#include <condition_variable>
#include <mutex>

std::array<std::condition_variable, 3> cvs;
std::mutex mutex;
int next = 0;
int turn = 0;

void print(int id) {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex);
        cvs[id].wait(lock, [id] {
            return next > 100 || turn == id;
        });

        if (next > 100) {
            lock.unlock();
            for (auto& cv : cvs) {
                cv.notify_one();
            }
            return;
        }

        std::cout << "thread " << id << ": " << next++ << '\n';
        turn = (turn + 1) % 3;
        const int nextThread = turn;

        lock.unlock();
        cvs[nextThread].notify_one();
    }
}
```

启动线程后还要调用一次 `cvs[0].notify_one()`，让第一个线程开始。这个版本可以使用 `notify_one`，因为每个条件变量只对应一个明确的线程。

面试简答：

> `notify_one` 只唤醒一个不确定的等待者，适合任意等待者都能处理工作的场景；`notify_all` 唤醒全部等待者，适合不同线程等待不同谓词或需要全体退出的场景。三个线程共用一个条件变量时，`notify_one` 可能唤醒错误线程并导致所有线程继续等待，因此应使用 `notify_all`，或者改成每个线程一个条件变量。

## 七、把同一个裸指针分别交给两个 `shared_ptr`

下面的代码是错误的：

```cpp
Widget* raw = new Widget;

std::shared_ptr<Widget> first(raw);
std::shared_ptr<Widget> second(raw); // 错误
```

虽然两个 `shared_ptr` 内部保存了相同地址，但它们分别创建了独立的控制块：

```text
first  -> 控制块 A：强引用计数 1 -> raw
second -> 控制块 B：强引用计数 1 -> raw
```

当 `first` 和 `second` 析构时，两个控制块都会认为自己是最后一个所有者，于是对同一个地址执行两次 `delete`。结果是未定义行为，常见表现包括：

- double free；
- 堆损坏；
- 程序崩溃；
- 第一次释放后，另一个 `shared_ptr` 继续访问已经销毁的对象。

### 1. 正确写法

只创建一个控制块，再复制 `shared_ptr`：

```cpp
auto first = std::make_shared<Widget>();
std::shared_ptr<Widget> second = first;
```

此时两个智能指针共享同一个控制块，引用计数为 2。

如果某个类需要从成员函数中安全地获得指向自身的 `shared_ptr`，使用 `std::enable_shared_from_this`：

```cpp
#include <memory>

class Session : public std::enable_shared_from_this<Session> {
public:
    std::shared_ptr<Session> self() {
        return shared_from_this();
    }
};

auto session = std::make_shared<Session>();
auto sameOwner = session->self();
```

不能在对象尚未由 `shared_ptr` 管理时调用 `shared_from_this()`，否则会抛出 `std::bad_weak_ptr`。

面试简答：

> 从同一个裸指针分别构造两个 `shared_ptr`，会产生两个独立控制块，最终对同一对象执行两次删除，属于未定义行为。应该先创建一个 `shared_ptr`，再复制它；对象内部需要共享自身时使用 `enable_shared_from_this`。

## 八、父类析构函数为什么通常需要是虚函数

如果一个基类可能被多态使用，并且对象可能通过基类指针被删除，基类析构函数必须是虚函数：

```cpp
class Base {
public:
    virtual ~Base() = default;
};

class Derived : public Base {
public:
    ~Derived() override {
        // 释放 Derived 自己拥有的资源
    }
};

Base* object = new Derived;
delete object; // 先调用 Derived::~Derived，再调用 Base::~Base
```

如果 `Base` 的析构函数不是虚函数：

```cpp
class Base {
public:
    ~Base() = default;
};
```

再通过 `Base*` 删除实际的 `Derived` 对象，行为是未定义的。不能只简单理解为“只调用父类析构函数”，因为标准层面已经不保证程序行为。常见后果是派生类资源未释放、内存泄漏或堆状态损坏。

同样的问题也会出现在智能指针中：

```cpp
std::shared_ptr<Base> object(new Derived);
```

这段具体写法的删除器通常记录的是 `Derived*`，可以正确删除完整对象；但不要因此认为非虚析构的多态基类就是安全设计。只要接口允许 `Base*` 的通用删除，仍然应该使用虚析构函数。对于 `std::unique_ptr<Base>` 指向 `Derived` 的常见转换，虚析构函数尤其重要。

### 1. 什么时候可以不使用虚析构函数

如果一个基类明确不允许通过基类指针删除，可以把析构函数设为 `protected` 且非虚：

```cpp
class InterfaceBase {
protected:
    ~InterfaceBase() = default;
};
```

这样外部代码不能写 `delete basePointer`，设计意图由编译器强制执行。

常见设计准则是：

> 基类析构函数应该是 public virtual，或者 protected non-virtual。

### 2. 纯虚析构函数仍然需要定义

析构函数可以声明为纯虚函数，使类保持抽象：

```cpp
class Base {
public:
    virtual ~Base() = 0;
};

Base::~Base() = default;
```

即使它是纯虚函数，也必须提供定义，因为销毁派生类对象时，最终仍然会调用基类析构函数。

### 3. 构造和析构期间不要依赖虚函数分派

在基类构造函数和析构函数执行期间，对象不会被当作完整派生类进行虚函数分派：

```cpp
class Base {
public:
    Base() { initialize(); }
    virtual ~Base() { cleanup(); }

    virtual void initialize();
    virtual void cleanup();
};
```

这里调用的是当前构造或析构层级的版本，而不是派生类重写版本。因为派生类部分可能尚未构造，或者已经被销毁。构造、析构函数中调用可重写虚函数通常是危险设计。

面试简答：

> 多态基类如果允许通过基类指针删除对象，析构函数必须是 public virtual，否则删除派生类对象是未定义行为。若不允许通过基类删除，可以使用 protected non-virtual 析构函数。纯虚析构函数仍然必须提供定义。

## 九、最终速记

```text
map
  平衡树、key 有序、O(log n)
  自定义 key 需要严格弱序：operator<、比较器或 operator<=>

unordered_map
  哈希表、平均 O(1)、不保证顺序
  自定义 key 需要 hash 和相等比较

三个线程轮流打印
  mutex 保护 next 和 turn
  condition_variable 必须配合谓词
  单个条件变量承载不同谓词时使用 notify_all
  每线程一个条件变量时可以精确 notify_one

shared_ptr
  同一裸指针不能分别构造多个 shared_ptr
  否则产生独立控制块并发生 double delete

多态析构
  public virtual：允许通过基类删除
  protected non-virtual：禁止通过基类删除
  纯虚析构函数仍需提供定义
```

这些题的高分回答不应只停留在“红黑树”“条件变量”或“虚析构”几个关键词上。更重要的是说明约束为什么存在、错误代码会如何失效，以及什么设计能够让编译器或类型系统帮助我们避免错误。
