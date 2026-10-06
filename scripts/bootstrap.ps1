param([switch]$PortableSdk, [switch]$NoCmake)   # -NoCmake: use the CMake already on PATH (CI)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $root '.deps'
New-Item -ItemType Directory -Force -Path $deps | Out-Null
function Get-Package([string]$Url, [string]$Name, [string]$Destination) {
    $archive = Join-Path $deps "$Name.zip"
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $Url -OutFile $archive }
    if (-not (Test-Path -LiteralPath (Join-Path $deps $Destination))) {
        Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $deps $Destination)
    }
    Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Select-Object Path,Hash
}
Get-Package 'https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-1.1.63/OpenXR.Loader.1.1.63.nupkg' 'openxr' 'openxr'
if (-not $NoCmake) { Get-Package 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip' 'cmake' 'cmake' }
if ($PortableSdk) {
    Get-Package 'https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp/10.0.26100.8249/microsoft.windows.sdk.cpp.10.0.26100.8249.nupkg' 'winsdk' 'winsdk'
    Get-Package 'https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp.x64/10.0.26100.8249/microsoft.windows.sdk.cpp.x64.10.0.26100.8249.nupkg' 'winsdk-x64' 'winsdk-x64'
    Get-Package 'https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp.x86/10.0.26100.8249/microsoft.windows.sdk.cpp.x86.10.0.26100.8249.nupkg' 'winsdk-x86' 'winsdk-x86'
}
