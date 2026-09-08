---
{
  "title": "C++ 每日一题：Linux 如何阻止非法内存访问？主线程栈和普通线程栈有什么区别？",
  "slug": "cpp-daily-linux-memory-protection-thread-stacks",
  "date": "2026-09-08",
  "updated": "2026-09-08",
  "excerpt": "从虚拟内存、页表权限、缺页异常和保护页出发，说明 Linux 如何阻止非法内存访问，并比较主线程栈与 pthread 普通线程栈。",
  "category": { "name": "C++每日一题", "slug": "cpp-daily" },
  "tags": [
    { "name": "C++", "slug": "cpp" },
    { "name": "每日一题", "slug": "daily-question" },
    { "name": "Linux 内存", "slug": "linux-memory" },
    { "name": "线程栈", "slug": "thread-stack" }
  ],
  "featured": false,
  "draft": false,
  "seoDescription": "C++ 每日一题：理解 Linux 的页表权限、缺页异常、SIGSEGV 和栈保护页，并掌握主线程栈与普通线程栈的区别。"
}
---

## 问：Linux 如何阻止非法内存访问？主线程栈和普通线程栈有什么区别？

## 答

一句话概括：**Linux 以虚拟内存和页表权限描述进程可以访问哪些页面，CPU 的 MMU 在每次访存时执行检查；访问越权时触发缺页异常，内核无法修复就向当前线程发送 `SIGSEGV`。主线程和普通线程共享同一个进程地址空间，但各自拥有独立的用户栈；两类栈的创建者、大小控制方式和能否动态增长不同。**

### 一、Linux 如何阻止非法内存访问？

用户程序操作的是虚拟地址，而不是直接读写物理内存。一次典型的内存访问可以简化为：

```text
CPU 发出虚拟地址
        ↓
TLB / 多级页表完成地址翻译
        ↓
MMU 检查页面是否存在，以及读、写、执行和用户态权限
        ↓
权限允许：访问物理页
权限不允许或页面不存在：触发缺页异常（page fault）
        ↓
内核处理异常：补页、写时复制，或者发送 SIGSEGV
```

页表项不仅保存虚拟页到物理页的映射，还带有权限和状态信息。不同体系结构的位定义不完全相同，但理解下面几类权限就足够应对大多数面试题：

| 权限或状态 | 作用 |
| --- | --- |
| Present | 页面当前是否存在有效映射 |
| User / Supervisor | 用户态代码能否访问该页面 |
| Read / Write | 页面是否允许读取或写入 |
| Execute / NX | 页面是否允许作为指令执行 |

因此，“阻止访问”的关键并不是程序每次访问都主动调用内核，而是 **Linux 建立页表规则，CPU/MMU 在硬件层执行规则，异常发生后再由内核接管**。

缺页异常也不一定代表程序出错：

- 第一次访问尚未分配物理页的匿名内存时，内核可以按需分配页面。
- 写入写时复制页面时，内核可以复制出新的私有页并恢复执行。
- 访问未映射地址、向只读页写入或在不可执行页取指时，内核通常无法修复，会发送 `SIGSEGV`。

应用还可以通过 `mmap()` 和 `mprotect()` 设置页面权限。例如，把一页设置成 `PROT_NONE` 后，任何读写都会触发异常：

```cpp
#include <sys/mman.h>
#include <unistd.h>

const long page_size = sysconf(_SC_PAGESIZE);
void* page = mmap(nullptr, page_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

// 将这一页变成不可访问页。
mprotect(page, page_size, PROT_NONE);
```

### 二、保护页为什么能检测栈溢出？

保护页（guard page）是一段被设置为不可读、不可写的虚拟内存。线程栈向边界增长并越过可用区域时，一旦触碰保护页，MMU 就会触发异常，内核通常向出错线程发送 `SIGSEGV`。

保护页的作用是**尽早发现越界**，避免栈指针悄悄进入相邻映射；它不能保证检测所有栈内数组越界，也不能代替 AddressSanitizer 等内存错误检测工具。如果一次性跨过保护页直接落到另一段有效映射，仍可能产生更隐蔽的破坏。

### 三、主线程栈和普通线程栈有什么区别？

这里的“普通线程”指通过 `pthread_create()` 创建的线程。二者都是用户态栈，都保存函数调用帧、返回地址和普通局部变量；线程进入内核执行系统调用时使用的内核栈是另一个概念。

| 对比项 | 主线程栈 | `pthread_create()` 创建的普通线程栈 |
| --- | --- | --- |
| 创建时机 | 程序通过 `execve()` 启动时建立初始栈 | 创建线程时由 pthread 实现准备 |
| 初始内容 | 包含 `argc`、`argv`、环境变量和辅助向量等启动信息 | 从线程入口函数所需的初始执行上下文开始 |
| 大小控制 | 上限主要受进程启动时的 `RLIMIT_STACK` 影响 | 默认值通常取决于程序启动时的 `RLIMIT_STACK`；也可用 `pthread_attr_setstacksize()` 单独指定 |
| 动态增长 | Linux 上只有主线程栈能够按需动态增长，仍受栈上限和地址空间约束 | 创建时确定栈映射大小，不能像主线程栈一样自动扩展 |
| 保护方式 | 不由 `pthread_create()` 的 guard size 属性管理，依赖初始栈映射及其边界保护 | 默认通常带一页保护区，可用 `pthread_attr_setguardsize()` 调整 |
| 地址空间 | 位于进程的同一个虚拟地址空间 | 同样位于该进程地址空间，只是每个线程拥有不同的栈区间 |
| 生命周期 | 通常随整个进程存在；从 `main()` 返回会终止进程 | 线程结束后由 pthread 运行库回收或缓存其栈映射 |

需要特别注意：**线程栈独立，不等于线程内存隔离。** 所有线程仍然可以访问同一进程中的堆、全局变量、静态变量和已映射文件；只要拿到了另一个线程栈中对象的地址，理论上也能访问它。因此，页表权限通常只能做到进程级地址空间保护，不能自动阻止同一进程内的线程互相读写内存。

### 四、为什么创建大量线程容易消耗虚拟地址空间？

普通线程的栈通常在创建时预留一段固定大小的虚拟地址空间。假设默认线程栈为 8 MiB，创建 1000 个线程就可能预留约 8 GiB 栈地址空间，另外还有保护页和线程控制数据。

预留虚拟地址空间不等于立刻占用同样大小的物理内存，但它会增加地址空间、页表和管理开销；线程真正触碰更多栈页面时，物理内存消耗也会随之增长。工程上更常见的做法是使用有上限的线程池，并在确认调用深度和局部对象大小后再合理缩小线程栈。

### 五、如何查看和验证线程栈？

可以先查看主线程栈限制：

```bash
ulimit -s
```

查看进程映射及权限：

```bash
cat /proc/<pid>/maps
```

`[stack]` 表示初始进程栈，也就是主线程栈。较新的 Linux 不再在 `/proc/<pid>/maps` 中为每个普通线程标注 `[stack:<tid>]`，所以调试时更可靠的方式是在程序内使用 GNU 扩展 `pthread_getattr_np()`，再通过 `pthread_attr_getstack()` 和 `pthread_attr_getguardsize()` 查询实际栈地址、大小和保护区。

### 面试总结

回答这道题时，可以按下面的顺序组织：

1. Linux 给每个进程建立虚拟地址空间和页表，MMU 按页检查访问权限。
2. 合法但尚未就绪的访问可由内核补页；非法访问通常转化为 `SIGSEGV`。
3. 保护页本质上是 `PROT_NONE` 页面，常用于尽早发现线程栈越界。
4. 主线程栈由程序启动过程建立，受 `RLIMIT_STACK` 控制，并可按需增长。
5. 普通线程栈由 pthread 创建，大小通常在创建时固定，可以设置 stack size 和 guard size。
6. 每个线程有独立栈，但同一进程的线程仍共享地址空间，栈独立不代表内存隔离。

### 参考资料

- Linux Kernel Documentation: Page Tables — https://www.kernel.org/doc/html/latest/mm/page_tables.html
- Linux man-pages: `mprotect(2)` — https://man7.org/linux/man-pages/man2/mprotect.2.html
- Linux man-pages: `pthread_create(3)` — https://man7.org/linux/man-pages/man3/pthread_create.3.html
- Linux man-pages: `pthread_attr_setstacksize(3)` — https://man7.org/linux/man-pages/man3/pthread_attr_setstacksize.3.html
- Linux man-pages: `pthread_attr_setguardsize(3)` — https://man7.org/linux/man-pages/man3/pthread_attr_setguardsize.3.html
- Linux man-pages: `pthread_getattr_np(3)` — https://man7.org/linux/man-pages/man3/pthread_getattr_np.3.html
- Linux man-pages: `/proc/pid/maps` — https://man7.org/linux/man-pages/man5/proc_pid_maps.5.html
