# ============================================================
# A EXECUTER SUR TA MACHINE DE DEV (celle avec UE 5.3 + Satisfactory)
# Lance ce script dans PowerShell en mode Administrateur
# ============================================================
#
# AVANT de lancer ce script :
#   1. Va sur https://github.com/ThomasDelosEpsi/SatisfactoryModStats/settings/actions/runners
#   2. Clique "New self-hosted runner" -> Windows
#   3. Copie le token affiche dans la ligne "--token XXXXXX"
#   4. Passe-le en parametre : .\setup-runner-devmachine.ps1 -Token "XXXXXX"
#
param(
    [Parameter(Mandatory=$true)]
    [string]$Token
)

$RunnerDir  = "C:\actions-runner"
$Owner      = "ThomasDelosEpsi"
$Repo       = "SatisfactoryModStats"
$RunnerName = "SatisfactoryDevMachine"

# 1. Telecharge le runner
Write-Host "Telechargement du runner GitHub Actions..."
$release = Invoke-RestMethod "https://api.github.com/repos/actions/runner/releases/latest" `
    -Headers @{"User-Agent"="setup"}
$asset = $release.assets | Where-Object { $_.name -like "actions-runner-win-x64-*.zip" } | Select-Object -First 1
$zip = "$env:TEMP\actions-runner.zip"
Invoke-WebRequest $asset.browser_download_url -OutFile $zip -UseBasicParsing
if (-not (Test-Path $RunnerDir)) { New-Item -ItemType Directory -Path $RunnerDir | Out-Null }
Expand-Archive $zip $RunnerDir -Force
Remove-Item $zip
Write-Host "Extrait dans $RunnerDir"

# 2. Configure
Write-Host "Configuration..."
& "$RunnerDir\config.cmd" `
    --url "https://github.com/$Owner/$Repo" `
    --token $Token `
    --name $RunnerName `
    --labels "self-hosted,Windows,X64,satisfactory" `
    --work "_work" `
    --unattended

# 3. Installe et demarre le service Windows (necessite admin)
Write-Host "Installation du service Windows..."
& "$RunnerDir\svc.cmd" install
& "$RunnerDir\svc.cmd" start

$svc = Get-Service | Where-Object { $_.DisplayName -like "*$RunnerName*" } | Select-Object -First 1
if ($svc -and $svc.Status -eq "Running") {
    Write-Host "Service en cours : $($svc.DisplayName)" -ForegroundColor Green
} else {
    Write-Host "Verifie dans services.msc que le service tourne." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Runner configure ! Verifie sur :"
Write-Host "https://github.com/$Owner/$Repo/settings/actions/runners"
