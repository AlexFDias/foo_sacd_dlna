[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$WtlInclude = $env:WTL_INCLUDE,
    [Parameter(Mandatory=$false)]
    [string]$WtlRoot = $env:WTL_ROOT
)

$ErrorActionPreference = 'Stop'
$canonicalWtl = (Resolve-Path (Join-Path $PSScriptRoot '..\..\WTL\include') -ErrorAction SilentlyContinue).Path
if (-not $WtlInclude -and $canonicalWtl) { $WtlInclude = $canonicalWtl }

Write-Host "foo_sacd_dlna build environment check" -ForegroundColor Cyan
Write-Host "Project: v142 / x64"

if (-not $WtlInclude -and $WtlRoot) { $WtlInclude = Join-Path $WtlRoot 'include' }
if (-not $WtlInclude) {
    Write-Host "WTL include path not supplied." -ForegroundColor Yellow
    Write-Host "Use: .\tools\check_build_env.ps1 -WtlInclude 'C:\path\to\WTL\include'"
    exit 2
}

$atlapp = Join-Path $WtlInclude 'atlapp.h'
if (-not (Test-Path $atlapp)) {
    Write-Host "FAIL: atlapp.h not found: $atlapp" -ForegroundColor Red
    exit 3
}
Write-Host "OK: WTL atlapp.h -> $atlapp" -ForegroundColor Green

$required = @('atlctrls.h','atlwin.h','atlcrack.h')
foreach ($f in $required) {
    $path = Join-Path $WtlInclude $f
    if (-not (Test-Path $path)) {
        Write-Host "FAIL: missing WTL header: $path" -ForegroundColor Red
        exit 4
    }
}
Write-Host "OK: required WTL headers found" -ForegroundColor Green

$vswhereCandidates = @(
    "$env:ProgramFiles(x86)\Microsoft Visual Studio\Installer\vswhere.exe",
    "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
)
$vswhere = $vswhereCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($vswhere) {
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.v141 -property installationPath 2>$null
    if ($install) { Write-Host "Visual Studio: $install" }
} else {
    Write-Host "WARN: vswhere.exe not found; continuing." -ForegroundColor Yellow
}

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if ($cl) { Write-Host "OK: cl.exe -> $($cl.Source)" -ForegroundColor Green }
else { Write-Host "WARN: cl.exe is not on PATH. Run from a VS Developer Command Prompt." -ForegroundColor Yellow }

$msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if ($msbuild) { Write-Host "OK: msbuild.exe -> $($msbuild.Source)" -ForegroundColor Green }
else { Write-Host "WARN: msbuild.exe is not on PATH. Run from a VS Developer Command Prompt." -ForegroundColor Yellow }

Write-Host "Environment check passed for WTL." -ForegroundColor Green
