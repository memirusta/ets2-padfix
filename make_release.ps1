<#
  Builds the plugin and packs dist\padfix-<version>.zip:
    padfix_setup.exe, install.bat, uninstall.bat, README.md, LICENSE,
    THIRD_PARTY_SCS_SDK.txt, THIRD_PARTY_MINHOOK.txt,
    files\padfix.dll, files\padfix\padfix_helper.exe

  Code signing: Windows Smart App Control blocks unsigned programs and plugins
  outright, and antivirus software trusts signed ones far more. Pass the SHA-1
  thumbprint of a code-signing certificate in your Windows certificate store
  (or set PADFIX_SIGN_THUMBPRINT) to sign padfix.dll, padfix_helper.exe and
  padfix_setup.exe before they are packed:
    make_release.ps1 -Version 1.1.0 -SignThumbprint <thumbprint>
#>
param(
    [string]$Version = '1.0.0',
    [string]$SignThumbprint = $env:PADFIX_SIGN_THUMBPRINT,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version must look like 1.2.3, got '$Version'"
}

$env:PADFIX_VERSION = $Version
cmd /c "`"$root\build.bat`""
if ($LASTEXITCODE -ne 0) {
    throw 'build failed'
}

$binaries = 'padfix.dll', 'padfix_helper.exe', 'padfix_setup.exe' | ForEach-Object { Join-Path $root "build\$_" }
if ($SignThumbprint) {
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Sort-Object { [version]$_.Directory.Parent.Name } | Select-Object -Last 1
    if (-not $signtool) {
        throw 'signtool.exe not found. Install the Windows SDK.'
    }
    & $signtool.FullName sign /sha1 $SignThumbprint /fd sha256 /tr $TimestampUrl /td sha256 $binaries
    if ($LASTEXITCODE -ne 0) {
        throw 'signing failed'
    }
    & $signtool.FullName verify /pa /q $binaries
    if ($LASTEXITCODE -ne 0) {
        throw 'signature verification failed'
    }
    Write-Host 'Binaries signed.'
}
else {
    Write-Warning 'This release is NOT code-signed. Smart App Control will block it and antivirus software may flag it.'
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
Copy-Item (Join-Path $root 'build\padfix_setup.exe') (Join-Path $stage 'padfix_setup.exe')
Copy-Item (Join-Path $root 'build\padfix.dll') (Join-Path $stage 'files\padfix.dll')
Copy-Item (Join-Path $root 'build\padfix_helper.exe') (Join-Path $stage 'files\padfix\padfix_helper.exe')

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Host "Created $zip"
