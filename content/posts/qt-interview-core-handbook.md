---
{
  "title": "Qt 面试核心手册：从原理到高频问答",
  "slug": "qt-interview-core-handbook",
  "date": "2026-09-19",
  "updated": "2026-09-19",
  "excerpt": "面向 Qt 5/Qt 6、C++ 桌面与嵌入式岗位，系统梳理元对象、对象树、信号槽、事件循环、线程、Model/View、QML、网络、构建部署与性能调试。",
  "category": { "name": "C++ 实践", "slug": "cpp" },
  "tags": [
    { "name": "Qt", "slug": "qt" },
    { "name": "C++", "slug": "cpp" },
    { "name": "面试复习", "slug": "interview" },
    { "name": "GUI 开发", "slug": "gui-development" }
  ],
  "featured": false,
  "draft": false,
  "seoDescription": "Qt 5/Qt 6 面试核心手册，覆盖元对象系统、对象树、信号槽、事件循环、线程亲和性、Model/View、QML 集成、网络 I/O、构建部署、性能调试和高频代码审查题。"
}
---

> 面向 Qt 5/Qt 6、C++ 桌面与嵌入式岗位。正文以 Qt 6 为准；涉及版本差异时单独说明。资料核对日期为 2026-09-19，主要依据 Qt 6.11 官方文档。

## 怎么使用这份手册

不要逐句死背。先抓住 Qt 的五条主线：

1. **元对象系统**：让 C++ 获得运行时类型信息、属性、信号槽和动态调用。
2. **对象树与所有权**：父对象管理子对象生命周期，解决 GUI 对象的组合与释放。
3. **事件循环**：定时器、窗口事件、异步网络、跨线程 queued signal 最终都靠事件循环派发。
4. **线程亲和性**：`QObject` 属于某个线程；“代码在哪个线程执行”通常由调用方式和接收者亲和性共同决定。
5. **数据与界面分离**：Widgets 的 Model/View、Qt Quick 的 model/delegate 都强调数据不是控件本身。

面试回答推荐四步法：**先下定义 → 讲机制 → 说边界/坑 → 给使用场景**。例如回答 queued connection：它是异步投递；Qt 把调用封装成事件交给接收者线程的事件循环；因此参数需要可复制且元类型可识别，接收线程没有事件循环就无法及时执行；适合跨线程通知。

---

## 一、Qt 整体认知

### 1. Qt 是什么？它不只是 GUI 库吗？

Qt 是跨平台 C++ 应用框架。GUI 只是其中一部分；Qt Core 提供对象模型、容器、线程、I/O、插件、序列化等，另外还有 Network、SQL、Multimedia、QML/Qt Quick、WebEngine 等模块。跨平台的核心不是“所有平台代码完全相同”，而是 Qt 用统一 API 包装操作系统能力，并通过平台插件与原生窗口系统、输入、主题等交互。

### 2. Qt Widgets 与 Qt Quick/QML 怎么选？

| 维度 | Qt Widgets | Qt Quick/QML |
|---|---|---|
| 典型场景 | 传统桌面、复杂表格、工具类软件 | 动效、触屏、车机、现代嵌入式 UI |
| 渲染 | 以 QWidget/绘制事件为核心 | 场景图，适合 GPU 加速与动画 |
| UI 表达 | C++ 或 `.ui` | 声明式 QML，JS 用于轻量逻辑 |
| 架构建议 | Model/View，业务逻辑放 C++ | UI/QML + 业务/C++，用属性、信号和模型连接 |

二者可以混用，但有渲染、事件与维护成本，不能把“能嵌套”误当成“应该任意嵌套”。

### 3. Qt 程序从 `main()` 到界面出现经历什么？

典型过程：创建 `QApplication`/`QGuiApplication`/`QCoreApplication` → 创建并显示顶层窗口或加载 QML → 调用 `exec()` 进入主事件循环 → 系统事件、定时器、queued signal、网络完成通知被分发 → 退出事件循环并析构对象。

`QCoreApplication` 无 GUI；`QGuiApplication` 支持 GUI 基础；使用 Widgets 时需要 `QApplication`。

### 4. Qt 的模块化有什么价值？

减少依赖和体积，明确链接边界。CMake 中按模块链接，例如：

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
target_link_libraries(app PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
```

---

## 二、元对象系统：QObject、moc、属性、反射

### 5. Qt 为什么需要元对象系统？

C++ 原生 RTTI 主要提供 `typeid`/`dynamic_cast`，无法直接描述“可枚举的方法、属性和信号”。Qt 用 `QObject`、`Q_OBJECT`、`moc` 生成的代码和 `QMetaObject` 补上这些能力，用于：

- 运行时查询类名、继承、方法、枚举、属性；
- 信号槽和 `QMetaObject::invokeMethod()`；
- 动态属性、对象查找、QML/C++ 集成；
- 翻译等机制。

它不是一般意义上的完整 C++ 反射，而是一套 Qt 自己的、受宏和代码生成驱动的运行时类型系统。

### 6. `Q_OBJECT` 做了什么？忘写会怎样？

它在类中声明元对象相关成员，moc 再生成实现。自定义信号、槽、属性、`qobject_cast` 等依赖这些元数据。派生自 `QObject` 但不使用新增元对象特性时可以不写；一旦声明 signals/`Q_PROPERTY` 等就应写。遗漏时可能表现为链接错误（如 vtable 相关）或元对象行为不符合预期。

### 7. moc 是编译器吗？

不是 C++ 编译器。moc 扫描含 `Q_OBJECT` 等宏的声明，生成额外 C++ 源码，再交给正常编译器编译。CMake 的 `AUTOMOC` 或 `qt_add_executable()` 通常自动完成这一步。

### 8. `QObject` 为什么不可复制？

`QObject` 表示有身份的对象：它可能处在父子树中、有连接、有动态属性、有线程亲和性。复制后这些身份关系没有清晰语义。因此 `QObject` 使用禁用复制机制；通常通过指针使用。需要复制的是对象承载的“值”，应拆成值类型、DTO 或隐式共享类。

### 9. `Q_PROPERTY` 有什么用？

它把 C++ 成员暴露为元对象属性，常见组成是 `READ`、`WRITE`、`NOTIFY`，还可有 `MEMBER`、`RESET`、`CONSTANT`、`FINAL`、`BINDABLE` 等。

```cpp
class User : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
public:
    QString name() const { return m_name; }
    void setName(const QString &v) {
        if (m_name == v) return;
        m_name = v;
        emit nameChanged();
    }
signals:
    void nameChanged();
private:
    QString m_name;
};
```

关键点：值没变时通常不要发 `NOTIFY`；QML 绑定依赖通知信号重新求值。只提供 getter 不等于属性可自动观察。

### 10. `QVariant` 与 `QMetaType` 是什么关系？

`QMetaType` 描述运行时类型；`QVariant` 是类型安全的通用值容器，依靠元类型信息存储和取回不同类型。自定义类型用于 `QVariant`、queued connection 或属性系统时，通常需要 `Q_DECLARE_METATYPE(T)`；某些按名称查找或跨线程排队场景还需 `qRegisterMetaType<T>()`。Qt 6 的部分 API 会自动注册类型，但不要把它泛化成“所有场景永远不必注册”。

### 11. `qobject_cast` 和 `dynamic_cast` 的区别？

`qobject_cast<T*>` 基于 Qt 元对象系统，目标通常是带 `Q_OBJECT` 的 `QObject` 类型，不依赖编译器 RTTI，并支持 Qt 接口声明机制。`dynamic_cast` 基于 C++ RTTI，适用于任意多态 C++ 层次。Qt 对象体系内通常优先 `qobject_cast`；普通 C++ 类型使用 `dynamic_cast`。

---

## 三、对象树、生命周期与资源管理

### 12. Qt 的父子对象机制是什么？

给 `QObject` 设置 parent 后，parent 析构时会删除 children；child 析构时会把自己从 parent 的 children 列表移除。因此任意析构顺序一般不会造成二次删除。它是**所有权/生命周期机制**，不等于 C++ 继承。

对 `QWidget`，父子关系还通常表示视觉层级、坐标系和裁剪关系；但这只是 QWidget 语义，不能推广到所有 QObject。

### 13. 栈对象使用 parent 有什么坑？

危险写法：先构造 child，后构造 parent，再把 parent 设给 child。C++ 会先析构后构造的 parent；parent 试图 `delete` 栈上的 child，产生未定义行为。

```cpp
QPushButton button;
QWidget window;
button.setParent(&window); // 危险：window 先析构并 delete &button
```

若 parent 先构造、child 后构造，析构顺序匹配，通常安全；但混用栈生命周期与动态所有权容易出错，面试中应说明构造顺序和所有权必须一致。

### 14. `delete` 与 `deleteLater()` 怎么选？

- 确认对象不在处理事件、且当前线程可以安全销毁时，可直接 `delete`。
- 不确定当前是否处于该对象的事件处理/信号回调栈，或需要跨线程安排销毁时，用 `deleteLater()`。它投递 DeferredDelete 事件，等控制权回到对象所属线程的事件循环后删除。
- 所在线程没有可运行的事件循环时，延迟删除不会按通常方式及时执行。工作线程常连接 `QThread::finished` 到 worker 的 `deleteLater()`。

### 15. `QPointer`、智能指针、parent 分别解决什么问题？

- parent-child：QObject 独占式生命周期管理。
- `QPointer<T>`：观察 QObject；对象析构后自动变 `nullptr`，不拥有对象。
- `QScopedPointer`/`std::unique_ptr`：明确独占所有权，适合没有 parent 的对象。
- `QSharedPointer`/`std::shared_ptr`：共享所有权，但不应随意与 parent 同时拥有同一对象，否则所有权冲突。
- `QWeakPointer`/`std::weak_ptr`：观察由对应 shared pointer 管理的对象；它和 `QPointer` 的管理模型不同。

### 16. 连接会自动断开吗？

sender 或 receiver 析构时，涉及它的连接会自动移除。带 context object 的 lambda/functor 连接也会在 context 销毁后断开。捕获裸指针的 lambda 若没有合适 context，仍可能访问悬空对象。

---

## 四、信号与槽

### 17. 信号槽的本质是什么？

它是类型安全的对象间通知机制。连接时建立关系；emit 时，Qt 遍历连接，根据连接类型直接调用槽或向目标线程投递元调用事件。它降低发送者对接收者的依赖，但不是“完全无成本的魔法”。

### 18. 新旧 `connect` 语法有何区别？

```cpp
// 推荐：编译期检查，可连接普通成员函数/lambda
connect(sender, &Sender::valueChanged,
        receiver, &Receiver::setValue);

// 旧式：字符串匹配，错误通常到运行时才暴露
connect(sender, SIGNAL(valueChanged(int)),
        receiver, SLOT(setValue(int)));
```

重载信号可用 `qOverload<int>(&QComboBox::currentIndexChanged)` 或显式 `static_cast` 消除歧义。

### 19. 信号与槽参数必须完全一致吗？

槽可以少接收信号末尾的一些参数，但已有参数必须兼容。函数指针语法会尽量在编译期检查。queued connection 还要求参数可被复制并能由元类型系统处理。

### 20. 一个信号连接多个槽时顺序如何？

通常按建立连接的顺序激活；但不应把复杂业务正确性建立在隐含顺序上，尤其涉及 queued connection 时，实际执行还取决于事件队列。需要严格顺序时，用一个协调槽明确串联。

### 21. 五种常见连接类型怎么解释？

| 类型 | 行为 | 主要风险/用途 |
|---|---|---|
| `AutoConnection` | emit 时判断；同线程相当于 Direct，跨线程相当于 Queued | 默认首选 |
| `DirectConnection` | 在发出信号的当前调用线程立即执行槽 | 跨线程直接操作 receiver 很危险 |
| `QueuedConnection` | 封装为事件，交给 receiver 所在线程事件循环 | 异步；接收线程需处理事件 |
| `BlockingQueuedConnection` | 类似 Queued，但发送线程等待槽执行完 | 同线程会死锁；跨线程也易互等死锁 |
| `UniqueConnection` | 防止重复连接，可与基础类型组合 | 对成员函数连接有效，不适用于任意 lambda/functor 去重 |

Qt 6 还有 `SingleShotConnection` 标志：调用一次后自动断开。

### 22. `AutoConnection` 判断的是 sender 和 receiver 的“对象归属线程”吗？

更精确的说法是：在信号发出时，根据**当前执行 emit 的线程**与 receiver 的线程亲和性决定直接还是排队。信号可能从非 sender 亲和线程被调用，所以仅比较两个对象的 `thread()` 会漏掉边界情况。

### 23. queued signal 的参数何时复制？能传引用吗？

调用被排队后，原调用栈很快结束，所以 Qt 必须保存参数副本。接口可以写 `const T&`，但队列中保存的是值语义副本；不要期待接收者看到发送后对原对象的修改。非 const 引用、不可复制类型通常不适合 queued connection。

### 24. emit 是异步的吗？

不一定。Direct 时 emit 会同步调用槽，所有槽返回后 emit 后一行才继续；Queued 时只是投递，emit 通常很快返回。Auto 取决于当时线程关系。因此“信号槽一定异步”是错误答案。

### 25. 如何避免重复连接？

保存并管理 `QMetaObject::Connection`、在重连前 `disconnect`，或对成员函数连接使用 `Qt::UniqueConnection`。不要反复进入页面就盲目 connect；重复连接会让一个 signal 触发多次槽。

### 26. lambda 连接最常见的生命周期错误是什么？

按引用捕获局部变量，槽晚于局部变量生命周期执行；或捕获已释放对象的裸 `this`/指针。优先给连接提供 context：

```cpp
connect(reply, &QNetworkReply::finished, this, [this, reply] {
    // this 销毁后连接自动断开
    reply->deleteLater();
});
```

若 `reply` 可能提前销毁，可捕获 `QPointer<QNetworkReply>`。

### 27. 信号槽性能慢吗？

它比直接的非虚函数调用有额外开销：查找连接、遍历、参数处理，queued 还会分配/投递事件。但对 GUI、I/O 业务通常不是瓶颈。高频像素/采样内循环应批处理、限频或改用直接数据通路；不要在没有测量前为了微小开销破坏架构。

---

## 五、事件系统与事件循环

### 28. 什么是事件循环？

`Q(Core|Gui|)Application::exec()` 持续从原生消息系统和 Qt 事件队列获取事件并分发。窗口输入、绘制、定时器、socket notifier、queued signal、`deleteLater()` 都依赖它。槽里长时间阻塞会导致界面卡死，本质是主线程无法返回事件循环。

### 29. signal/slot 与 event 有什么区别？

- event 通常是对某个 QObject 的底层消息，走 `event()` 再分派到 `mousePressEvent()`、`timerEvent()` 等虚函数。
- signal 是对象状态变化的广播，可有多个接收者，不要求继承重写。
- queued signal 的实现又依赖事件系统，因此二者不是互斥世界。

### 30. `sendEvent()` 和 `postEvent()` 的区别？

`sendEvent()` 同步调用目标对象的事件处理，返回时已经处理；`postEvent()` 把事件交给队列，稍后由目标线程事件循环处理，事件对象所有权转给 Qt。跨线程通知一般采用投递方式，不应跨线程同步调用 GUI 对象。

### 31. `event()`、具体事件处理器、event filter 的顺序？

大致是：应用/对象上的 event filter 先观察 → 对象的 `event()` → `event()` 按类型调用 `keyPressEvent()` 等具体处理器。filter 返回 `true` 表示事件已处理并停止继续传递；返回 `false` 让正常流程继续。

过滤器与被过滤对象需要在同一线程，否则不会按预期过滤。

### 32. `accept()`/`ignore()` 和 `event()` 返回值有什么区别？

事件对象的 accepted 状态常用于表示某个事件是否被接受，并影响向父控件传播等行为；`event()`/filter 的布尔返回值表示处理流程是否到此为止。具体语义依事件类型而异，不能把两者机械地当成同一个标志。

### 33. `update()` 与 `repaint()` 的区别？

`update()` 请求稍后重绘，Qt 能合并多次请求，通常应优先使用；`repaint()` 倾向立即同步绘制，频繁调用可能造成闪烁、递归或性能问题。真正绘制放在 `paintEvent()` 中，用 `QPainter`，不要依靠在任意位置临时画完就永久保留。

### 34. 为什么不推荐用 `processEvents()` 解决卡顿？

它会引入重入：当前操作尚未结束，用户输入、定时器甚至同一逻辑可能再次进入，破坏状态不变量。它还可能让 DeferredDelete 等行为变复杂。正确方案通常是把长任务拆片、异步化或放工作线程，用事件驱动更新进度。只有理解重入边界后才在受控场景使用。

### 35. 什么是嵌套事件循环？

模态对话框、某些同步等待写法会在当前调用栈未退出时再启动一个事件循环。此时其他事件仍可能运行，造成重入与意外生命周期变化。现代代码优先用异步完成信号，少用“开一个局部 `QEventLoop` 把异步伪装成同步”。

---

## 六、线程：面试最重要的一章

### 36. `QObject` 的线程亲和性是什么？

每个 QObject 关联一个线程，可由 `thread()` 查询。queued signal 和 posted event 会在对象所属线程处理。对象通常在创建它的线程中；无 parent 时可用 `moveToThread()` 改变亲和性。

三条约束必须会说：

1. child 必须和 parent 在同一线程，因此有 parent 的对象不能随便移动；移动 parent 会连同 children 一起移动。
2. 事件驱动对象（定时器、网络对象等）应在其所属线程创建、启动和销毁，并依赖该线程事件循环。
3. 跨线程直接调用 QObject 成员并不自动安全；用 queued signal、锁或文档明确标注为线程安全的 API。

### 37. `QThread` 对象属于哪个线程？

`QThread` 实例本身“生活在”创建它的旧线程；`run()` 才在新线程执行。因此 queued 到 `QThread` 对象的槽通常在创建者线程执行，不是在它管理的新线程执行。这是最高频陷阱之一。

### 38. 两种使用 QThread 的方式怎么选？

**Worker-object 模式（常规首选）**：创建 QObject worker，`moveToThread()`，通过 queued signal 调它；适合需要事件循环、定时器、网络和多个槽的任务。

```cpp
auto *thread = new QThread(this);
auto *worker = new Worker;               // 不设置 parent
worker->moveToThread(thread);
connect(thread, &QThread::started, worker, &Worker::start);
connect(worker, &Worker::finished, thread, &QThread::quit);
connect(thread, &QThread::finished, worker, &QObject::deleteLater);
connect(thread, &QThread::finished, thread, &QObject::deleteLater);
thread->start();
```

**继承 QThread 并重写 `run()`**：适合一个封闭的阻塞算法、无需 QObject 事件循环的任务。若要事件循环，`run()` 中需调用 `exec()`（默认实现会调用）。重写并非“错误”，错误是误以为 QThread 对象的槽自动在新线程。

### 39. `moveToThread()` 会立刻让正在运行的函数换线程吗？

不会。它改变对象后续 queued event 的投递目标，不会迁移当前调用栈或把普通成员调用变成异步调用。移动还要求对象没有 parent；通常由对象当前所属线程发起“push”，并要处理失败结果。移动过程中定时器会在目标线程重新注册，频繁移动可能推迟定时器。

### 40. 为什么在主线程创建 worker 的成员对象可能出问题？

如果成员是动态创建的 QObject，却没设置为 worker 的 child，那么移动 worker 时该成员不会自动移动，仍留在主线程。之后 worker 在新线程操作它就违反亲和性。解决方法：在目标线程的启动槽中创建资源，或确保合法 parent-child 关系随 worker 一起移动。

### 41. 如何安全停止线程？

采用协作取消：主线程调用 `requestInterruption()` 或发送停止信号；任务在合理粒度检查 `isInterruptionRequested()`，清理后退出；事件循环线程用 `quit()`/`exit()`；最后 `wait()` 等待完成。不要把 `terminate()` 当正常停止方式，它可能在持锁或资源状态不一致时强杀线程。

销毁仍在运行的 `QThread` 通常会导致错误；生命周期上要先请求退出并等待（Qt 特定工厂线程 API 的例外应按文档处理）。

### 42. `QMutex`、`QReadWriteLock`、`QSemaphore`、`QWaitCondition` 各自用途？

- `QMutex`：互斥访问共享状态；用 `QMutexLocker` 做 RAII。
- `QReadWriteLock`：读多写少且临界区足够大时可能受益。
- `QSemaphore`：管理 N 个同类资源/生产消费计数。
- `QWaitCondition`：在互斥锁保护的条件上等待，必须用循环重新检查谓词，防止虚假唤醒和状态已变化。
- 原子类型：适合简单计数/标志，不等于能保护复合不变量。

### 43. Qt 容器是线程安全的吗？

通常说法是“可重入，但同一实例并发读写不自动安全”。多个线程各自操作不同实例通常可以；对同一共享实例的写，或读写并发，需要同步。隐式共享并不让业务对象自动线程安全：引用计数可能是原子的，但容器内容的复合操作不是事务。

### 44. GUI 能在工作线程更新吗？

不应。`QWidget` 及其子类只能在主 GUI 线程使用。工作线程做计算/I/O，发信号把结果传回 GUI 线程更新界面。不要因为一次测试“没崩”就认为跨线程 UI 是安全的。

### 45. 跨线程信号为什么有时不执行？

排查顺序：接收者所属线程是否正确 → 该线程事件循环是否运行/是否被长任务阻塞 → 接收者是否已销毁 → 连接是否真的成功 → 参数元类型是否可排队 → 线程是否已经退出 → 是否错误地把耗时槽放到了 QThread 对象本身。

### 46. 如何避免线程死锁？

固定加锁顺序，缩小临界区，不在持锁时 emit 未知外部槽，不在持锁时等待另一线程，慎用 `BlockingQueuedConnection`，优先消息传递和不可变数据。发生卡死时抓所有线程栈，看“谁在等谁”，而不是只看 GUI 线程。

---

## 七、值类型、容器与隐式共享

### 47. 什么是隐式共享（Copy-on-Write）？

多个值对象复制时先共享同一数据块并增加引用计数；某个对象要修改时才 detach 并复制。这样传值往往便宜，同时保持值语义。`QString`、`QByteArray`、`QImage` 等许多 Qt 类型采用此机制。

```cpp
QImage a("photo.png");
QImage b = a;       // 通常共享数据
b.setPixelColor(0, 0, Qt::red); // b 写入前分离
```

### 48. 隐式共享有哪些性能陷阱？

- 以为复制永远是 O(1)：首次写会产生深拷贝，可能在延迟敏感路径突然变贵。
- 非 const API 可能触发 detach；只读访问优先 const。
- STL 风格迭代器跨复制/修改可能遇到“隐式共享迭代器问题”，不要在容器被复制或修改后继续依赖旧迭代器。
- 多线程共享后分别只读通常容易处理；任何写入都要重新评估同步和分离成本。

### 49. `QString`、`QByteArray`、`QStringView` 如何选？

- `QString`：Unicode 文本（Qt 6 中以 UTF-16 code units 表示），不要用下标把一个 `QChar` 当成完整“用户可见字符”；代理对、组合字符需专门处理。
- `QByteArray`：字节数据、协议、文件原始内容，不代表某种固定文本编码。
- `QStringView`/`QByteArrayView`：不拥有数据的只读视图，减少分配；必须保证底层数据活得足够久。

边界转换要显式：如 `QString::fromUtf8()`、`toUtf8()`，避免依赖本地 8 位编码。

### 50. Qt 容器与 STL 容器如何选？

Qt 容器与 Qt API、隐式共享、元类型结合自然；STL 容器与标准算法和跨库接口更自然。现代 Qt 6 两者互操作已较好。团队一致性、API 边界和性能测量比“绝对谁更快”重要。Qt 6 中 `QList` 与 `QVector` 的实现关系已改变，不能照搬老版 Qt 的性能口诀。

---

## 八、Widgets、布局与绘制

### 51. QWidget 的 paint 流程是什么？

窗口系统或 `update()` 产生绘制请求，Qt 合并脏区域并发送 `QPaintEvent`，控件在 `paintEvent()` 中用 `QPainter` 绘制。`QPainter` 的状态（笔、刷、变换、裁剪）可 `save()`/`restore()`。大量自定义项若每项都是 QWidget，可能有较高对象成本，可考虑 Model/View、Graphics View 或 Qt Quick。

### 52. 布局管理器为什么比固定坐标好？

布局会处理窗口缩放、字体变化、翻译后文本长度、风格与 DPI。尺寸由 `sizeHint()`、`minimumSizeHint()`、size policy、stretch 等共同决定。固定像素坐标在不同平台和高 DPI 下容易错位。

### 53. `show()`、`hide()`、`close()` 有何区别？

`show/hide` 改可见性；`close()` 发送 close event，处理器可以拒绝。窗口关闭后是否销毁取决于所有权以及 `WA_DeleteOnClose` 等设置，不能认为 `close()` 等于 `delete`。

### 54. 模态与非模态对话框怎么选？

`exec()` 启动局部事件循环并同步返回结果，容易产生嵌套事件循环和重入；`open()`/`show()` 配合 finished 信号是异步方式，通常更易组合。需要阻塞式用户流程时可用模态，但要理解它并未阻塞整个 GUI 事件处理。

### 55. 高 DPI 常见问题是什么？

区分设备无关像素与物理像素；避免到处硬编码尺寸；提供合适倍率的图像或矢量资源；自绘时理解 device pixel ratio；多屏幕可能有不同缩放比例。Qt 6 默认启用高 DPI 缩放语义，但资源和自绘仍需正确处理。

---

## 九、Model/View/Delegate

### 56. Qt Model/View 架构是什么？

- Model：数据与访问接口，核心是 `rowCount()`、`columnCount()`、`data()`，可编辑时实现 `setData()`、`flags()` 等。
- View：展示、选择、滚动，如 `QListView`、`QTableView`、`QTreeView`。
- Delegate：单元格绘制与编辑器创建，默认 `QStyledItemDelegate`。
- Selection model：独立管理当前项和选区，可被多个 view 共享。

它避免把数据复制进每个 item widget，适合大数据与多视图。

### 57. `QModelIndex` 是什么？能长期保存吗？

它是模型中一个位置的轻量临时句柄，可带 row/column、model 和 internal id/pointer。模型结构变化后普通 `QModelIndex` 可能失效；需要跨结构变化保存时考虑 `QPersistentModelIndex`，但模型 reset 等仍会使其失效，且大量持有有成本。

### 58. 自定义 model 插入/删除数据为何必须调用 begin/end？

`beginInsertRows()`/`endInsertRows()` 等让 view、selection 和 persistent index 在结构变化前后正确更新。只修改底层容器而不通知，界面与索引状态会失配。普通数据值变化发 `dataChanged()`；整体重建才考虑 `beginResetModel()`/`endResetModel()`，不要动辄 reset。

### 59. `dataChanged()` 应怎样发？

给出准确的左上/右下索引和发生变化的 roles，减少无效刷新。频繁逐单元格发信号可能拖慢 UI，可按连续区域批量合并，但不能漏通知。

### 60. 如何实现排序和过滤？

通常用 `QSortFilterProxyModel` 放在源模型与 view 之间。view 拿到的是 proxy index；访问源模型时要 `mapToSource()`，反向则 `mapFromSource()`。代理可以链式组合，但要注意映射和性能。

### 61. delegate 的绘制和编辑流程？

显示时 view 调 delegate 的 `paint()` 和 `sizeHint()`；编辑时一般经过 `createEditor()` → `setEditorData()` → 用户编辑 → `setModelData()` → `destroyEditor()`。若只是画按钮外观，不应为每个 cell 创建真正的 QWidget；可在 delegate 中绘制并处理 editor event。

### 62. 大数据模型如何优化？

按需加载（`canFetchMore/fetchMore`）、避免 `data()` 中做昂贵 I/O、缓存适当的派生数据、精确发变化信号、减少 reset、按块更新、避免为每个单元格创建控件。先 profile，区分模型耗时、delegate 绘制和数据库查询。

---

## 十、QML 与 C++ 集成

### 63. QML 的属性绑定是什么？

属性可以由表达式计算；依赖属性变化时绑定重新求值。给目标属性直接赋一个普通值通常会移除原绑定。复杂、频繁或业务关键逻辑应放 C++，QML 保持声明式 UI 与轻量交互。

### 64. 如何把 C++ 类型暴露给 QML？

常见方式：

- 注册可实例化类型（如 `QML_ELEMENT` 配合构建系统，或 `qmlRegisterType`）；
- 注册单例；
- 暴露一个已存在的 QObject；
- 用 `QAbstractItemModel` 向列表/表格提供数据。

大项目优先显式模块和类型注册，少依赖全局 context property，因为后者依赖不透明、工具支持和复用性较弱。

### 65. QML 如何观察 C++ 数据变化？

通过带 `NOTIFY` 的 `Q_PROPERTY`、信号、或模型角色。属性 setter 值变化后发通知；模型数据变化发 `dataChanged()` 并带正确 role。没有通知时，QML 初次能读取但后续通常不知道何时更新。

### 66. QML 对 C++ QObject 的所有权怎么判断？

不能用一句“QML 都会自动删除”概括。所有权取决于对象创建方式、parent、注册/工厂返回方式和显式 ownership 设置。C++ 持有的长寿命对象通常设置清晰 parent 或声明 C++ ownership；不要让 C++ 与 QML 都认为自己是唯一所有者。

### 67. QML 性能问题如何排查？

关注过度绑定求值、创建过多 delegate、不可见对象仍执行动画/定时器、JavaScript 大计算、频繁跨 C++/QML 边界、图片体积、场景图过度绘制。使用 QML Profiler 和渲染诊断，不能只凭“QML 慢”猜测。

---

## 十一、网络、I/O、数据库与序列化

### 68. Qt 网络为什么通常不需要“一请求一线程”？

`QNetworkAccessManager`、socket 等是事件驱动 API，发起请求后立即返回，完成时通过 signal 通知；主线程可同时处理 UI。只有请求完成后的 CPU 密集解析/计算才可能需要线程池。

### 69. `QNetworkAccessManager` 应如何管理？

通常按合适作用域长期复用，而不是每次请求临时创建；它管理连接复用、代理、cookie 等。`QNetworkReply` 是每次请求对象：处理 finished/error、检查 HTTP 状态和网络错误，读取数据后 `deleteLater()`。不要在等待 reply 时用局部死循环或阻塞主线程。

### 70. TCP 为什么会“粘包”？

TCP 是字节流，没有应用消息边界。一次 `write()` 不保证对应一次 `readyRead()`；接收端要维护缓冲区，并按长度前缀、分隔符或固定头协议持续解析完整帧，同时处理半包、多包、非法长度和最大包限制。

### 71. 文件写入如何避免损坏？

配置类文件可用 `QSaveFile`：写临时文件，成功后 `commit()` 原子替换，降低崩溃留下半文件的风险。大文件要分块读写；文本要明确编码；错误分支检查 `open/read/write/commit` 返回值。

### 72. Qt SQL 的线程规则是什么？

数据库连接有线程归属约束：连接应在使用它的线程中创建和使用，不要把同一个 `QSqlDatabase` 连接跨线程并发用。通常每个线程建立独立连接并使用唯一 connection name；线程结束前关闭查询与连接。事务用 RAII/明确错误路径保证提交或回滚。

### 73. Qt 序列化要注意什么？

`QDataStream` 是二进制流，通信双方要约定 stream version、字节序和数据结构版本；不要假定不同 Qt 版本默认格式永远相同。JSON 可读、跨语言，但体积与解析成本更高。反序列化不可信数据要限制长度、深度和类型，防止内存耗尽。

---

## 十二、构建、资源、插件与部署

### 74. qmake 与 CMake 怎么看？

qmake 是 Qt 传统构建工具；Qt 6 官方项目通常推荐 CMake。要会 `find_package`、target-based linking、AUTOMOC/AUTOUIC/AUTORCC，以及不要依赖全局 include/link 状态。维护 Qt 5 老项目仍可能遇到 `.pro`。

### 75. `.ui`、`.qrc`、moc/uic/rcc 分别是什么？

- `.ui`：Designer XML，由 uic 转成 C++ UI 代码。
- `.qrc`：资源清单，由 rcc 编译进程序或二进制资源。
- moc：生成元对象代码。
- uic：处理 UI 表单。
- rcc：处理资源。

资源以 `:/prefix/file` 或 `qrc:/...`（常见于 QML/URL）访问。资源编进程序后是只读的；用户配置应写到标准可写目录，而非资源路径。

### 76. Qt 插件机制核心是什么？

定义纯虚接口，使用 `Q_DECLARE_INTERFACE` 声明接口标识；插件 QObject 实现接口并使用 `Q_PLUGIN_METADATA`；宿主用 `QPluginLoader` 加载，再 `qobject_cast` 到接口。部署时要保证 ABI、编译器/架构、Qt 版本和 debug/release 兼容。

### 77. 程序在开发机运行、客户机启动失败怎么排查？

检查 Qt 动态库、平台插件（Windows 常见 `platforms/qwindows`）、图像/TLS/数据库插件、编译器运行库、架构、环境变量、QML imports。使用 `windeployqt`、`macdeployqt` 等部署工具作为起点，再在干净环境验证。不要简单把整个 Qt 安装目录复制过去。

### 78. 静态链接一定更简单吗？

不一定。它能减少运行时动态库，但会增加体积和构建复杂度，插件需静态导入，还涉及 Qt 许可证合规。商业发布必须根据 LGPL/GPL/商业许可证及第三方组件条款做合规评估；面试中不要给法律结论式承诺。

---

## 十三、设计、性能与调试题

### 79. 如何设计“后台加载，界面显示进度，可取消”？

1. GUI 线程只负责发起、展示和最终合并结果。
2. I/O 若已有异步 API，优先事件驱动；CPU/阻塞任务放 worker 或 `QThreadPool`。
3. 进度用 queued signal 限频上报，避免每条数据都刷新 UI。
4. 取消使用原子/中断请求，任务在安全点检查。
5. 结果使用值对象或清晰所有权传回；退出时先取消、等待，再释放依赖。
6. 明确错误、取消、成功三种终态，保证只完成一次。

### 80. 如何处理每秒几万条数据而不让 UI 卡顿？

后台接收后批处理；使用有上限队列提供背压；只把 UI 需要的摘要/最新快照传回；按 16–100ms 节流刷新；模型用批量插入和精确信号；曲线做降采样/可视范围裁剪。关键是控制生产速率、内存上限和渲染频率，而非简单“再开一个线程”。

### 81. Qt 程序内存不断增长怎么查？

先区分泄漏与缓存/事件积压：

- QObject 是否有 parent，或 finished 分支是否都 `deleteLater()`；
- reply、timer、窗口、模型是否重复创建；
- queued signal 是否生产快于消费，事件队列持续增长；
- 图片/网络/QML 缓存是否有上限；
- lambda 捕获和 shared pointer 是否形成循环；
- 用 ASan、LeakSanitizer、Valgrind（平台适用时）、系统 profiler 和 QObject 日志验证。

### 82. UI 卡死如何定位？

卡住时抓主线程栈。常见原因：槽内 CPU 长任务、同步网络/文件/数据库、等待 worker、死锁、无限循环、海量绘制或事件风暴。若主线程停在 `wait()`/锁上，继续查看持锁线程；若事件循环正常但帧慢，用 profiler 分析 paint/model/QML 帧。

### 83. 崩溃只在退出时发生，最可能是什么？

对象析构顺序、线程未停、重复所有权、延迟事件访问已释放对象、全局/静态 QObject 在 QApplication 后析构、插件先卸载但对象仍存活。建立显式 shutdown 顺序：停止新任务 → 断开外部输入 → 请求线程退出 → 等待 → 销毁线程资源 → 最后销毁上层对象。

### 84. 如何做 Qt 单元测试？

用 Qt Test 的 `QTest`、`QCOMPARE/QVERIFY`、数据驱动测试；信号用 `QSignalSpy`。异步测试等待明确条件并设置超时，避免固定 `qWait(5000)` 让测试又慢又脆。GUI 测试要区分模型/业务单测与少量端到端交互测试。

### 85. 如何让 Qt 代码可测试？

把业务状态机与 QWidget/QML 分开；依赖通过接口或构造函数注入；把时间、网络、文件系统边界封装；用信号报告结果但避免全局 singleton 泛滥；模型可以脱离真实 view 测试。测试结构通常也是良好生产结构。

---

## 十四、常见“判断题陷阱”

下面每句都应能立即判断：

1. **“信号槽一定异步。”** 错；Direct 同步，Auto 取决于 emit 时线程关系。
2. **“QThread 对象的槽运行在它创建的新线程。”** 错；QThread 对象通常属于创建它的线程。
3. **“把对象 moveToThread 后，普通成员调用会自动去新线程。”** 错；普通调用就在调用者当前线程执行。
4. **“QObject 有 parent，所以可以在任意线程调用。”** 错；parent 管生命周期，不提供线程安全。
5. **“隐式共享使 Qt 容器线程安全。”** 错；引用计数安全不等于内容操作安全。
6. **“close() 一定删除窗口。”** 错；是否删除由所有权和属性决定。
7. **“deleteLater() 马上删除。”** 错；它依赖 DeferredDelete 事件与事件循环。
8. **“QTimer 开了一个后台线程。”** 错；超时由所属线程事件循环分发。
9. **“TCP 一次 readyRead 对应发送端一次 write。”** 错；TCP 没有消息边界。
10. **“槽必须接收信号的全部参数。”** 错；槽可以忽略尾部参数。
11. **“所有 Qt 类都继承 QObject。”** 错；大量值类型如 `QString`、`QImage` 不是 QObject。
12. **“有事件循环就不会卡 UI。”** 错；长槽阻塞事件循环仍会卡。
13. **“processEvents 是解决卡顿的标准方案。”** 错；它可能引入重入，优先拆分或异步。
14. **“QML 属性读得到，就会自动随 C++ 值更新。”** 错；一般需要 NOTIFY/bindable/模型通知。
15. **“Model 数据变了，View 会自动扫描出来。”** 错；必须发正确变化信号。
16. **“Qt 资源路径可写。”** 错；编译资源通常只读。
17. **“一个数据库连接可以随便跨线程使用。”** 错；连接应遵守创建/使用线程约束。
18. **“重写 QThread::run 永远是错误设计。”** 错；封闭任务可以，关键是理解对象亲和性和是否需要事件循环。
19. **“UniqueConnection 能可靠去重任意 lambda。”** 错；主要支持成员函数连接的唯一性检查。
20. **“Qt 6 与 Qt 5 API/行为完全相同。”** 错；模块、容器、正则、编码、构建和弃用 API 都有差异。

---

## 十五、代码审查题：找出问题

### 题 1：线程其实没有切过去

```cpp
class MyThread : public QThread {
public slots:
    void doWork() { heavyWork(); }
};

MyThread *t = new MyThread;
connect(button, &QPushButton::clicked, t, &MyThread::doWork);
t->start();
```

**问题**：`t` 对象属于创建它的 GUI 线程；clicked 到 `doWork` 很可能直接在 GUI 线程执行，照样卡界面。应使用 worker-object，或把封闭算法写进 `run()` 并通过安全接口传入数据。

### 题 2：线程中创建了错误 parent 的对象

```cpp
void MyThread::run() {
    QTcpSocket socket(this);
    exec();
}
```

**问题**：`this`（QThread 对象）属于创建线程，而 `socket` 在新线程创建，parent 与 child 不能位于不同线程。局部 socket 不设此 parent；或使用 worker-object。

### 题 3：跨线程 lambda 被强制 Direct

```cpp
connect(worker, &Worker::resultReady, label, [label](QString s) {
    label->setText(s);
}, Qt::DirectConnection);
```

**问题**：若 signal 从工作线程发出，lambda 会在工作线程操作 QWidget。使用 Auto/Queued，并把 `label`（或拥有它的窗口）作为 context；捕获可进一步用安全生命周期策略。

### 题 4：网络 reply 泄漏

```cpp
auto *reply = manager->get(request);
connect(reply, &QNetworkReply::finished, [reply] {
    qDebug() << reply->readAll();
});
```

**问题**：没有 context，且 reply 未释放。连接应提供 context，处理所有完成路径后 `reply->deleteLater()`，并检查错误与 HTTP 状态。

### 题 5：模型悄悄改了数据

```cpp
items.push_back(x);
```

**问题**：view 不知道结构变化。插入前后调用 `beginInsertRows/endInsertRows`，并保证范围、parent index 与实际容器变化一致。

### 题 6：局部事件循环把异步变同步

```cpp
QEventLoop loop;
connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
loop.exec();
```

**问题**：虽然 UI 未必完全冻结，却引入嵌套事件循环、重入、超时和生命周期难题。优先把后续逻辑放进 finished 回调或 coroutine/状态机式异步流程。

---

## 十六、Qt 5 与 Qt 6 面试时值得主动提的差异

- Qt 6 项目主推 CMake；Qt 5 老项目常见 qmake。
- Qt 6 对高 DPI 的默认行为更统一，Qt 5 中常见的某些应用属性不再需要或已变化。
- Qt 6 `QList` 与 `QVector` 的实现/性能关系变化，旧版“QList 节省移动成本”等经验不能直接套用。
- Qt 5 的 `QRegExp` 已被更现代的 `QRegularExpression` 取代（后者其实从 Qt 5 已提供，Qt 6 清理旧 API）。
- 文本编码相关旧 API 有清理和变化；边界处应明确 UTF-8/UTF-16，而不是依赖隐式本地编码。
- Qt 6 清理了大量废弃 API，部分模块迁移到 Add-ons 或更换实现。迁移时应先读 Qt 6 porting guide，再逐项修复编译错误和行为变化。
- 具体小版本也会增加 API，例如新的连接标志或 `QThread` 便利接口；回答时区分“长期稳定概念”和“版本特定函数”。

---

## 十七、30 道快速自测题（建议口述）

1. 为什么 QObject 不可复制？
2. `Q_OBJECT`、moc、`QMetaObject` 的关系？
3. parent-child 与 C++ 智能指针各解决什么？
4. 栈上 child 设置 parent 为什么可能崩溃？
5. `deleteLater()` 依赖什么？线程退出时怎么清理 worker？
6. `QPointer` 与 `QSharedPointer` 的本质区别？
7. emit 是同步还是异步？
8. AutoConnection 的判断时机和依据？
9. BlockingQueuedConnection 为什么会死锁？
10. queued 参数为什么需要值语义/元类型？
11. event filter 返回 true 表示什么？
12. `sendEvent` 与 `postEvent` 区别？
13. 为什么槽里 sleep 会卡界面？
14. 为什么少用 `processEvents()`？
15. QThread 对象和 `run()` 分别在哪个线程？
16. worker-object 模式的正确销毁链是什么？
17. `moveToThread` 有 parent 时为何失败？
18. 跨线程普通函数调用安全吗？
19. GUI 为什么必须留在主线程？
20. 隐式共享解决了什么，detach 何时发生？
21. `QString` 与 `QByteArray` 的边界是什么？
22. `QModelIndex` 为什么不能随便长期缓存？
23. 插入 model 行时必须发哪些通知？
24. proxy index 如何转换到 source index？
25. QML 绑定为什么不更新？
26. QNetworkReply 谁释放？
27. TCP 如何定义消息边界？
28. 数据库连接为什么通常每线程一个？
29. 客户机缺平台插件时怎样排查？
30. 退出时崩溃的标准排查顺序是什么？

若能不看答案、每题用 30–90 秒讲出“机制 + 坑 + 实例”，已经能覆盖多数中初级 Qt 面试；高级岗位还要结合图形栈、平台层、性能剖析、架构和真实项目深挖。

---

## 十八、项目深挖：比背题更重要

面试官常沿着项目追问。至少准备一个 STAR 风格案例，并回答：

- 为什么选 Widgets/QML，而不是另一个？
- 对象由谁创建、谁销毁，异常路径是否一致？
- 哪些代码在哪个线程，线程如何退出？
- 数据量、刷新频率、时延、内存占用是多少？优化前后如何测量？
- 网络断线、半包、超时、重试、幂等如何处理？
- 模型如何通知 view，为什么没有使用 item-based widget？
- 怎样复现和定位最难的崩溃/卡顿？
- Qt 版本、编译器、部署平台、构建方式是什么？
- 做过哪些自动化测试？失败时如何保留日志和上下文？

好的回答不是“用了多线程所以快”，而是“主线程 50ms 一次批量刷新；worker 每批解析 1000 条；有界队列最多 20 批，满时丢弃旧遥测快照；通过 profiler 将 99 分位 UI 卡顿从 180ms 降到 24ms”。数字、权衡和失败处理最能体现真实经验。

---

## 十九、七天复习路线

**第 1 天：对象模型**——自己写 QObject + property + signal/slot；解释 moc、对象树、`QPointer`。

**第 2 天：事件循环**——写 event filter、自定义 event、timer；比较 send/post、update/repaint。

**第 3 天：线程**——分别实现 worker-object 和 `run()` 任务；验证槽实际线程；实现协作取消与安全退出。

**第 4 天：Model/View**——实现可编辑 table model、排序过滤、批量插入；用 Model Test 检查不变量（如环境可用）。

**第 5 天：网络与存储**——写异步 HTTP、TCP 长度帧解析、每线程数据库连接；覆盖错误与超时。

**第 6 天：Widgets/QML**——按目标岗位重点练一个；检查高 DPI、属性通知、delegate 性能。

**第 7 天：模拟面试**——口述上面 30 题；准备两个项目案例；做一次退出崩溃和 UI 卡顿的栈分析演练。

---

## 二十、权威资料索引

优先看官方文档；博客适合补案例，但涉及线程、生命周期和版本行为时应回到对应版本官方文档核对。

- [Qt Object Model](https://doc.qt.io/qt-6/object.html)
- [QObject Class](https://doc.qt.io/qt-6/qobject.html)
- [Signals & Slots](https://doc.qt.io/qt-6/signalsandslots.html)
- [Threads and QObjects](https://doc.qt.io/qt-6/threads-qobject.html)
- [QThread Class](https://doc.qt.io/qt-6/qthread.html)
- [The Event System](https://doc.qt.io/qt-6/eventsandfilters.html)
- [Implicit Sharing](https://doc.qt.io/qt-6/implicit-sharing.html)
- [Model/View Programming](https://doc.qt.io/qt-6/model-view-programming.html)
- [Overview: QML and C++ Integration](https://doc.qt.io/qt-6/qtqml-cppintegration-overview.html)
- [Qt Network](https://doc.qt.io/qt-6/qtnetwork-index.html)
- [Qt SQL](https://doc.qt.io/qt-6/qtsql-index.html)
- [The Qt Resource System](https://doc.qt.io/qt-6/resources.html)
- [Build with CMake](https://doc.qt.io/qt-6/cmake-manual.html)
- [High DPI](https://doc.qt.io/qt-6/highdpi.html)
- [Qt 6 Porting Guide](https://doc.qt.io/qt-6/portingguide.html)
- [Qt Test Overview](https://doc.qt.io/qt-6/qtest-overview.html)

### 最后记住的一句话

遇到陌生 Qt 题，先问三个问题：**这个对象归谁所有？它属于哪个线程？这段动作靠哪个事件循环发生？** 大多数生命周期、卡顿、崩溃和线程题都能沿这三条线推出来。
