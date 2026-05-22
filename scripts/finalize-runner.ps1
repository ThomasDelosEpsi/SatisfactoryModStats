$RunnerDir = "C:\actions-runner"
$Owner     = "ThomasDelosEpsi"
$Repo      = "SatisfactoryModStats"
$PAT       = $env:GITHUB_PAT   # export GITHUB_PAT=ghp_... avant d'executer

Write-Host ""
Write-Host "=== 1. Verification du PAT ===" -ForegroundColor Cyan
try {
    $resp   = Invoke-WebRequest -Uri "https://api.github.com/user" `
        -Headers @{ Authorization = "Bearer $PAT"; Accept = "application/vnd.github+json" } `
        -UseBasicParsing
    $user   = ($resp.Content | ConvertFrom-Json).login
    $scopes = $resp.Headers["X-OAuth-Scopes"]
    Write-Host "  Login  : $user" -ForegroundColor Green
    if ($scopes) { Write-Host "  Scopes : $scopes" }
    else         { Write-Host "  Type   : Fine-grained PAT (pas de scopes affiches)" }
} catch {
    Write-Host "  ERREUR : $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== 2. Detection des chemins ===" -ForegroundColor Cyan

$sfCandidates = @(
    "C:\Program Files (x86)\Steam\steamapps\common\Satisfactory\FactoryGame.uproject",
    "D:\Steam\steamapps\common\Satisfactory\FactoryGame.uproject",
    "C:\SteamLibrary\steamapps\common\Satisfactory\FactoryGame.uproject",
    "D:\SteamLibrary\steamapps\common\Satisfactory\FactoryGame.uproject",
    "E:\SteamLibrary\steamapps\common\Satisfactory\FactoryGame.uproject",
    "C:\Games\Satisfactory\FactoryGame.uproject",
    "D:\Games\Satisfactory\FactoryGame.uproject"
)
$ueCandidates = @(
    "C:\Program Files\Epic Games\UE_5.3",
    "D:\Program Files\Epic Games\UE_5.3",
    "C:\UE_5.3", "D:\UE_5.3", "E:\UE_5.3"
)

$sfProj = $null
foreach ($c in $sfCandidates) { if (Test-Path $c) { $sfProj = $c; break } }
$ueRoot = $null
foreach ($c in $ueCandidates) { if (Test-Path $c) { $ueRoot = $c; break } }

if ($sfProj) { Write-Host "  Satisfactory : $sfProj" -ForegroundColor Green }
else         { $sfProj = "CONFIGURER_MANUELLEMENT"; Write-Host "  Satisfactory : NON DETECTE" -ForegroundColor Yellow }

if ($ueRoot)  { Write-Host "  UE 5.3       : $ueRoot" -ForegroundColor Green }
else          { $ueRoot = "CONFIGURER_MANUELLEMENT"; Write-Host "  UE 5.3       : NON DETECTE" -ForegroundColor Yellow }

$modOut = "C:\ModBuilds\FactoryMonitor"
if (-not (Test-Path $modOut)) { New-Item -ItemType Directory -Path $modOut -Force | Out-Null }
Write-Host "  ModOutput    : $modOut" -ForegroundColor Green

Write-Host ""
Write-Host "=== 3. Variables GitHub ===" -ForegroundColor Cyan

$headers = @{
    Authorization          = "Bearer $PAT"
    Accept                 = "application/vnd.github+json"
    "X-GitHub-Api-Version" = "2022-11-28"
}
$baseUrl = "https://api.github.com/repos/$Owner/$Repo/actions/variables"

$varMap = @{}
$varMap["UE_ROOT"]    = $ueRoot
$varMap["SF_PROJ"]    = $sfProj
$varMap["MOD_OUTPUT"] = $modOut

foreach ($name in $varMap.Keys) {
    $val  = $varMap[$name]
    $body = '{"name":"' + $name + '","value":"' + ($val -replace '\\','\\\\') + '"}'
    $ok   = $false

    try {
        Invoke-RestMethod -Method PATCH -Uri "$baseUrl/$name" `
            -Headers $headers -Body $body -ContentType "application/json" | Out-Null
        Write-Host "  [UPDATED] $name = $val" -ForegroundColor Green
        $ok = $true
    } catch { }

    if (-not $ok) {
        try {
            Invoke-RestMethod -Method POST -Uri $baseUrl `
                -Headers $headers -Body $body -ContentType "application/json" | Out-Null
            Write-Host "  [CREATED] $name = $val" -ForegroundColor Green
            $ok = $true
        } catch {
            $msg = $_.Exception.Message
            Write-Host "  [FAILED]  $name : $msg" -ForegroundColor Red
        }
    }
}

Write-Host ""
Write-Host "=== 4. Installation du service Windows (UAC requise) ===" -ForegroundColor Cyan
Write-Host "  Une fenetre UAC va s'ouvrir. Clique OUI."

$svcCmd = Join-Path $RunnerDir "svc.cmd"
if (-not (Test-Path $svcCmd)) {
    Write-Host "  ERREUR : svc.cmd introuvable dans $RunnerDir" -ForegroundColor Red
    exit 1
}

$batPath = "$env:TEMP\runner_svc_install.bat"
$line1   = "@echo off"
$line2   = "cd /d `"$RunnerDir`""
$line3   = "call `"$svcCmd`" install"
$line4   = "call `"$svcCmd`" start"
$line5   = "echo."
$line6   = "echo Service installe et demarre avec succes."
$line7   = "pause"
($line1, $line2, $line3, $line4, $line5, $line6, $line7) -join "`r`n" |
    Out-File -FilePath $batPath -Encoding ascii

Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$batPath`"" -Verb RunAs -Wait

Start-Sleep -Seconds 3
$svc = Get-Service | Where-Object { $_.DisplayName -like "*actions.runner*" } | Select-Object -First 1
if ($svc -and $svc.Status -eq "Running") {
    Write-Host "  Service en cours d'execution : $($svc.DisplayName)" -ForegroundColor Green
} else {
    Write-Host "  Verif dans services.msc si le service est bien Running." -ForegroundColor Yellow
}

Remove-Item $batPath -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "=== SETUP TERMINE ===" -ForegroundColor Green
Write-Host "  Runner en ligne : https://github.com/$Owner/$Repo/settings/actions/runners"
Write-Host "  Declenche un build : push sur main ou Actions > Run workflow"
Write-Host ""
