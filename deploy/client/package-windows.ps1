param(
    [string]$Configuration = 'Release',
    [string]$CaFile = '',
    [string]$WorkspaceRoot = '',
    [string]$VcInstallDirectory = ''
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

<# 自动发现 Visual Studio 的 VC 根目录，确保普通 PowerShell 中运行脚本也能部署编译器运行库。 #>
if ([string]::IsNullOrWhiteSpace($VcInstallDirectory)) {
    $visualStudioRoot = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022'
    foreach ($edition in (Get-ChildItem -LiteralPath $visualStudioRoot -Directory -ErrorAction SilentlyContinue)) {
        $candidate = Join-Path $edition.FullName 'VC'
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            $VcInstallDirectory = $candidate
            break
        }
    }
}
if (-not [string]::IsNullOrWhiteSpace($VcInstallDirectory)) {
    $VcInstallDirectory = [IO.Path]::GetFullPath($VcInstallDirectory)
    $env:VCINSTALLDIR = $VcInstallDirectory
}

# 扫描 QML 源目录才能部署 Qt Quick Controls 与 Multimedia 的动态插件；仅扫描 EXE 无法发现声明式导入。
$qmlSourceDirectory = Join-Path $workspaceRoot 'apps\client\qml'
& $deployTool --release --compiler-runtime --no-translations --qmldir $qmlSourceDirectory $clientExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

<# 将 MSVC x64 CRT 作为应用本地 DLL 部署，目标机器无需先运行 vc_redist 安装器即可启动客户端。 #>
if (-not [string]::IsNullOrWhiteSpace($VcInstallDirectory)) {
    $redistRoot = Join-Path $VcInstallDirectory 'Redist\MSVC'
    $crtDirectory = ''
    $redistVersions = Get-ChildItem -LiteralPath $redistRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending
    foreach ($redistVersion in $redistVersions) {
        $candidate = Join-Path $redistVersion.FullName 'x64\Microsoft.VC143.CRT'
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            $crtDirectory = $candidate
            break
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($crtDirectory)) {
        foreach ($runtimeLibrary in (Get-ChildItem -LiteralPath $crtDirectory -Filter '*.dll' -File)) {
            Copy-Item -LiteralPath $runtimeLibrary.FullName -Destination $clientDirectory -Force
        }
    }
}

if (-not [string]::IsNullOrWhiteSpace($CaFile)) {
    $resolvedCaFile = (Resolve-Path -LiteralPath $CaFile).Path
    $certificateDirectory = Join-Path $clientDirectory 'certs'
    New-Item -ItemType Directory -Path $certificateDirectory -Force | Out-Null
    # 证书由部署者显式提供或来自当前本地 Compose，不在程序中硬编码信任锚。.
    Copy-Item -LiteralPath $resolvedCaFile -Destination (Join-Path $certificateDirectory 'server.crt') -Force
} else {
    <# 发布包不得默认夹带开发/测试 CA；本地联调通过 ORGLINK_TLS_CA_FILE 显式指向工作区外部信任锚。 #>
    $packagedCaFile = Join-Path $clientDirectory 'certs\server.crt'
    if (Test-Path -LiteralPath $packagedCaFile -PathType Leaf) {
        Remove-Item -LiteralPath $packagedCaFile -Force
    }
    $certificateDirectory = Join-Path $clientDirectory 'certs'
    if ((Test-Path -LiteralPath $certificateDirectory -PathType Container) -and
        -not (Get-ChildItem -LiteralPath $certificateDirectory -Force)) {
        Remove-Item -LiteralPath $certificateDirectory -Force
    }
}

Write-Output "Windows client package is ready: $clientDirectory"
