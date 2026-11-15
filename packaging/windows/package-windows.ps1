# Builds the Windows release: a folder OneNode\ in a zip, with Qt and OpenSSL inside it.
#
# Usage: pwsh packaging/windows/package-windows.ps1 -Build build -Version v0.1.0 -Out dist -OpenSslBin C:\vcpkg\installed\x64-windows\bin
#
# Why OpenSSL goes into the folder: OneNode forces Qt's OpenSSL TLS backend (the same on every system, with TLS 1.3).
# Qt loads libssl-3-x64.dll and libcrypto-3-x64.dll from the program's own folder.
param(
    [Parameter(Mandatory = $true)] [string] $Build,
    [Parameter(Mandatory = $true)] [string] $Version,
    [Parameter(Mandatory = $true)] [string] $Out,
    [Parameter(Mandatory = $true)] [string] $OpenSslBin
)
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$folder = Join-Path $Out "OneNode"
Remove-Item -Recurse -Force $folder -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $folder | Out-Null

Copy-Item (Join-Path $Build "Release\appOneNode.exe") $folder
Copy-Item (Join-Path $Build "Release\onenode-cli.exe") $folder

# --no-translations / --no-opengl-sw keep the download small (Qt Quick uses Direct3D, the software OpenGL copy is 20 MB)
windeployqt --release --no-translations --no-opengl-sw --qmldir (Join-Path $root "qml") (Join-Path $folder "appOneNode.exe")
windeployqt --release --no-translations --no-opengl-sw (Join-Path $folder "onenode-cli.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

Copy-Item (Join-Path $OpenSslBin "libssl-3-x64.dll") $folder
Copy-Item (Join-Path $OpenSslBin "libcrypto-3-x64.dll") $folder
Copy-Item (Join-Path $root "LICENSE") $folder
Copy-Item (Join-Path $root "THIRD_PARTY_NOTICES.md") $folder

# The TLS plugin must be there, or every connection would fail
if (-not (Test-Path (Join-Path $folder "tls\qopensslbackend.dll"))) { throw "tls\qopensslbackend.dll is missing from the package" }

$zip = Join-Path $Out "OneNode-$Version-windows-x64.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $folder -DestinationPath $zip
Write-Host "Wrote $zip"
