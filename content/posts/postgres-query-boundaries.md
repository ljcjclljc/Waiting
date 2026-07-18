---
{
  "title": "为 PostgreSQL 查询划清边界",
  "slug": "postgres-query-boundaries",
  "date": "2026-07-04",
  "updated": "2026-07-18",
  "excerpt": "把 SQL 集中到 Repository，让 Controller 只处理 HTTP，让 Service 保留业务规则。",
  "category": { "name": "网络编程", "slug": "networking" },
  "tags": [
    { "name": "PostgreSQL", "slug": "postgresql" },
    { "name": "系统设计", "slug": "systems-design" }
  ],
  "featured": false,
  "draft": false
}
---
一个可维护的服务应避免在模板和 Controller 中散落 SQL。

> Controller 处理协议，Service 处理规则，Repository 处理数据。

这个博客的新内容模型不再需要 PostgreSQL：文章由 Git 管理，评论由 GitHub Discussions 管理。保留这篇文章，是因为同样的边界原则仍然适用于其他动态业务模块。

## 什么时候仍然需要数据库

用户账户、订单、实时协作或需要服务端聚合的数据适合进入数据库。只读文章并不需要为了“动态”而强行写入数据库；Drogon 可以动态读取文件并完成服务端渲染。
