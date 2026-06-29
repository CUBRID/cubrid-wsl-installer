<#
.SYNOPSIS
    create tar / tar.gz for WSL2 import from cubrid-wsl2 Docker image
    (Windows + Docker Desktop).

.DESCRIPTION
    porting of original bash script create_image.sh to PowerShell.
    each TAG:
      1) if container with the same name exists, stop / rm.
      2) create container with docker create -i.
      3) export rootfs to tar with docker export.
      4) compress with tar -czf. (use bsdtar included in Windows 10/11).
      5) move tar / tar.gz file to tar_images / targz_images directory.
      6) remove container.

.PARAMETER BaseImageName
    base image name. default 'cubrid-wsl2'.

.PARAMETER Tags
    list of tags to process. default ('latest', '11.4').

.EXAMPLE
    PS> .\create_image.ps1
.EXAMPLE
    PS> .\create_image.ps1 -Tags latest,11.4,11.3
#>

[CmdletBinding()]
param(
    [string]$BaseImageName = 'cubrid-wsl2',
    [string[]]$Tags = @('latest', '11.4')
)

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

$ShellPath = Split-Path -Parent $MyInvocation.MyCommand.Definition
$TarDir    = Join-Path $ShellPath 'tar_images'
$TarGzDir  = Join-Path $ShellPath 'targz_images'

Write-Host "ShellPath = $ShellPath"
Write-Host "TarDir    = $TarDir"
Write-Host "TarGzDir  = $TarGzDir"

Test-DockerDaemon

if (Test-Path $TarDir)   { Remove-Item -Recurse -Force $TarDir }
if (Test-Path $TarGzDir) { Remove-Item -Recurse -Force $TarGzDir }
New-Item -ItemType Directory -Path $TarDir   | Out-Null
New-Item -ItemType Directory -Path $TarGzDir | Out-Null

$tarCmd = Get-Command tar.exe -ErrorAction SilentlyContinue
if (-not $tarCmd) {
    throw "tar.exe not found. Windows 10 1803+ / Windows 11 or separate bsdtar/Git for Windows is required."
}

Push-Location $ShellPath
try {
    foreach ($TagName in $Tags) {
        Write-Host ""
        Write-Host "===================================================="
        Write-Host " Creating image for tag: $TagName"
        Write-Host "===================================================="

        $ImageName       = "${BaseImageName}:${TagName}"
        $ContainerName   = "cubrid-$TagName"
        $Wsl2ImageName   = 'cubrid-wsl2.tar'
        $CopyImageName   = "cubrid-wsl2-$TagName.tar"
        $Wsl2ImageGzName = "cubrid-wsl2-$TagName.tar.gz"

        $existing = (docker ps -aq -f "name=^/${ContainerName}$") | Out-String
        $existing = $existing.Trim()
        if ($existing) {
            Write-Host "Container $ContainerName already exists. Stop and remove it."
            docker stop $ContainerName 2>$null | Out-Null
            Invoke-Native -Description "docker rm $ContainerName" -- docker rm $ContainerName
        } else {
            Write-Host "Container $ContainerName does not exist. Will create new container."
        }

        Write-Host "Creating container $ContainerName"
        Invoke-Native -Description "docker create $ContainerName" `
            -- docker create -i --name $ContainerName $ImageName

        Write-Host "Exporting container $ContainerName to $Wsl2ImageName"
        Invoke-Native -Description "docker export $ContainerName" `
            -- docker export --output $Wsl2ImageName $ContainerName
        Write-Host "Image $ImageName exported successfully"

        Write-Host "Compressing $Wsl2ImageName -> $Wsl2ImageGzName"
        Invoke-Native -Description "tar -czf $Wsl2ImageGzName" `
            -- tar -czf $Wsl2ImageGzName $Wsl2ImageName

        $tarGzDest = Join-Path $TarGzDir $Wsl2ImageGzName
        $tarDest   = Join-Path $TarDir   $CopyImageName
        Move-Item -Force $Wsl2ImageGzName $tarGzDest
        Move-Item -Force $Wsl2ImageName   $tarDest

        Write-Host "Compressed Image path:   $tarGzDest"
        Write-Host "Uncompressed Image path: $tarDest"

        Write-Host "Removing container $ContainerName"
        Invoke-Native -Description "docker rm $ContainerName" -- docker rm $ContainerName
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "All images created."
