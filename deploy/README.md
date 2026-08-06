# Blog Deployment

## Publishing model

Articles now use two independent lines:

1. Local to server: `deploy/publish-to-blog.ps1` streams the local `content/posts` bundle directly to the server over a restricted SSH command. The server validates and hot-loads it without restarting Drogon.
2. Local to GitHub: commit and push `content/posts` normally. GitHub remains the backup and version history for articles.

The server never contacts GitHub and no longer fetches the repository. GitHub Actions builds the container image only.

## Publish from local

```powershell
powershell -ExecutionPolicy Bypass -File deploy/publish-to-blog.ps1
```

To publish first and then push the same content to GitHub as a backup, add `-PushGit`:

```powershell
powershell -ExecutionPolicy Bypass -File deploy/publish-to-blog.ps1 -PushGit
```

Defaults:

- Server: `114.55.119.188`
- User: `blog-deploy`
- Identity: `$env:USERPROFILE\.ssh\github_actions_blog_content_v2`
- Known hosts: `$env:USERPROFILE\.ssh\known_hosts`

Override them with `-Server`, `-User`, `-IdentityFile`, and `-KnownHostsFile` if needed.

## What the script does

1. Creates a tar bundle of `content/posts`.
2. Computes the SHA256 digest and uses its first 40 hex characters as the content version.
3. Runs `ssh blog-deploy@<server> "publish <digest>"` and streams the tar on stdin.
4. The server checks the digest, rejects unsafe tar paths/symlinks/oversized content, validates Markdown, swaps `content/posts`, writes `.content-version`, and waits for `/health` to report the new digest.
5. Drogon checks the Markdown directory fingerprint every two seconds and reloads within about two seconds; the container is not restarted. The version marker remains useful for deployment status, but it is not required for direct file changes to be detected.

## First-time server setup

Upload the `deploy` scripts and the Ed25519 public key to the server, then run as root:

```bash
bash /tmp/deploy/install-blog-content-deploy.sh /tmp/blog-publish.pub
```

The installer installs the restricted command at `/usr/local/sbin/blog-content-deploy-command`. Only the exact command `publish <40-hex content digest>` is accepted.

Verify with:

```bash
curl -fsS http://127.0.0.1:8080/health
```

`contentReload` should be `up`, and `contentVersion` should equal the digest reported by the local publish script.

## Code changes

Only C++, templates, CSS, configuration, or static assets require building a new container image and restarting the blog service. Markdown article changes do not.

## GitHub backup

GitHub still stores the articles and history. The `publish.yml` workflow keeps building/publishing the GHCR image on pushes to `main`, but it no longer deploys article content to the server.
