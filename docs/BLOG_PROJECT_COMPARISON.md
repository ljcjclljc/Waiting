# 同类博客项目对比与取舍

调研日期：2026-07-19。Star 与 fork 数量来自调研时的 GitHub 页面，只用于衡量社区成熟度，会随时间变化。

## 对比结果

| 项目 | 技术与规模 | 主要功能 | 优点 | 不适合直接照搬的部分 |
| --- | --- | --- | --- | --- |
| [Butterfly](https://github.com/jerryc127/hexo-theme-butterfly) | Hexo 主题，约 8.3k stars | 目录、相关文章、上下篇、代码复制、搜索、分享、评论、归档、PWA、懒加载 | 功能完整，视觉表达和主题能力强 | 配置项和第三方脚本较多，容易增加首屏负担 |
| [NexT](https://github.com/theme-next/hexo-theme-next) | Hexo 主题，约 8.3k stars | 目录、相关文章、代码复制、阅读进度、书签、PJAX、搜索、评论 | 成熟稳定，文档完善，长文阅读体验好 | 历史配置较复杂，PJAX 会增加页面状态维护成本 |
| [PaperMod](https://github.com/adityatelange/hugo-PaperMod) | Hugo 主题，约 13.8k stars | Open Graph、Twitter Cards、Schema.org、搜索、归档、面包屑、分享、相关文章、代码复制 | 构建快、SEO 完整、页面克制且轻量 | 默认视觉个性较弱，动态能力依赖额外服务 |
| [AstroPaper](https://github.com/satnaing/astro-paper) | Astro 博客，约 4.9k stars | 无障碍、SEO、Pagefind、归档、分页、RSS、sitemap、折叠目录 | 现代、轻量，键盘和读屏体验扎实 | 依赖 Astro 构建链，功能广度小于 Butterfly |
| [GodplaceBlog](https://github.com/Godplace-g7/--GodplaceBlog--) | Spring Boot、Vue 3、Redis、RabbitMQ | 文章后台、标签、评论、图片资源和完整动态业务 | 管理功能全面，适合多用户内容系统 | 架构较重，登录与后台违背当前 Git 驱动发布模型 |
| [Chirpy](https://github.com/cotes2020/jekyll-theme-chirpy) | Jekyll 技术博客，约 10.2k stars | 置顶文章、层级分类、热门标签、目录、数学公式、Mermaid、搜索与 SEO | 技术文章的信息层级清楚，桌面与移动导航成熟 | 固定侧栏和大量集成功能会分散长文注意力 |
| [Fluid](https://github.com/fluid-dev/hexo-theme-fluid) | Hexo Material 主题，约 8.2k stars | 懒加载、多代码高亮、暗色模式、脚注、LaTeX、Mermaid、本地搜索 | 图片与内容过渡自然，中文排版和配置文档完善 | 大图与 Material 动效使用过多时首屏成本偏高 |
| [Stack](https://github.com/CaiJimmy/hugo-theme-stack) | Hugo 卡片式主题，约 6.4k stars | 卡片文章流、侧栏资料、分类标签与响应式布局 | 摘要扫描效率高，内容入口直观 | 卡片和侧栏占据较多宽度，不适合直接套到当前开放式阅读布局 |

GitHub 上能找到的 Drogon 博客示例大多是低关注度的 CRUD API 演示，需要数据库、账户和管理接口。它们没有提供比当前项目更成熟的内容发布或阅读体验，因此不作为代码基础。

## 当前博客的定位

本项目保留 Drogon 服务端渲染，同时采用接近 Hexo/Hugo 的发布模型：

```text
本地 Markdown -> Git 提交 -> GitHub Actions 构建 -> Drogon 容器发布
                                      \-> Giscus / GitHub Discussions 评论
```

浏览器端没有文章新增、编辑或删除接口；访客只能阅读和评论。这样既保留了 Drogon 的动态路由与服务端渲染，也让文章变更始终经过 Git 历史。

## 已吸收的能力

- 参考 PaperMod 与 AstroPaper：补齐 canonical、Open Graph、Twitter Cards、文章语义化 Schema.org、独立归档页和全量 sitemap。
- 参考 Butterfly 与 NexT：加入代码复制、上一篇/下一篇、相关文章、文章目录和阅读进度。
- 参考 Chirpy、Fluid 与 Stack：采用滚动可用的筛选栏、编号式文章索引、全宽内容带和更明确的长文层级，但不引入固定三栏或厚重卡片。
- 延续当前设计：主页固定雨景视频，内容随页面滚动；文章、搜索、分类和归档页保持纯色阅读背景。
- 修正细节：分类和标签页显示真实名称；未知路由使用自定义 404；回到顶部按钮不再被页脚覆盖。

## 明确不引入

- 登录、管理员后台或浏览器写文章接口。
- PostgreSQL、Redis、RabbitMQ 等当前业务不需要的运行依赖。
- PJAX、PWA、多套评论系统和第三方分享脚本。
- 会跟踪访客或拖慢首屏的第三方统计脚本。

这些取舍让功能提升集中在阅读、发现、SEO 和可访问性上，不破坏“内容只能从本地 Markdown 与 GitHub 提交发布”的核心约束。
