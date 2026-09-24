<#
.SYNOPSIS
    Reproducible MSBuild invocation for foo_sacd_dlna.sln.

.DESCRIPTION
    Referenced from BUILD.md section 32. Wraps the msbuild command lines
    from BUILD.md sections 9/10 so Configuration, Platform and an optional
    explicit WTL path are always passed consistently, and so CI (or a human)
    doesn't have to remember the exact msbuild flags.

.PARAMETER Configuration
    Debug or Release. Default: Debug.

.PARAMETER Platform
    Build platform. Only x64 is supported by this project (see BUILD.md
    section 7 -- do not use Win32/x86). Default: x64.

.PARAMETER WtlInclude
    Optional explicit path to the WTL include directory (passed through to
    MSBuild as the WTLIncludeDir property). If omitted, WTL.props performs
    its own auto-discovery as documented in BUILD.md section 32.

.PARAMETER Clean
    Run an MSBuild Clean before Build, matching BUILD.md section 10's
    "build limpo" example.

.EXAMPLE
    .\tools\build.ps1 -Configuration Debug -Platform x64

.EXAMPLE
    .\tools\build.ps1 -Configuration Release -Platform x64 -WtlInclude 'C:\dev\WTL10_10320\include'
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [string]$WtlInclude,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$solutionPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'foo_sacd_dlna.sln'
if (-not (Test-Path $solutionPath)) {
    Write-Error "Could not find foo_sacd_dlna.sln next to tools\ (expected at $solutionPath)."
    exit 1
}

if (-not (Get-Command msbuild.exe -ErrorAction SilentlyContinue)) {
    Write-Error "msbuild.exe not found on PATH. Run this from a 'Developer PowerShell for VS 2022' (see BUILD.md section 10), or run .\tools\check_build_env.ps1 first to diagnose the environment."
    exit 1
}

$msbuildArgs = @(
    "`"$solutionPath`"",
    "/m",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform"
)
if ($WtlInclude) {
    $msbuildArgs += "/p:WTLIncludeDir=`"$WtlInclude`""
}

if ($Clean) {
    Write-Host "== msbuild /t:Clean $Configuration|$Platform ==" -ForegroundColor Cyan
    & msbuild.exe @msbuildArgs "/t:Clean"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Clean failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

Write-Host "== msbuild /t:Build $Configuration|$Platform ==" -ForegroundColor Cyan
& msbuild.exe @msbuildArgs "/t:Build"
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "`nBuild succeeded: $Configuration|$Platform" -ForegroundColor Green
} else {
    Write-Host "`nBuild failed with exit code $exitCode" -ForegroundColor Red
}
exit $exitCode
