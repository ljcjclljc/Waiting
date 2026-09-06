# Publish the local AI knowledge and prompt directories to the server and GitHub.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File deploy/publish-ai-assets.ps1

[CmdletBinding()]
param(
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

function Invoke-Git([string[]]$Arguments) {
    & git.exe -C $repoRoot @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git command failed: git $($Arguments -join ' ')"
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$knowledgeDirectory = Join-Path $repoRoot 'knowledge_base'
$promptDirectory = Join-Path $repoRoot 'prompt_optimization'
$directories = @(
    @{ Name = 'knowledge_base'; Path = $knowledgeDirectory },
    @{ Name = 'prompt_optimization'; Path = $promptDirectory }
)
$allowedExtensions = @('.md', '.markdown', '.txt', '.json', '.csv', '.yaml', '.yml')
$maxFiles = 1000
$maxBundleBytes = 16MB

foreach ($directory in $directories) {
    if (-not (Test-Path -LiteralPath $directory.Path -PathType Container)) {
        throw "Required directory not found: $($directory.Path)"
    }
}
if (-not (Test-Path -LiteralPath $IdentityFile -PathType Leaf)) {
    throw "SSH identity file not found: $IdentityFile"
}
if (-not (Test-Path -LiteralPath $KnownHostsFile -PathType Leaf)) {
    throw "SSH known_hosts file not found: $KnownHostsFile"
}

$files = @()
foreach ($directory in $directories) {
    $entries = @(Get-ChildItem -LiteralPath $directory.Path -Force -Recurse)
    foreach ($file in $entries) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Symbolic links and reparse points are not allowed: $($file.FullName)"
        }
        if ($file.PSIsContainer) {
            continue
        }
        if ($allowedExtensions -notcontains $file.Extension.ToLowerInvariant()) {
            throw "Unsupported file type in $($directory.Name): $($file.FullName)"
        }
        if ($file.Length -gt 128KB) {
            throw "File is larger than 128 KB: $($file.FullName)"
        }
        $files += [pscustomobject]@{ Root = $directory; File = $file }
    }
}
if ($files.Count -eq 0) {
    throw 'No supported files found in knowledge_base or prompt_optimization'
}
if ($files.Count -gt $maxFiles) {
    throw "Too many AI asset files: $($files.Count)"
}
$totalBytes = ($files | ForEach-Object { $_.File.Length } | Measure-Object -Sum).Sum
if ($totalBytes -gt $maxBundleBytes) {
    throw "AI asset bundle is larger than 16 MB: $totalBytes bytes"
}

& git.exe -C $repoRoot rev-parse --is-inside-work-tree *> $null
if ($LASTEXITCODE -ne 0) { throw "Not a Git repository: $repoRoot" }
$branch = (& git.exe -C $repoRoot branch --show-current).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) { throw 'Git is in detached HEAD state' }
$remote = (& git.exe -C $repoRoot remote get-url origin).Trim()
if ([string]::IsNullOrWhiteSpace($remote)) { throw 'Git remote origin is not configured' }

$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) ('chen-blog-ai-assets-' + [guid]::NewGuid().ToString('N'))
$bundleRoot = Join-Path $temporaryDirectory 'bundle'
$tarFile = Join-Path $temporaryDirectory 'ai-assets.tar'
New-Item -ItemType Directory -Path $bundleRoot -Force | Out-Null

try {
    foreach ($directory in $directories) {
        $targetRoot = Join-Path $bundleRoot $directory.Name
        New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
        foreach ($entry in ($files | Where-Object { $_.Root.Name -eq $directory.Name })) {
            $relative = $entry.File.FullName.Substring($directory.Path.Length) -replace '^[\\/]+', ''
            $target = Join-Path $targetRoot $relative
            New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
            Copy-Item -LiteralPath $entry.File.FullName -Destination $target -Force
        }
    }

    & tar.exe -cf $tarFile -C $bundleRoot 'knowledge_base' 'prompt_optimization'
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $tarFile -PathType Leaf)) {
        throw 'tar failed to create the AI asset bundle'
    }
    $hash = (Get-FileHash -LiteralPath $tarFile -Algorithm SHA256).Hash.ToLowerInvariant()
    $digest = $hash.Substring(0, 40)

    Write-Host "Committing AI assets to GitHub branch $branch"
    Invoke-Git @('add', '--', 'knowledge_base', 'prompt_optimization')
    $staged = @(& git.exe -C $repoRoot diff --cached --name-only -- 'knowledge_base' 'prompt_optimization')
    if ($staged.Count -gt 0) {
        Invoke-Git @('commit', '--only', '-m', 'publish blog AI knowledge assets', '--', 'knowledge_base', 'prompt_optimization')
        Invoke-Git @('push', 'origin', $branch)
    }
    else {
        Write-Host 'No GitHub changes detected in the AI asset directories.'
    }

    Write-Host "Publishing AI assets digest $digest to ${User}@${Server}"
    $sshPath = (Get-Command ssh.exe -ErrorAction Stop).Source
    $arguments = @(
        '-F', 'NUL', '-i', $IdentityFile, '-o', 'BatchMode=yes',
        '-o', 'IdentitiesOnly=yes', '-o', 'StrictHostKeyChecking=yes',
        '-o', "UserKnownHostsFile=$KnownHostsFile", "$User@$Server",
        "ai-publish $digest"
    )
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $sshPath
    $startInfo.Arguments = ($arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    try {
        $bytes = [IO.File]::ReadAllBytes($tarFile)
        $process.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
        $process.StandardInput.Close()
    }
    catch {
        if (-not $process.HasExited) { $process.Kill() }
        throw
    }
    if (-not $process.WaitForExit(180000)) {
        if (-not $process.HasExited) { $process.Kill() }
        throw 'SSH AI asset publish timed out after 180 seconds'
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($stdout) { Write-Host $stdout.TrimEnd() }
    if ($stderr) { Write-Warning $stderr.TrimEnd() }
    if ($process.ExitCode -ne 0) {
        throw "AI asset publish failed on ${Server} with exit code $($process.ExitCode)"
    }
    Write-Host "AI assets published successfully: $digest"
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
