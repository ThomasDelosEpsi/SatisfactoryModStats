$RunnerDir  = "C:\actions-runner"
$RunCmd     = Join-Path $RunnerDir "run.cmd"
$TaskName   = "GitHubActionsRunner-Satisfactory"

Write-Host "=== Creation de la tache planifiee ===" -ForegroundColor Cyan

if (-not (Test-Path $RunCmd)) {
    Write-Host "ERREUR : run.cmd introuvable dans $RunnerDir" -ForegroundColor Red
    exit 1
}

# Supprime la tache si elle existe deja
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue

# Cree l'action : lancer run.cmd au demarrage de session
$action  = New-ScheduledTaskAction `
    -Execute  "cmd.exe" `
    -Argument "/c `"$RunCmd`" > C:\actions-runner\_diag\runner-stdout.log 2>&1" `
    -WorkingDirectory $RunnerDir

# Declencheur : au login de l'utilisateur courant
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME

# Parametres : masque la fenetre de console
$settings = New-ScheduledTaskSettingsSet `
    -ExecutionTimeLimit (New-TimeSpan -Hours 0) `
    -RestartCount 3 `
    -RestartInterval (New-TimeSpan -Minutes 1) `
    -StartWhenAvailable

$principal = New-ScheduledTaskPrincipal `
    -UserId    $env:USERNAME `
    -LogonType Interactive `
    -RunLevel  Limited

# Enregistre la tache (pas besoin d'admin pour l'utilisateur courant)
Register-ScheduledTask `
    -TaskName  $TaskName `
    -Action    $action `
    -Trigger   $trigger `
    -Settings  $settings `
    -Principal $principal `
    -Force | Out-Null

Write-Host "  Tache creee : $TaskName" -ForegroundColor Green

# Lance le runner immediatement en arriere-plan
Write-Host ""
Write-Host "=== Demarrage immediat du runner ===" -ForegroundColor Cyan
Start-Process -FilePath "cmd.exe" `
    -ArgumentList "/c `"$RunCmd`"" `
    -WorkingDirectory $RunnerDir `
    -WindowStyle Hidden

Start-Sleep -Seconds 5

# Verifie que le processus tourne
$proc = Get-Process -Name "Runner.Listener" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "  Runner.Listener en cours (PID $($proc.Id))" -ForegroundColor Green
} else {
    Write-Host "  Processus non detecte encore — attends 10s et relance cette verification." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== RUNNER CONFIGURE ===" -ForegroundColor Green
Write-Host "  Redemarre au login automatiquement grace a la tache planifiee."
Write-Host "  Verifie le statut ONLINE sur :"
Write-Host "  https://github.com/ThomasDelosEpsi/SatisfactoryModStats/settings/actions/runners"
