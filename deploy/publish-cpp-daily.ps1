# Publish C++ daily-question Markdown files from a specified folder to the blog server and GitHub.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File deploy/publish-cpp-daily.ps1 `
#     -SourceDirectory C:\path\to\cpp-daily
#
# The source folder may contain only cpp-daily articles. Files are committed to
# the current Git branch before the complete content bundle is sent over the
# restricted SSH channel, so GitHub and the server receive the same questions.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDirectory,
    [string]$Server = '114.55.119.188',
    [string]$User = 'blog-deploy',
    [string]$IdentityFile = "$env:USERPROFILE\.ssh\github_actions_blog_content_v2",
    [string]$KnownHostsFile = "$env:USERPROFILE\.ssh\known_hosts"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Quote-Argument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Read-FrontMatter([string]$Path) {
    $text = [System.IO.File]::ReadAllText($Path)
    $match = [regex]::Match($text, '(?s)^---\r?\n(.*?)\r?\n---(?:\r?\n|$)')
    if (-not $match.Success) {
        throw "${Path}: expected JSON front matter"
    }

    try {
        $metadata = $match.Groups[1].Value | ConvertFrom-Json
    }
    catch {
        throw "${Path}: invalid JSON front matter: $($_.Exception.Message)"
    }

    foreach ($field in @('title', 'slug', 'date', 'excerpt', 'category')) {
        if (-not $metadata.PSObject.Properties[$field]) {
            throw "${Path}: required field '$field' is missing"
        }
    }
    if ([string]::IsNullOrWhiteSpace([string]$metadata.title) -or
        [string]::IsNullOrWhiteSpace([string]$metadata.date) -or
        [string]::IsNullOrWhiteSpace([string]$metadata.excerpt)) {
        throw "${Path}: title, date and excerpt must not be empty"
    }
    if ([string]::IsNullOrWhiteSpace($text.Substring($match.Length))) {
        throw "${Path}: article body is empty"
    }
    return $metadata
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$postsDirectory = Join-Path $repoRoot 'content\posts'
$sourcePath = (Resolve-Path -LiteralPath $SourceDirectory -ErrorAction Stop).Path

if (-not (Test-Path -LiteralPath $postsDirectory -PathType Container)) {
    throw "content/posts directory not found: $postsDirectory"
}
if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
    throw "SourceDirectory must be a directory: $sourcePath"
}
if (-not (Test-Path -LiteralPath $IdentityFile -PathType Leaf)) {
    throw "SSH identity file not found: $IdentityFile"
}
if (-not (Test-Path -LiteralPath $KnownHostsFile -PathType Leaf)) {
    throw "SSH known_hosts file not found: $KnownHostsFile"
}
& git.exe -C $repoRoot rev-parse --is-inside-work-tree *> $null
if ($LASTEXITCODE -ne 0) { throw "Not a Git repository: $repoRoot" }
$branch = (& git.exe -C $repoRoot branch --show-current).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) { throw 'Git is in detached HEAD state' }
if ([string]::IsNullOrWhiteSpace((& git.exe -C $repoRoot remote get-url origin).Trim())) {
    throw 'Git remote origin is not configured'
}

$sourceFiles = @(Get-ChildItem -LiteralPath $sourcePath -File -Filter '*.md')
if ($sourceFiles.Count -eq 0) {
    throw "No Markdown files found in $sourcePath"
}

foreach ($file in $sourceFiles) {
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Symbolic links and reparse points are not allowed: $($file.FullName)"
    }

    $metadata = Read-FrontMatter $file.FullName
    $slug = [string]$metadata.slug
    if ($slug -notmatch '^[a-z0-9]+(?:-[a-z0-9]+)*$' -or $file.BaseName -ne $slug) {
        throw "$($file.FullName): filename must match a lowercase slug"
    }
    $categorySlugProperty = $metadata.category.PSObject.Properties['slug']
    if ($null -eq $categorySlugProperty -or
        [string]$categorySlugProperty.Value -ne 'cpp-daily') {
        throw "$($file.FullName): category.slug must be cpp-daily"
    }
}

# Keep the source of truth in content/posts so the GitHub Actions deployment
# and the direct restricted publish use the exact same Markdown files.
$relativePaths = @()
foreach ($file in $sourceFiles) {
    $destination = Join-Path $postsDirectory $file.Name
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    $relativePaths += ('content/posts/' + $file.Name)
}
& git.exe -C $repoRoot add -- $relativePaths
if ($LASTEXITCODE -ne 0) { throw 'git add failed for C++ daily files' }
$staged = @(& git.exe -C $repoRoot diff --cached --name-only -- $relativePaths)
if ($staged.Count -gt 0) {
    & git.exe -C $repoRoot commit --only -m 'publish C++ daily questions' -- $relativePaths
    if ($LASTEXITCODE -ne 0) { throw 'git commit failed for C++ daily files' }
    & git.exe -C $repoRoot push origin $branch
    if ($LASTEXITCODE -ne 0) { throw "git push failed for branch $branch" }
    Write-Host "Published C++ daily files to GitHub branch $branch."
}
else {
    Write-Host 'No GitHub changes detected for the C++ daily files.'
}

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('chen-blog-cpp-daily-' + [guid]::NewGuid().ToString('N'))
$bundleRoot = Join-Path $temporaryDirectory 'bundle'
$bundlePosts = Join-Path $bundleRoot 'content\posts'
$tarFile = Join-Path $temporaryDirectory 'content-posts.tar'
New-Item -ItemType Directory -Path $bundlePosts -Force | Out-Null

try {
    Get-ChildItem -LiteralPath $postsDirectory -Force | Copy-Item -Destination $bundlePosts -Recurse -Force
    foreach ($file in $sourceFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $bundlePosts $file.Name) -Force
    }

    $validatorCandidates = @(
        (Join-Path $repoRoot 'build\windows-debug\Debug\drogon_blog.exe'),
        (Join-Path $repoRoot 'build\verify\Debug\drogon_blog.exe')
    )
    $validator = $validatorCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if ($validator) {
        & $validator --validate-content $bundlePosts
        if ($LASTEXITCODE -ne 0) {
            throw 'Drogon content validation failed'
        }
    }

    & tar.exe -cf $tarFile -C $bundleRoot 'content/posts'
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $tarFile -PathType Leaf)) {
        throw 'tar failed to create the content bundle'
    }

    $hash = (Get-FileHash -LiteralPath $tarFile -Algorithm SHA256).Hash.ToLowerInvariant()
    $digest = $hash.Substring(0, 40)
    Write-Host "Publishing C++ daily content digest $digest to ${User}@${Server}"

    $sshPath = (Get-Command ssh.exe -ErrorAction Stop).Source
    $arguments = @(
        '-F', 'NUL',
        '-i', $IdentityFile,
        '-o', 'BatchMode=yes',
        '-o', 'IdentitiesOnly=yes',
        '-o', 'StrictHostKeyChecking=yes',
        '-o', "UserKnownHostsFile=$KnownHostsFile",
        "$User@$Server",
        "publish $digest"
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $sshPath
    $startInfo.Arguments = ($arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    try {
        $bytes = [System.IO.File]::ReadAllBytes($tarFile)
        $process.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
        $process.StandardInput.Close()
    }
    catch {
        if (-not $process.HasExited) {
            $process.Kill()
        }
        throw
    }

    if (-not $process.WaitForExit(180000)) {
        if (-not $process.HasExited) {
            $process.Kill()
        }
        throw 'SSH publish timed out after 180 seconds'
    }

    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($stdout) { Write-Host $stdout.TrimEnd() }
    if ($stderr) { Write-Warning $stderr.TrimEnd() }
    if ($process.ExitCode -ne 0) {
        throw "publish failed on ${Server} with exit code $($process.ExitCode)"
    }
    Write-Host "Server accepted C++ daily content digest $digest."
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
