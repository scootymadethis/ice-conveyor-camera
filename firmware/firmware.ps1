<#
.SYNOPSIS
  Windows entrypoints for building/flashing the ESP32-S3-EYE firmware.
  Runs PlatformIO from firmware\.venv (mirrors the Makefile firmware-* targets).
  If execution is blocked:
      powershell -ExecutionPolicy Bypass -File .\firmware.ps1 <command>

.EXAMPLE
  .\firmware.ps1 venv          # create .venv + install PlatformIO
  .\firmware.ps1 secrets       # create include\secrets.h from the example
  .\firmware.ps1 build         # pio run
  .\firmware.ps1 upload        # pio run -t upload  (flash via COM)
  .\firmware.ps1 monitor       # pio device monitor (serial @ 115200)
  .\firmware.ps1 ports         # list serial devices (find your COM port)
#>
param(
    [Parameter(Position = 0)][string]$Command = "help",
    [Parameter(Position = 1)][string]$PyVersion = ""
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
$pio = Join-Path $here ".venv\Scripts\pio.exe"
$venvPy = Join-Path $here ".venv\Scripts\python.exe"

function Require-Pio {
    if (-not (Test-Path $pio)) {
        Write-Host "PlatformIO venv not found - run: .\firmware.ps1 venv" -ForegroundColor Yellow
        exit 2
    }
}

function Require-Secrets {
    if (-not (Test-Path (Join-Path $here "include\secrets.h"))) {
        Write-Host "include\secrets.h missing - run: .\firmware.ps1 secrets   then edit it" -ForegroundColor Yellow
        exit 2
    }
}

function Find-Python {
    # PlatformIO supports Python 3.6-3.13; prefer a known-good launcher if present
    # (the default Python may be too new for PlatformIO).

    if ($PyVersion) {
        $v = [string]$PyVersion

        # Convert "3.13.0" -> "3.13"
        if ($v -match '^(\d+)\.(\d+)') {
            $v = "$($Matches[1]).$($Matches[2])"
        }

        return @("py", "-$v")
    }

    foreach ($v in "3.13", "3.12", "3.11", "3.10") {
        & py "-$v" --version 1>$null 2>$null
        if ($LASTEXITCODE -eq 0) {
            return @("py", "-$v")
        }
    }

    return @("py")
}

switch ($Command) {
    "venv" {
        $pyCmd = Find-Python
        $exe = $pyCmd[0]
        $pre = @(); if ($pyCmd.Count -gt 1) { $pre = $pyCmd[1..($pyCmd.Count - 1)] }
        Write-Host "creating .venv with: $($pyCmd -join ' ')" -ForegroundColor Cyan
        & $exe @pre -m venv .venv
        & $venvPy -m pip install -q -U pip
        & $venvPy -m pip install -q -U platformio
        & $pio --version
    }
    "secrets" {
        $dst = Join-Path $here "include\secrets.h"
        if (Test-Path $dst) {
            Write-Host "include\secrets.h already exists (not overwritten)" -ForegroundColor Yellow
        }
        else {
            Copy-Item (Join-Path $here "include\secrets.example.h") $dst
            Write-Host "created include\secrets.h - edit Wi-Fi + HUB_BASE_URL before flashing" -ForegroundColor Green
        }
    }
    "build" {
        Require-Secrets; Require-Pio
        & $pio run
        exit $LASTEXITCODE
    }
    "upload" {
        Require-Secrets; Require-Pio
        & $pio run -t upload
        exit $LASTEXITCODE
    }
    "monitor" {
        Require-Pio
        & $pio device monitor
    }
    "ports" {
        Require-Pio
        & $pio device list
    }
    default {
        Write-Host @"
ESP32-S3-EYE firmware - Windows entrypoints (run from the firmware\ folder)

  .\firmware.ps1 venv [pyver]          create .venv + install PlatformIO
                                       (optional pyver e.g. 3.12 if PlatformIO
                                        does not support your default Python)
  .\firmware.ps1 secrets               create include\secrets.h from the example
  .\firmware.ps1 build                 pio run
  .\firmware.ps1 upload                pio run -t upload  (flash via COM)
  .\firmware.ps1 monitor               pio device monitor (serial @ 115200)
  .\firmware.ps1 ports                 list serial devices (find your COM port)

If execution is blocked: powershell -ExecutionPolicy Bypass -File .\firmware.ps1 <command>
"@
    }
}
