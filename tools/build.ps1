[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Debug',
    [ValidateSet('x64')]
    [string]$Platform = 'x64',
    [Parameter(Mandatory=$false)]
    [string]$WtlInclude = $env:WTL_INCLUDE,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$canonicalWtl = (Resolve-Path (Join-Path $PSScriptRoot '..\..\WTL\include') -ErrorAction SilentlyContinue).Path
if (-not $WtlInclude -and $canonicalWtl) { $WtlInclude = $canonicalWtl }
$root = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $root 'foo_sacd_dlna.sln'

if (-not $WtlInclude -and $env:WTL_ROOT) { $WtlInclude = Join-Path $env:WTL_ROOT 'include' }
if (-not $WtlInclude) { throw "WTL include path not specified. Use -WtlInclude or set WTL_INCLUDE / WTL_ROOT." }
if (-not (Test-Path (Join-Path $WtlInclude 'atlapp.h'))) { throw "atlapp.h not found under $WtlInclude" }

$msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if (-not $msbuild) { throw "msbuild.exe not found. Run this script from a Visual Studio Developer PowerShell/Command Prompt." }

if ($Clean) {
    & $msbuild.Source $solution /t:Clean /m "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:WTLIncludeDir=$WtlInclude"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

& $msbuild.Source $solution /t:Build /m /v:minimal "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:WTLIncludeDir=$WtlInclude"
exit $LASTEXITCODE
