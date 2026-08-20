<#
.SYNOPSIS
    create tar / tar.gz for WSL2 import from cubrid-wsl2 Docker image
    (Windows + Docker Desktop).

.DESCRIPTION
    each TAG:
      1) verify the cubrid-wsl2:<TAG> image exists (built by build_image.ps1).
      2) if container with the same name exists, stop / rm.
      3) create container with docker create -i.
      4) export rootfs to tar with docker export.
      5) compress with tar -czf. (use bsdtar included in Windows 10/11).
      6) move tar / tar.gz file to tar_images / targz_images directory.
      7) publish the tar.gz to ..\os_image\<TAG>\cubrid-wsl2-latest.tar.gz,
         overwriting whatever is there. build.bat -v <TAG> picks the image up
         from that per-version directory.
      8) remove container.

    Only the artifacts of the tags being processed are replaced, so images
    built for other versions stay in tar_images / targz_images and keep their
    own os_image\<TAG> directory.

    Two file names are fixed on purpose:
      - the tar *inside* the tar.gz is always cubrid-wsl2.tar, because the
        installer extracts it by that name (CUB_WSL_EXTRACT_IMAGE_FILE).
      - the published tar.gz is always cubrid-wsl2-latest.tar.gz, because WiX
        harvests it by that name (CUB_WSL_IMAGE_FILE). The version lives in
        the directory name, not the file name.

.PARAMETER Version
    CUBRID versions to process, default ('11.4'). Positional, so
    `.\create_image.ps1 10.2` works, and aliased to -v to match build.bat.
    Versions below 10.2 are rejected (see MinSupportedTag below).

.PARAMETER BaseImageName
    base image name. default 'cubrid-wsl2'.

.EXAMPLE
    PS> .\create_image.ps1
.EXAMPLE
    # Positional - shortest form:
    PS> .\create_image.ps1 10.2
.EXAMPLE
    PS> .\create_image.ps1 -v 11.4,11.3
#>

[CmdletBinding()]
param(
    # object[], not string[]: see ConvertTo-VersionTag for why.
    [Parameter(Position = 0)]
    [Alias('v', 'Tags')]
    [object[]]$Version = @('11.4'),
    [Parameter(Position = 1)]
    [string]$BaseImageName = 'cubrid-wsl2'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Oldest CUBRID version CUBRID For WSL supports; kept in step with
# build_image.ps1.
$MinSupportedTag = [version]'10.2'

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

function Test-DockerImage {
    param(
        [Parameter(Mandatory)][string]$ImageName,
        [Parameter(Mandatory)][string]$Tag
    )

    $imageId = (& docker images -q $ImageName | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($imageId)) {
        throw @"
Docker image '$ImageName' not found.
Build it first:
  .\build_image.ps1 $Tag
"@
    }
    Write-Host "Found image $ImageName ($imageId)"
}

$ShellPath   = Split-Path -Parent $MyInvocation.MyCommand.Definition
$TarDir      = Join-Path $ShellPath 'tar_images'
$TarGzDir    = Join-Path $ShellPath 'targz_images'
$OsImageRoot = Join-Path (Split-Path -Parent $ShellPath) 'os_image'

# Name WiX harvests the image by (CUB_WSL_IMAGE_FILE in CMakeLists.txt).
# It never carries the version - os_image\<TAG>\ does.
$InstallImageName = 'cubrid-wsl2-latest.tar.gz'

Write-Host "ShellPath   = $ShellPath"
Write-Host "TarDir      = $TarDir"
Write-Host "TarGzDir    = $TarGzDir"
Write-Host "OsImageRoot = $OsImageRoot"

$Version = @($Version | ForEach-Object { ConvertTo-VersionTag $_ })
Write-Host "Versions    = $($Version -join ', ')"

foreach ($TagName in $Version) {
    Test-SupportedTag -Tag $TagName
}

Test-DockerDaemon

# Keep artifacts of versions that are not being rebuilt: create the output
# directories if missing, but never wipe them wholesale.
foreach ($dir in @($TarDir, $TarGzDir)) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
}

$tarCmd = Get-Command tar.exe -ErrorAction SilentlyContinue
if (-not $tarCmd) {
    throw "tar.exe not found. Windows 10 1803+ / Windows 11 or separate bsdtar/Git for Windows is required."
}

Push-Location $ShellPath
try {
    foreach ($TagName in $Version) {
        Write-Host ""
        Write-Host "===================================================="
        Write-Host " Creating image for version: $TagName"
        Write-Host "===================================================="

        $ImageName       = "${BaseImageName}:${TagName}"
        $ContainerName   = "cubrid-$TagName"
        $Wsl2ImageName   = 'cubrid-wsl2.tar'
        $CopyImageName   = "cubrid-wsl2-$TagName.tar"
        $Wsl2ImageGzName = "cubrid-wsl2-$TagName.tar.gz"

        Test-DockerImage -ImageName $ImageName -Tag $TagName

        $nameFilter = 'name=^/' + [regex]::Escape($ContainerName) + '$'
        $existing = (docker ps -aq -f $nameFilter) | Out-String
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

        try {
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

            # Publish into the per-version directory build.bat -v <TAG> reads.
            $osImageVersionDir = Join-Path $OsImageRoot $TagName
            if (-not (Test-Path $osImageVersionDir)) {
                New-Item -ItemType Directory -Path $osImageVersionDir -Force | Out-Null
            }
            $installImageDest = Join-Path $osImageVersionDir $InstallImageName
            Copy-Item -Force $tarGzDest $installImageDest
            Write-Host "Install image path:      $installImageDest"
        } finally {
            Write-Host "Removing container $ContainerName"
            Invoke-Native -Description "docker rm $ContainerName" -- docker rm $ContainerName
        }
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "All images created."
