param(
    [Parameter(Mandatory = $true)][string] $App,
    [Parameter(Mandatory = $true)][string] $Stage,
    [Parameter(Mandatory = $true)][string] $OutputDir,
    [string] $VCRedist
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -Raw (Join-Path $repo 'cmake/VERSION')).Trim()
$App = (Resolve-Path -LiteralPath $App).Path
$stagePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Stage)
$null = New-Item -ItemType Directory -Force -Path $OutputDir
$output = Join-Path (Resolve-Path -LiteralPath $OutputDir).Path "sunlens-$version-windows-x64.exe"
if (Test-Path -LiteralPath $output) { throw "Refusing to overwrite $output" }

$qtDeploy = (Get-Command windeployqt.exe -ErrorAction Stop).Source
$qtVersion = & $qtDeploy --version
if ($LASTEXITCODE -ne 0 -or ($qtVersion -join ' ') -notmatch '\b6\.8\.3\b') {
    throw 'The installer notices and source URL require Qt 6.8.3.'
}
& cmake "-DAPP=$App" "-DWINDEPLOYQT=$qtDeploy" "-DSTAGE=$stagePath" -DQT_ONLY=ON `
    -P (Join-Path $repo 'cmake/stage-windows-x64.cmake')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }

$licenseDir = Join-Path $stagePath 'qt-licenses'
$null = New-Item -ItemType Directory -Path $licenseDir
foreach ($license in @('LGPL-3.0-only', 'GPL-3.0-only')) {
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/qt/qtbase/v6.8.3/LICENSES/$license.txt" `
        -OutFile (Join-Path $licenseDir "$license.txt") -TimeoutSec 60
}

if (-not $VCRedist) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Visual Studio installation not found' }
    $VCRedist = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Redist/MSVC') -Recurse -File `
        -Filter 'vc_redist.x64.exe' | Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $VCRedist) { throw 'Pass the official x64 Redistributable with -VCRedist' }
$VCRedist = (Resolve-Path -LiteralPath $VCRedist).Path
$signature = Get-AuthenticodeSignature -LiteralPath $VCRedist
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') {
    throw 'Expected a valid Microsoft signature for the x64 Visual C++ Redistributable'
}

$nsis = (Get-Command makensis.exe -ErrorAction SilentlyContinue).Source
if (-not $nsis) { $nsis = Join-Path ${env:ProgramFiles(x86)} 'NSIS/makensis.exe' }
& $nsis "/DSUNLENS_VERSION=$version" "/DSTAGE=$stagePath" "/DVCREDIST=$VCRedist" "/DOUTPUT=$output" `
    (Join-Path $PSScriptRoot 'sunlens-x64.nsi')
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output -PathType Leaf)) { throw 'NSIS failed' }
