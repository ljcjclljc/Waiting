---
{
  "title": "C++20 协程如何简化 Web 服务",
  "slug": "coroutines-in-cpp-web-services",
  "date": "2026-07-11",
  "updated": "2026-07-18",
  "excerpt": "用顺序写法表达异步操作，并保留事件循环的吞吐能力。",
  "category": { "name": "C++ 实践", "slug": "cpp" },
  "tags": [
    { "name": "Drogon", "slug": "drogon" },
    { "name": "C++20", "slug": "cpp20" },
    { "name": "性能", "slug": "performance" }
  ],
  "featured": false,
  "draft": false
}
---
协程不是线程。它让异步控制流更容易阅读，同时避免阻塞 Drogon 的 I/O 线程。

## 基本原则

1. 网络和数据库调用优先使用协程接口。
2. 不在事件循环中执行长时间的阻塞操作。
3. 明确处理异常、取消和超时。

```cpp
drogon::Task<drogon::HttpResponsePtr> article(std::string slug)
{
    auto post = co_await loadPost(slug);
    co_return render(post);
}
```

对于本博客，Markdown 在请求中读取和渲染。文章规模较小时实现简单直接；流量和内容数量增长后，可以按文件修改时间建立内存缓存，并在部署启动阶段完成预热。
