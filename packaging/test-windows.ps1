param(
    [Parameter(Mandatory = $true)][string] $Installer,
    [Parameter(Mandatory = $true)][string] $Fixture
)

$ErrorActionPreference = 'Stop'
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$fixturePath = (Resolve-Path -LiteralPath $Fixture).Path
$target = Join-Path $env:ProgramFiles 'Sunlens'
if (Test-Path -LiteralPath $target) { throw "Test requires a clean install target: $target" }

function Invoke-Process($Exe, $Options, $Allowed = @(0)) {
    $process = Start-Process -FilePath $Exe -ArgumentList $Options -WorkingDirectory $env:RUNNER_TEMP `
        -PassThru -Wait
    if ($process.ExitCode -notin $Allowed) { throw "$Exe returned $($process.ExitCode)" }
}

$uninstaller = Join-Path $target 'uninstall.exe'
$app = Join-Path $target 'app/sunlens.exe'
$core = Join-Path $target 'app/cores/sing-box'
try {
    try {
        Invoke-Process $installerPath '/S' @(0, 3010)
    } catch {
        $setupLog = Join-Path $target 'setup-error.log'
        if (Test-Path -LiteralPath $setupLog) { Write-Host (Get-Content -Raw -LiteralPath $setupLog) }
        throw
    }
    foreach ($file in @($app, (Join-Path $core 'sing-box.exe'), (Join-Path $core 'wintun.dll'))) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing installed file: $file" }
    }
    foreach ($protocol in @('http', 'socks5')) {
        $config = Join-Path $env:RUNNER_TEMP "sunlens-$protocol.json"
        & $fixturePath --write-config $protocol | Set-Content -Encoding utf8NoBOM $config
        if ($LASTEXITCODE -ne 0) { throw "Could not generate $protocol fixture" }
        & (Join-Path $core 'sing-box.exe') check -c $config
        if ($LASTEXITCODE -ne 0) { throw "Installed sing-box rejected $protocol fixture" }
    }

    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:QT_PLUGIN_PATH, Env:QML2_IMPORT_PATH, Env:QML_IMPORT_PATH -ErrorAction SilentlyContinue
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_QUICK_BACKEND = 'software'
    Invoke-Process $app '--smoke-test'
} finally {
    if (Test-Path -LiteralPath $uninstaller) { Invoke-Process $uninstaller '/S' }
}
if (Test-Path -LiteralPath $app) { throw 'The uninstall left the application executable behind' }
