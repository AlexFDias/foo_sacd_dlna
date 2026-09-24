<#
.SYNOPSIS
    Checks that this machine has what BUILD.md says is required to compile
    foo_sacd_dlna, and reports exactly what is missing.

.DESCRIPTION
    Referenced from BUILD.md ("Verificar o WTL") and PREFERENCES_FIELDS.md.
    Checks, in order:
      1. Running on Windows.
      2. msbuild.exe is on PATH (i.e. this is a Developer Command Prompt /
         Developer PowerShell for VS 2022, or msbuild has otherwise been
         added to PATH).
      3. The foobar2000 SDK is discoverable next to this script's solution
         (best-effort: looks for a sibling folder containing foobar2000\SDK.h
         or similar, but does not fail the check if it can't find it, since
         SDK placement is documented as user-chosen in BUILD.md section 6).
      4. WTL headers are discoverable, using the same marker file
         (include\atlapp.h) that WTL.props itself looks for.

    Exit code 0 means every mandatory check passed; non-zero means at least
    one mandatory check failed. WTL/SDK discovery failures print the exact
    property (WTLIncludeDir / WTL_INCLUDE / WTL_ROOT) that would fix them,
    matching WTL.props.

.PARAMETER WtlInclude
    Explicit path to the WTL include directory (the folder that directly
    contains atlapp.h). If omitted, this script tries to auto-discover a
    sibling folder the same way WTL.props does.

.EXAMPLE
    .\tools\check_build_env.ps1

.EXAMPLE
    .\tools\check_build_env.ps1 -WtlInclude 'C:\dev\WTL10_10320\include'
#>
[CmdletBinding()]
param(
    [string]$WtlInclude
)

$ErrorActionPreference = 'Stop'
$failures = 0

function Write-Pass($msg) { Write-Host "  [PASS] $msg" -ForegroundColor Green }
function Write-Fail($msg) { Write-Host "  [FAIL] $msg" -ForegroundColor Red; $script:failures++ }
function Write-Info($msg) { Write-Host "  [INFO] $msg" -ForegroundColor Yellow }

Write-Host "== foo_sacd_dlna build environment check ==`n"

Write-Host "-- Platform --"
if ($IsWindows -or $env:OS -eq 'Windows_NT') {
    Write-Pass "Running on Windows"
} else {
    Write-Fail "This project only builds on Windows (Visual Studio 2022 / MSVC v142)"
}

Write-Host "`n-- MSBuild --"
$msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if ($msbuild) {
    Write-Pass "msbuild.exe found: $($msbuild.Source)"
} else {
    $msg = "msbuild.exe not found on PATH. Open a 'Developer PowerShell for VS 2022' " +
           "or 'Developer Command Prompt for VS 2022' instead of a plain shell (see BUILD.md section 10)."
    Write-Fail $msg
}

Write-Host "`n-- Visual Studio C++ toolset --"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsInfo = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($vsInfo) {
        Write-Pass "Visual Studio with the C++ x64/x86 build tools found: $vsInfo"
    } else {
        Write-Fail "vswhere did not find an installation with the C++ x64/x86 build tools (Workload: Desktop development with C++, see BUILD.md section 3)"
    }
} else {
    Write-Info "vswhere.exe not found (usually under Program Files (x86)\Microsoft Visual Studio\Installer); skipping this check"
}

Write-Host "`n-- WTL headers --"
$solutionDir = Split-Path -Parent $PSScriptRoot
$devRoot = Split-Path -Parent $solutionDir

if (-not $WtlInclude -and $env:WTL_INCLUDE) { $WtlInclude = $env:WTL_INCLUDE }
if (-not $WtlInclude -and $env:WTL_ROOT) { $WtlInclude = Join-Path $env:WTL_ROOT 'include' }

if (-not $WtlInclude) {
    # Same auto-discovery WTL.props performs: a sibling of the SDK root
    # containing include\atlapp.h.
    $marker = Get-ChildItem -Path $devRoot -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'include\atlapp.h' } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
    if ($marker) {
        $WtlInclude = Split-Path -Parent $marker
    }
}

if ($WtlInclude -and (Test-Path (Join-Path $WtlInclude 'atlapp.h'))) {
    Write-Pass "WTL headers found: $WtlInclude"
} else {
    $msg = ("WTL headers not found (looked for include\atlapp.h under {0}). " -f $devRoot) +
           "Set -WtlInclude explicitly, or the WTLIncludeDir / WTL_INCLUDE / WTL_ROOT " +
           "MSBuild property (see BUILD.md section 32)."
    Write-Fail $msg
}

Write-Host "`n-- foobar2000 SDK --"
$sdkMarker = Get-ChildItem -Path $devRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'foobar2000\SDK\foobar2000.h') } |
    Select-Object -First 1
if ($sdkMarker) {
    Write-Pass "foobar2000 SDK found: $($sdkMarker.FullName)"
} else {
    Write-Info "Could not auto-detect the foobar2000 SDK next to this repository (BUILD.md section 6 describes the recommended layout). This does not fail the check -- MSBuild will report a clear 'cannot open include file' error if the SDK paths in the project are wrong."
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "All mandatory checks passed." -ForegroundColor Green
    exit 0
} else {
    Write-Host "$failures mandatory check(s) failed." -ForegroundColor Red
    exit 1
}
