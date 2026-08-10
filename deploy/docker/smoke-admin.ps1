param(
    [string]$EnvironmentFile = (Join-Path $PSScriptRoot '.env'),
    [int]$AdminWebPort = 7444
)

$ErrorActionPreference = 'Stop'

function Read-OrgLinkEnvironment([string]$Path) {
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*([^#=]+)=(.*)$') {
            $values[$matches[1].Trim()] = $matches[2].Trim()
        }
    }
    return $values
}

function Resolve-OrgLinkPublicHost([hashtable]$Values) {
    if (-not [string]::IsNullOrWhiteSpace([string]$Values['ORGLINK_PUBLIC_HOST'])) {
        return [string]$Values['ORGLINK_PUBLIC_HOST']
    }
    # 与一键部署脚本保持一致：优先选择可路由的私网 IPv4，避免用回环地址掩盖局域网证书问题。
    $candidate = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
        Sort-Object InterfaceMetric |
        Select-Object -First 1 -ExpandProperty IPAddress
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        throw '无法确定管理端局域网地址。'
    }
    return $candidate
}

$values = Read-OrgLinkEnvironment $EnvironmentFile
$certificatePath = Join-Path $PSScriptRoot 'runtime-certs\server.crt'
$cookieFile = [System.IO.Path]::GetTempFileName()

try {
    $baseUri = "https://$(Resolve-OrgLinkPublicHost $values):$AdminWebPort"
    # curl 使用部署 CA 完成证书链和主机名校验；禁止使用 -k 或其他跳过 TLS 校验的参数。
    $health = curl.exe --silent --show-error --fail --cacert $certificatePath "$baseUri/health" | ConvertFrom-Json
    $loginPayload = @{
        loginName = [string]$values['ORGLINK_BOOTSTRAP_LOGIN']
        password = [string]$values['ORGLINK_BOOTSTRAP_PASSWORD']
    } | ConvertTo-Json -Compress
    $login = $loginPayload | curl.exe --silent --show-error --fail --cacert $certificatePath `
        --cookie-jar $cookieFile -H 'Content-Type: application/json' --data-binary '@-' `
        "$baseUri/api/admin/auth/login" | ConvertFrom-Json

    $overview = curl.exe --silent --show-error --fail --cacert $certificatePath --cookie $cookieFile `
        "$baseUri/api/admin/overview" | ConvertFrom-Json
    $tree = curl.exe --silent --show-error --fail --cacert $certificatePath --cookie $cookieFile `
        "$baseUri/api/admin/organizations/tree" | ConvertFrom-Json
    $people = curl.exe --silent --show-error --fail --cacert $certificatePath --cookie $cookieFile `
        "$baseUri/api/admin/persons?departmentId=0&page=1&pageSize=20" | ConvertFrom-Json
    $files = curl.exe --silent --show-error --fail --cacert $certificatePath --cookie $cookieFile `
        "$baseUri/api/admin/files?page=1&pageSize=20" | ConvertFrom-Json

    $invalidCsrfStatus = '{}' | curl.exe --silent --output NUL --write-out '%{http_code}' `
        --cacert $certificatePath --cookie $cookieFile -H 'Content-Type: application/json' `
        -H 'X-CSRF-Token: invalid' --data-binary '@-' "$baseUri/api/admin/departments"

    [pscustomobject]@{
        Health = $health.status
        LoginRole = $login.role
        Departments = $overview.departments
        People = $overview.people
        Online = $overview.onlinePeople
        OrganizationsReturned = @($tree.organizations).Count
        DepartmentsReturned = @($tree.departments).Count
        PositionsReturned = @($tree.positions).Count
        PersonPageCount = @($people.items).Count
        FilePageCount = @($files.items).Count
        InvalidCsrfStatus = [int]$invalidCsrfStatus
    }
}
finally {
    if (Test-Path -LiteralPath $cookieFile) {
        # Cookie 文件只用于单次回归，完成后精确删除，避免管理会话令牌落盘残留。
        Remove-Item -LiteralPath $cookieFile -Force
    }
    $loginPayload = $null
}
