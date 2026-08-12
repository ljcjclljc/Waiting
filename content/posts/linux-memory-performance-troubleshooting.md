---
{
  "title": "Linux 内存性能排障：从 free、Swap 到内存泄漏与 OOM",
  "slug": "linux-memory-performance-troubleshooting",
  "date": "2026-08-12",
  "updated": "2026-08-12",
  "excerpt": "从虚拟内存、页缓存和缺页异常出发，串联 free、vmstat、pidstat、smaps、pmap、cachestat、slabtop、memleak、Valgrind 与 OOM 日志，形成完整的内存瓶颈定位路径。",
  "category": { "name": "Linux 性能", "slug": "linux-performance" },
  "tags": [
    { "name": "Linux", "slug": "linux" },
    { "name": "内存", "slug": "memory" },
    { "name": "性能排查", "slug": "performance" },
    { "name": "面试复习", "slug": "interview" }
  ],
  "featured": false,
  "draft": false,
  "seoDescription": "Linux 内存性能排查与面试复习指南，详解虚拟内存、Buffer/Cache、Swap、缺页异常、内存泄漏、OOM、容器内存限制，以及 free、vmstat、smaps、memleak 等命令。"
}
---

> 这篇文章既是面试前复习提纲，也是一份可以照着执行的线上排障手册。核心方法是：先判断系统是否真的有内存压力，再区分进程匿名内存、文件页缓存、内核 Slab、Swap、缺页异常或 cgroup 限制，最后把问题落到具体进程、内存映射和分配调用栈。

## 一、先记住完整排查路径

```text
内存告警、延迟升高或进程被杀
  |
  +-- free -h + /proc/meminfo：available 是否不足，内存花在哪里？
  |
  +-- vmstat 1 + sar -r/-S/-B：问题是持续增长、回收、Swap 还是缺页？
  |      |
  |      +-- 进程 RSS/PSS 增长：top/ps/pidstat -> smaps_rollup/pmap
  |      |                         -> memleak/Valgrind/ASan 找分配栈
  |      +-- Cached 增长：cachestat/cachetop -> 找高 miss 或 dirty 的进程
  |      +-- Slab 增长：slabtop + /proc/slabinfo -> 找内核对象
  |      +-- si/so 持续非零：sar -W + smem/VmSwap -> 找换入换出进程
  |      +-- majflt/s 高：pidstat -r -> 文件映射、冷启动或 Swap I/O
  |      +-- OOM：journalctl/dmesg -> 读 victim、cgroup、oom_score
  |
  +-- 宿主机看着正常但容器 OOM：memory.current/events/stat/pressure
  |
  +-- NUMA 机器还有 free 却回收：numastat + /proc/zoneinfo
```

排查时要形成证据链：总量说明有没有压力，趋势说明问题何时发生，分类指标说明内存被谁使用，进程映射或分配栈说明代码根因。只凭 `free` 的 `free` 列很小，不能得出“内存不足”的结论。

## 二、先理解 Linux 内存工作的主线

### 1. 虚拟内存、页表、MMU 和 TLB

每个进程看到的是独立、连续的虚拟地址空间。内核通过页表记录虚拟页到物理页的映射，CPU 的 MMU 负责地址转换，TLB 则缓存近期的页表转换结果。

Linux 通常以 4 KiB 页为最小映射单位，也支持 2 MiB、1 GiB 等 Huge Page。大页可以减少页表项和 TLB miss，适合数据库、DPDK 等大内存工作负载，但会提高连续物理内存需求和管理复杂度，不能看到“大页更快”就盲目启用。

当进程第一次访问尚未建立有效映射的地址时，会发生缺页异常：

- **次缺页（minor fault）**：不需要从磁盘读取数据，例如匿名页首次分配，或目标页已在页缓存中。
- **主缺页（major fault）**：需要存储 I/O，例如从文件或 Swap 读回页面，延迟明显更高。

系统调用 `malloc()` 成功通常只代表获得一段虚拟地址空间。受按需分配影响，物理页往往在首次写入时才真正分配。因此 VIRT 很大不等于物理内存已经耗尽。

### 2. 进程地址空间有哪些部分

典型用户空间从低地址到高地址包括：

- 代码和只读段：程序指令、只读常量。
- 数据段：已初始化和未初始化的全局/静态变量。
- 堆：动态分配区域，传统上向高地址增长。
- 文件映射区：动态库、普通文件映射、共享内存和匿名 `mmap()`。
- 栈：局部变量、返回地址和调用上下文；线程通常各自拥有栈。

glibc 的 `malloc()` 可能通过 `brk()` 扩展堆，也可能使用匿名 `mmap()`。具体阈值不是固定的 128 KiB，它会受分配器版本、运行状态和配置影响。小块释放后也可能被分配器缓存而不立即归还内核，所以“RSS 没降”不一定等于泄漏。

### 3. 内存不足时内核做什么

Linux 会优先回收不活跃页面：

1. 丢弃可重新从磁盘读取的干净文件页。
2. 回写脏文件页后再回收。
3. 启用 Swap 时，把不活跃匿名页换出。
4. 回收仍无法满足分配时，触发 OOM killer 终止进程。

`kswapd` 在后台按水位回收；分配路径来不及等待时会发生 direct reclaim，业务线程直接参与回收，容易造成延迟尖峰。现代系统还可以通过 PSI 观察任务因内存回收或等待而停顿的程度。

## 三、`free`：先判断系统是否真的内存不足

```bash
free -h
free -h -w
free -h -s 1
```

- `-h`：按 GiB、MiB 等单位显示。
- `-w`：把 `buffers` 和 `cache` 分开显示，便于进一步区分。
- `-s 1`：每秒刷新一次；按 `Ctrl+C` 结束。适合快速看趋势，但正式趋势分析优先用 `vmstat` 或 `sar`。

关键字段：

| 字段 | 含义 | 排障重点 |
| --- | --- | --- |
| `total` | 可管理的物理内存总量 | 不是简单等于内存条标称容量 |
| `used` | 按 free 当前口径计算的已用内存 | 不同 procps 版本口径可能略有差异 |
| `free` | 完全未使用的内存 | 小并不一定有问题 |
| `shared` | 主要是 tmpfs 等共享内存 | 容器 `/dev/shm`、tmpfs 异常时会增长 |
| `buff/cache` | Buffer、页缓存和可回收 Slab 等 | 多数可在压力下回收，不等于泄漏 |
| `available` | 不发生明显 Swap 时，新应用大致可用的内存估计 | 判断内存余量优先看它 |

Linux 会利用闲置内存做页缓存，因此 `free` 很小而 `available` 仍很大是正常现象。真正值得关注的是：`available` 持续下降、Swap 活跃、回收压力升高、业务延迟恶化，或者出现 OOM。

Swap 行中的 `used` 也只是当前有多少页面留在 Swap，并不代表系统此刻仍在频繁交换。判断性能影响要看 `vmstat` 的 `si/so` 或 `sar -W` 的实时速率。

## 四、`/proc/meminfo`：拆开内存构成

```bash
grep -E 'MemTotal|MemFree|MemAvailable|Buffers|Cached|SwapCached|Active|Inactive|AnonPages|Mapped|Shmem|Slab|SReclaimable|SUnreclaim|Dirty|Writeback|PageTables|Committed_AS|CommitLimit|Huge' /proc/meminfo
watch -n 1 "grep -E 'MemAvailable|Cached|AnonPages|Slab|SUnreclaim|Dirty|SwapFree' /proc/meminfo"
```

第一条输出关键分类，第二条每秒观察变化。`/proc/meminfo` 是累计状态快照，比较连续样本才能看趋势。

重点指标：

- `MemAvailable`：系统估算的新负载可用内存。
- `Cached`：文件页缓存，不包含 `SwapCached`。
- `AnonPages`：用户态堆、栈等匿名页的重要组成。
- `Shmem`：tmpfs、共享内存等使用量。
- `Slab`：内核对象缓存，分为可回收 `SReclaimable` 和不可回收 `SUnreclaim`。
- `Dirty`：已修改但未回写存储的文件页。
- `Writeback`：正在回写的页面。
- `PageTables`：页表占用；进程或映射数量异常时可能升高。
- `Committed_AS`：系统已承诺给进程的虚拟内存总量，不等于当前 RSS。
- `CommitLimit`：当前 overcommit 策略下的承诺上限。

查看内存承诺策略：

```bash
sysctl vm.overcommit_memory vm.overcommit_ratio
```

`vm.overcommit_memory` 常见值为：`0` 启发式判断，`1` 总是允许超额承诺，`2` 严格按承诺上限检查。不要为了绕过分配失败直接改成 `1`；这可能把问题推迟到真正访问页面时，以 OOM 的方式爆发。

## 五、Buffer、Cache 和 Slab 怎么区分

### 1. 不要只背“Buffer 写磁盘、Cache 读文件”

现代 Linux 中更实用的理解是：

- `Buffers` 主要与块设备元数据和原始块缓冲相关，通常不大。
- `Cached` 主要是普通文件的页缓存，读文件会填充它，缓冲写也会先形成脏页。
- `SReclaimable` 是内核 Slab 中可回收的对象缓存，例如部分 inode、dentry。
- `SUnreclaim` 是当前不可回收的 Slab，持续异常增长时要警惕内核对象或驱动问题。

`free` 的 `buff/cache` 会合并多个分类，所以发现它很大后要回到 `/proc/meminfo` 判断究竟是文件页还是 Slab。

### 2. `vmstat`：观察内存、Swap、I/O 和回收趋势

```bash
vmstat -w 1
vmstat -w 1 10
vmstat -s
```

- `-w`：宽输出，避免大数值挤在一起。
- `1`：每秒采样。
- `1 10`：每秒一次，共 10 组。第一行通常是开机以来平均值，分析当前问题看后续样本。
- `-s`：输出开机以来的内存事件和计数，适合总览，不适合判断瞬时速率。

内存相关字段：

| 字段 | 含义 | 看到异常后怎么走 |
| --- | --- | --- |
| `swpd` | 已使用 Swap | 只代表存量，继续看 `si/so` |
| `free` | 完全空闲内存 | 与 `available`、缓存一起判断 |
| `buff` | Buffer | 回到 `/proc/meminfo` 验证 |
| `cache` | 页缓存等 | 持续增长时用缓存工具找来源 |
| `si` | 每秒从 Swap 换入 | 持续非零说明访问了已换出页面 |
| `so` | 每秒换出到 Swap | 持续非零说明匿名页回收活跃 |
| `bi/bo` | 块设备读入/写出 | 与 Swap 和文件回写交叉判断 |

`vmstat` 的 `si/so` 位于 swap 区域，不能和 `top` CPU 行中的软中断 `si` 混淆。

### 3. `cachestat`：看整机页缓存命中率

BCC 版本常见命令：

```bash
sudo cachestat 1 10
sudo /usr/share/bcc/tools/cachestat 1 10
```

不同发行版命令路径和字段名可能不同，先用 `command -v cachestat` 确认。参数 `1 10` 表示每秒输出一次，共 10 次。常见字段包括缓存命中次数、未命中次数、脏页事件和命中率。

适用场景：

- 应用读延迟升高，怀疑工作集超过内存导致缓存命中率下降。
- 文件读取带宽很大，要区分请求来自内存页缓存还是磁盘。
- 发布或冷启动后主缺页增加，验证缓存预热是否完成。

低命中率同时伴随块设备读取和主缺页上升，说明确实在访问存储；仅命中率波动而没有 I/O 和延迟变化，不能单独定性为瓶颈。

### 4. `cachetop`：按进程观察页缓存活动

```bash
sudo cachetop
sudo cachetop 5
sudo /usr/share/bcc/tools/cachetop 5
```

`5` 表示每 5 秒刷新。它通常按缓存访问量排序，展示 PID、命中、未命中、脏页和命中率，用于回答“哪个进程正在大量读写页缓存”。

判断方法：

1. `/proc/meminfo` 发现 `Cached` 持续增长。
2. `cachestat 1` 确认页缓存活动和命中率变化。
3. `cachetop 5` 找出访问最活跃或 miss 较高的进程。
4. 再结合 `pidstat -d 1`、`iostat` 或应用请求日志，确认是否为大文件扫描、备份、日志或冷数据访问。

eBPF/BCC 工具依赖内核能力、BTF/内核头和权限。生产机无法运行时，可用 `sar -r`、`pidstat -d`、`mincore()` 类工具或应用级指标替代，但观测维度会弱一些。

### 5. `slabtop`：页缓存不大但 Slab 很大

```bash
sudo slabtop -o
sudo slabtop -s c
grep -E 'Slab|SReclaimable|SUnreclaim' /proc/meminfo
```

- `-o`：输出一次后退出，适合保存现场。
- `-s c`：按 cache size 排序；具体排序字符用 `man slabtop` 确认。

重点看对象名、对象数量、单对象大小和总 cache size。常见大项包括 dentry、inode、网络连接跟踪等。`SReclaimable` 大通常可以在压力下回收；`SUnreclaim` 持续增长且无法随负载回落，更值得检查内核模块、网络对象或驱动泄漏。

## 六、`top`、`ps`、`pidstat`：定位高内存进程

### 1. `top`：实时按内存排序

```bash
top
top -p 1234
top -b -n 3 -d 1 -o %MEM
```

进入交互界面后按 `M` 按内存排序；`-p` 只看指定进程；批处理命令采样 3 次，适合记录现场。

进程字段：

- `VIRT`：全部虚拟地址空间，包括未实际驻留、共享库、映射文件和已换出页面。大不等于泄漏。
- `RES`：当前驻留物理内存，通常包含共享驻留页，不能简单把多个进程 RES 相加。
- `SHR`：可能共享的驻留页，包括共享库、代码段和共享内存；不保证每一页当前真的被多个进程共享。
- `%MEM`：RES 占物理内存的比例。

### 2. `ps`：保存进程内存快照

```bash
ps -eo pid,ppid,user,stat,vsz,rss,pmem,comm,args --sort=-rss | head -n 20
ps -p 1234 -o pid,ppid,vsz,rss,pmem,etime,comm,args
```

- `vsz`：虚拟内存，通常以 KiB 显示。
- `rss`：常驻集，通常以 KiB 显示。
- `etime`：已运行时间，有助于区分刚启动峰值与长期增长。

`ps` 是瞬时快照。判断泄漏不能只看一次 RSS，而要持续采样，最好同时关联请求量、连接数、队列长度和缓存大小。

### 3. `pidstat -r`：连续看进程内存和缺页

```bash
pidstat -r -p ALL 1
pidstat -r -p 1234 1 10
```

- `-r`：报告进程缺页和内存使用。
- `-p ALL`：监控所有进程，也可以指定 PID。
- `1 10`：每秒一次，共 10 次。

常见字段：

- `minflt/s`：每秒次缺页，不需要存储 I/O；程序首次触碰大量匿名页时可能很高。
- `majflt/s`：每秒主缺页，需要存储 I/O；持续升高通常对延迟影响明显。
- `VSZ`、`RSS`、`%MEM`：虚拟、常驻和比例。

如果 RSS 持续增长而负载规模稳定，进入 `smaps/pmap`；如果 `majflt/s` 高，结合 `sar -B`、`vmstat` 的 `si/bi` 和存储指标判断是 Swap 还是文件映射读取。

## 七、RSS、PSS、USS：统计进程内存不要重复计算

### 1. 三个指标的区别

- `RSS`：进程驻留的全部物理页，共享页会在每个进程中重复计算。
- `PSS`：共享页按共享进程数量平摊，再加私有页。汇总多个进程时更合理。
- `USS`：进程独占的私有驻留页，进程退出后理论上可立即释放的部分。

所以不能把所有进程 RSS 直接相加，尤其是大量 worker 共享代码和库时，结果会明显大于实际物理使用量。

### 2. `/proc/PID/status`：快速查看一个进程

```bash
grep -E 'Name|Pid|VmPeak|VmSize|VmHWM|VmRSS|RssAnon|RssFile|RssShmem|VmSwap|Threads' /proc/1234/status
```

- `VmHWM`：历史 RSS 峰值。
- `VmRSS`：当前 RSS。
- `RssAnon`：匿名驻留页，堆栈增长常反映在这里。
- `RssFile`：文件映射驻留页。
- `RssShmem`：共享内存驻留页。
- `VmSwap`：该进程换出的匿名私有页；共享 Swap 的统计存在口径限制。

### 3. `smaps_rollup`：低成本获得 PSS 汇总

```bash
grep -E 'Rss|Pss|Pss_Anon|Pss_File|Pss_Shmem|Private|Shared|Anonymous|Swap' /proc/1234/smaps_rollup
```

`smaps_rollup` 把所有映射汇总，适合快速判断匿名、文件、共享和 Swap 构成。较老内核没有该文件时，读取 `/proc/1234/smaps` 并自行汇总。

权限受 `ptrace_scope`、用户身份和容器边界限制；无权限时不要为了方便关闭整机安全设置，应由目标用户或获得最小必要诊断权限后读取。

### 4. `pmap`：找出哪段地址空间增长

```bash
pmap -x 1234
pmap -XX 1234
watch -n 5 'pmap -x 1234 | tail -n 1'
```

- `-x`：展示映射地址、大小、RSS、脏页和映射名。
- `-XX`：输出更完整的内核映射字段，但格式依 procps 版本变化。
- `watch` 示例每 5 秒观察汇总行；要找具体增长区，保存两次 `pmap -x` 再比较。

判断方向：

- `[heap]` 或大量匿名映射增长：检查分配器缓存、对象生命周期和泄漏。
- 某个文件映射 RSS 增长：检查 mmap 文件、数据库或索引工作集。
- 线程栈映射数量增长：检查线程泄漏和线程池上限。
- VIRT 增长但 RSS 不变：只是地址空间预留，暂时不是物理内存压力。

### 5. `smem`：按 PSS 排序更准确

```bash
sudo smem -tk
sudo smem -r -k -s pss
sudo smem -p -k -P 'your-process-regex'
```

- `-t`：显示合计。
- `-k`：使用可读单位。
- `-r`：反向排序。
- `-s pss`：按 PSS 排序。
- `-P`：按进程映射/命令正则筛选，具体匹配口径以本机 `smem --help` 为准。

`smem` 从 procfs 读取 PSS/USS/RSS，未必默认安装。无法安装时可读取 `smaps_rollup`；统计全系统 PSS 会遍历大量映射，进程很多时不要高频运行。

## 八、发现内存持续增长：怎么确认泄漏

“内存上涨”可能是泄漏，也可能是正常缓存、分配器 arena、业务工作集增长或流量增加。正确顺序是：

1. `free -h` 与 `vmstat 1` 确认 `available` 是否持续下降。
2. `pidstat -r -p ALL 1` 找 RSS 持续增长的进程。
3. `/proc/PID/smaps_rollup` 区分匿名、文件和共享内存。
4. `pmap -x PID` 找增长的映射类型。
5. 在可控时间窗用 `memleak`、Valgrind、ASan/LSan 或堆分析器取得分配栈。

### 1. BCC `memleak`：附加到正在运行的进程

```bash
sudo /usr/share/bcc/tools/memleak -p 1234
sudo /usr/share/bcc/tools/memleak -p 1234 -a
sudo /usr/share/bcc/tools/memleak -p 1234 5 6
```

不同 BCC 版本的参数可能有差异，必须先运行：

```bash
sudo /usr/share/bcc/tools/memleak --help
```

一般来说，`-p` 指定目标 PID，`-a` 显示每个未释放地址，末尾的 `5 6` 表示每 5 秒输出一次、共输出 6 次。工具会周期输出 outstanding allocations 及调用栈；过滤参数的准确含义以本机版本为准。

使用方法：让应用在稳定请求下运行，观察同一调用栈的未释放字节和对象数是否跨多个周期持续增长。单次看到 outstanding allocation 不一定是泄漏，因为对象可能只是生命周期较长。

目标程序最好保留调试符号，并使用帧指针或可靠的 DWARF 展栈信息。容器进程要注意宿主机 PID、符号路径和 mount namespace；工具无法解析时可在宿主机以容器对应 PID 运行，或在诊断镜像中部署符号。

### 2. Valgrind Memcheck：适合测试环境复现

```bash
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file=valgrind.%p.log ./your_program --args
```

- `--tool=memcheck`：启用内存错误检查。
- `--leak-check=full`：输出每条泄漏的分配栈。
- `--show-leak-kinds=all`：显示 definitely/indirectly/possibly lost 和 still reachable。
- `--track-origins=yes`：追踪未初始化值来源，开销更大。
- `--log-file=...`：按 PID 保存日志。

重点先修 `definitely lost`。`still reachable` 表示退出时仍可达，可能是缓存或运行库保留，不应机械当作泄漏。Valgrind 会显著拖慢程序并增加内存开销，通常不直接挂在线上高流量进程，也不能像 BCC memleak 那样方便地附加到任意已运行进程。

### 3. AddressSanitizer/LeakSanitizer：开发和 CI 优先

```bash
clang++ -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined app.cpp -o app
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./app
```

ASan 能发现越界、use-after-free 等问题，LSan 负责退出时泄漏检测。它需要重新编译，适合测试和 CI。生产二进制不应未经评估直接开启 sanitizer。

C++ 中更根本的修复通常是使用 RAII：`std::vector`、`std::string`、`std::unique_ptr` 和明确所有权，避免裸 `new/delete` 跨多条异常路径手工配对。对于高频分配，再考虑对象池、arena 和分配器统计，而不是先把池子无限放大。

## 九、Swap 升高：区分存量和正在交换

### 1. `swapon` 和 `free`：看配置与存量

```bash
swapon --show --bytes
cat /proc/swaps
free -h
sysctl vm.swappiness
```

- `swapon --show`：列出 Swap 设备/文件、大小、已用量和优先级。
- `/proc/swaps`：底层数据源。
- `vm.swappiness`：文件页和匿名页回收偏好，范围通常为 0 到 200（现代内核可能支持超过 100）；它不是“内存使用百分比”。

Swap 已用量高但长时间没有 `si/so`，可能只是冷匿名页仍留在磁盘，对当前性能影响有限。真正的抖动是持续换入换出并伴随延迟、I/O 和 PSI 上升。

### 2. `sar -S`、`sar -W`：看 Swap 容量与速率

```bash
sar -S 1 10
sar -W 1 10
sar -r ALL 1 10
```

- `-S`：Swap 空间使用量。
- `-W`：每秒换入、换出页数。
- `-r ALL`：内存利用率及更完整分类；字段依 sysstat 版本变化。
- `1 10`：每秒一次，共 10 次。

`pswpin/s`、`pswpout/s` 持续非零说明交换活跃。若同时块设备延迟、`vmstat wa` 和 `/proc/pressure/memory` 上升，Swap 正在影响响应时间。

### 3. 找到使用 Swap 的进程

有 `smem` 时：

```bash
sudo smem -r -k -s swap
```

没有 `smem` 时：

```bash
for status in /proc/[0-9]*/status; do
  awk '/^(Name|Pid|VmSwap):/{printf "%s ", $2} END{print ""}' "$status" 2>/dev/null
done | sort -k3 -nr | head
```

输出依次为进程名、PID 和 `VmSwap` 数值，通常单位是 KiB。这个脚本只做快速归因；共享页面和进程退出竞态会造成统计偏差。

进一步查看目标进程：

```bash
grep -E 'Rss|Pss|Anonymous|Swap' /proc/1234/smaps_rollup
```

### 4. 为什么还有空闲内存却发生 Swap

常见原因：

- Swap 是此前压力期留下的存量，内核不会只因当前 free 增加就主动全部换回。
- NUMA 节点局部内存不足，虽然整机还有空闲页。
- cgroup 内存达到限制，宿主机仍很空闲。
- 内核水位、内存碎片或高阶页分配触发回收。
- 文件页与匿名页回收权衡导致部分冷匿名页被换出。

查看 NUMA：

```bash
numactl --hardware
numastat -m
numastat -p 1234
grep -E 'Node|zone|pages free|min|low|high' /proc/zoneinfo
```

`numastat -m` 看各节点内存，`-p` 看进程分布；`/proc/zoneinfo` 展示各 zone 的 free 和水位。它们用于解释局部回收，不建议看到水位就直接调整 `vm.min_free_kbytes`，错误设置会浪费大量内存或加剧分配失败。

### 5. 不要在线上直接“清空 Swap”

```bash
sudo swapoff -a
sudo swapon -a
```

`swapoff` 会尝试把已换出页面全部搬回物理内存。若 available 不足，可能造成严重 I/O、分配失败甚至 OOM。它不是常规清理命令，只应在维护窗口、确认有足够物理余量并评估业务影响后使用。

修改 `vm.swappiness`、禁用 Swap 或用 `mlockall()` 锁页也都属于容量与延迟策略，不是看到 Swap 告警后的通用修复。数据库、Kubernetes 节点、休眠系统和通用服务器的取舍并不相同。

## 十、缺页异常和内存访问延迟

### 1. `sar -B`：观察系统分页活动

```bash
sar -B 1 10
```

`-B` 报告分页统计，常见字段包括每秒 page fault、major fault、页面扫描和回收效率；字段会随 sysstat 版本变化，先用 `man sar` 对照本机含义。

判断方式：

- `fault/s` 高但 `majflt/s` 低：可能是正常的匿名页首次触碰或页缓存命中。
- `majflt/s` 高且磁盘读、Swap 换入同步上升：内存访问被存储 I/O 拖慢。
- 页面扫描/回收持续很高且 PSI 上升：系统存在回收抖动。

### 2. `pidstat -r`：把缺页归因到进程

```bash
pidstat -r -p ALL 1
pidstat -r -p 1234 1
```

先由 `sar -B` 确认系统级分页异常，再用 `pidstat` 找 `majflt/s` 高的进程。可能根因包括：

- 服务冷启动加载大量动态库、模型或索引。
- 内存映射文件的工作集超过内存。
- 进程页面刚从 Swap 换回。
- 随机访问模式造成页缓存命中率低。

如果主缺页来自文件映射，结合 `pmap -x` 找映射文件，使用缓存和存储工具确认；如果来自 Swap，回到上一节找 `VmSwap` 和换入速率。

### 3. `perf stat`：检查页错误和 TLB miss

```bash
perf stat -e page-faults,minor-faults,major-faults,context-switches ./your_program
perf stat -e dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses -p 1234 -- sleep 30
```

第一条在程序完整运行期间计数；第二条附加 PID 采样 30 秒。TLB 事件名称和硬件支持依 CPU、内核而异，可用 `perf list | grep -i tlb` 查看本机可用事件。

TLB miss 高时再考虑访问局部性、数据结构布局、Huge Page 或线程/NUMA 亲和性。大页是验证后的优化方向，不是发现缺页异常后的第一步，因为缺页异常和 TLB miss 不是同一个概念。

## 十一、OOM：先读日志，再判断是整机还是容器

### 1. 查看内核 OOM 日志

```bash
journalctl -k -g 'Out of memory|Killed process|oom-kill' --since '1 hour ago'
dmesg -T | grep -Ei 'out of memory|killed process|oom-kill'
```

- `journalctl -k`：只看内核日志。
- `-g`：按正则过滤，较旧 systemd 可能不支持，可改用管道 `grep`。
- `--since`：限制时间窗口。
- `dmesg -T`：将内核时间戳转为可读时间；容器内或普通用户可能无权限。

日志重点读：触发分配的进程、被杀进程、`oom_score_adj`、内存与 Swap 状态、是 global OOM 还是 memory cgroup OOM。被杀进程不一定就是泄漏源，它只是 OOM 算法在当时选出的 victim。

### 2. 查看和调整 OOM 分值

```bash
cat /proc/1234/oom_score
cat /proc/1234/oom_score_adj
```

`oom_score` 是内核动态计算结果；`oom_score_adj` 范围为 `-1000` 到 `1000`，越大越容易被杀，`-1000` 基本禁止选择。老资料中的 `/proc/PID/oom_adj` 已过时，现代系统使用 `oom_score_adj`。

不要随意把关键进程都设为 `-1000`。如果所有大进程都受保护，内核可能杀死更不合适的进程，甚至让系统无法恢复。正确做法是先设置合理容量、限制和优雅降级，再谨慎调整优先级。

### 3. 容器 cgroup v2 内存排查

先确认进程所在 cgroup：

```bash
cat /proc/1234/cgroup
```

进入对应 cgroup 目录后查看：

```bash
cat memory.current
cat memory.max
cat memory.high
cat memory.swap.current
cat memory.swap.max
cat memory.events
cat memory.stat
cat memory.pressure
```

- `memory.current`：当前 cgroup 内存使用。
- `memory.max`：硬上限；`max` 表示无限制。
- `memory.high`：软节流阈值，超过后任务会承受回收压力。
- `memory.events`：`high`、`max`、`oom`、`oom_kill` 等计数。计数增长比单次快照更关键。
- `memory.stat`：拆分 anon、file、kernel、slab、pagetables、workingset 等。
- `memory.pressure`：该 cgroup 的 PSI 内存压力。

宿主机 `free` 很充足但容器 `oom_kill` 增长，说明问题是容器限制而不是整机容量。Kubernetes 还应结合 Pod `requests/limits`、退出码 137、`OOMKilled` 状态和节点 eviction 事件。

cgroup v1 文件名不同，常见为 `memory.usage_in_bytes`、`memory.limit_in_bytes`、`memory.failcnt`。先用 `stat -fc %T /sys/fs/cgroup` 判断环境，不要混用两套文件。

## 十二、PSI：内存还有余量但业务已经卡顿

```bash
cat /proc/pressure/memory
watch -n 1 'cat /proc/pressure/memory'
```

示例字段包括：

- `some`：至少一个任务因内存压力停顿的时间比例。
- `full`：所有非空闲任务同时因内存压力停顿的比例，通常更严重。
- `avg10/avg60/avg300`：过去 10、60、300 秒的平均压力。
- `total`：累计停顿微秒数。

PSI 衡量的是任务因回收、压缩或内存等待而失去运行机会的时间，不是内存使用百分比。`available` 尚未归零但 PSI 持续升高，说明系统可能已经在频繁回收，业务延迟会先于 OOM 恶化。

证据链可以是：`memory.pressure` 上升 -> `sar -B` 页面扫描和主缺页上升 -> `vmstat si/so` 或 I/O 活跃 -> 找到高 RSS、缓存 miss 或 Swap 进程。

## 十三、按现象选择命令

| 现场现象 | 第一层确认 | 第二层定位 | 最终归因 |
| --- | --- | --- | --- |
| `free` 很小 | `free -h` 看 `available` | `/proc/meminfo` 拆 anon/cache/slab | 正常缓存或真实压力 |
| available 持续下降 | `vmstat 1`、`sar -r` | `pidstat -r`、`top` | `smaps_rollup`、`pmap` |
| 某进程 RSS 增长 | `pidstat -r -p PID 1` | `status`、`smaps_rollup`、`pmap -x` | memleak/Valgrind/ASan |
| buff/cache 很大 | `/proc/meminfo` | Cached 用 cachestat/cachetop；Slab 用 slabtop | 文件访问或内核对象 |
| Swap used 很大 | `vmstat 1`、`sar -W` 看是否活跃 | `smem -s swap`、`VmSwap` | 进程、NUMA、cgroup、水位 |
| 主缺页高 | `sar -B 1` | `pidstat -r -p ALL 1` | 映射文件、冷启动或 Swap |
| OOM | `journalctl -k`、`dmesg` | 读 global/memcg 和 victim | 泄漏、限制或容量规划 |
| 宿主机空闲但容器 OOM | `memory.current/max/events` | `memory.stat/pressure` | limit、工作集、缓存和并发 |
| Slab 持续增长 | `SUnreclaim` 趋势 | `slabtop -o` | 内核对象、网络或驱动 |
| 内存不满但延迟抖动 | `/proc/pressure/memory` | `sar -B/-W`、vmstat | direct reclaim、Swap、压缩 |
| VIRT 巨大、RES 正常 | `top/ps` | `pmap -x` | 地址预留或文件映射，未必异常 |

## 十四、四类典型故障的完整闭环

### 场景 1：C++ 服务内存泄漏

```bash
free -h
vmstat -w 1
pidstat -r -p ALL 1
grep -E 'Rss|Pss|Anonymous|Swap' /proc/1234/smaps_rollup
pmap -x 1234
sudo /usr/share/bcc/tools/memleak -p 1234
```

证据链：`available` 持续下降 -> 进程 RSS 随时间单调增长且吞吐稳定 -> PSS 和匿名私有页增长 -> `[heap]` 或匿名映射增长 -> 同一分配调用栈的未释放字节持续增加。最后才能定性为泄漏，并回到对象所有权或异常路径修复。

### 场景 2：文件页缓存挤压内存

```bash
free -h -w
grep -E 'Cached|Dirty|Writeback|Slab|SReclaimable' /proc/meminfo
vmstat -w 1
sudo cachestat 1 10
sudo cachetop 5
pidstat -d -p ALL 1
```

证据链：`buff/cache` 增长 -> `/proc/meminfo` 确认主要是 `Cached` 而非 `SUnreclaim` -> 页缓存 miss/dirty 活跃 -> `cachetop` 找到文件访问进程 -> `pidstat -d` 和业务日志确认大文件扫描、备份或日志任务。若 `available` 仍充足且没有回收压力，这可能只是正常利用内存，不需要“清缓存”。

### 场景 3：Swap 抖动造成延迟

```bash
vmstat -w 1
sar -W 1 10
cat /proc/pressure/memory
sudo smem -r -k -s swap
numastat -m
```

证据链：`si/so` 持续非零 -> PSI 和磁盘 I/O 同期上升 -> 找到 Swap 较多的进程 -> 判断是进程工作集过大、NUMA 局部不足、容器限制还是历史冷页。修复可能是减少工作集、限制并发、调整容量/NUMA 策略；不能先执行 `swapoff -a` 掩盖根因。

### 场景 4：容器被 OOMKill，但宿主机内存足够

```bash
cat /proc/1234/cgroup
cat /sys/fs/cgroup/<path>/memory.current
cat /sys/fs/cgroup/<path>/memory.max
cat /sys/fs/cgroup/<path>/memory.events
cat /sys/fs/cgroup/<path>/memory.stat
cat /sys/fs/cgroup/<path>/memory.pressure
```

证据链：宿主机 `available` 正常 -> cgroup 的 `oom_kill` 计数增加 -> `memory.current` 接近 `memory.max` -> `memory.stat` 判断 anon、file 或 kernel 主导 -> 再定位进程和请求模式。解决方向是修泄漏、控制并发/缓存、合理设置 limit 和 request，而不是给宿主机盲目加内存。

## 十五、线上排障最容易犯的错误

1. **把 `free` 列小当作内存不足。** 优先看 `available`、回收、Swap 和 PSI。
2. **把 VIRT 当作物理占用。** 物理压力看 RSS/PSS，地址构成看 `pmap/smaps`。
3. **把所有进程 RSS 相加。** 共享页会重复，汇总优先使用 PSS。
4. **只看 Swap 已用量。** 性能影响看 `si/so`、`pswpin/s`、I/O 和压力趋势。
5. **看到 buff/cache 大就 drop caches。** 缓存是性能机制；清缓存会制造冷启动和 I/O 峰值，而且不能修复泄漏。
6. **线上直接 `swapoff -a`。** 强制换入可能造成 OOM 和严重抖动。
7. **只采样一个时刻。** 泄漏、回收和 Swap 必须看时间序列，并与业务负载对齐。
8. **忽略分配器缓存。** `free()` 后 RSS 不降可能是 allocator 保留 arena，不必然是对象仍不可达。
9. **忽略容器和 NUMA 边界。** 整机有余量，不代表 cgroup 或本地节点有余量。
10. **在生产直接跑高开销工具。** Valgrind、完整 smaps 扫描、无过滤 eBPF 都应评估开销和权限。

`echo 3 > /proc/sys/vm/drop_caches` 只会丢弃可回收缓存，并不会解决匿名内存泄漏；执行前还常伴随 `sync`，会放大 I/O。它适合受控基准测试，不是生产告警的修复按钮。

## 十六、面试高频问答

### 1. 为什么 Linux 的 free 很小却不一定内存不足？

Linux 会把空闲内存用于页缓存和可回收内核缓存。`free` 只表示完全未使用页，`available` 还估算了可回收缓存，更能表示新负载余量。

### 2. VIRT、RSS、PSS 有什么区别？

VIRT 是全部虚拟地址空间；RSS 是驻留物理页但共享页会重复；PSS 把共享页按进程平摊，统计多进程总物理成本时更合理。

### 3. minor fault 和 major fault 有什么区别？

两者都表示页表映射未满足访问。minor fault 不需要存储 I/O，例如匿名页首次分配或页已在缓存；major fault 需要从文件或 Swap 读入，延迟高得多。

### 4. 内存释放后 RSS 为什么不下降？

用户态分配器可能保留已释放块以便复用；页面仍可能留在 arena，或只有部分页可归还。先用堆分析器确认对象是否不可达，再区分真实泄漏与分配器缓存/碎片。

### 5. 如何排查内存泄漏？

先用 `free/vmstat` 确认系统余量下降，用 `pidstat -r` 找增长进程，用 `smaps_rollup/pmap` 确认匿名私有映射增长，再用 memleak、Valgrind 或 LSan 找持续增长的分配栈，最后修复对象生命周期。

### 6. Swap 使用高就一定有性能问题吗？

不一定。Swap used 是存量；没有持续换入换出时，可能只是冷页。要结合 `vmstat si/so`、`sar -W`、磁盘延迟和 PSI 判断当前是否抖动。

### 7. OOM 为什么杀了看起来不是最大的进程？

内核根据可杀内存、`oom_score_adj`、cgroup 边界等综合选择 victim。被杀者不一定是根因，必须读完整 OOM 日志并查看限制和内存增长来源。

### 8. Buffer 和 Cache 有什么区别？

实务上，Buffers 主要与块设备缓冲/元数据有关；Cached 主要是普通文件页缓存；`free` 的 buff/cache 还可能包含可回收 Slab。现代内核不要只靠“Buffer 写、Cache 读”的口诀判断。

## 十七、最后的命令速查

```bash
# 系统总览
free -h -w
vmstat -w 1
grep -E 'MemAvailable|Cached|AnonPages|Slab|Dirty|SwapFree' /proc/meminfo
cat /proc/pressure/memory

# 进程内存与缺页
top
ps -eo pid,ppid,vsz,rss,pmem,comm --sort=-rss | head
pidstat -r -p ALL 1
grep -E 'Rss|Pss|Anonymous|Swap' /proc/PID/smaps_rollup
pmap -x PID
sudo smem -r -k -s pss

# 缓存和 Slab
sudo cachestat 1 10
sudo cachetop 5
sudo slabtop -o

# Swap 和分页
swapon --show
sar -S 1 10
sar -W 1 10
sar -B 1 10
sudo smem -r -k -s swap

# 泄漏
sudo /usr/share/bcc/tools/memleak -p PID
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all ./app

# OOM
journalctl -k -g 'Out of memory|Killed process|oom-kill' --since '1 hour ago'
cat /proc/PID/oom_score
cat /proc/PID/oom_score_adj

# 容器 cgroup v2
cat memory.current
cat memory.max
cat memory.events
cat memory.stat
cat memory.pressure

# NUMA
numastat -m
numastat -p PID
grep -E 'Node|zone|pages free|min|low|high' /proc/zoneinfo
```

面试回答内存排障题时，先说清楚你要区分的是匿名内存、文件页缓存、内核 Slab、Swap、缺页、OOM 还是 cgroup 限制；再用总量、趋势和进程指标逐层排除；最后用映射分布或分配调用栈落到代码。命令只是取证工具，完整证据链才是性能分析能力。
