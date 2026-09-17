$ErrorActionPreference = "Stop"

if (-not (Test-Path "extern\AseApi")) {
    New-Item -ItemType Directory -Force "extern" | Out-Null
    git clone https://github.com/ArkServerApi/AseApi.git extern/AseApi
}

$msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
if (-not $msbuild) {
    throw "MSBuild was not found. Install Visual Studio 2022 Build Tools with 'Desktop development with C++'."
}

msbuild KBDHybridBridge.sln /m /p:Configuration=Release /p:Platform=x64

New-Item -ItemType Directory -Force "server-package\KBDHybridBridge" | Out-Null
Copy-Item "build\KBDHybridBridge.dll" "server-package\KBDHybridBridge\" -Force
Copy-Item "PluginInfo.json" "server-package\KBDHybridBridge\" -Force
Copy-Item "config.json" "server-package\KBDHybridBridge\" -Force

if (Test-Path "KBDHybridBridge-server.zip") { Remove-Item "KBDHybridBridge-server.zip" -Force }
Compress-Archive -Path "server-package\KBDHybridBridge" -DestinationPath "KBDHybridBridge-server.zip"

Write-Host "Built: KBDHybridBridge-server.zip"
