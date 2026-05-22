$svc = Get-Service | Where-Object { $_.Name -like "*actions.runner*" } | Select-Object -First 1
if ($svc) {
    Write-Host "Nom       : $($svc.Name)"
    Write-Host "Statut    : $($svc.Status)"
    Write-Host "Demarrage : $($svc.StartType)"
} else {
    Write-Host "AUCUN service actions.runner trouve"
}
