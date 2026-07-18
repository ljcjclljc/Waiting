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

服务器需要拉取该镜像并将 `8080` 映射到反向代理。自动部署到具体服务器还需要服务器地址、SSH 凭据和域名，这些信息不应提交到仓库；应保存为 GitHub Actions Secrets 或由服务器端更新服务管理。

## 内容发布

文章只从 `content/posts/*.md` 加载。网站没有写文章或删除文章的路由，因此浏览器用户无法修改内容。修改文章必须经过 Git 提交和部署。
