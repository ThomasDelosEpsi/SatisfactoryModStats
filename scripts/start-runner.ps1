$RunnerDir   = "C:\actions-runner"
$RunCmd      = Join-Path $RunnerDir "run.cmd"
$StartupDir  = [Environment]::GetFolderPath("Startup")
$ShortcutPath = Join-Path $StartupDir "GitHubActionsRunner.lnk"

Write-Host "=== Ajout au dossier Startup Windows ==="

$shell    = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($ShortcutPath)
$shortcut.TargetPath       = "cmd.exe"
$shortcut.Arguments        = "/c `"$RunCmd`""
$shortcut.WorkingDirectory = $RunnerDir
$shortcut.WindowStyle      = 7
$shortcut.Description      = "GitHub Actions Runner - Satisfactory"
$shortcut.Save()

if (Test-Path $ShortcutPath) {
    Write-Host "  Raccourci cree : $ShortcutPath" -ForegroundColor Green
} else {
    Write-Host "  ECHEC creation raccourci" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== Demarrage immediat du runner ==="

$existing = Get-Process -Name "Runner.Listener" -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "  Runner deja en cours (PID $($existing.Id))" -ForegroundColor Green
} else {
    Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$RunCmd`"" -WorkingDirectory $RunnerDir -WindowStyle Minimized
    Start-Sleep -Seconds 6
    $proc = Get-Process -Name "Runner.Listener" -ErrorAction SilentlyContinue
    if ($proc) {
        Write-Host "  Runner.Listener demarre (PID $($proc.Id))" -ForegroundColor Green
    } else {
        Write-Host "  Processus pas encore visible, attendre quelques secondes" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "=== DONE ==="
Write-Host "  Startup  : $ShortcutPath"
Write-Host "  Verification GitHub : https://github.com/ThomasDelosEpsi/SatisfactoryModStats/settings/actions/runners"
