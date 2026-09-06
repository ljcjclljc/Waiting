# 博客问答

博客问答页面位于 `/chat`，后端接口为 `POST /api/chat`。后端使用智谱开放平台的 `glm-4.5-air`（GLM-4.5-Air），API Key 不会发送到浏览器。

## 本地配置

运行时只读取环境变量 `ZHIPU_API_KEY`，不会从 JSON 配置或请求参数读取 API Key。不要把密钥写入仓库、镜像、前端代码或日志。当前后端只允许智谱官方 `https://open.bigmodel.cn` HTTPS 端点。

```powershell
$env:ZHIPU_API_KEY = "你的智谱 API Key"
.\build\windows-debug\Debug\drogon_blog.exe config\config.dev.json
```

Docker 运行时通过同名环境变量注入：

```powershell
$env:ZHIPU_API_KEY = "你的智谱 API Key"
docker compose up --build
```

生产服务器不要把密钥写入 Git 或镜像。若使用项目根目录的 `.env`，请确保该文件只允许部署用户读取（例如 `chmod 600 .env`），替换密钥后执行 `docker compose up -d --force-recreate`，使旧进程不再保留旧密钥。

曾经出现在聊天记录、日志或截图中的密钥都应立即在智谱控制台禁用并重新生成；不要继续复用已经暴露过的密钥。

## 可扩展资料

- `knowledge_base/`：放置 Markdown、TXT、JSON、CSV、YAML 或 YML 资料。每个文件最多 128 KB，总量最多读取 256 KB。
- `prompt_optimization/`：放置经过审核的角色、术语和回答规范。总量最多读取 64 KB，每次提问都会重新扫描。

知识库内容只作为参考资料，不能覆盖系统安全规则；提示词优化目录用于补充助手定位和回答风格。

生产环境应让 Nginx、Caddy 或云负载均衡器负责公网 HTTPS，并将请求转发到本机 `127.0.0.1:8080`，同时设置 `X-Forwarded-For`。应用只在连接来自回环地址时读取并校验该头，否则使用 TCP 对端地址。Compose 已不再把博客端口暴露到公网；浏览器到博客和博客到智谱均应使用 HTTPS。`/api/chat` 按客户端 IP 限制为每分钟 12 次，进程全局每分钟 60 次，最多同时处理 8 个智谱请求。
