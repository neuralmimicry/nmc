param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('client', 'server')]
    [string]$Component,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [Alias('Input')]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$Platform,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$Rename
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($InputPath)) {
    throw 'Input artifact path is required.'
}

if ([string]::IsNullOrWhiteSpace($Rename)) {
    if ($Component -eq 'client') {
        $Rename = 'nmc.exe'
    }
    else {
        $Rename = 'nmc_server.exe'
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedInputPath = (Resolve-Path -LiteralPath $InputPath).Path
$archiveBaseName = "nmc-$Component-$Version-$Platform"
$outputPath = New-Item -ItemType Directory -Force -Path $OutputDir
$stageRoot = Join-Path $outputPath.FullName '.stage'
$payloadDir = Join-Path $stageRoot $archiveBaseName
$archivePath = Join-Path $outputPath.FullName "$archiveBaseName.zip"
$checksumPath = Join-Path $outputPath.FullName "$archiveBaseName.sha256.txt"

if (Test-Path $stageRoot) {
    Remove-Item -Recurse -Force $stageRoot
}
New-Item -ItemType Directory -Force -Path $payloadDir | Out-Null
Copy-Item -LiteralPath $resolvedInputPath -Destination (Join-Path $payloadDir $Rename)

$readmePath = Join-Path $repoRoot 'README.md'
if (Test-Path $readmePath) {
    Copy-Item -LiteralPath $readmePath -Destination (Join-Path $payloadDir 'README.md')
}

$versionPath = Join-Path $repoRoot 'VERSION'
if (Test-Path $versionPath) {
    Copy-Item -LiteralPath $versionPath -Destination (Join-Path $payloadDir 'VERSION')
}

if (Test-Path $archivePath) {
    Remove-Item -Force $archivePath
}
Compress-Archive -Path $payloadDir -DestinationPath $archivePath -Force

$hash = (Get-FileHash -Algorithm SHA256 $archivePath).Hash.ToLowerInvariant()
"$hash  $(Split-Path -Leaf $archivePath)" | Out-File -FilePath $checksumPath -Encoding ascii -NoNewline

Write-Host ''
Write-Host 'packaged NMC release artifacts:'
Write-Host "  $archivePath"
Write-Host "  $checksumPath"

Remove-Item -Recurse -Force $stageRoot
