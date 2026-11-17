[CmdletBinding()]
param(
    [switch]$DryRun,
    [string]$Message = 'Update M2A23 firmware project'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$ExpectedRemote = 'https://github.com/released/M2A23BSP_ISP_CAN_PWM_LIN.git'
$ExpectedBranch = 'main'
$ProjectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$KnownLocalTrackedFiles = @(
    'SampleCode/Template/AP/Keil/Template.uvguix.AveryC',
    'SampleCode/Template/ISP_CAN/Keil/Template.uvguix.AveryC',
    'SampleCode/Template/AP/Keil/Nu_Link_Driver.ini',
    'SampleCode/Template/ISP_CAN/Keil/Nu_Link_Driver.ini'
)
$KnownAllowedTrackedFiles = @(
    'SampleCode/Template/AP/Keil/ProgramTargetImageWithCRC.ini',
    'SampleCode/Template/AP/Keil/srec_cat.exe'
)

function Invoke-GitCommand
{
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$GitArgs,
        [int[]]$AllowedExitCodes = @(0)
    )

    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try
    {
        $output = @(& git.exe @GitArgs 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $previousErrorAction
    }
    $rawLines = @($output | ForEach-Object { $_.ToString() })
    $lines = @($rawLines | Where-Object { $_ -notmatch '^warning:' })

    if ($AllowedExitCodes -notcontains $exitCode)
    {
        throw "git $($GitArgs -join ' ') failed with exit code $exitCode.`n$($rawLines -join [Environment]::NewLine)"
    }

    return [PSCustomObject]@{
        ExitCode = $exitCode
        Lines = $lines
        Text = $lines -join [Environment]::NewLine
    }
}

function Assert-RepositoryState
{
    $topLevel = (Invoke-GitCommand -GitArgs @('rev-parse', '--show-toplevel')).Text.Trim().Replace('/', '\')
    $branch = (Invoke-GitCommand -GitArgs @('symbolic-ref', '--quiet', '--short', 'HEAD')).Text.Trim()
    $fetchUrl = (Invoke-GitCommand -GitArgs @('remote', 'get-url', 'origin')).Text.Trim()
    $pushUrl = (Invoke-GitCommand -GitArgs @('remote', 'get-url', '--push', 'origin')).Text.Trim()
    $upstream = (Invoke-GitCommand -GitArgs @('rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{u}')).Text.Trim()
    $gitDir = (Invoke-GitCommand -GitArgs @('rev-parse', '--absolute-git-dir')).Text.Trim()
    $activePaths = @('MERGE_HEAD', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'index.lock', 'rebase-apply', 'rebase-merge', 'sequencer')

    if (-not [string]::Equals($topLevel, $ProjectRoot, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Repository root mismatch. Expected '$ProjectRoot', found '$topLevel'."
    }
    if ($branch -cne $ExpectedBranch)
    {
        throw "Branch mismatch. Expected '$ExpectedBranch', found '$branch'."
    }
    if (($fetchUrl -cne $ExpectedRemote) -or ($pushUrl -cne $ExpectedRemote))
    {
        throw "Origin mismatch. Expected fetch and push URL '$ExpectedRemote'."
    }
    if ($upstream -cne "origin/$ExpectedBranch")
    {
        throw "Upstream mismatch. Expected 'origin/$ExpectedBranch', found '$upstream'."
    }

    foreach ($relativePath in $activePaths)
    {
        if (Test-Path -LiteralPath (Join-Path $gitDir $relativePath))
        {
            throw "An active Git operation or lock exists: $relativePath"
        }
    }

    $indexState = Invoke-GitCommand -GitArgs @('diff', '--cached', '--quiet', '--') -AllowedExitCodes @(0, 1)
    if ($indexState.ExitCode -ne 0)
    {
        throw 'The Git index already contains staged changes. Commit, unstage, or review them separately first.'
    }

    [void](Invoke-GitCommand -GitArgs @('var', 'GIT_AUTHOR_IDENT'))
}

function Get-LiveRemoteSha
{
    $result = Invoke-GitCommand -GitArgs @('ls-remote', '--heads', 'origin', "refs/heads/$ExpectedBranch")
    $lines = @($result.Lines | Where-Object { $_ })

    if ($lines.Count -ne 1)
    {
        throw "Expected one live origin/$ExpectedBranch ref, found $($lines.Count)."
    }

    return (($lines[0] -split '\s+')[0]).ToLowerInvariant()
}

function Assert-RemoteAncestry
{
    $head = (Invoke-GitCommand -GitArgs @('rev-parse', 'HEAD')).Text.Trim().ToLowerInvariant()
    $tracking = (Invoke-GitCommand -GitArgs @('rev-parse', "origin/$ExpectedBranch")).Text.Trim().ToLowerInvariant()
    $live = Get-LiveRemoteSha

    if (($head -cne $live) -or ($tracking -cne $live))
    {
        throw "Local HEAD, origin/$ExpectedBranch, and the live remote are not identical. Pull, merge, rebase, reset, and force-push are intentionally not automated."
    }

    return $live
}

function Get-PendingPaths
{
    $paths = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    $trackedChanges = (Invoke-GitCommand -GitArgs @('-c', 'core.quotepath=false', 'diff', '--name-only', 'HEAD', '--')).Lines
    $untrackedChanges = (Invoke-GitCommand -GitArgs @('-c', 'core.quotepath=false', 'ls-files', '--others', '--exclude-standard')).Lines

    foreach ($path in @($trackedChanges) + @($untrackedChanges))
    {
        if ($path)
        {
            [void]$paths.Add($path.Replace('\', '/'))
        }
    }

    return @($paths | Sort-Object)
}

function Test-ForbiddenPath
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $normalized = $Path.Replace('\', '/')
    $leaf = [IO.Path]::GetFileName($normalized)

    if ($normalized -match '(^|/)(\.git|\.vs|\.codex|\.claude|obj|lst|Debug|Release|captures|recordings|sessions|logs|private|tmp|artifacts)(/|$)') { return $true }
    if ($normalized -match '^(docs/internal/|MANUAL_CLEANUP\.md$|HANDOFF\.md$|CODEX_HANDOFF\.md$|SELF_CHECK\.md$|AGENTS\.md$)') { return $true }
    if ($leaf -match '(?i)\.uvguix\.' -or $leaf -match '(?i)\.(user|suo|ncb)$') { return $true }
    if ($normalized -match '(?i)(^|/)(auth\.json|credentials[^/]*|secrets?\.[^/]+|\.env(?:\.[^/]+)?|github_push\.local\.psd1)$') { return $true }
    if ($normalized -match '(?i)\.(obj|o|axf|elf|bin|hex|map|lnp|dep|d|log|dmp|tmp|bak|exe|dll|zip|7z|ini)$') { return $true }

    return $false
}

function Assert-SafePath
{
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([IO.Path]::IsPathRooted($Path) -or $Path.Split('/') -contains '..')
    {
        throw "Unsafe repository-relative path: $Path"
    }

    $fullPath = [IO.Path]::GetFullPath((Join-Path $ProjectRoot $Path))
    $rootPrefix = $ProjectRoot.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Path escapes the repository root: $Path"
    }
}

function Assert-PublicContent
{
    param([Parameter(Mandatory = $true)][string[]]$Paths)

    $textExtensions = @('.bat', '.c', '.cmd', '.h', '.json', '.md', '.ps1', '.s', '.txt', '.uvoptx', '.uvprojx', '.xml', '.yml', '.yaml')
    $credentialPatterns = @(
        '[A-Za-z]:\\Users\\',
        '[A-Za-z]:\\SourceCode\\',
        '-----BEGIN [A-Z ]*PRIVATE KEY-----',
        ('gh' + 'p_[A-Za-z0-9]{20,}'),
        ('github_' + 'pat_[A-Za-z0-9_]{20,}'),
        '(?i)(password|passwd|api[_-]?key|access[_-]?token)\s*[:=]\s*["'']?[^\s"'']{8,}'
    )

    foreach ($path in $Paths)
    {
        Assert-SafePath -Path $path
        if (Test-ForbiddenPath -Path $path)
        {
            throw "Generated, private, or unsupported publication path: $path"
        }

        $fullPath = Join-Path $ProjectRoot $path
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf))
        {
            continue
        }

        $item = Get-Item -LiteralPath $fullPath
        if ($item.Length -gt 10MB)
        {
            throw "Publication candidate exceeds 10 MB and requires separate review: $path"
        }

        if ($textExtensions -contains $item.Extension.ToLowerInvariant())
        {
            $content = [IO.File]::ReadAllText($item.FullName)
            foreach ($pattern in $credentialPatterns)
            {
                if ($content -match $pattern)
                {
                    throw "Possible private path or credential pattern in: $path"
                }
            }
        }
    }
}

function Get-TrackedLocalCleanup
{
    $tracked = @((Invoke-GitCommand -GitArgs @('-c', 'core.quotepath=false', 'ls-files')).Lines)
    $cleanup = @($KnownLocalTrackedFiles | Where-Object { $tracked -contains $_ })
    $unexpected = @($tracked | Where-Object {
        (Test-ForbiddenPath -Path $_) -and
        ($KnownLocalTrackedFiles -notcontains $_) -and
        ($KnownAllowedTrackedFiles -notcontains $_)
    })

    if ($unexpected.Count -ne 0)
    {
        throw "Tracked private/generated paths require a separate reviewed removal:`n$($unexpected -join [Environment]::NewLine)"
    }

    return $cleanup
}

function Get-PathFingerprint
{
    param([Parameter(Mandatory = $true)][string[]]$Paths)

    $manifest = New-Object 'Collections.Generic.List[string]'
    foreach ($path in @($Paths | Sort-Object -Unique))
    {
        $fullPath = Join-Path $ProjectRoot $path
        $hash = 'deleted'
        if (Test-Path -LiteralPath $fullPath -PathType Leaf)
        {
            $hash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        $manifest.Add("$path`t$hash")
    }

    $sha = [Security.Cryptography.SHA256]::Create()
    try
    {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($manifest -join "`n"))
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally
    {
        $sha.Dispose()
    }
}

function Show-PublicationReview
{
    param(
        [Parameter(Mandatory = $true)][string[]]$PendingPaths,
        [Parameter(Mandatory = $true)][string[]]$CleanupPaths,
        [Parameter(Mandatory = $true)][string]$RemoteSha
    )

    Write-Output "Repository : $ExpectedRemote"
    Write-Output "Branch     : $ExpectedBranch"
    Write-Output "Remote SHA : $RemoteSha"
    Write-Output ''
    Write-Output 'Pending source changes:'
    if ($PendingPaths.Count -eq 0) { Write-Output '  (none)' }
    foreach ($path in $PendingPaths)
    {
        $fullPath = Join-Path $ProjectRoot $path
        $hash = if (Test-Path -LiteralPath $fullPath -PathType Leaf) { (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant() } else { 'deleted' }
        Write-Output "  $path [$hash]"
    }

    Write-Output ''
    Write-Output 'Tracked local files to remove from Git while preserving local copies:'
    if ($CleanupPaths.Count -eq 0) { Write-Output '  (none)' }
    foreach ($path in $CleanupPaths) { Write-Output "  $path" }

    Write-Output ''
    $status = Invoke-GitCommand -GitArgs @('-c', 'core.quotepath=false', 'status', '--short', '--untracked-files=all')
    if ($status.Text) { Write-Output $status.Text }
    $stat = Invoke-GitCommand -GitArgs @('--no-pager', 'diff', '--stat', 'HEAD', '--')
    if ($stat.Text) { Write-Output $stat.Text }
    Write-Output ''
    Write-Output 'Firmware publication remains subject to exact-version real-board acceptance.'
}

try
{
    if ([string]::IsNullOrWhiteSpace($Message) -or $Message.Contains("`r") -or $Message.Contains("`n"))
    {
        throw 'Commit message must be one non-empty line.'
    }

    Set-Location -LiteralPath $ProjectRoot
    Assert-RepositoryState
    $remoteSha = Assert-RemoteAncestry
    $pendingPaths = @(Get-PendingPaths)
    $cleanupPaths = @(Get-TrackedLocalCleanup)
    Assert-PublicContent -Paths $pendingPaths
    [void](Invoke-GitCommand -GitArgs @('diff', '--check', 'HEAD', '--'))
    $fingerprint = Get-PathFingerprint -Paths (@($pendingPaths) + @($cleanupPaths))

    Show-PublicationReview -PendingPaths $pendingPaths -CleanupPaths $cleanupPaths -RemoteSha $remoteSha

    if (($pendingPaths.Count -eq 0) -and ($cleanupPaths.Count -eq 0))
    {
        Write-Output 'UNCHANGED: no reviewed files require publication.'
        exit 0
    }

    if ($DryRun)
    {
        Write-Output 'DRY RUN: no index, commit, or remote change was made.'
        exit 0
    }

    if ((Read-Host 'Type PUSH to commit and push this reviewed scope; anything else cancels') -cne 'PUSH')
    {
        Write-Output 'CANCELED: no index, commit, or remote change was made.'
        exit 0
    }

    Assert-RepositoryState
    if ((Assert-RemoteAncestry) -cne $remoteSha) { throw 'Live remote changed during confirmation.' }
    $pendingNow = @(Get-PendingPaths)
    $cleanupNow = @(Get-TrackedLocalCleanup)
    if (@(Compare-Object -ReferenceObject $pendingPaths -DifferenceObject $pendingNow).Count -ne 0) { throw 'Pending paths changed during confirmation.' }
    if (@(Compare-Object -ReferenceObject $cleanupPaths -DifferenceObject $cleanupNow).Count -ne 0) { throw 'Tracked cleanup paths changed during confirmation.' }
    if ((Get-PathFingerprint -Paths (@($pendingNow) + @($cleanupNow))) -cne $fingerprint) { throw 'Publication candidate content changed during confirmation.' }

    foreach ($path in $cleanupPaths)
    {
        [void](Invoke-GitCommand -GitArgs @('rm', '--cached', '--quiet', '--', $path))
    }
    foreach ($path in $pendingPaths)
    {
        [void](Invoke-GitCommand -GitArgs @('add', '--', $path))
    }

    $expectedStaged = @(@($pendingPaths) + @($cleanupPaths) | Sort-Object -Unique)
    $actualStaged = @((Invoke-GitCommand -GitArgs @('-c', 'core.quotepath=false', 'diff', '--cached', '--name-only', '--')).Lines | Sort-Object -Unique)
    if (@(Compare-Object -ReferenceObject $expectedStaged -DifferenceObject $actualStaged).Count -ne 0)
    {
        throw 'The staged path set does not match the reviewed publication scope.'
    }

    [void](Invoke-GitCommand -GitArgs @('diff', '--cached', '--check'))
    $stagedStat = Invoke-GitCommand -GitArgs @('--no-pager', 'diff', '--cached', '--stat')
    if ($stagedStat.Text) { Write-Output $stagedStat.Text }
    [void](Invoke-GitCommand -GitArgs @('commit', '-m', $Message))
    [void](Invoke-GitCommand -GitArgs @('push', 'origin', $ExpectedBranch))

    $localSha = (Invoke-GitCommand -GitArgs @('rev-parse', 'HEAD')).Text.Trim().ToLowerInvariant()
    $publishedSha = Get-LiveRemoteSha
    if ($localSha -cne $publishedSha)
    {
        throw "Push returned success but remote SHA '$publishedSha' differs from local '$localSha'."
    }

    Write-Output "PUBLISHED: $ExpectedRemote $ExpectedBranch $localSha"
    exit 0
}
catch
{
    Write-Output "ERROR: $($_.Exception.Message)"
    exit 1
}
