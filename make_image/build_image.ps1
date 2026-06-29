<#
.SYNOPSIS
    build cubrid-wsl2 Docker image (Windows + Docker Desktop).

.DESCRIPTION
    porting of original bash script build_image.sh to PowerShell.
      1) if cubrid-docker repository is not exists, git clone.
      2) copy cubrid.sh / docker-entrypoint.sh to build directory.
      3) build cubrid-wsl2:<TAG> image with docker build.
      4) if TAG is equal to LatestTag, build cubrid-wsl2:latest image.

.PARAMETER Tags
    build tags. default ('11.4').

.PARAMETER LatestTag
    `latest` tag. default '11.4'.

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

.EXAMPLE
    PS> .\build_image.ps1
.EXAMPLE
    PS> .\build_image.ps1 -Tags 11.4,11.3 -LatestTag 11.4
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
    [string]$LatestTag = '11.4',
    [string]$Repo = 'https://github.com/CUBRID/cubrid-docker.git',
    [switch]$NoCache,
    [switch]$Pull,
    [switch]$Clean
)

$useNoCache = $NoCache.IsPresent -or $Clean.IsPresent
$usePull    = $Pull.IsPresent    -or $Clean.IsPresent

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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

$ShellPath  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$DockerPath = Join-Path $ShellPath 'cubrid-docker'

Write-Host "ShellPath  = $ShellPath"
Write-Host "DockerPath = $DockerPath"

Test-DockerDaemon

if (-not (Test-Path $DockerPath)) {
    Write-Host "Cloning cubrid-docker into $DockerPath ..."
    Invoke-Native -Description 'git clone' -- git clone $Repo $DockerPath
} elseif (-not (Test-Path (Join-Path $DockerPath '.git'))) {
    throw @"
Existing directory '$DockerPath' is not a git repository (missing .git).
Please remove it and retry:
  Remove-Item -Recurse -Force '$DockerPath'
"@
} else {
    Write-Host "cubrid-docker already exists. Pulling latest changes ..."
    Invoke-Native -Description 'git fetch' -- git -C $DockerPath fetch --all --prune
    Invoke-Native -Description 'git pull'  -- git -C $DockerPath pull --ff-only
}

$commonBuildFlags = @('build')
if ($useNoCache) { $commonBuildFlags += '--no-cache' }
if ($usePull)    { $commonBuildFlags += '--pull' }

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

Write-Host ""
Write-Host "Build options: NoCache=$useNoCache  Pull=$usePull  Clean=$($Clean.IsPresent)"

foreach ($TagName in $Tags) {
    Write-Host ""
    Write-Host "===================================================="
    Write-Host " Building image for tag: $TagName"
    Write-Host "===================================================="

    $BuildDirPath = Join-Path $DockerPath $TagName
    if (-not (Test-Path $BuildDirPath)) {
        throw "Build directory not found: $BuildDirPath"
    }

    $CubridSh         = Join-Path $ShellPath 'cubrid.sh'
    $DockerEntrypoint = Join-Path $ShellPath 'docker-entrypoint.sh'

    foreach ($f in @($CubridSh, $DockerEntrypoint)) {
        if (-not (Test-Path $f)) {
            throw "Source file not found: $f"
        }
    }

    Copy-Item -Force $CubridSh         (Join-Path $BuildDirPath 'cubrid.sh')
    Copy-Item -Force $DockerEntrypoint (Join-Path $BuildDirPath 'docker-entrypoint.sh')

    $imageTags = @("cubrid-wsl2:$TagName")
    if ($TagName -eq $LatestTag) {
        $imageTags += 'cubrid-wsl2:latest'
    }

    Push-Location $BuildDirPath
    try {
        Invoke-DockerBuild -Tags $imageTags -Context $BuildDirPath
    } finally {
        Pop-Location
    }
}

Write-Host ""
Write-Host "All builds completed."
