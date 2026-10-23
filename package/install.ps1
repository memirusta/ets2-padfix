<#
  padfix installer.

  Finds the game in your Steam libraries, copies the plugin into
  bin\win_x64\plugins and, if the game has blacklisted "Stable Gamepad",
  moves it to the whitelist.

  Usage:
    install.bat                          Install
    install.bat -GameDir "D:\...\Euro Truck Simulator 2"
    uninstall.bat                        Uninstall
#>
param(
    [switch]$Uninstall,
    [string]$GameDir
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

$games = @(
    @{ Name = 'Euro Truck Simulator 2'; Exe = 'eurotrucks2' },
    @{ Name = 'American Truck Simulator'; Exe = 'amtrucks' }
)

function Get-SteamLibraries {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) {
        return @()
    }
    $libraries = @($steam -replace '/', '\')
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
            $libraries += $m.Groups[1].Value -replace '\\\\', '\'
        }
    }
    # Steam reports the same folder with different letter case (d:\steam, D:\Steam).
    $libraries | ForEach-Object { [IO.Path]::GetFullPath($_).TrimEnd('\') } | Sort-Object -Unique
}

function Find-Installs {
    $candidates = @()
    if ($GameDir) {
        $candidates += $GameDir
    }
    else {
        foreach ($library in Get-SteamLibraries) {
            foreach ($game in $games) {
                $candidates += Join-Path $library "steamapps\common\$($game.Name)"
            }
        }
    }
    foreach ($dir in ($candidates | Sort-Object -Unique)) {
        foreach ($game in $games) {
            if (Test-Path -LiteralPath (Join-Path $dir "bin\win_x64\$($game.Exe).exe")) {
                [pscustomobject]@{ Game = $game; Dir = $dir }
            }
        }
    }
}

$installs = @(Find-Installs)
if ($installs.Count -eq 0) {
    Write-Host 'Game not found.' -ForegroundColor Red
    Write-Host 'Pass the game folder yourself:  install.bat -GameDir "D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2"'
    exit 1
}

$documents = [Environment]::GetFolderPath('MyDocuments')
$failed = $false

foreach ($install in $installs) {
    $name = $install.Game.Name
    Write-Host ''
    Write-Host "== $name ($($install.Dir))" -ForegroundColor Cyan

    if (Get-Process -Name $install.Game.Exe -ErrorAction SilentlyContinue) {
        Write-Host "  $name is running. Close the game first, then run this again." -ForegroundColor Red
        $failed = $true
        continue
    }
    Get-Process -Name padfix_helper -ErrorAction SilentlyContinue | Stop-Process -Force

    $plugins = Join-Path $install.Dir 'bin\win_x64\plugins'
    $dll = Join-Path $plugins 'padfix.dll'
    $helperDir = Join-Path $plugins 'padfix'

    if ($Uninstall) {
        Remove-Item -LiteralPath $dll -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $helperDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host '  padfix removed.' -ForegroundColor Green
        Write-Host '  Remember to bind your controls to your own gamepad again in the game.'
        continue
    }

    New-Item -ItemType Directory -Force -Path $helperDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $here 'files\padfix.dll') -Destination $dll -Force
    Copy-Item -LiteralPath (Join-Path $here 'files\padfix\padfix_helper.exe') -Destination (Join-Path $helperDir 'padfix_helper.exe') -Force
    Write-Host '  Plugin copied.' -ForegroundColor Green

    # The game sometimes hides the device in the controls menu; the helper fixes that.
    $globalControls = Join-Path $documents "$name\global_controls.sii"
    if (Test-Path -LiteralPath $globalControls) {
        $helper = Join-Path $helperDir 'padfix_helper.exe'
        $code = (Start-Process -FilePath $helper -ArgumentList '--fix-whitelist', "`"$globalControls`"" -Wait -PassThru).ExitCode
        switch ($code) {
            0 { Write-Host '  Stable Gamepad is not blacklisted.' }
            2 { Write-Host '  Stable Gamepad was removed from the blacklist and whitelisted.' -ForegroundColor Green }
            default { Write-Host '  Could not read global_controls.sii, skipped this step.' -ForegroundColor Yellow }
        }
    }
}

if (-not $Uninstall) {
    Write-Host ''
    Write-Host 'Next steps:' -ForegroundColor Cyan
    Write-Host '  1. In Steam, open the game''s Properties > Controller and disable Steam Input.'
    Write-Host '  2. Start the game and continue past the "advanced SDK features" warning.'
    Write-Host '  3. Options > Controls > Input type: choose "Keyboard + Stable Gamepad" and bind your buttons.'
}

if ($failed) {
    exit 1
}
