---
{
  "title": "用 Drogon 构建一个 Git 驱动的博客",
  "slug": "building-a-drogon-blog",
  "date": "2026-07-17",
  "updated": "2026-07-19",
  "excerpt": "让 Markdown 成为唯一内容源，用 Drogon 完成动态渲染，再由 GitHub 管理发布与评论。",
  "category": { "name": "系统设计", "slug": "systems" },
  "tags": [
    { "name": "Drogon", "slug": "drogon" },
    { "name": "GitHub", "slug": "github" },
    { "name": "C++20", "slug": "cpp20" }
  ],
  "featured": true,
  "draft": false,
  "cover": "/images/drogon.jpg",
  "seoDescription": "使用 Drogon、Markdown、GitHub Actions 和 Giscus 构建只读文章、开放评论的个人博客。"
}
---
这个博客没有管理后台，也不把文章写进数据库。`content/posts` 中的 Markdown 文件是唯一事实来源：本地写作、Git 提交、推送到 GitHub，然后由自动化流程构建和发布。

## 为什么保留服务端渲染

- 首屏直接返回完整 HTML，对搜索引擎和弱网更友好。
- 页面在 JavaScript 失败时仍然可以阅读。
- Drogon 的路由、缓存头和异步 I/O 仍然适合承载动态站点。
- 内容版本由 Git 保存，文章的每一次变化都可以审查和回退。

```cpp
auto post = repository.findPublishedBySlug(slug);
post["contentHtml"] = MarkdownService::render(post["content"].asString());
```

## 发布边界

网站不提供新增、修改或删除文章的 HTTP 接口。访客只能读取已经推送并部署的文章；评论交给 Giscus，数据保存在 GitHub Discussions 中。

这种边界很接近 Hexo 的写作体验，但运行时仍然是 Drogon：Hexo 的“内容即文件”与 Drogon 的动态路由可以同时存在。
