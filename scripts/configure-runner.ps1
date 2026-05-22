param(
    [string]$Token = "A4XBFQ3WHZALEV4D2K2WIULKCATHQ"
)

$RunnerDir = "C:\actions-runner"
$Owner     = "ThomasDelosEpsi"
$Repo      = "SatisfactoryModStats"
$configExe = Join-Path $RunnerDir "config.cmd"

Write-Host "=== Configuration du runner ===" -ForegroundColor Cyan

if (-not (Test-Path $configExe)) {
    Write-Error "config.cmd introuvable dans $RunnerDir"
    exit 1
}

# Lance config.cmd sans interaction
$args = "--url https://github.com/$Owner/$Repo " +
        "--token $Token " +
        "--name SatisfactoryModRunner " +
        "--labels self-hosted,Windows,X64,satisfactory " +
        "--work _work " +
        "--unattended"

$proc = Start-Process -FilePath "cmd.exe" `
    -ArgumentList "/c `"$configExe`" $args" `
    -WorkingDirectory $RunnerDir `
    -Wait -PassThru -NoNewWindow

if ($proc.ExitCode -eq 0) {
    Write-Host "Runner configure avec succes !" -ForegroundColor Green
    Write-Host "Labels : self-hosted, Windows, X64, satisfactory"
} else {
    Write-Host "Echec configuration (code $($proc.ExitCode))" -ForegroundColor Red
}
