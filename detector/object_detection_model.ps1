<#
.SYNOPSIS
  Windows entrypoints for building/flashing the ESP32 object_detection_model.
  Runs PlatformIO from detector\.venv (mirrors the Makefile detector/object_detection_model targets).
  If execution is blocked:
      powershell -ExecutionPolicy Bypass -File .\object_detection_model.ps1 <command>

.EXAMPLE
  .\object_detection_model.ps1 venv          # create .venv + install PlatformIO
  .\object_detection_model.ps1 model       # check that include\model_data.h contains the generated model
  .\object_detection_model.ps1 build         # pio run
  .\object_detection_model.ps1 upload        # pio run -t upload  (flash via COM)
  .\object_detection_model.ps1 monitor       # pio device monitor (serial @ 115200)
  .\object_detection_model.ps1 ports         # list serial devices (find your COM port)
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
        Write-Host "PlatformIO venv not found - run: .\object_detection_model.ps1 venv" -ForegroundColor Yellow
        exit 2
    }
}

function Require-Model {
    if (-not (Test-Path (Join-Path $here "include\model_data.h"))) {
        Write-Host "include\model_data.h missing - generate the model header first" -ForegroundColor Yellow
        exit 2
    }
}

function Find-Python {
    if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
        return $null
    }

    if ($PyVersion) {
        $v = [string]$PyVersion

        if ($v -match '^(\d+)\.(\d+)') {
            $v = "$($Matches[1]).$($Matches[2])"
        }

        try {
            & py "-$v" --version 1>$null 2>$null
            if ($LASTEXITCODE -eq 0) {
                return @("py", "-$v")
            }
        } catch {}
    }

    foreach ($v in "3.13", "3.12", "3.11", "3.10") {
        try {
            & py "-$v" --version 1>$null 2>$null
            if ($LASTEXITCODE -eq 0) {
                return @("py", "-$v")
            }
        } catch {}
    }

    try {
        & py --version 1>$null 2>$null
        if ($LASTEXITCODE -eq 0) {
            return @("py")
        }
    } catch {}

    return $null
}

switch ($Command) {
    "venv" {
        $pyCmd = Find-Python

        if (-not $pyCmd) {
            Write-Host "Python non trovato. Installa Python 3.10-3.13 o abilita il launcher py." -ForegroundColor Yellow
            exit 2
        }

        $exe = $pyCmd[0]
        $pre = @(); if ($pyCmd.Count -gt 1) { $pre = $pyCmd[1..($pyCmd.Count - 1)] }

        Write-Host "creating .venv with: $($pyCmd -join ' ')" -ForegroundColor Cyan
        & $exe @pre -m venv .venv
        & $venvPy -m pip install -q -U pip
        & $venvPy -m pip install -q -U platformio
        & $pio --version
    }
    "model" {
        $dst = Join-Path $here "include\model_data.h"
        if (Test-Path $dst) {
            Write-Host "include\model_data.h found. Build can continue." -ForegroundColor Green
        }
        else {
            Write-Host "include\model_data.h missing. Generate it from the model pipeline before building." -ForegroundColor Yellow
            exit 2
        }
    }
    "build" {
        Require-Model; Require-Pio
        & $pio run
        exit $LASTEXITCODE
    }
    "upload" {
        Require-Model; Require-Pio
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
ESP32 object_detection_model - Windows entrypoints (run from the detector\ folder)

  .\object_detection_model.ps1 venv [pyver]          create .venv + install PlatformIO
                                       (optional pyver e.g. 3.12 if PlatformIO
                                        does not support your default Python)
  .\object_detection_model.ps1 model               check that include\model_data.h contains the generated model
  .\object_detection_model.ps1 build                 pio run
  .\object_detection_model.ps1 upload                pio run -t upload  (flash via COM)
  .\object_detection_model.ps1 monitor               pio device monitor (serial @ 115200)
  .\object_detection_model.ps1 ports                 list serial devices (find your COM port)

If execution is blocked: powershell -ExecutionPolicy Bypass -File .\object_detection_model.ps1 <command>
"@
    }
}
