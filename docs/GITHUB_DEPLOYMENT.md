# GitHub 部署清单

## 仓库设置

1. 创建公开 GitHub 仓库并推送本项目。
2. 在仓库 Settings > General > Features 中启用 Discussions。
3. 在 <https://github.com/apps/giscus> 为仓库安装 Giscus。
4. 在 <https://giscus.app/zh-CN> 选择仓库和 Discussions 分类，复制 `repo-id` 与 `category-id`。
5. 更新生产配置中的 `site.github` 和 `site.giscus`。

## 容器发布

推送到 `main` 后，GitHub Actions 会发布：

```text
ghcr.io/OWNER/REPOSITORY:latest
```

服务器需要拉取该镜像并将 `8080` 映射到反向代理。C++、模板、CSS、配置或静态资源变化时需要重新构建和滚动替换容器；Markdown 文章不需要。

## 内容发布

文章只从 `content/posts/*.md` 加载。网站没有写文章或删除文章的路由，因此浏览器用户无法修改内容。

发布到服务器：

```powershell
powershell -ExecutionPolicy Bypass -File deploy/publish-to-blog.ps1
```

服务器不再访问 GitHub。`publish-to-blog.ps1` 会把本地 Markdown 包直接流式发送给受限 SSH 账号，服务器校验后写入 `content/posts`，Drogon 在约两秒内热加载，不重启容器。

备份到 GitHub：正常提交并推送 `content/posts`，或使用 `-PushGit` 让发布脚本在服务器发布成功后自动完成提交和推送。GitHub Actions 只构建容器镜像，不再部署文章。
