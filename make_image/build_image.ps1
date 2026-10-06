<#
.SYNOPSIS
    build cubrid-wsl2 Docker image (Windows + Docker Desktop).

.DESCRIPTION
      1) if cubrid-docker repository is not exists, git clone.
         otherwise fetch and hard-reset it to the remote default branch, so a
         build always uses the latest upstream recipe.
      2) stage the version directory into .build\<VERSION>, copy
         docker-entrypoint.sh over the one the version ships, rewrite the
         engine download URL in a copy of its Dockerfile, and append the
         "CUBRID For WSL" overlay to it.
      3) build cubrid-wsl2:<VERSION> image with docker build from that staging
         directory, labelled with the full engine version it fetched.

    By default the engine tarball comes from the newest nightly build of the
    requested version; -Release pins the published <VERSION>_latest tarball
    instead. Either way the URL the upstream Dockerfile hardcodes is replaced,
    so the only thing that decides which engine lands in the image is this
    script - see Resolve-EngineBuild.

    The full engine version (e.g. 11.4.6.1963-0e7d3c1) is recorded as the
    image label 'cubrid.version'. create_image.ps1 reads it back from there
    and writes it next to the published tar.gz, which is how build.bat learns
    what to name the installer.

    The clone is treated as a read-only build artifact: nothing is ever copied
    into it, so the reset in step 1 can always succeed and every version is
    built from pristine upstream sources.

    docker-entrypoint.sh is the only file staged from make_image. It replaces
    the container-oriented entrypoint the versions ship, because on WSL the
    installer creates demodb itself.

    The overlay in step 2 is what makes versions other than 11.4 usable. The
    upstream Dockerfiles differ - only 11.4 installs ~/.cubrid.sh, and the
    older ones put the databases directory in /var/lib/cubrid - while the
    Windows side (installer, tray app, starter) always runs
    `. ~/.cubrid.sh; cubrid ...`. The overlay writes that file in place when
    the version does not already provide one; nothing is copied for it.

.PARAMETER Version
    CUBRID versions to build, default ('11.4'). Positional, so
    `.\build_image.ps1 10.2` works, and aliased to -v to match build.bat.
    Several can be given at once, e.g. -v 10.2,11.3

    A version may name a whole line or one exact nightly build; anything
    less than a full build name resolves to the newest build under it:

      11.4                  newest 11.4 nightly
      11.5.0                newest 11.5.0.* nightly
      11.5.0.2632           that build
      11.5.0.2632-e17fdab   that exact directory

    Whatever is passed also names the image tag (cubrid-wsl2:<VERSION>) and
    the os_image\<VERSION>\ directory, so two builds of the same line can be
    kept side by side by naming them precisely. The Dockerfile is always
    taken from the <major>.<minor> recipe; a line newer than every
    directory the repository ships (11.5 and up today) builds from the
    newest one it has - see Resolve-BuildDir.
    Versions below 10.2 are rejected (see MinSupportedTag below).

.PARAMETER Repo
        cubrid-docker repository URL. default https://github.com/CUBRID/cubrid-docker.git.

.PARAMETER Release
    Build from the published <VERSION>_latest tarball on ftp.cubrid.org
    instead of the newest nightly build. The full version then reads
    '<VERSION>-latest' (e.g. 11.4-latest), because that is all the release
    URL discloses.

.PARAMETER NoClean
    Reuse the BuildKit cache and the local base image: drops both --no-cache
    and --pull. Builds are clean by default because 'dnf -y update' and the
    package installs keep the same cache key however stale their results
    get. Use this for quick iteration, and combine with -NoCache / -Pull to
    re-enable one of them.

.PARAMETER NoCache
    Append --no-cache to docker build so the BuildKit cache is bypassed for
    this run. On by default; meaningful together with -NoClean.

.PARAMETER Pull
    Append --pull to docker build so the FROM base image is re-fetched from
    the registry even if a local copy already exists. On by default;
    meaningful together with -NoClean.

.PARAMETER Clean
    Kept for compatibility: a clean build (-NoCache -Pull) is the default,
    so this switch no longer changes anything. It still forces both flags on
    even when -NoClean is given.

.PARAMETER SkipRepoUpdate
    Skip the fetch / reset step and build from the clone as it currently is.
    Use when working offline, or when testing local edits to the clone.

.EXAMPLE
    PS> .\build_image.ps1
.EXAMPLE
    # Positional - shortest form:
    PS> .\build_image.ps1 10.2
.EXAMPLE
    PS> .\build_image.ps1 -v 11.4,11.3
.EXAMPLE
    PS> .\build_image.ps1 -Repo 'https://github.com/CUBRID/cubrid-docker.git'
.EXAMPLE
    # Published 11.4_latest tarball instead of tonight's build:
    PS> .\build_image.ps1 11.4 -Release
.EXAMPLE
    # Newest nightly of a patch level:
    PS> .\build_image.ps1 11.5.0
.EXAMPLE
    # One exact build (quote it; PowerShell would read 11.4 as a number):
    PS> .\build_image.ps1 '11.5.0.2632-e17fdab'
.EXAMPLE
    # Reuse cached layers for a quick iteration:
    PS> .\build_image.ps1 -NoClean
.EXAMPLE
    # Cached layers, but re-pull the base image:
    PS> .\build_image.ps1 -NoClean -Pull
#>

[CmdletBinding()]
param(
    # object[], not string[]: see ConvertTo-VersionTag for why.
    [Parameter(Position = 0)]
    [Alias('v', 'Tags')]
    [object[]]$Version = @('11.4'),
    [Parameter(Position = 1)]
    [string]$Repo = 'https://github.com/CUBRID/cubrid-docker.git',
    [switch]$Release,
    [switch]$NoClean,
    [switch]$NoCache,
    [switch]$Pull,
    [switch]$Clean,
    [switch]$SkipRepoUpdate
)

# Clean by default. The engine layer re-runs on its own once the rewritten
# URL changes, but the layers above it - 'dnf -y update', the package
# installs, the base image itself - keep the same cache key forever while
# what they produce goes stale, so a cached build ships month-old OS
# packages under a fresh engine.
$useNoCache = $NoCache.IsPresent -or $Clean.IsPresent -or (-not $NoClean.IsPresent)
$usePull    = $Pull.IsPresent    -or $Clean.IsPresent -or (-not $NoClean.IsPresent)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Oldest CUBRID version CUBRID For WSL supports. 10.0 / 10.1 are built
# FROM centos:7, whose package mirrors are retired, so they cannot be built
# any more and are deliberately out of scope.
$MinSupportedTag = [version]'10.2'

# Where both the nightly builds and the published _latest tarballs live.
$FtpEngineRoot = 'http://ftp.cubrid.org/CUBRID_Engine'

# ------------------------------------------------------------------
# Appended to a *copy* of each version's Dockerfile. ASCII only: it is
# appended with -Encoding Ascii so no BOM lands in the middle of the file.
#
# No file is COPYed here. 11.4 installs .cubrid.sh from its own repository
# copy, so the overlay only writes the file for versions that ship none, and
# leaves an existing one untouched. Same for the .bash_profile block, which
# makes the whole overlay idempotent.
# ------------------------------------------------------------------
$WslOverlay = @'

# ============================================================
# CUBRID For WSL overlay - appended by build_image.ps1.
# Normalises the layout the Windows side depends on:
#   /home/cubrid/.cubrid.sh        sourced by every wsl.exe call
#   /home/cubrid/CUBRID/databases  CUBRID_DATABASES as .cubrid.sh defines it
# ============================================================
RUN set -eux; \
    if [ ! -f /home/cubrid/.cubrid.sh ]; then \
        echo 'export CUBRID=/home/cubrid/CUBRID' >> /home/cubrid/.cubrid.sh; \
        echo 'export CUBRID_DATABASES=$CUBRID/databases' >> /home/cubrid/.cubrid.sh; \
        echo '' >> /home/cubrid/.cubrid.sh; \
        echo 'LD_LIBRARY_PATH=$CUBRID/lib:$CUBRID/cci/lib:$LD_LIBRARY_PATH' >> /home/cubrid/.cubrid.sh; \
        echo 'PATH=$CUBRID/bin:/usr/sbin:$PATH' >> /home/cubrid/.cubrid.sh; \
        echo 'export LD_LIBRARY_PATH SHLIB_PATH LIBPATH PATH' >> /home/cubrid/.cubrid.sh; \
    fi; \
    chmod 755 /home/cubrid/.cubrid.sh; \
    chown cubrid:cubrid /home/cubrid/.cubrid.sh; \
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

function ConvertTo-VersionTag {
    param($Value)

    if ($Value -is [double] -or $Value -is [single] -or $Value -is [decimal]) {
        return ([double]$Value).ToString('0.0###', [cultureinfo]::InvariantCulture)
    }
    return [string]$Value
}

function Get-TagNumber {
    <#
        Numeric part of a tag, as a [version]. A tag is one to four dotted
        numbers with an optional -<shorthash> suffix, so all four accepted
        forms reduce to a number that can be compared and sorted:

          11.5                  -> 11.5
          11.5.0                -> 11.5.0
          11.5.0.2632           -> 11.5.0.2632
          11.5.0.2632-e17fdab   -> 11.5.0.2632

        Returns $null when the tag is not of that shape.
    #>
    param([Parameter(Mandatory)][string]$Tag)

    if ($Tag -notmatch '^(\d+(?:\.\d+){1,3})(?:-[0-9a-fA-F]+)?$') { return $null }
    $parsed = $null
    if (-not [version]::TryParse($Matches[1], [ref]$parsed)) { return $null }
    return $parsed
}

function Get-RecipeTag {
    <#
        The <major>.<minor> a tag belongs to, which is the granularity
        cubrid-docker keeps its Dockerfiles at. '11.5.0.2632-e17fdab' and
        '11.5' both build from the 11.5 recipe.
    #>
    param([Parameter(Mandatory)][string]$Tag)

    $number = Get-TagNumber -Tag $Tag
    return "$($number.Major).$($number.Minor)"
}

function Test-SupportedTag {
    param([Parameter(Mandatory)][string]$Tag)

    $parsed = Get-TagNumber -Tag $Tag
    if ($null -eq $parsed) {
        throw @"
Tag '$Tag' is not a CUBRID version number.
Expected one of: 11.4 | 11.5.0 | 11.5.0.2632 | 11.5.0.2632-e17fdab
"@
    }
    if ($parsed -lt $MinSupportedTag) {
        throw @"
Tag '$Tag' is not supported. CUBRID For WSL supports $MinSupportedTag and later.
10.0 / 10.1 are built FROM centos:7, whose package mirrors are retired, so
those images can no longer be built.
"@
    }
}

function Resolve-EngineBuild {
    <#
        Which engine tarball to build from, and the full version string that
        names it. Both layouts put the version in the file name, so the full
        version and the URL are resolved together and always agree:

          nightly  <root>/nightly/daily_build/<FULL>/drop/CUBRID-<FULL>-Linux.x86_64.tar.gz
          release  <root>/<TAG>_latest/CUBRID-<TAG>-latest-Linux.x86_64.tar.gz

        A release URL discloses no build number, so its full version is just
        '<TAG>-latest' - which is what the installer is then named after.

        The tag may name a whole line or one exact build; everything shorter
        than a full directory name resolves to the newest build under it:

          11.5                -> newest 11.5.*
          11.5.0              -> newest 11.5.0.*
          11.5.0.2632         -> the 11.5.0.2632-<hash> build
          11.5.0.2632-e17fdab -> exactly that build
    #>
    param([Parameter(Mandatory)][string]$Tag)

    if ($Release) {
        # ftp.cubrid.org publishes one _latest per <major>.<minor>, so a tag
        # any more precise than that cannot be honoured. Building the minor
        # line instead would hand back a different engine than was asked for.
        $recipe = Get-RecipeTag -Tag $Tag
        if ($Tag -ne $recipe) {
            throw @"
-Release cannot build '$Tag': releases are published per <major>.<minor>.
Use '-v $recipe -Release' for the published $recipe release, or drop
-Release to build '$Tag' from the nightly archive.
"@
        }
        $full = "$Tag-latest"
        return [pscustomobject]@{
            FullVersion = $full
            Url         = "$FtpEngineRoot/${Tag}_latest/CUBRID-$full-Linux.x86_64.tar.gz"
        }
    }

    $index = "$FtpEngineRoot/nightly/daily_build/"
    Write-Host "Resolving the latest nightly build of $Tag ..."

    $html = (Invoke-WebRequest -Uri $index -UseBasicParsing).Content
    $all = [regex]::Matches($html, 'href="(\d+(?:\.\d+){1,3}-[0-9a-fA-F]+)/"') |
                ForEach-Object { $_.Groups[1].Value } |
                Select-Object -Unique

    # A tag matches a directory it names outright, or one it is a prefix of.
    # The separator is required so that '11.4' does not also match '11.40.*',
    # and '11.5.0.263' does not match '11.5.0.2632-...'.
    $dirs = @($all | Where-Object {
        $_ -eq $Tag -or $_.StartsWith("$Tag.") -or $_.StartsWith("$Tag-")
    })
    if ($dirs.Count -eq 0) {
        throw @"
No nightly build matching '$Tag' found under $index
Pass a whole line (11.5), a patch level (11.5.0), a build (11.5.0.2632) or
an exact directory (11.5.0.2632-e17fdab). For the published release of a
<major>.<minor> line, use -Release instead.
"@
    }

    # Sort on the numeric part only. As strings '10.2.18.9024' sorts below
    # '10.2.9.0867', which would pick a months-old build.
    $full = $dirs | Sort-Object { Get-TagNumber -Tag $_ } | Select-Object -Last 1
    if ($dirs.Count -gt 1) {
        Write-Host "  $($dirs.Count) builds match '$Tag'; taking the newest."
    }

    return [pscustomobject]@{
        FullVersion = $full
        Url         = "$index$full/drop/CUBRID-$full-Linux.x86_64.tar.gz"
    }
}

function Resolve-BuildDir {
    <#
        Directory in the clone whose Dockerfile builds $Tag.

        A version newer than anything the repository ships falls back to the
        newest directory it has: nightly builds appear for the development
        line (11.5 today) long before cubrid-docker grows a directory for it,
        and the recipes differ only in the engine tarball they fetch - which
        New-WslBuildContext rewrites per build anyway. A gap *below* the
        newest directory is a typo, not a new release line, so it still
        fails.
    #>
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Tag
    )

    # cubrid-docker keeps one directory per <major>.<minor>, so a tag naming
    # an exact build (11.5.0.2632-e17fdab) looks up the 11.5 recipe.
    $recipe = Get-RecipeTag -Tag $Tag

    $exact = Join-Path $Path $recipe
    if (Test-Path $exact) { return $exact }

    $dirs = Get-ChildItem -Directory -Path $Path |
                ForEach-Object {
                    $parsed = $null
                    if ([version]::TryParse($_.Name, [ref]$parsed)) {
                        [pscustomobject]@{ Name = $_.Name; Version = $parsed }
                    }
                } |
                Sort-Object Version
    if (-not $dirs) {
        throw "No CUBRID version directories found in '$Path'."
    }

    $newest = @($dirs)[-1]
    if ([version]$recipe -lt $newest.Version) {
        $available = (@($dirs) | ForEach-Object { $_.Name }) -join ', '
        throw @"
Build directory not found: $exact
Version '$recipe' does not exist in the cubrid-docker repository.
Available versions: $available
"@
    }

    Write-Host "cubrid-docker has no '$recipe' directory; building from '$($newest.Name)' (the newest it ships)."
    return (Join-Path $Path $newest.Name)
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
        Copy one version directory out of the clone, point its Dockerfile at
        the engine tarball we resolved, and lay the WSL overlay on top of the
        copy. Building from here keeps the clone pristine.
    #>
    param(
        [Parameter(Mandatory)][string]$SourceDir,
        [Parameter(Mandatory)][string]$StagingDir,
        [Parameter(Mandatory)][string]$EntrypointSh,
        [Parameter(Mandatory)][string]$DownloadUrl
    )

    if (Test-Path $StagingDir) { Remove-Item -Recurse -Force $StagingDir }
    New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

    Copy-Item -Recurse -Force -Path (Join-Path $SourceDir '*') -Destination $StagingDir

    $dockerfile = Join-Path $StagingDir 'Dockerfile'
    if (-not (Test-Path $dockerfile)) {
        throw "Dockerfile not found in build context: $dockerfile"
    }

    # Every version COPYs docker-entrypoint.sh, so replacing it in the staging
    # copy is enough - and the clone keeps its original.
    Copy-Item -Force $EntrypointSh (Join-Path $StagingDir 'docker-entrypoint.sh')

    # Repoint the engine download. The whole URL is replaced rather than the
    # ENV behind it, because only 11.4 builds the URL from ENV
    # (${VERSION_PATH} / ${CUBRID_VERSION}) - 10.2 through 11.3 hardcode the
    # directory in the wget line. Matching http://...-Linux.x86_64.tar.gz
    # covers both, and stops short of the quotes 11.4 wraps the URL in.
    $urlPattern = 'http://ftp\.cubrid\.org/CUBRID_Engine/\S*?CUBRID-\S*?-Linux\.x86_64\.tar\.gz'
    $content = Get-Content -Path $dockerfile -Raw
    if ($content -notmatch $urlPattern) {
        throw @"
No CUBRID engine download URL found in $dockerfile
The upstream Dockerfile layout changed; update `$urlPattern in New-WslBuildContext.
"@
    }
    $content -replace $urlPattern, $DownloadUrl |
        Set-Content -Path $dockerfile -Encoding Ascii -NoNewline

    Add-Content -Path $dockerfile -Value $WslOverlay -Encoding Ascii

    return $StagingDir
}

function Invoke-DockerBuild {
    param(
        [Parameter(Mandatory)][string[]]$Tags,
        [Parameter(Mandatory)][string]  $Context,
        [Parameter(Mandatory)][string]  $FullVersion
    )
    if ($Tags.Count -lt 1) {
        throw "Invoke-DockerBuild: at least one tag is required"
    }
    $buildArgs = @() + $commonBuildFlags
    # Travels with the image, so create_image.ps1 can read the engine version
    # back no matter how long after the build it runs.
    $buildArgs += @('--label', "cubrid.version=$FullVersion")
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

$Version = @($Version | ForEach-Object { ConvertTo-VersionTag $_ })
Write-Host "Versions    = $($Version -join ', ')"

foreach ($TagName in $Version) {
    Test-SupportedTag -Tag $TagName
}

$DockerEntrypoint = Join-Path $ShellPath 'docker-entrypoint.sh'
if (-not (Test-Path $DockerEntrypoint)) {
    throw "Source file not found: $DockerEntrypoint"
}

Test-DockerDaemon

Sync-CubridDockerRepo -Path $DockerPath -RepoUrl $Repo

$commonBuildFlags = @('build')
if ($useNoCache) { $commonBuildFlags += '--no-cache' }
if ($usePull)    { $commonBuildFlags += '--pull' }

Write-Host ""
Write-Host "Build options: NoCache=$useNoCache  Pull=$usePull  Release=$($Release.IsPresent)"

foreach ($TagName in $Version) {
    Write-Host ""
    Write-Host "===================================================="
    Write-Host " Building image for version: $TagName"
    Write-Host "===================================================="

    $engine = Resolve-EngineBuild -Tag $TagName
    Write-Host "Engine version: $($engine.FullVersion)"
    Write-Host "Engine tarball: $($engine.Url)"

    $BuildDirPath = Resolve-BuildDir -Path $DockerPath -Tag $TagName

    $StagingPath = New-WslBuildContext -SourceDir $BuildDirPath `
                                       -StagingDir (Join-Path $StagingRoot $TagName) `
                                       -EntrypointSh $DockerEntrypoint `
                                       -DownloadUrl $engine.Url
    Write-Host "Build context: $StagingPath ($(Split-Path -Leaf $BuildDirPath) recipe + WSL overlay)"

    Invoke-DockerBuild -Tags @("cubrid-wsl2:$TagName") `
                       -Context $StagingPath `
                       -FullVersion $engine.FullVersion
}

Write-Host ""
Write-Host "All builds completed."
