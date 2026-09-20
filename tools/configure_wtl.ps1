param(
    [Parameter(Mandatory=$true)][string]$WtlInclude,
    [Parameter(Mandatory=$true)][string]$SdkRoot
)

$ErrorActionPreference = 'Stop'
$wtl = (Resolve-Path $WtlInclude).Path
$sdk = (Resolve-Path $SdkRoot).Path

$projects = @(
    (Join-Path $sdk 'foobar2000\helpers\foobar2000_sdk_helpers.vcxproj'),
    (Join-Path $sdk 'libPPUI\libPPUI.vcxproj'),
    (Join-Path $sdk 'foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj')
)

foreach ($project in $projects) {
    if (-not (Test-Path $project)) {
        Write-Warning "Project not found: $project"
        continue
    }

    Copy-Item $project "$project.bak" -Force
    [xml]$xml = Get-Content $project
    $ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
    $ns.AddNamespace('msb','http://schemas.microsoft.com/developer/msbuild/2003')

    $nodes = $xml.SelectNodes('//msb:ClCompile/msb:AdditionalIncludeDirectories', $ns)
    foreach ($node in $nodes) {
        $value = [string]$node.InnerText
        if ($value -notlike "*$wtl*") {
            $node.InnerText = "$wtl;$value"
        }
    }

    $xml.Save($project)
    Write-Host "Updated: $project"
}

Write-Host "WTL include configured: $wtl"
