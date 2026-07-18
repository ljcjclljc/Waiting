# 向量笔记：Drogon + GitHub 博客

这是一个采用 Hexo 式发布模型的 Drogon 博客：文章只存在于本地 Markdown 文件中，提交并推送到 GitHub 后由 Actions 构建容器；访客不能修改文章，只能通过 Giscus 在 GitHub Discussions 评论。

## 架构

```text
content/posts/*.md -> git push -> GitHub Actions -> GHCR 容器
                              \-> GitHub Discussions / Giscus 评论
```

- Drogon：路由、服务端渲染、搜索、分类、标签、RSS 和站点地图。
- Markdown：文章唯一数据源，使用严格 JSON Front Matter。
- GitHub：代码与内容版本、构建触发、容器发布和评论数据。
- PostgreSQL：不再是博客运行依赖。
- 管理后台：不存在，也没有文章写入 API。

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
3. 提交并推送：`git add content/posts && git commit && git push`。
4. `.github/workflows/publish.yml` 会验证 Docker 构建，并在 `main` 分支推送时发布到 `ghcr.io/<owner>/<repo>`。
5. 部署主机从 GHCR 拉取新镜像并重启后，文章才会显示。

## 绑定 GitHub 与 Giscus

复制 `config/config.example.json` 的 GitHub/Giscus 字段到实际配置：

- `site.github.repository_url`：仓库网页地址。
- `site.github.branch`：文章所在分支，默认 `main`。
- `site.giscus.repo`：`OWNER/REPOSITORY`。
- `site.giscus.repo_id`、`category_id`：从 <https://giscus.app/zh-CN> 获取。
- `site.giscus.enabled`：配置完成后设为 `true`。

仓库必须公开、启用 Discussions，并安装 Giscus GitHub App。GitHub Pages 不能运行 Drogon；Drogon 容器需要部署在支持 Docker 的主机上。
