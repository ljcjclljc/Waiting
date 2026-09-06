# 内容目录

`posts/*.md` 是博客文章的唯一来源。网站没有文章管理接口，只有提交到 GitHub 并完成部署的文件才会出现在站点中。

每篇文章使用 JSON Front Matter。JSON 由 JsonCpp 严格解析，避免用字符串拆分结构化元数据：

```markdown
---
{
  "title": "文章标题",
  "slug": "article-slug",
  "date": "2026-07-19",
  "updated": "2026-07-19",
  "excerpt": "文章摘要",
  "category": { "name": "C++ 实践", "slug": "cpp" },
  "tags": [{ "name": "Drogon", "slug": "drogon" }],
  "featured": false,
  "draft": false
}
---
正文直接开始，章节从 `##` 开始；页面的文章标题由 Front Matter 自动生成。
```

规则：

- 文件名必须为 `<slug>.md`。
- `slug`、分类 slug 和标签 slug 只能使用小写字母、数字和连字符。
- `draft: true` 的文件不会显示。
- 未来日期的文章在日期到达前不会显示。
- 原始 HTML 会被 Markdown 渲染器禁用。

## C++ 每日一题

每日一题栏目地址为 `/cpp-daily`，文章使用分类 `cpp-daily`，仍然放在 `content/posts` 中。例如：

```json
"category": { "name": "C++每日一题", "slug": "cpp-daily" }
```

如果希望从单独的文件夹批量发布每日一题，可以运行：

```powershell
powershell -ExecutionPolicy Bypass -File deploy/publish-cpp-daily.ps1 `
  -SourceDirectory C:\path\to\cpp-daily
```

脚本会保留服务器上的其他文章，只覆盖同 slug 的每日一题文件，并在发送前检查 JSON Front Matter、文件名和分类。
