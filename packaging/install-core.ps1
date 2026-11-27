param([Parameter(Mandatory = $true)][string] $Destination)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$scratch = Join-Path (Split-Path -Parent $PSCommandPath) 'core-downloads'

function Receive-Archive($Url, $ExpectedHash, $Name) {
    $zip = Join-Path $scratch "$Name.zip"
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $zip -TimeoutSec 120
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash -ne $ExpectedHash) {
        throw "SHA-256 mismatch: $Name"
    }
    Expand-Archive -LiteralPath $zip -DestinationPath (Join-Path $scratch $Name)
}

try {
    New-Item -ItemType Directory -Path $scratch | Out-Null
    $sing = 'sing-box-1.14.5-windows-amd64'
    Receive-Archive "https://github.com/SagerNet/sing-box/releases/download/v1.14.5/$sing.zip" `
        'b935f7b38bffcc1f600165e79e5d0c2d58b547942ac1b6cf61a27b5447c089e7' 'sing-box'
    Receive-Archive 'https://www.wintun.net/builds/wintun-0.14.1.zip' `
        '07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51' 'wintun'

    $core = Join-Path $scratch "sing-box/$sing"
    $driver = Join-Path $scratch 'wintun/wintun'
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($name in @('sing-box.exe', 'libcronet.dll', 'LICENSE')) {
        Copy-Item -LiteralPath (Join-Path $core $name) -Destination $Destination
    }
    Copy-Item -LiteralPath (Join-Path $driver 'bin/amd64/wintun.dll') -Destination $Destination
    Copy-Item -LiteralPath (Join-Path $driver 'LICENSE.txt') `
        -Destination (Join-Path $Destination 'Wintun-LICENSE.txt')
} finally {
    if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
}
