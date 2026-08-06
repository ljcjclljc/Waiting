# 晨's Blog：Drogon + GitHub 博客

> 在代码与晨光之间，记录 C++、网络与系统世界的思考。

晨's Blog 是一个采用 Hexo 式发布模型的 Drogon 动态博客。文章以本地 Markdown 文件为唯一内容源，直接发布到服务器进行热加载；同一内容推送到 GitHub 作为备份与版本历史，GitHub Actions 只构建容器镜像。网站不提供文章管理后台，访客只能阅读内容，并通过 Giscus 在 GitHub Discussions 中评论。

## 架构

```text
content/posts/*.md -> local publish script -> server content hot reload
content/posts/*.md -> git push -> GitHub backup/history
                                          \-> GitHub Discussions / Giscus 评论
```

- Drogon：路由、服务端渲染、搜索、分类、标签、归档、RSS 和站点地图。
- Markdown：文章唯一数据源，使用严格 JSON Front Matter。
- GitHub：代码与内容版本、构建触发、容器发布和评论数据。
- PostgreSQL：不再是博客运行依赖。
- 管理后台：不存在，也没有文章写入 API。

阅读端还提供文章目录、阅读进度、代码复制、上一篇/下一篇、相关文章、深浅主题、SEO 元数据和自定义 404。设计取舍与同类项目对比见 [`docs/BLOG_PROJECT_COMPARISON.md`](docs/BLOG_PROJECT_COMPARISON.md)。

## 本地构建

```powershell
$env:VCPKG_ROOT = 'G:\tool\vcpkg'
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

启动：

```powershell
.\build\windows-debug\Debug\drogon_blog.exe config\config.dev.json
```

访问 <http://127.0.0.1:8080>，健康检查为 <http://127.0.0.1:8080/health>。

也可以使用 Docker：

```powershell
docker compose up --build -d
```

## 发布文章

1. 在 `content/posts` 新建 `<slug>.md`，格式见 `content/README.md`。
2. 本地启动博客并检查文章、分类、标签和代码块。
3. 本地发布到服务器：`powershell -ExecutionPolicy Bypass -File deploy/publish-to-blog.ps1`。
4. 备份到 GitHub：`git add content/posts && git commit && git push`，或直接使用 `deploy/publish-to-blog.ps1 -PushGit` 一步完成。
5. 只有 C++、模板、CSS、配置或静态资源变化才需要重建并重启镜像；Markdown 文章由服务器热加载。

## 绑定 GitHub 与 Giscus

复制 `config/config.example.json` 的 GitHub/Giscus 字段到实际配置：

- `site.github.repository_url`：仓库网页地址。
- `site.github.branch`：文章所在分支，默认 `main`。
- `site.giscus.repo`：`OWNER/REPOSITORY`。
- `site.giscus.repo_id`、`category_id`：从 <https://giscus.app/zh-CN> 获取。
- `site.giscus.enabled`：配置完成后设为 `true`。

仓库必须公开、启用 Discussions，并安装 Giscus GitHub App。GitHub Pages 不能运行 Drogon；Drogon 容器需要部署在支持 Docker 的主机上。
