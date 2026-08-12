---
{
  "title": "Linux CPU 性能排障：从平均负载到 perf 的完整定位路径",
  "slug": "linux-cpu-performance-troubleshooting",
  "date": "2026-08-10",
  "updated": "2026-08-11",
  "excerpt": "从平均负载、CPU 使用率和上下文切换出发，串联 uptime、top、vmstat、mpstat、pidstat、sar、strace、perf 等工具，形成可落地的 CPU 瓶颈排查闭环。",
  "category": { "name": "Linux 性能", "slug": "linux-performance" },
  "tags": [
    { "name": "Linux", "slug": "linux" },
    { "name": "CPU", "slug": "cpu" },
    { "name": "性能排查", "slug": "performance" },
    { "name": "面试复习", "slug": "interview" }
  ],
  "featured": false,
  "draft": false,
  "seoDescription": "Linux CPU 性能排查与面试复习指南，详解平均负载、CPU 使用率、上下文切换、中断、I/O 和网络问题，以及 top、vmstat、pidstat、perf 等命令的完整使用路径。"
}
---

> 这篇文章适合面试前复习，也可以直接当作线上 CPU 告警的操作手册。核心原则只有一句：先确认系统层面的异常指标，再定位进程和线程，最后用调用栈或系统调用找到代码根因。

## 一、先记住完整排查路径

```text
告警或响应变慢
  |
  +-- uptime：负载是否异常，异常正在上升还是已经回落？
  |
  +-- top + vmstat：CPU 真忙，还是任务在等待 I/O？
  |      |
  |      +-- us 高：pidstat/top/ps 找进程和线程 -> perf 找热点函数
  |      +-- sy 高：pidstat/strace/perf 查系统调用、锁和内核路径
  |      +-- wa 高、b 高：dstat/sar/pidstat -d 查块设备与 I/O 进程
  |      +-- hi/si 高：/proc/interrupts、/proc/softirqs、sar -n、tcpdump
  |      +-- st 高：虚拟化宿主机争用，联系云平台或迁移实例
  |
  +-- cs 高：vmstat 看全局 -> pidstat -w 看进程 -> pidstat -wt 看线程
  |
  +-- top 看不到：execsnoop 抓短命进程，检查崩溃重启和定时任务
  |
  +-- perf record/report：用采样调用栈确认真正的 CPU 热点
```

不要一看到 load average 高就断言“CPU 不够”，也不要一看到 `wa` 高就断言“磁盘已到瓶颈”。负载、利用率、队列、吞吐、延迟和代码热点必须形成互相印证的证据链。

## 二、平均负载到底是什么

### 1. `uptime`：先看负载和趋势

```bash
uptime
```

示例：

```text
21:50:43 up 3:21, 1 user, load average: 0.29, 0.12, 0.03
```

- `21:50:43`：当前时间。
- `up 3:21`：系统已经运行 3 小时 21 分钟。
- `1 user`：当前登录会话数，不等于业务用户数。
- `0.29, 0.12, 0.03`：最近 1、5、15 分钟的平均负载。

这三个值是指数衰减的移动平均值，不是简单的算术平均值。比较它们可以判断趋势：

- 1 分钟值大于 5 分钟和 15 分钟值：负载最近正在上升。
- 1 分钟值小于后两项：高峰可能正在消退。
- 三项都高：问题已经持续一段时间。

Linux 平均负载主要统计两类任务：

- `R`，可运行状态：正在 CPU 上运行，或者已经就绪、正在等待 CPU。
- `D`，不可中断睡眠：通常正在等待磁盘、网络存储或其他内核资源。

因此，平均负载高有两条完全不同的原因：CPU 就绪队列很长，或者大量任务卡在不可中断等待。平均负载不是 CPU 使用率。

还可以直接查看内核提供的数据：

```bash
cat /proc/loadavg
```

示例：

```text
0.29 0.12 0.03 2/418 12345
```

前三项仍是 1、5、15 分钟负载；`2/418` 表示当前 2 个可运行任务、共 418 个任务；最后一项是最近创建进程的 PID。

### 2. `lscpu`、`nproc`：负载必须结合可用 CPU 看

```bash
lscpu
nproc
nproc --all
```

- `lscpu` 展示架构、逻辑 CPU 数、每核线程数、每插槽核心数、NUMA 等信息。
- `nproc` 返回当前进程可用的逻辑 CPU 数，容器或 CPU 亲和性受限时通常比物理机总数更有参考价值。
- `nproc --all` 返回系统安装的全部逻辑 CPU 数。

也可以使用：

```bash
grep -c '^processor' /proc/cpuinfo
```

这里统计的是逻辑 CPU，不要把输出直接说成物理核数，更不能说成“多少 G”。物理核数可结合 `lscpu` 中的 `Socket(s)`、`Core(s) per socket` 和 `Thread(s) per core` 判断。

负载可以先做归一化：

```text
归一化负载 = 1 分钟平均负载 / 当前可用逻辑 CPU 数
```

例如 4 个可用 CPU 上负载为 4，代表平均每个 CPU 对应约 1 个活跃任务；负载长期为 8，说明平均还有约 4 个任务在运行或等待。但“超过 CPU 数量的 70% 就一定有问题”不是通用规则：延迟敏感服务可能在更低负载就抖动，离线计算任务则可能长期接近 CPU 数仍属正常。正确做法是结合业务延迟、历史基线和队列趋势。

容器还要检查 CPU 配额。cgroup v2 常用：

```bash
cat /sys/fs/cgroup/cpu.max
cat /sys/fs/cgroup/cpuset.cpus.effective
```

`cpu.max` 输出 `quota period`，例如 `100000 100000` 约等于 1 个 CPU；`max 100000` 表示未设置配额。排查容器时应以实际配额为准，而不是宿主机的 CPU 总数。

## 三、CPU 使用率如何解读

### 1. `top`：第一屏完成系统总览和进程定位

```bash
top
top -d 1
top -p 1234
top -H -p 1234
top -b -n 3 -d 1
```

- `top`：交互式查看系统、进程和资源使用情况。
- `-d 1`：每 1 秒刷新，适合观察变化。
- `-p 1234`：只看指定 PID。
- `-H`：展开线程；与 `-p` 组合可找出进程内最忙的线程。
- `-b -n 3`：批处理模式采样 3 次，适合保存日志。第一屏可能包含启动以来的累计差异或初始化误差，判断瞬时问题时优先看后续样本。

交互界面常用按键：

- `P`：按 CPU 使用率排序。
- `M`：按内存使用率排序。
- `1`：展开每个逻辑 CPU 的统计。
- `H`：切换线程显示。
- `c`：显示完整命令行。
- `f`：选择展示字段。
- `q`：退出。

CPU 行重点字段：

| 字段 | 含义 | 高时优先怀疑 |
| --- | --- | --- |
| `us` | 普通用户态执行时间 | 业务计算、循环、序列化、算法热点 |
| `sy` | 内核态执行时间 | 系统调用、内核网络栈、锁、驱动 |
| `ni` | 调整过 nice 值的用户态时间 | 低优先级计算任务 |
| `id` | 空闲时间 | 越低代表 CPU 越忙 |
| `wa` | CPU 空闲且存在未完成 I/O 时的等待时间 | 存储延迟或 I/O 型负载，需要继续验证 |
| `hi` | 硬中断时间 | 设备、网卡、IRQ 分配 |
| `si` | 软中断时间 | 网络包、定时器、调度等软中断 |
| `st` | 虚拟机被宿主机拿走的 CPU 时间 | 宿主机超卖或邻居争用 |

进程列表中的 `%CPU` 要注意显示模式。默认 Irix 模式下，多线程进程在多核机器上可能超过 100%；按 `I` 可切换 Solaris 模式，让数值相对整机 CPU 总量归一化。

`top` 适合实时交互，但不适合精确保存历史。历史趋势交给 `sar`，逐 CPU 采样交给 `mpstat`，进程和线程明细交给 `pidstat`。

### 2. `htop` 和 `atop`：更直观或更全面的总览

```bash
htop
atop 1
sudo atop -r /var/log/atop/atop_20260811
```

- `htop` 是增强版 `top`，支持颜色区分、树形展示、搜索和更方便的线程切换。按 `F6` 选择排序字段，按 `F5` 切换进程树，按 `H` 隐藏或显示用户线程。它适合人工查看，但不是所有服务器都预装。
- `atop 1` 每秒刷新一次，同时展示 CPU、内存、磁盘、网络和进程资源。开启 `atop` 采集服务后，`atop -r <日志文件>` 可以回看历史故障现场。是否有历史数据取决于服务是否提前启用，不能等事故发生后再补。

### 3. `mpstat`：判断是所有 CPU 忙还是单核忙

```bash
mpstat -P ALL 1
mpstat -P ALL 1 5
```

- `-P ALL`：显示整体和每个逻辑 CPU。
- 第一个 `1`：每秒采样。
- 最后的 `5`：共输出 5 组样本；省略时持续运行，按 `Ctrl+C` 停止。

重点看 `%usr`、`%sys`、`%iowait`、`%irq`、`%soft`、`%steal`、`%idle`。典型判断：

- 所有 CPU 的 `%idle` 都很低：整机 CPU 容量可能不足。
- 只有一个 CPU 接近 100%，其他核空闲：可能是单线程热点、CPU 亲和性或中断集中。
- `%sys` 高：内核路径消耗大，继续查系统调用、网络、锁和中断。
- `%steal` 高：问题可能不在虚拟机内部。

`mpstat` 属于 `sysstat` 软件包。Ubuntu/Debian 常用 `sudo apt install sysstat`，RHEL 系常用 `sudo dnf install sysstat`。

### 4. `sar`：看历史 CPU 趋势

```bash
sar -u 1 5
sar -u -P ALL 1 5
sar -u -f /var/log/sa/sa11
```

- `-u`：CPU 使用率。
- `-P ALL`：按 CPU 展开。
- `1 5`：每秒一次，共 5 次。
- `-f`：读取指定历史数据文件；路径随发行版配置而异。

`sar` 和 `mpstat` 都来自 `sysstat`。`mpstat` 更适合现场逐 CPU 观察，`sar` 的优势是已有采集服务时可以回看告警发生前后的变化。

### 5. `/proc/stat`：理解所有 CPU 工具的数据源

```bash
grep '^cpu' /proc/stat
```

第一行是全部 CPU 汇总，后面是 `cpu0`、`cpu1` 等逐 CPU 累计时间，单位通常是 USER_HZ。字段依次包括 `user`、`nice`、`system`、`idle`、`iowait`、`irq`、`softirq`、`steal`、`guest`、`guest_nice`。

CPU 使用率必须用两次采样的增量计算，不能只读一次累计值：

```text
使用率 = 1 - (idle 增量 + iowait 增量) / 总时间增量
```

不同工具对 `iowait` 是否算作 idle 的展示口径可能不同；`guest` 时间也已经包含在 `user` 中，手工计算时不能重复相加。实际排障优先使用 `mpstat` 等成熟工具，查看 `/proc/stat` 主要是理解数据来源或处理极简环境。

## 四、从整机定位到进程和线程

### 1. `pidstat`：最关键的进程级采样工具

```bash
pidstat -u -p ALL 1
pidstat -u -p 1234 1
pidstat -u -t -p 1234 1
pidstat -w -p ALL 1
pidstat -w -t -p 1234 1
pidstat -d -p ALL 1
```

- `-u`：进程 CPU 使用率，关注 `%usr`、`%system`、`%wait`、`%CPU` 和 `CPU`。
- `-p ALL`：监控全部进程；也可以写具体 PID，多个 PID 用逗号分隔。
- `-t`：展开线程。找到了高 CPU 进程后，应继续确认是哪个线程。
- `-w`：上下文切换，关注 `cswch/s` 和 `nvcswch/s`。
- `-d`：进程 I/O，关注 `kB_rd/s`、`kB_wr/s`、`iodelay`。
- 最后的 `1`：每秒输出一次。没有采样间隔时通常只给出自进程启动以来的平均值，不适合捕捉当前抖动。

`cswch/s` 是每秒自愿上下文切换次数，常见于进程主动等待 I/O、锁、条件变量或睡眠；`nvcswch/s` 是每秒非自愿上下文切换次数，常见于时间片耗尽或被更高优先级任务抢占。绝对数值没有统一红线，要与历史基线、任务数和 CPU 数一起判断。

当 `pidstat -u` 找到 PID 1234 后，标准线程定位方式是：

```bash
pidstat -u -t -p 1234 1
top -H -p 1234
```

记录高 CPU 线程的 TID，后续可以让 `perf` 只采样该线程。

### 2. `ps`：生成一次性的进程快照

```bash
ps -eo pid,ppid,tid,psr,stat,pcpu,pmem,comm,args --sort=-pcpu | head -n 20
ps -L -p 1234 -o pid,tid,psr,stat,pcpu,comm --sort=-pcpu
ps -eo state,pid,ppid,wchan:32,comm | awk '$1 ~ /^D/'
ps -eo state,pid,ppid,comm | awk '$1 ~ /^Z/'
```

- 第一条按 CPU 倒序列出进程，适合记录现场。
- `-L -p 1234` 展开指定进程的线程。
- `psr` 是任务最近运行的 CPU 编号。
- `stat` 是进程状态；常见首字母包括 `R` 运行或就绪、`S` 可中断睡眠、`D` 不可中断睡眠、`T` 停止、`Z` 僵尸、`I` 空闲内核线程。
- `wchan` 显示任务在内核中的等待位置，可辅助判断 `D` 状态在等待什么；权限不足时信息可能受限。

`ps` 是瞬时快照，短峰值可能恰好错过；`pidstat 1` 和 `top -d 1` 是连续采样。进程 `%CPU` 也可能是生命周期平均值，不能只凭一次 `ps` 输出下结论。

### 3. `pstree`：查看父子关系和异常拉起链路

```bash
pstree -ap 1234
pstree -aps 1234
```

- `-a`：显示命令行参数。
- `-p`：显示 PID。
- `-s`：显示指定进程的父进程链。

它适合回答“谁启动了这个高 CPU 子进程”“崩溃后是谁不断重启它”“僵尸进程的父进程是谁”。僵尸进程本身已经退出，几乎不消耗 CPU；真正要修的是父进程没有正确调用 `wait()`/`waitpid()` 回收，或 `SIGCHLD` 处理存在问题。

## 五、上下文切换和运行队列

### 1. CPU 上下文切换是什么

CPU 上下文主要包括寄存器、程序计数器和栈指针等执行现场。调度器从一个任务切换到另一个任务时，要保存前者并恢复后者。进程切换通常还涉及地址空间等状态，成本一般高于同一进程内的线程切换。

系统调用会发生用户态与内核态的特权级切换，但它不必然等于“调度器把 CPU 从当前进程切给另一个进程”。面试时应区分模式切换和任务上下文切换。

### 2. `vmstat`：同时观察队列、切换、中断和 CPU

```bash
vmstat 1
vmstat 1 5
vmstat -w 1
```

- `1`：每秒采样。
- `1 5`：每秒一次，共 5 组。
- `-w`：宽输出，避免大数值挤在一起。

第一行通常是开机以来的平均值，分析当前问题时重点看后续样本。关键列：

| 列 | 含义 | 判断方式 |
| --- | --- | --- |
| `r` | 可运行任务数 | 长期大于可用 CPU 数，说明 CPU 排队明显 |
| `b` | 不可中断睡眠任务数 | 持续大于 0 时继续查磁盘、网络存储或内核等待 |
| `in` | 每秒中断数 | 突增时结合硬中断和软中断明细 |
| `cs` | 每秒上下文切换数 | 突增时用 `pidstat -w` 找进程 |
| `us/sy/id/wa/st` | CPU 时间分布 | 与 `top`、`mpstat` 含义相近 |
| `si/so` | 每秒换入/换出 | 持续非零可能存在内存压力，不是 softirq |

注意：`vmstat` 的 `in` 是中断次数，而 CPU 区域的 `sy` 是系统态；`si/so` 位于 swap 区域，表示 swap in/out，不要和 `top` 的软中断 `si` 混淆。

### 3. 用 `pidstat -w` 找出切换来源

```bash
pidstat -w -p ALL 1
pidstat -w -t -p 1234 1
```

排查顺序：

1. `vmstat 1` 发现 `cs` 相比历史基线明显升高。
2. `pidstat -w -p ALL 1` 按进程观察 `cswch/s` 和 `nvcswch/s`。
3. 对可疑 PID 使用 `pidstat -w -t -p PID 1`，定位线程。
4. 自愿切换高，查锁等待、条件变量、频繁 I/O、短周期 sleep；非自愿切换高，查线程过多、CPU 争用、优先级和时间片耗尽。
5. 最后结合 `perf` 的锁、自旋、调度器或业务热点调用栈确认代码位置。

## 六、硬中断和软中断

硬件中断的上半部要求快速响应设备；耗时工作通常推迟到软中断等下半部执行。网络收发、定时器和调度都可能造成软中断压力。

### 1. `/proc/interrupts`：硬中断分布

```bash
watch -n 1 'cat /proc/interrupts'
grep -E 'CPU|eth|ens|enp|nvme|virtio' /proc/interrupts
```

`/proc/interrupts` 按中断号和 CPU 展示累计次数。需要观察两次输出之间的增量，而不是只看累计总数。若网卡或 NVMe 中断几乎全部落在单个 CPU，同时该 CPU 的 `%irq` 很高，继续检查 IRQ affinity、RSS/RPS 和队列配置。

`watch` 每秒重新执行命令；单引号中的命令由 shell 周期运行。生产环境输出太长时先用 `grep` 过滤相关设备。

### 2. `/proc/softirqs`：软中断类型和 CPU 分布

```bash
watch -n 1 'cat /proc/softirqs'
grep -E 'NET_RX|NET_TX|TIMER|SCHED' /proc/softirqs
mpstat -I ALL -P ALL 1
```

- `NET_RX`、`NET_TX` 高：优先排查网络包速率、丢包和网卡队列。
- `TIMER` 高：检查高频定时器和唤醒。
- `SCHED` 高：可能与任务数量、调度和上下文切换有关。
- `mpstat -I ALL -P ALL 1` 以速率形式展示每个 CPU 的中断情况，比手工比较累计值方便。

如果 `top` 中 `si` 很高，同时 `/proc/softirqs` 的 `NET_RX` 增长最快，下一步使用 `sar -n DEV 1` 看包速率和错误，再用 `tcpdump` 分析流量来源。

## 七、负载高但 CPU 不高：检查 I/O

### 1. `dstat`：把 CPU、磁盘和网络放在同一时间轴

```bash
dstat -cdnm 1
dstat -d --disk-util 1
dstat -tcdn --top-cpu --top-io 1
```

- `-c`：CPU。
- `-d`：磁盘吞吐。
- `-n`：网络吞吐。
- `-m`：内存。
- `-t`：时间戳。
- `--disk-util`：磁盘忙碌度。
- `--top-cpu`、`--top-io`：显示最活跃的进程。

不同发行版中的插件支持可能不同；部分系统提供的是 `pcp-dstat`。使用它的价值是相关性：同一秒内 `wa`、磁盘吞吐或延迟、网络流量是否一起上升。

### 2. `sar -d`：查看块设备当前或历史数据

```bash
sar -d -p 1 5
sar -d -p -f /var/log/sa/sa11
```

- `-d`：块设备统计。
- `-p`：使用更易读的设备名。
- `1 5`：每秒一次，共 5 次。
- `-f`：读取历史记录。

重点看 `tps`、`rkB/s`、`wkB/s`、平均等待时间和设备利用率字段；字段名会随 sysstat 版本变化。不要只看吞吐量，还要看延迟和队列。SSD、RAID、云盘及并行设备上，单独用 `%util=100%` 也不总能证明设备已经达到真实性能上限。

### 3. `pidstat -d`：把 I/O 归因到进程

```bash
pidstat -d -p ALL 1
pidstat -d -p 1234 1
```

先由 `dstat` 或 `sar -d` 确认 I/O 与故障同时间发生，再用 `pidstat -d` 找持续读写或 `iodelay` 高的进程。若 `vmstat` 的 `b` 很高，还可以用以下命令查看 `D` 状态任务和等待点：

```bash
ps -eo state,pid,ppid,wchan:32,comm | awk '$1 ~ /^D/'
```

`iowait` 高只说明 CPU 在某些时段空闲且存在未完成 I/O。它既不指出哪个设备，也不证明设备已经饱和；如果还有其他可运行任务，CPU 会去运行它们，`iowait` 反而可能下降。因此必须用设备指标和进程 I/O 继续验证。

## 八、软中断高时检查网络

### 1. `sar -n`：先看吞吐、包速率和错误

```bash
sar -n DEV 1 5
sar -n EDEV 1 5
sar -n TCP,ETCP 1 5
```

- `DEV`：网卡吞吐和每秒收发包数，关注 `rxpck/s`、`txpck/s`、`rxkB/s`、`txkB/s`。
- `EDEV`：网卡错误、丢包和丢弃。
- `TCP`：TCP 活动，如主动连接、被动连接和报文段。
- `ETCP`：TCP 错误，如重传。

CPU 软中断更容易被很高的 PPS，也就是每秒包数推高，而不只是大带宽。例如大量小包的吞吐量不高，但包处理成本很大。

### 2. `tcpdump`：确认流量来自哪里

```bash
sudo tcpdump -nn -i any -c 200
sudo tcpdump -nn -i eth0 'tcp port 8080'
sudo tcpdump -nn -i eth0 -w cpu-incident.pcap 'host 10.0.0.8 and tcp'
```

- `-nn`：不解析主机名和端口名，减少额外开销并让输出更直接。
- `-i`：指定网卡；`any` 便于先总览，精确分析时指定具体接口。
- `-c 200`：抓到 200 个包后退出，避免无限运行。
- 引号中的内容是 BPF 过滤表达式，可按协议、IP、端口组合。
- `-w`：写入 pcap 文件，供 Wireshark 等工具离线分析。

抓包可能包含业务敏感数据，并会带来额外开销。生产环境应限制接口、过滤条件、包数或文件大小，不能无边界抓取。

判断链路是：`top` 发现 `si` 高 -> `/proc/softirqs` 发现 `NET_RX` 增长快 -> `sar -n DEV,EDEV` 找到高 PPS 或丢包网卡 -> `tcpdump` 用过滤条件确认源 IP、端口、协议和异常报文模式。

## 九、`strace`：系统态高时看系统调用

```bash
sudo strace -f -tt -T -c -p 1234
sudo strace -f -tt -T -e trace=futex,read,write,recvfrom,sendto -p 1234
```

- `-p 1234`：附加到指定进程。
- `-f`：跟踪其线程和后续创建的子进程。
- `-tt`：输出带微秒级时间戳。
- `-T`：显示每次系统调用耗时。
- `-c`：只做汇总，显示调用次数、错误数和耗时占比，适合先找方向；按 `Ctrl+C` 结束后输出统计。
- `-e trace=...`：只跟踪指定系统调用，降低噪声。

典型解释：

- `futex` 次数多且耗时长：可能存在锁竞争或线程同步问题，但还要结合调用栈。
- 大量极短的 `read`/`write`：可能是小块 I/O 或日志过于频繁。
- `recvfrom`/`sendto` 密集：继续查网络调用模式和包速率。
- 某个调用频繁返回错误：应用可能在无效重试。

`strace` 会暂停被跟踪线程来记录系统调用，高频系统调用场景可能产生明显扰动。线上优先短时间、带过滤地使用；CPU 用户态热点应该首选采样型的 `perf`，而不是 `strace`。

## 十、`perf`：从高 CPU 线程定位到函数和调用链

### 1. `perf top`：实时查看热点

```bash
sudo perf top
sudo perf top -p 1234
sudo perf top -t 5678
sudo perf top -g -p 1234
```

- 不带 PID 时观察整机热点。
- `-p` 只采样指定进程。
- `-t` 只采样指定线程 TID。
- `-g` 采集调用关系，能看到热点函数由谁调用。

`Overhead` 表示样本占比，不等于函数精确耗时。样本太少时结论不稳定，应延长观察时间或使用 `perf record` 固化现场。

### 2. `perf record` 和 `perf report`：保存一段时间的调用栈

```bash
sudo perf record -F 99 -g -p 1234 -- sleep 30
sudo perf report
sudo perf report --stdio
```

- `-F 99`：目标采样频率为每秒 99 次。用非整百频率可减少与周期性任务同频造成的偏差。
- `-g`：记录调用栈。
- `-p 1234`：只采样目标进程。
- `-- sleep 30`：采样 30 秒后自动结束。`--` 分隔 perf 参数与被执行的命令。
- `perf report`：交互式分析生成的 `perf.data`。
- `--stdio`：输出文本报告，便于粘贴到事故记录。

如果要采样一个尚未启动的命令：

```bash
sudo perf record -F 99 -g -- ./your_program --your-args
sudo perf report
```

常见热点解释：

- 业务函数占比高：检查死循环、重复计算、算法复杂度和批处理大小。
- `pthread_spin_*` 或自旋代码占比高：检查忙等和锁竞争。
- `futex`、调度器函数占比高：检查线程数、锁、条件变量和上下文切换。
- 内存复制、分配释放函数占比高：检查大对象复制、频繁分配和序列化。
- 内核网络或文件系统函数占比高：回到网络、I/O 和系统调用指标交叉验证。

C/C++ 程序应保留可解析符号。调试构建可使用 `-g`；生产环境为了更可靠地展开栈，可以在评估性能影响后使用 `-fno-omit-frame-pointer`，并单独保存与线上二进制严格匹配的调试符号。优化会造成内联和函数重排，因此报告中的调用关系不一定与源码逐行对应。

`perf` 可能受 `kernel.perf_event_paranoid`、容器权限和内核符号限制。不要为了方便长期放宽整机安全策略；优先在已授权的诊断环境中运行，或由管理员配置最小必要权限。

## 十一、`execsnoop`：抓住 top 看不到的短命进程

持续高 CPU 但 `top` 和 `pidstat` 找不到稳定进程时，常见原因有：程序频繁执行短命子进程，或者服务崩溃后被 systemd、容器平台反复拉起。

BCC 工具常见命令：

```bash
sudo execsnoop-bpfcc
```

有些发行版或工具集中的命令名是：

```bash
sudo execsnoop
sudo /usr/share/bpftrace/tools/execsnoop.bt
```

先用 `command -v execsnoop-bpfcc`、`command -v execsnoop` 确认本机安装的是哪个实现。输出通常包含执行时间、父 PID、PID、返回值和命令参数，可以发现每秒反复执行的脚本、编译器、压缩程序或异常重启进程。

发现可疑短命进程后：

1. 用 PPID 配合 `pstree -aps <PID>` 找启动者。
2. 检查 systemd 重启策略、定时任务、容器 restart count 和应用崩溃日志。
3. 对可稳定复现的命令使用 `perf record -- <command>` 直接采样。

eBPF 工具需要相应内核能力、调试信息和权限；不同发行版的软件包名不同，无法运行时先确认 BCC/bpftrace 与当前内核是否匹配。

## 十二、按现象选择命令

| 现场现象 | 第一层确认 | 第二层定位 | 最终归因 |
| --- | --- | --- | --- |
| 平均负载高 | `uptime`、`nproc` | `vmstat 1` 看 `r`/`b`，`top` 看 CPU 分布 | 按 CPU 或 I/O 分支继续 |
| 用户态 `us` 高 | `mpstat -P ALL 1` | `pidstat -u -p ALL 1`、`top -H -p PID` | `perf top/record/report` |
| 系统态 `sy` 高 | `top`、`mpstat` | `pidstat -u`、`strace -c` | `perf` 查内核/系统调用栈 |
| 上下文切换高 | `vmstat 1` 看 `cs` | `pidstat -w -p ALL 1` | `pidstat -wt` + `perf` |
| 硬中断 `hi` 高 | `mpstat -I ALL -P ALL 1` | `/proc/interrupts` | IRQ、驱动、队列和亲和性 |
| 软中断 `si` 高 | `/proc/softirqs` | `sar -n DEV,EDEV 1` | `tcpdump` 确认流量模式 |
| `wa`、`b` 高 | `vmstat 1`、`dstat` | `sar -d -p 1`、`pidstat -d 1` | 等待点、块设备和进程调用链 |
| 单核满载 | `mpstat -P ALL 1` | `top -H`、`ps -L` | 单线程热点、亲和性或中断集中 |
| `st` 高 | `top`、`mpstat` | 对比云监控和同宿主机事件 | 宿主机争用或实例规格问题 |
| 瞬时进程反复出现 | `pidstat 1`、服务重启计数 | `execsnoop`、`pstree` | 启动者、崩溃原因、定时任务 |
| 僵尸进程多 | `ps` 查 `Z` | `pstree -aps PID` 找父进程 | 修复 `wait/waitpid/SIGCHLD` |

## 十三、三类典型故障的完整闭环

### 场景 1：CPU 计算热点

```bash
uptime
mpstat -P ALL 1 5
pidstat -u -p ALL 1
pidstat -u -t -p 1234 1
sudo perf record -F 99 -g -p 1234 -- sleep 30
sudo perf report
```

证据链应是：负载高且 `r` 偏大 -> `%usr` 高、`%idle` 低 -> 某进程持续高 CPU -> 某线程最忙 -> `perf report` 中某个业务函数或算法占据主要样本。到这里才能把问题定性为 CPU 计算瓶颈，并回到源码优化。

### 场景 2：I/O 等待推高平均负载

```bash
uptime
vmstat 1
dstat -tcdn --top-io 1
sar -d -p 1 5
pidstat -d -p ALL 1
ps -eo state,pid,ppid,wchan:32,comm | awk '$1 ~ /^D/'
```

证据链应是：负载高但 CPU 仍有空闲 -> `b` 和 `wa` 同期升高 -> 某块设备延迟或队列上升 -> 某进程产生主要 I/O -> `D` 状态任务的等待点与该设备或文件系统一致。此时 CPU 不是主瓶颈，增加 CPU 通常无效。

### 场景 3：网络包推高软中断

```bash
top
watch -n 1 'cat /proc/softirqs'
sar -n DEV 1 5
sar -n EDEV 1 5
sudo tcpdump -nn -i eth0 -c 500 'tcp port 8080'
```

证据链应是：`si` 高 -> `NET_RX` 或 `NET_TX` 增长明显并集中在少数 CPU -> 某网卡 PPS 或丢包异常 -> 抓包发现具体源、端口或异常请求模式。之后再决定是限流、优化协议处理、调整网卡多队列/RSS/RPS，还是修复上游流量。

## 十四、线上执行时容易犯的错误

1. **只采样一次。** CPU、负载和队列都是随时间变化的指标，至少连续采样，并记录故障时间窗口。
2. **把累计值当速率。** `/proc/interrupts`、`/proc/softirqs`、`/proc/stat` 都是累计计数，必须比较增量。
3. **只看平均值。** 整机平均 CPU 不高，仍可能有单核、单线程或单个 cgroup 被打满。
4. **混淆负载和利用率。** `R` 与 `D` 都会进入负载，但只有实际执行才消耗 CPU 时间。
5. **直接认定 iowait 等于磁盘瓶颈。** 必须继续看设备延迟、队列、吞吐和进程 I/O。
6. **先上重型工具。** 先用 `/proc`、`uptime`、`vmstat`、`pidstat` 缩小范围，再短时使用 `strace`、`tcpdump`、`perf`。
7. **忽略容器限制。** 宿主机很空闲不代表容器没有被 CPU quota 节流；容器中的 CPU 数和负载解释要结合 cgroup。
8. **没有历史基线。** “每秒 10 万次上下文切换是否高”无法脱离 CPU 数、线程数、吞吐和正常时段回答。

## 十五、面试高频问答

### 1. 平均负载高，CPU 使用率一定高吗？

不一定。平均负载包含正在运行、等待 CPU 的 `R` 状态任务，也包含不可中断睡眠的 `D` 状态任务。大量 I/O 等待可以让负载很高，而 CPU 仍有较多空闲。

### 2. CPU 使用率高，平均负载一定高吗？

也不一定。例如一个单线程任务打满 16 核机器中的 1 核，进程 CPU 很高，但整机负载可能只有约 1，远低于 CPU 总数。

### 3. 自愿和非自愿上下文切换有什么区别？

自愿切换是任务主动阻塞，例如等待 I/O、锁、条件变量或 sleep；非自愿切换是仍想运行但被调度器换下，例如时间片用完、被高优先级任务抢占。使用 `pidstat -w` 区分二者。

### 4. 为什么系统调用不等于进程上下文切换？

系统调用会让同一线程从用户态进入内核态，再返回用户态，这是特权级和执行环境的切换；只有调度器决定换另一个任务运行时，才发生任务上下文切换。

### 5. 如何排查某个 C++ 服务 CPU 过高？

先用 `pidstat -u` 找到高 CPU 进程，再用 `top -H -p PID` 或 `pidstat -ut -p PID 1` 找线程，最后用 `perf record -F 99 -g -p PID -- sleep 30` 和 `perf report` 查看热点函数及调用链。若 `%sys` 高，再结合 `strace -f -T -c -p PID` 看系统调用。

### 6. 为什么 top 找不到 CPU 消耗者？

可能是采样间隔内进程已经退出，也可能服务在频繁崩溃重启。用 `execsnoop` 监控进程执行事件，再结合 `pstree`、systemd 或容器重启记录找到启动者。

## 十六、最后的命令速查

```bash
# 负载与 CPU 数
uptime
cat /proc/loadavg
nproc
lscpu

# 整机 CPU、队列和趋势
top -d 1
vmstat 1
mpstat -P ALL 1
sar -u -P ALL 1 5

# 进程与线程
pidstat -u -p ALL 1
pidstat -u -t -p PID 1
top -H -p PID
ps -L -p PID -o pid,tid,psr,stat,pcpu,comm

# 上下文切换
pidstat -w -p ALL 1
pidstat -w -t -p PID 1

# 中断
cat /proc/interrupts
cat /proc/softirqs
mpstat -I ALL -P ALL 1

# I/O
dstat -tcdn --top-io 1
sar -d -p 1 5
pidstat -d -p ALL 1

# 网络
sar -n DEV,EDEV 1 5
sudo tcpdump -nn -i eth0 -c 500 'tcp port 8080'

# 系统调用、函数热点与短命进程
sudo strace -f -tt -T -c -p PID
sudo perf top -g -p PID
sudo perf record -F 99 -g -p PID -- sleep 30
sudo perf report
sudo execsnoop-bpfcc
```

面试回答 CPU 排查题时，不要从背诵一串工具开始。先说清楚你要区分的是 CPU 计算、内核消耗、调度切换、I/O 等待还是中断压力；再说明每一层用什么指标排除哪种可能；最后用进程、线程和调用栈把问题落到具体代码。这样的回答才构成完整闭环。
