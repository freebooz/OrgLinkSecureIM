[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$deployDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$environmentFile = Join-Path $deployDirectory '.env'

if (-not (Test-Path -LiteralPath $environmentFile)) {
    throw '缺少 deploy/docker/.env。请复制 .env.example，并填写数据库、消息密钥和初始管理员秘密。.'
}

# ConvertFrom-StringData 原生处理注释和 CRLF/LF，不输出值；避免多行正则在旧版 Windows PowerShell 中误判。
$rawEnvironment = [IO.File]::ReadAllText($environmentFile, [Text.Encoding]::UTF8)
$environmentValues = ConvertFrom-StringData -StringData $rawEnvironment

$hasAdminPassword = $environmentValues.ContainsKey('ORGLINK_POSTGRES_ADMIN_PASSWORD') -and
    [Text.Encoding]::UTF8.GetByteCount([string]$environmentValues['ORGLINK_POSTGRES_ADMIN_PASSWORD']) -ge 1
$hasAppPassword = $environmentValues.ContainsKey('ORGLINK_DATABASE_PASSWORD') -and
    [Text.Encoding]::UTF8.GetByteCount([string]$environmentValues['ORGLINK_DATABASE_PASSWORD']) -ge 1
$hasMessageKey = $environmentValues.ContainsKey('ORGLINK_MESSAGE_STORAGE_KEY') -and
    [Text.Encoding]::UTF8.GetByteCount([string]$environmentValues['ORGLINK_MESSAGE_STORAGE_KEY']) -ge 32
$hasBootstrapPassword = $environmentValues.ContainsKey('ORGLINK_BOOTSTRAP_PASSWORD') -and
    [Text.Encoding]::UTF8.GetByteCount([string]$environmentValues['ORGLINK_BOOTSTRAP_PASSWORD']) -ge 12
$hasMinioAccessKey = $environmentValues.ContainsKey('ORGLINK_MINIO_ACCESS_KEY') -and
    [Text.Encoding]::UTF8.GetByteCount([string]$environmentValues['ORGLINK_MINIO_ACCESS_KEY']) -ge 3
$hasMinioSecretKey = $environmentValues.ContainsKey('ORGLINK_MINIO_SECRET_KEY') -and
    [Text.Encoding]::UTF8.GetByteCount([string]$environmentValues['ORGLINK_MINIO_SECRET_KEY']) -ge 16
$hasLiveKitApiKey = $environmentValues.ContainsKey('ORGLINK_LIVEKIT_API_KEY') -and
    [Text.Encoding]::UTF8.GetByteCount([string]$environmentValues['ORGLINK_LIVEKIT_API_KEY']) -ge 3
$hasLiveKitApiSecret = $environmentValues.ContainsKey('ORGLINK_LIVEKIT_API_SECRET') -and
    [Text.Encoding]::UTF8.GetByteCount([string]$environmentValues['ORGLINK_LIVEKIT_API_SECRET']) -ge 16
if (-not ($hasAdminPassword -and $hasAppPassword -and $hasMessageKey -and $hasBootstrapPassword -and
          $hasMinioAccessKey -and $hasMinioSecretKey -and $hasLiveKitApiKey -and $hasLiveKitApiSecret)) {
    throw '.env 中数据库、消息加密、MinIO 或 LiveKit 的密钥缺失/长度不符合要求。'
}

# LiveKit 的 ICE 候选和客户端 WebSocket 地址必须是客户端可直达的宿主机 IPv4。
# 未显式配置时依据默认路由选择地址，并仅注入当前 Compose 进程，不把探测结果写回含秘密的 .env。
function Resolve-OrgLinkLanAddress {
    $routes = Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
        Where-Object { $_.State -eq 'Alive' } |
        Sort-Object RouteMetric, InterfaceMetric
    foreach ($route in $routes) {
        $address = Get-NetIPAddress -AddressFamily IPv4 -InterfaceIndex $route.InterfaceIndex -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -notlike '127.*' -and $_.AddressState -eq 'Preferred' } |
            Select-Object -First 1 -ExpandProperty IPAddress
        if ($address) { return [string]$address }
    }
    throw '无法自动确定局域网 IPv4，请在 .env 设置 ORGLINK_PUBLIC_HOST 和 ORGLINK_LIVEKIT_NODE_IP。'
}

$configuredPublicHost = if ($environmentValues.ContainsKey('ORGLINK_PUBLIC_HOST')) {
    [string]$environmentValues['ORGLINK_PUBLIC_HOST']
} else { '' }
$configuredNodeIp = if ($environmentValues.ContainsKey('ORGLINK_LIVEKIT_NODE_IP')) {
    [string]$environmentValues['ORGLINK_LIVEKIT_NODE_IP']
} else { '' }
$detectedAddress = ''
if ([string]::IsNullOrWhiteSpace($configuredPublicHost) -or [string]::IsNullOrWhiteSpace($configuredNodeIp)) {
    $detectedAddress = Resolve-OrgLinkLanAddress
}
$env:ORGLINK_PUBLIC_HOST = if ([string]::IsNullOrWhiteSpace($configuredPublicHost)) {
    $detectedAddress
} else { $configuredPublicHost }
$env:ORGLINK_LIVEKIT_NODE_IP = if ([string]::IsNullOrWhiteSpace($configuredNodeIp)) {
    $detectedAddress
} else { $configuredNodeIp }

$certificateDirectory = Join-Path $deployDirectory 'runtime-certs'
New-Item -ItemType Directory -Path $certificateDirectory -Force | Out-Null
docker compose --project-directory $deployDirectory --env-file $environmentFile -f (Join-Path $deployDirectory 'compose.yml') up -d --build --wait
if ($LASTEXITCODE -ne 0) {
    # docker 是原生进程，PowerShell 的 Stop 策略不会自动把其非零退出码转换为异常；这里显式失败，防止部署脚本误报成功。
    throw "Docker Compose 启动失败，退出码：$LASTEXITCODE。请检查镜像拉取、构建输出和容器日志。"
}
