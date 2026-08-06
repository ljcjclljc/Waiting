# GitHub 文章热部署

博客将 GitHub `main` 作为文章的唯一发布源。提交合并后，GitHub Actions 使用受限 SSH 密钥通知服务器部署该提交。服务器从 GitHub 获取准确的提交 SHA，校验 `content/posts`，同步文章并更新 `.content-version`；Drogon 在两秒内构建并切换到新快照，不需要重启容器。

## 边界

- 只有 `content/posts/*.md` 能通过这条链路热更新。
- C++、模板、CSS、配置及静态资源仍需构建新镜像并滚动替换容器。
- 服务器不会从 `/opt/chen-blog` 的脏工作树执行 `git pull`，内容使用独立裸仓库 `/var/lib/chen-blog-content/repo.git` 获取。
- 候选内容先在隔离目录校验。校验或健康检查失败时，不会激活该版本；健康检查失败还会恢复磁盘上的上一份文章。
- 服务器只接受 GitHub 当前的 `main` 头提交；过期工作流或其他分支不能覆盖较新的文章。

## 首次配置服务器

生成专用 Ed25519 密钥，将公钥和 `deploy` 目录上传到服务器，然后以 `root` 执行安装器：

```bash
deploy/install-blog-content-deploy.sh /tmp/github-actions-blog-content.pub
```

将专用公钥写入 `/home/blog-deploy/.ssh/authorized_keys`，必须保留强制命令和限制：

```text
command="/usr/local/sbin/blog-content-deploy-command",restrict ssh-ed25519 AAAA... github-actions-blog-content
```

SSH 强制命令需要账号具备可执行 Shell，因此账号使用 `/bin/bash`，但密码已锁定，专用公钥也只能进入强制命令。`deploy-blog-content` 自身会再次严格校验参数；`blog-deploy` 不能获得交互式 Shell，也不能直接运行 Docker。

## GitHub Actions 密钥

仓库需要三个 Actions Secret：

- `BLOG_HOST`：服务器 IP 或 SSH 主机名。
- `BLOG_DEPLOY_KEY`：专用 Ed25519 私钥。
- `BLOG_KNOWN_HOSTS`：从服务器 `/etc/ssh/ssh_host_ed25519_key.pub` 生成的固定主机公钥行。

不要使用 `StrictHostKeyChecking=no`，也不要把私钥提交到仓库。

## 发布文章

在 `content/posts` 新建或修改 Markdown，先在本地验证构建，然后提交并推送。只有进入 `main` 的提交才会触发服务器部署：

```bash
git add content/posts/my-post.md
git commit -m "publish my post"
git push
```

部署结果可通过健康接口确认：

```bash
curl -fsS http://127.0.0.1:8080/health
```

`contentReload` 应为 `up`，`contentVersion` 应等于 GitHub Actions 部署的提交 SHA。
