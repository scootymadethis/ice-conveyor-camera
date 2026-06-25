<#
.SYNOPSIS
  Windows entrypoints for running the Python hub app.
  Creates/uses .venv, installs requirements.txt, then runs main.py without arguments.

  If execution is blocked:
      powershell -ExecutionPolicy Bypass -File .\hub.ps1 <command>

.EXAMPLE
  .\hub.ps1 venv          # create .venv + install requirements.txt
  .\hub.ps1 install       # install/update requirements.txt into .venv
  .\hub.ps1 run           # install requirements.txt, then run main.py
  .\hub.ps1 start         # alias of run
#>

$ErrorActionPreference = "Stop"

$Command = "help"
$PyVersion = ""

if ($args.Count -ge 1) {
    $Command = [string]$args[0]
}

if ($args.Count -ge 2) {
    $PyVersion = [string]$args[1]
}

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

$venvPy = Join-Path $here ".venv\Scripts\python.exe"
$requirements = Join-Path $here "requirements.txt"
$main = Join-Path $here "main.py"

function Require-Venv {
    if (-not (Test-Path $venvPy)) {
        Write-Host ".venv not found - run: .\hub.ps1 venv" -ForegroundColor Yellow
        exit 2
    }
}

function Require-Requirements {
    if (-not (Test-Path $requirements)) {
        Write-Host "requirements.txt missing" -ForegroundColor Yellow
        exit 2
    }
}

function Require-Main {
    if (-not (Test-Path $main)) {
        Write-Host "main.py missing" -ForegroundColor Yellow
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

function Install-Requirements {
    Require-Venv
    Require-Requirements

    Write-Host "upgrading pip..." -ForegroundColor Cyan
    & $venvPy -m pip install -q -U pip

    Write-Host "installing requirements.txt..." -ForegroundColor Cyan
    & $venvPy -m pip install -r $requirements
}

switch ($Command) {
    "venv" {
        $pyCmd = Find-Python

        if (-not $pyCmd) {
            Write-Host "Python non trovato. Installa Python 3.10-3.13 o abilita il launcher py." -ForegroundColor Yellow
            exit 2
        }

        $exe = $pyCmd[0]
        $pre = @()
        if ($pyCmd.Count -gt 1) {
            $pre = $pyCmd[1..($pyCmd.Count - 1)]
        }

        Write-Host "creating .venv with: $($pyCmd -join ' ')" -ForegroundColor Cyan

        & $exe @pre -m venv .venv

        if ($LASTEXITCODE -ne 0) {
            Write-Host "Errore durante la creazione della .venv" -ForegroundColor Red
            exit $LASTEXITCODE
        }

        & $venvPy -m pip install -q -U pip

        if (Test-Path $requirements) {
            Write-Host "installing requirements.txt..." -ForegroundColor Cyan
            & $venvPy -m pip install -r $requirements
        }
        else {
            Write-Host "requirements.txt non trovato: .venv creata senza installare dipendenze" -ForegroundColor Yellow
        }

        Write-Host ".venv ready" -ForegroundColor Green
    }

    "install" {
        Install-Requirements
        Write-Host "requirements installed" -ForegroundColor Green
    }

    "run" {
        Require-Venv
        Require-Requirements
        Require-Main

        Install-Requirements

        Write-Host "running main.py..." -ForegroundColor Cyan
        & $venvPy $main
        exit $LASTEXITCODE
    }

    "start" {
        Require-Venv
        Require-Requirements
        Require-Main

        Install-Requirements

        Write-Host "running main.py..." -ForegroundColor Cyan
        & $venvPy $main
        exit $LASTEXITCODE
    }

    default {
        Write-Host @"
Python hub - Windows entrypoints (run from the project folder)

  .\hub.ps1 venv [pyver]        create .venv + install requirements.txt
                                optional pyver example: 3.12

  .\hub.ps1 install             install/update requirements.txt into .venv

  .\hub.ps1 run                 install requirements, then run main.py
  .\hub.ps1 start               alias of run

If execution is blocked:
  powershell -ExecutionPolicy Bypass -File .\hub.ps1 <command>
"@
    }
}
