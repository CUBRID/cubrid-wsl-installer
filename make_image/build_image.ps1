<#
.SYNOPSIS
    build cubrid-wsl2 Docker image (Windows + Docker Desktop).

.DESCRIPTION
    porting of original bash script build_image.sh to PowerShell.
      1) if cubrid-docker repository is not exists, git clone.
         otherwise fetch and hard-reset it to the remote default branch, so a
         build always uses the latest upstream recipe.
      2) stage the version directory into .build\<TAG> and append the
         "CUBRID For WSL" overlay to a copy of its Dockerfile.
      3) build cubrid-wsl2:<TAG> image with docker build from that staging
         directory.

    The clone is treated as a read-only build artifact: nothing is ever copied
    into it, so the reset in step 1 can always succeed and every version is
    built from pristine upstream sources.

    The overlay in step 2 is what makes versions other than 11.4 usable.
    The upstream Dockerfiles differ - only 11.4 installs ~/.cubrid.sh, and the
    older ones put the databases directory in /var/lib/cubrid - while the
    Windows side (installer, tray app, starter) always runs
    `. ~/.cubrid.sh; cubrid ...`. The overlay normalises that layout.

.PARAMETER Tags
    build tags. default ('11.4'). Any version directory present in the
    cubrid-docker repository is valid, e.g. -Tags 10.2,11.3
    Versions below 10.2 are rejected (see MinSupportedTag below).

.PARAMETER Repo
        cubrid-docker repository URL. default https://github.com/CUBRID/cubrid-docker.git.

.PARAMETER NoCache
    Append --no-cache to docker build so the BuildKit cache is bypassed for
    this run. Independent switch; combine freely with other options.

.PARAMETER Pull
    Append --pull to docker build so the FROM base image is re-fetched from
    the registry even if a local copy already exists. Independent switch.

.PARAMETER Clean
    Convenience switch that implies both -NoCache and -Pull. Equivalent to
    `-NoCache -Pull`. Useful when the user just wants a clean rebuild without
    remembering both flag names. Combining with -NoCache / -Pull is harmless
    (the effect is OR-ed).

.PARAMETER SkipRepoUpdate
    Skip the fetch / reset step and build from the clone as it currently is.
    Use when working offline, or when testing local edits to the clone.

.EXAMPLE
    PS> .\build_image.ps1
.EXAMPLE
    PS> .\build_image.ps1 -Tags 11.4,11.3
.EXAMPLE
    PS> .\build_image.ps1 -Tags 10.2
.EXAMPLE
    PS> .\build_image.ps1 -Repo 'https://github.com/CUBRID/cubrid-docker.git'
.EXAMPLE
    # Force fresh layers but keep the existing base image:
    PS> .\build_image.ps1 -NoCache
.EXAMPLE
    # Re-pull the base image only, reuse cached layers:
    PS> .\build_image.ps1 -Pull
.EXAMPLE
    # Fully clean rebuild (= -NoCache -Pull):
    PS> .\build_image.ps1 -Clean
#>

[CmdletBinding()]
param(
    [string[]]$Tags = @('11.4'),
    [string]$Repo = 'https://github.com/CUBRID/cubrid-docker.git',
    [switch]$NoCache,
    [switch]$Pull,
    [switch]$Clean,
    [switch]$SkipRepoUpdate
)

$useNoCache = $NoCache.IsPresent -or $Clean.IsPresent
$usePull    = $Pull.IsPresent    -or $Clean.IsPresent

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Oldest CUBRID version CUBRID For WSL supports. 10.0 / 10.1 are built
# FROM centos:7, whose package mirrors are retired, so they cannot be built
# any more and are deliberately out of scope.
$MinSupportedTag = [version]'10.2'

# ------------------------------------------------------------------
# Appended to a *copy* of each version's Dockerfile. ASCII only: it is
# appended with -Encoding Ascii so no BOM lands in the middle of the file.
#
# 11.4 already does all of this upstream, so the overlay is written to be
# idempotent - it re-copies the same files and skips the .bash_profile block
# when one is already present.
# ------------------------------------------------------------------
$WslOverlay = @'

# ============================================================
# CUBRID For WSL overlay - appended by build_image.ps1.
# Normalises the layout the Windows side depends on:
#   /home/cubrid/.cubrid.sh        sourced by every wsl.exe call
#   /home/cubrid/entrypoint.sh     one-shot hook, run and deleted by .cubrid.sh
#   /home/cubrid/CUBRID/databases  CUBRID_DATABASES as .cubrid.sh defines it
# ============================================================
COPY wsl_cubrid.sh /home/cubrid/.cubrid.sh
COPY wsl_entrypoint.sh /home/cubrid/entrypoint.sh

RUN set -eux; \
    chmod 755 /home/cubrid/.cubrid.sh /home/cubrid/entrypoint.sh; \
    chown cubrid:cubrid /home/cubrid/.cubrid.sh /home/cubrid/entrypoint.sh; \
    mkdir -p /home/cubrid/CUBRID/databases; \
    chown -R cubrid:cubrid /home/cubrid/CUBRID; \
    touch /home/cubrid/.bash_profile; \
    if ! grep -q 'cubrid.sh' /home/cubrid/.bash_profile; then \
        echo '#----------------------------------------------------------------' >> /home/cubrid/.bash_profile; \
        echo '# set CUBRID environment variables' >> /home/cubrid/.bash_profile; \
        echo '#----------------------------------------------------------------' >> /home/cubrid/.bash_profile; \
        echo 'if [ -f /home/cubrid/.cubrid.sh ]; then' >> /home/cubrid/.bash_profile; \
        echo '. /home/cubrid/.cubrid.sh' >> /home/cubrid/.bash_profile; \
        echo 'fi' >> /home/cubrid/.bash_profile; \
    fi; \
    chown cubrid:cubrid /home/cubrid/.bash_profile

ENTRYPOINT []
CMD ["/bin/bash"]
'@

function Invoke-Native {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Description,
        [Parameter(Mandatory, ValueFromRemainingArguments = $true)][object[]]$CommandAndArgs
    )
    if ($CommandAndArgs.Count -lt 1) {
        throw "Invoke-Native: no command given for '$Description'"
    }
    $exe, $rest = $CommandAndArgs
    Write-Host "[exec] $exe $($rest -join ' ')"
    if ($null -eq $rest) {
        & $exe
    } else {
        & $exe @rest
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed (exit code $LASTEXITCODE)"
    }
}

function Test-DockerDaemon {
    if (-not (Get-Command docker.exe -ErrorAction SilentlyContinue)) {
        throw @"
docker.exe not found.
Please check if Docker Desktop is installed and registered in PATH.
"@
    }

    Write-Host "Checking Docker daemon connectivity (docker info) ..."
    $null = & docker info 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw @"
Docker daemon not connected.
Please check the following:
  1) If Docker Desktop is running (the whale icon in the system tray is stable)
  2) If Linux containers mode is enabled (the 'Switch to Linux containers' item in the tray menu is visible)
  3) If 'Use the WSL 2 based engine' is enabled in Settings > General
  4) If Docker Desktop was just started, wait for the WSL2 backend to fully start (1~2 minutes)
"@
    }
    Write-Host "Docker daemon OK."
}

function Test-SupportedTag {
    param([Parameter(Mandatory)][string]$Tag)

    $parsed = $null
    if (-not [version]::TryParse($Tag, [ref]$parsed)) {
        throw "Tag '$Tag' is not a CUBRID version number (expected e.g. 10.2, 11.4)."
    }
    if ($parsed -lt $MinSupportedTag) {
        throw @"
Tag '$Tag' is not supported. CUBRID For WSL supports $MinSupportedTag and later.
10.0 / 10.1 are built FROM centos:7, whose package mirrors are retired, so
those images can no longer be built.
"@
    }
}

function Get-RemoteDefaultRef {
    <#
        Name of the remote HEAD, e.g. 'origin/master'. Freshly cloned repos have
        refs/remotes/origin/HEAD; repos cloned with --single-branch or by older
        git may not, so ask the remote in that case.
    #>
    param([Parameter(Mandatory)][string]$Path)

    $ref = (& git -C $Path symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>$null | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($ref)) {
        & git -C $Path remote set-head origin --auto 2>$null | Out-Null
        $ref = (& git -C $Path symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>$null | Out-String).Trim()
    }
    if ([string]::IsNullOrWhiteSpace($ref)) {
        throw "Could not determine the default branch of 'origin' in '$Path'."
    }
    return $ref
}

function Sync-CubridDockerRepo {
    <#
        Bring the clone to exactly the remote default branch. The clone is a
        build artifact (it is listed in .gitignore), so discarding local state
        is the right call - and it is what keeps the update from ever failing
        on leftovers written by an older revision of this script.
    #>
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$RepoUrl
    )

    if (-not (Test-Path $Path)) {
        Write-Host "Cloning cubrid-docker into $Path ..."
        Invoke-Native -Description 'git clone' -- git clone $RepoUrl $Path
    } elseif (-not (Test-Path (Join-Path $Path '.git'))) {
        throw @"
Existing directory '$Path' is not a git repository (missing .git).
Please remove it and retry:
  Remove-Item -Recurse -Force '$Path'
"@
    }

    if ($SkipRepoUpdate) {
        $current = (& git -C $Path log -1 --pretty=format:'%h %s' | Out-String).Trim()
        Write-Host "-SkipRepoUpdate given; building from the clone as-is: $current"
        return
    }

    # -Repo may have changed since the clone was made.
    $currentUrl = (& git -C $Path remote get-url origin 2>$null | Out-String).Trim()
    if ($currentUrl -ne $RepoUrl) {
        Write-Host "Updating origin URL: '$currentUrl' -> '$RepoUrl'"
        Invoke-Native -Description 'git remote set-url' -- git -C $Path remote set-url origin $RepoUrl
    }

    Write-Host "Fetching latest cubrid-docker ..."
    Invoke-Native -Description 'git fetch' -- git -C $Path fetch --all --prune --tags

    $defaultRef = Get-RemoteDefaultRef -Path $Path
    Write-Host "Resetting clone to $defaultRef (local changes in the clone are discarded) ..."
    Invoke-Native -Description 'git reset --hard' -- git -C $Path reset --hard $defaultRef
    Invoke-Native -Description 'git clean'        -- git -C $Path clean -fd

    $head = (& git -C $Path log -1 --pretty=format:'%h %s' | Out-String).Trim()
    Write-Host "cubrid-docker is up to date: $head"
}

function New-WslBuildContext {
    <#
        Copy one version directory out of the clone and lay the WSL overlay on
        top of the copy. Building from here keeps the clone pristine.
    #>
    param(
        [Parameter(Mandatory)][string]$SourceDir,
        [Parameter(Mandatory)][string]$StagingDir,
        [Parameter(Mandatory)][string]$CubridSh,
        [Parameter(Mandatory)][string]$EntrypointSh
    )

    if (Test-Path $StagingDir) { Remove-Item -Recurse -Force $StagingDir }
    New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

    Copy-Item -Recurse -Force -Path (Join-Path $SourceDir '*') -Destination $StagingDir

    $dockerfile = Join-Path $StagingDir 'Dockerfile'
    if (-not (Test-Path $dockerfile)) {
        throw "Dockerfile not found in build context: $dockerfile"
    }

    # Distinct names so the overlay never collides with a version that ships
    # its own cubrid.sh / docker-entrypoint.sh (11.4 does).
    Copy-Item -Force $CubridSh     (Join-Path $StagingDir 'wsl_cubrid.sh')
    Copy-Item -Force $EntrypointSh (Join-Path $StagingDir 'wsl_entrypoint.sh')

    Add-Content -Path $dockerfile -Value $WslOverlay -Encoding Ascii

    return $StagingDir
}

function Invoke-DockerBuild {
    param(
        [Parameter(Mandatory)][string[]]$Tags,
        [Parameter(Mandatory)][string]  $Context
    )
    if ($Tags.Count -lt 1) {
        throw "Invoke-DockerBuild: at least one tag is required"
    }
    $buildArgs = @() + $commonBuildFlags
    foreach ($t in $Tags) {
        $buildArgs += @('-t', $t)
    }
    $buildArgs += $Context
    $desc = "docker build " + ($Tags -join ', ')
    Invoke-Native -Description $desc -- docker @buildArgs
}

$ShellPath   = Split-Path -Parent $MyInvocation.MyCommand.Definition
$DockerPath  = Join-Path $ShellPath 'cubrid-docker'
$StagingRoot = Join-Path $ShellPath '.build'

Write-Host "ShellPath   = $ShellPath"
Write-Host "DockerPath  = $DockerPath"
Write-Host "StagingRoot = $StagingRoot"

# Validate every tag before doing any work, so a typo fails immediately
# instead of after a long clone / build.
foreach ($TagName in $Tags) {
    Test-SupportedTag -Tag $TagName
}

$CubridSh         = Join-Path $ShellPath 'cubrid.sh'
$DockerEntrypoint = Join-Path $ShellPath 'docker-entrypoint.sh'

foreach ($f in @($CubridSh, $DockerEntrypoint)) {
    if (-not (Test-Path $f)) {
        throw "Source file not found: $f"
    }
}

Test-DockerDaemon

Sync-CubridDockerRepo -Path $DockerPath -RepoUrl $Repo

$commonBuildFlags = @('build')
if ($useNoCache) { $commonBuildFlags += '--no-cache' }
if ($usePull)    { $commonBuildFlags += '--pull' }

Write-Host ""
Write-Host "Build options: NoCache=$useNoCache  Pull=$usePull  Clean=$($Clean.IsPresent)"

foreach ($TagName in $Tags) {
    Write-Host ""
    Write-Host "===================================================="
    Write-Host " Building image for tag: $TagName"
    Write-Host "===================================================="

    $BuildDirPath = Join-Path $DockerPath $TagName
    if (-not (Test-Path $BuildDirPath)) {
        $available = (Get-ChildItem -Directory -Path $DockerPath |
                        Where-Object { $_.Name -ne '.git' } |
                        ForEach-Object { $_.Name }) -join ', '
        throw @"
Build directory not found: $BuildDirPath
Tag '$TagName' does not exist in the cubrid-docker repository.
Available versions: $available
"@
    }

    $StagingPath = New-WslBuildContext -SourceDir $BuildDirPath `
                                       -StagingDir (Join-Path $StagingRoot $TagName) `
                                       -CubridSh $CubridSh `
                                       -EntrypointSh $DockerEntrypoint
    Write-Host "Build context: $StagingPath (upstream $TagName + WSL overlay)"

    Invoke-DockerBuild -Tags @("cubrid-wsl2:$TagName") -Context $StagingPath
}

Write-Host ""
Write-Host "All builds completed."
