$PAT   = $env:GITHUB_PAT   # export GITHUB_PAT=ghp_... avant d'executer
$Owner = "ThomasDelosEpsi"
$Repo  = "SatisfactoryModStats"

$headers = @{
    Authorization          = "Bearer $PAT"
    Accept                 = "application/vnd.github+json"
    "X-GitHub-Api-Version" = "2022-11-28"
}

$user = Invoke-RestMethod "https://api.github.com/user" -Headers $headers
Write-Host "Login : $($user.login)"

$baseUrl = "https://api.github.com/repos/$Owner/$Repo/actions/variables"

$vars = [ordered]@{
    UE_ROOT    = "CONFIGURER_MANUELLEMENT"
    SF_PROJ    = "CONFIGURER_MANUELLEMENT"
    MOD_OUTPUT = "C:\ModBuilds\FactoryMonitor"
}

foreach ($name in $vars.Keys) {
    $val  = $vars[$name]
    $body = '{"name":"' + $name + '","value":"' + $val + '"}'
    try {
        Invoke-RestMethod -Method PATCH -Uri "$baseUrl/$name" -Headers $headers -Body $body -ContentType "application/json" | Out-Null
        Write-Host "[UPDATED] $name"
    } catch {
        try {
            Invoke-RestMethod -Method POST -Uri $baseUrl -Headers $headers -Body $body -ContentType "application/json" | Out-Null
            Write-Host "[CREATED] $name"
        } catch {
            $code = $_.Exception.Response.StatusCode.value__
            Write-Host "[FAILED $code] $name : $($_.Exception.Message)"
        }
    }
}
