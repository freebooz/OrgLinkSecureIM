param(
    [string]$Configuration = 'Release',
    [string]$CaFile = '',
    [string]$WorkspaceRoot = ''
)

$ErrorActionPreference = 'Stop'

# 脚本只更新当前生产构建输出目录，不删除源码、用户数据或其他配置的构建制品。.
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    # 某些受控 PowerShell 宿主不会提供 PSScriptRoot；从仓库根执行时以当前目录作为可靠回退。.
    $WorkspaceRoot = (Get-Location).Path
}
$workspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
$qtRoot = Join-Path $workspaceRoot '.tools\Qt\6.8.3\msvc2022_64'
$deployTool = Join-Path $qtRoot 'bin\windeployqt.exe'
$clientDirectory = Join-Path $workspaceRoot "build\windows-qt-production\apps\client\$Configuration"
$clientExecutable = Join-Path $clientDirectory 'orglink-client.exe'

if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    throw "windeployqt was not found: $deployTool"
}
if (-not (Test-Path -LiteralPath $clientExecutable -PathType Leaf)) {
    throw "Production client was not found. Build it first: $clientExecutable"
}

# windeployqt 根据 EXE 的真实导入表部署 Qt DLL、平台、TLS、SQL 和图像插件，避免人工漏拷依赖。.
& $deployTool --release --compiler-runtime --no-translations $clientExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

if ([string]::IsNullOrWhiteSpace($CaFile)) {
    $defaultCaFile = Join-Path $workspaceRoot 'deploy\docker\runtime-certs\server.crt'
    if (Test-Path -LiteralPath $defaultCaFile -PathType Leaf) {
        $CaFile = $defaultCaFile
    }
}

if (-not [string]::IsNullOrWhiteSpace($CaFile)) {
    $resolvedCaFile = (Resolve-Path -LiteralPath $CaFile).Path
    $certificateDirectory = Join-Path $clientDirectory 'certs'
    New-Item -ItemType Directory -Path $certificateDirectory -Force | Out-Null
    # 证书由部署者显式提供或来自当前本地 Compose，不在程序中硬编码信任锚。.
    Copy-Item -LiteralPath $resolvedCaFile -Destination (Join-Path $certificateDirectory 'server.crt') -Force
}

Write-Output "Windows client package is ready: $clientDirectory"
