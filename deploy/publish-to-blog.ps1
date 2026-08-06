# Publish local content/posts directly to the blog server.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File deploy/publish-to-blog.ps1
#   powershell -ExecutionPolicy Bypass -File deploy/publish-to-blog.ps1 -PushGit
#
# The server receives a tar bundle over SSH stdin and validates it before
# activating the new content. GitHub is not contacted by the server.

[CmdletBinding()]
param(
    [string]$Server = '114.55.119.188',
    [string]$User = 'blog-deploy',
    [string]$IdentityFile = "$env:USERPROFILE\.ssh\github_actions_blog_content_v2",
    [string]$KnownHostsFile = "$env:USERPROFILE\.ssh\known_hosts",
    [switch]$PushGit,
    [string]$CommitMessage = 'publish: update blog content'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Quote-Argument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$postsDirectory = Join-Path $repoRoot 'content\posts'
if (-not (Test-Path -LiteralPath $postsDirectory -PathType Container)) {
    throw "content/posts directory not found: $postsDirectory"
}
if (-not (Test-Path -LiteralPath $IdentityFile -PathType Leaf)) {
    throw "SSH identity file not found: $IdentityFile"
}
if (-not (Test-Path -LiteralPath $KnownHostsFile -PathType Leaf)) {
    throw "SSH known_hosts file not found: $KnownHostsFile"
}

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('chen-blog-publish-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
$tarFile = Join-Path $temporaryDirectory 'content-posts.tar'

try {
    & tar.exe -cf $tarFile -C $repoRoot 'content/posts'
    if ($LASTEXITCODE -ne 0) {
        throw 'tar failed to create the content bundle'
    }
    if (-not (Test-Path -LiteralPath $tarFile -PathType Leaf)) {
        throw 'tar did not create the content bundle'
    }

    $hash = (Get-FileHash -LiteralPath $tarFile -Algorithm SHA256).Hash.ToLowerInvariant()
    $digest = $hash.Substring(0, 40)
    Write-Host "Publishing content digest $digest to ${User}@${Server}"

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
    $exitCode = $process.ExitCode
    if ($stdout) {
        Write-Host $stdout.TrimEnd()
    }
    if ($stderr) {
        Write-Warning $stderr.TrimEnd()
    }
    if ($exitCode -ne 0) {
        throw "publish failed on ${Server} with exit code ${exitCode}"
    }

    Write-Host "Server accepted content digest $digest."

    if ($PushGit) {
        Push-Location $repoRoot
        try {
            & git.exe add content/posts
            if ($LASTEXITCODE -ne 0) {
                throw 'git add failed'
            }
            $changes = & git.exe status --porcelain -- content/posts
            if ($LASTEXITCODE -ne 0) {
                throw 'git status failed'
            }
            if ($changes) {
                & git.exe commit -m $CommitMessage
                if ($LASTEXITCODE -ne 0) {
                    throw 'git commit failed'
                }
            }
            & git.exe push origin HEAD
            if ($LASTEXITCODE -ne 0) {
                throw 'git push failed'
            }
            Write-Host 'GitHub backup push completed.'
        }
        finally {
            Pop-Location
        }
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
