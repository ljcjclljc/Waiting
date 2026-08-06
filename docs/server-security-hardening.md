# 博客服务器安全基线

更新日期：2026-07-23

## 攻击面

当前服务器需要防范的主要网络风险包括：

- SSH 密码爆破、凭据填充和 root 账号直接登录。
- 公开管理端口被扫描、弱口令攻击或已知漏洞利用。
- Nginx、Portainer、Harbor、Docker、runc 等旧版本组件漏洞。
- HTTP 请求洪泛、慢连接、超大请求和资源耗尽。
- Markdown XSS、危险 URL、路径穿越和恶意内容文件。
- Docker Socket、特权容器或危险挂载导致的主机接管。
- DNS 劫持、无 HTTPS、证书错误和反向代理配置错误。
- 日志写满磁盘、缺少审计记录、备份缺失和供应链污染。

## 已实施措施

### SSH

- 禁用密码登录和键盘交互认证。
- root 只允许公钥登录。
- 登录尝试次数限制为 3，登录窗口限制为 30 秒。
- 禁用 X11 和 SSH Agent 转发。
- 保留本地端口转发，便于通过 SSH 隧道访问管理服务。

服务器备份文件：

```text
/etc/ssh/sshd_config.bak-before-blog-hardening
```

### 网络入口

- 公网只应直接保留 `22`、`80` 和 `443`。
- `5000` 为 root 权限远程管理服务，只允许服务器本机访问。
- `5001` 和 `8080` 已通过 `DOCKER-USER` 链限制公网访问。
- `5002` 和 `5003` 的 Docker NAT 原始目标端口规则已生成，等待远程审批恢复后部署。
- 防火墙规则由 systemd 服务恢复，并在 Docker 启动后重新应用。

管理服务应通过 SSH 隧道访问：

```powershell
ssh -L 5000:127.0.0.1:5000 `
    -L 5001:127.0.0.1:5001 `
    -L 5002:127.0.0.1:5002 `
    -L 5003:127.0.0.1:5003 `
    -L 8080:127.0.0.1:8080 `
    root@114.55.119.188
```

### 博客入口

- Nginx 按来源 IP 限制请求速率和并发连接数。
- 请求体限制为 256 KB。
- Drogon 仅注册 GET 路由，不提供文章上传、编辑或管理接口。
- 响应启用 CSP、禁止 MIME 嗅探、禁止 iframe 嵌入并限制引用信息。

### 博客容器

- 使用非 root 用户 `65532:65532`。
- 根文件系统只读，内容和配置卷只读。
- 启用 `no-new-privileges` 并丢弃全部 Linux capabilities。
- 限制为 1 个 CPU、512 MB 内存和 128 个进程。
- `/tmp` 使用 16 MB tmpfs。
- Docker JSON 日志限制为 10 MB、最多保留 3 个文件。

### 内容安全

- Markdown 禁止原始 HTML。
- 过滤 `javascript:`、危险 `data:` 等 URL Scheme。
- 文章文件、Front Matter、文章数量和标签数量均有限制。
- 搜索参数最多处理 80 字节。

## 仍需处理

- 为 `waiting.org.cn` 添加 DNS A 记录并签发 HTTPS 证书，然后启用 HSTS。
- 升级 Nginx Proxy Manager `2.9.19`、Portainer `2.19.4`、Harbor `2.10.0`。
- 更新 Docker Engine、containerd 和 runc，并在维护窗口验证所有容器。
- 评估移除以 root 运行的 `/clouddream/remote-manage/RemoteManage.dll`。
- Portainer 即使取消 privileged，挂载 Docker Socket 后仍具有主机级控制能力，必须限制入口并使用强密码。
- SELinux 当前关闭，应在兼容性测试后于维护窗口启用。
- 配置异机加密备份和定期恢复演练。
- 在阿里云安全组中同步只开放 22、80、443，形成云侧和主机侧双层控制。

## 验证清单

```text
SSH 公钥登录成功
SSH 密码登录失败
公网 22/80/443 可达
公网 5000/5001/5002/5003/8080 不可达
博客 /health 返回 200
waiting.org.cn 首页返回 200
Docker 重启后防火墙规则仍存在
博客容器资源限制仍存在
```
