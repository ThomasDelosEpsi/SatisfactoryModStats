#Requires -RunAsAdministrator
# Installe RunnerService.exe comme service Windows automatique

$RunnerDir   = "C:\actions-runner"
$ServiceExe  = Join-Path $RunnerDir "bin\RunnerService.exe"
$Owner       = "ThomasDelosEpsi"
$Repo        = "SatisfactoryModStats"
$RunnerName  = "SatisfactoryModRunner"
$ServiceName = "actions.runner.$Owner.$Repo.$RunnerName"
$DisplayName = "GitHub Actions Runner ($Owner-$Repo.$RunnerName)"

Write-Host "Installation du service : $ServiceName"

# Supprime le service s'il existe deja
$existing = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "Service existant detecte — suppression..."
    Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
    sc.exe delete $ServiceName | Out-Null
    Start-Sleep -Seconds 2
}

# Cree le service
New-Service `
    -Name        $ServiceName `
    -DisplayName $DisplayName `
    -BinaryPathName "`"$ServiceExe`"" `
    -StartupType Automatic `
    -Description "GitHub Actions self-hosted runner for Satisfactory mod builds"

# Configure le chemin de travail via le registre (requis par le runner)
$regPath = "HKLM:\SYSTEM\CurrentControlSet\Services\$ServiceName"
if (Test-Path $regPath) {
    Set-ItemProperty -Path $regPath -Name "AppDirectory" -Value $RunnerDir -ErrorAction SilentlyContinue
}

# Demarre le service
Start-Service -Name $ServiceName
Start-Sleep -Seconds 3

$svc = Get-Service -Name $ServiceName
Write-Host "Statut : $($svc.Status)"
if ($svc.Status -eq "Running") {
    Write-Host "Service demarre avec succes !" -ForegroundColor Green
} else {
    Write-Host "Le service ne semble pas Running. Verifier dans services.msc" -ForegroundColor Yellow
}
