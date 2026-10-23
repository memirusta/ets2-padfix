<#
  Builds the plugin and packs dist\padfix-<version>.zip:
    install.bat, uninstall.bat, install.ps1, README.md, LICENSE,
    THIRD_PARTY_SCS_SDK.txt, files\padfix.dll, files\padfix\padfix_helper.exe
#>
param([string]$Version = '1.0.0')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

cmd /c "`"$root\build.bat`""
if ($LASTEXITCODE -ne 0) {
    throw 'build failed'
}

$name = "padfix-$Version"
$stage = Join-Path $root "dist\$name"
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'files\padfix') | Out-Null

Copy-Item (Join-Path $root 'package\*') $stage
Copy-Item (Join-Path $root 'README.md'), (Join-Path $root 'LICENSE') $stage
Copy-Item (Join-Path $root 'sdk\sdk_license.txt') (Join-Path $stage 'THIRD_PARTY_SCS_SDK.txt')
Copy-Item (Join-Path $root 'build\padfix.dll') (Join-Path $stage 'files\padfix.dll')
Copy-Item (Join-Path $root 'build\padfix_helper.exe') (Join-Path $stage 'files\padfix\padfix_helper.exe')

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Host "Created $zip"
