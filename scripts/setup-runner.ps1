#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Installe et configure le self-hosted GitHub Actions runner pour compiler
    le mod Satisfactory FactoryMonitor, puis configure les Variables du workflow.

.DESCRIPTION
    Ce script fait tout automatiquement :
      1. Télécharge la dernière version du runner Windows (x64)
      2. L'installe dans C:\actions-runner
      3. Le configure avec le label "satisfactory"
      4. L'installe comme service Windows (démarre automatiquement)
      5. Configure les 3 Variables GitHub du workflow via l'API

    LA SEULE CHOSE MANUELLE : récupérer le token d'enregistrement sur GitHub.
    ─────────────────────────────────────────────────────────────────────────
    Va sur : https://github.com/ThomasDelosEpsi/SatisfactoryModStats/settings/actions/runners/new
    Copie le token affiché dans la section "Configure" (ligne --token XXXXX)
    ─────────────────────────────────────────────────────────────────────────

.PARAMETER RegistrationToken
    Token d'enregistrement récupéré sur GitHub (expire après 1 heure).

.PARAMETER GitHubPAT
    Personal Access Token GitHub avec les permissions repo+actions:write.
    Nécessaire pour configurer les Variables du workflow via l'API.
    Génère-le sur : https://github.com/settings/tokens
    (Permissions requises : repo → toutes, workflow)

.PARAMETER UERoot
    Chemin vers le dossier d'installation d'Unreal Engine 5.3.
    Défaut : C:\Program Files\Epic Games\UE_5.3

.PARAMETER SatisfactoryProject
    Chemin complet vers FactoryGame.uproject.
    Défaut : détection automatique dans les emplacements courants.

.PARAMETER ModOutputDir
    Dossier de sortie pour les fichiers .smod générés.
    Défaut : C:\ModBuilds\FactoryMonitor

.EXAMPLE
    # Installation minimale (token seulement)
    .\setup-runner.ps1 -RegistrationToken "AXYZ..."

    # Installation complète avec configuration des Variables GitHub
    .\setup-runner.ps1 -RegistrationToken "AXYZ..." -GitHubPAT "ghp_..."

    # Installation avec chemins personnalisés
    .\setup-runner.ps1 `
        -RegistrationToken "AXYZ..." `
        -GitHubPAT "ghp_..." `
        -UERoot "D:\UnrealEngine\5.3" `
        -SatisfactoryProject "D:\Games\Satisfactory\FactoryGame.uproject"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true,
               HelpMessage = "Token depuis https://github.com/ThomasDelosEpsi/SatisfactoryModStats/settings/actions/runners/new")]
    [ValidateNotNullOrEmpty()]
    [string]$RegistrationToken,

    [Parameter(Mandatory = $false,
               HelpMessage = "PAT GitHub pour configurer les Variables du workflow")]
    [string]$GitHubPAT,

    [Parameter(Mandatory = $false)]
    [string]$UERoot = "C:\Program Files\Epic Games\UE_5.3",

    [Parameter(Mandatory = $false)]
    [string]$SatisfactoryProject = "",

    [Parameter(Mandatory = $false)]
    [string]$ModOutputDir = "C:\ModBuilds\FactoryMonitor"
)

# ─────────────────────────────────────────────────────────────────────────────
# Configuration globale
# ─────────────────────────────────────────────────────────────────────────────
$ErrorActionPreference = "Stop"
$RepoOwner  = "ThomasDelosEpsi"
$RepoName   = "SatisfactoryModStats"
$RunnerDir  = "C:\actions-runner"
$RunnerLabel = "satisfactory"
$RunnerName  = "SatisfactoryModRunner"

# Couleurs de console
function Write-Step  { param($msg) Write-Host "`n[STEP] $msg" -ForegroundColor Cyan }
function Write-OK    { param($msg) Write-Host "  ✅ $msg" -ForegroundColor Green }
function Write-Warn  { param($msg) Write-Host "  ⚠️  $msg" -ForegroundColor Yellow }
function Write-Fail  { param($msg) Write-Host "  ❌ $msg" -ForegroundColor Red }

Write-Host @"

╔══════════════════════════════════════════════════════════════╗
║       FactoryMonitor — Setup GitHub Actions Runner           ║
║       Dépôt : $RepoOwner/$RepoName
╚══════════════════════════════════════════════════════════════╝
"@ -ForegroundColor Magenta

# ─────────────────────────────────────────────────────────────────────────────
# ÉTAPE 1 — Détection automatique de Satisfactory
# ─────────────────────────────────────────────────────────────────────────────
Write-Step "Détection de l'environnement Satisfactory..."

# Emplacements courants où Satisfactory peut être installé
$SFCandidates = @(
    "C:\Program Files (x86)\Steam\steamapps\common\Satisfactory\FactoryGame.uproject",
    "D:\Steam\steamapps\common\Satisfactory\FactoryGame.uproject",
    "E:\Steam\steamapps\common\Satisfactory\FactoryGame.uproject",
    "C:\Games\Satisfactory\FactoryGame.uproject",
    "D:\Games\Satisfactory\FactoryGame.uproject"
)

if ($SatisfactoryProject -eq "") {
    foreach ($candidate in $SFCandidates) {
        if (Test-Path $candidate) {
            $SatisfactoryProject = $candidate
            Write-OK "Satisfactory détecté automatiquement : $SatisfactoryProject"
            break
        }
    }
    if ($SatisfactoryProject -eq "") {
        Write-Warn "Satisfactory introuvable aux emplacements standards."
        Write-Warn "Renseigne le chemin manuellement avec -SatisfactoryProject"
        Write-Warn "La Variable SF_PROJ sera vide — à corriger dans GitHub Settings."
        $SatisfactoryProject = "CHEMIN_A_CONFIGURER"
    }
} else {
    if (Test-Path $SatisfactoryProject) {
        Write-OK "Satisfactory (fourni) : $SatisfactoryProject"
    } else {
        Write-Warn "Chemin fourni introuvable : $SatisfactoryProject"
    }
}

# Vérifie UE 5.3
if (Test-Path $UERoot) {
    Write-OK "Unreal Engine 5.3 : $UERoot"
} else {
    Write-Warn "UE 5.3 introuvable à $UERoot"
    Write-Warn "Mets à jour la Variable UE_ROOT dans GitHub Settings après l'installation."
}

# ─────────────────────────────────────────────────────────────────────────────
# ÉTAPE 2 — Téléchargement du runner GitHub Actions
# ─────────────────────────────────────────────────────────────────────────────
Write-Step "Téléchargement du GitHub Actions Runner..."

# Récupère la dernière version via l'API GitHub (pas d'auth nécessaire)
$latestRelease = Invoke-RestMethod `
    -Uri "https://api.github.com/repos/actions/runner/releases/latest" `
    -Headers @{ "User-Agent" = "FactoryMonitorSetup" }

$runnerVersion = $latestRelease.tag_name -replace '^v', ''
$runnerAsset   = $latestRelease.assets | Where-Object {
    $_.name -like "actions-runner-win-x64-*.zip"
} | Select-Object -First 1

if (-not $runnerAsset) {
    throw "Impossible de trouver l'asset du runner Windows x64 dans la release $runnerVersion."
}

Write-OK "Version du runner : $runnerVersion"

# Crée le dossier d'installation
if (-not (Test-Path $RunnerDir)) {
    New-Item -ItemType Directory -Path $RunnerDir | Out-Null
}

$zipPath = Join-Path $env:TEMP "actions-runner.zip"
Write-Host "  → Téléchargement depuis $($runnerAsset.browser_download_url)..."
Invoke-WebRequest -Uri $runnerAsset.browser_download_url -OutFile $zipPath -UseBasicParsing
Write-OK "Téléchargement terminé."

# Extraction
Write-Host "  → Extraction dans $RunnerDir..."
Expand-Archive -Path $zipPath -DestinationPath $RunnerDir -Force
Remove-Item $zipPath
Write-OK "Extraction terminée."

# ─────────────────────────────────────────────────────────────────────────────
# ÉTAPE 3 — Configuration du runner
# ─────────────────────────────────────────────────────────────────────────────
Write-Step "Configuration du runner..."

$configScript = Join-Path $RunnerDir "config.cmd"
if (-not (Test-Path $configScript)) {
    throw "config.cmd introuvable dans $RunnerDir — l'extraction a peut-être échoué."
}

# Démarre config.cmd en mode non-interactif
# --unattended  : pas de prompts
# --labels      : ajoute le label "satisfactory" utilisé dans le workflow
# --runasservice est géré séparément via svc.cmd
& $configScript `
    --url "https://github.com/$RepoOwner/$RepoName" `
    --token $RegistrationToken `
    --name $RunnerName `
    --labels "self-hosted,Windows,X64,$RunnerLabel" `
    --work "_work" `
    --unattended

if ($LASTEXITCODE -ne 0) {
    throw "La configuration du runner a échoué (code $LASTEXITCODE). Vérifie que le token n'a pas expiré."
}
Write-OK "Runner configuré avec les labels : self-hosted, Windows, X64, $RunnerLabel"

# ─────────────────────────────────────────────────────────────────────────────
# ÉTAPE 4 — Installation et démarrage du service Windows
# ─────────────────────────────────────────────────────────────────────────────
Write-Step "Installation du service Windows..."

$svcScript = Join-Path $RunnerDir "svc.cmd"

# Installe le service (tourne sous le compte SYSTEM)
& $svcScript install
if ($LASTEXITCODE -ne 0) { throw "Échec de l'installation du service." }
Write-OK "Service installé."

# Démarre le service
& $svcScript start
if ($LASTEXITCODE -ne 0) { throw "Échec du démarrage du service." }
Write-OK "Service démarré."

# Vérifie l'état du service
$svc = Get-Service -Name "actions.runner.$RepoOwner.$RepoName.$RunnerName" -ErrorAction SilentlyContinue
if ($svc -and $svc.Status -eq "Running") {
    Write-OK "Service en cours d'exécution : $($svc.Name)"
} else {
    Write-Warn "Impossible de confirmer l'état du service. Vérifie dans services.msc."
}

# ─────────────────────────────────────────────────────────────────────────────
# ÉTAPE 5 — Configuration des Variables GitHub via l'API (optionnel)
# ─────────────────────────────────────────────────────────────────────────────
Write-Step "Configuration des Variables du workflow GitHub..."

if (-not $GitHubPAT) {
    Write-Warn "Pas de PAT fourni (-GitHubPAT) — les Variables ne seront pas créées automatiquement."
    Write-Warn "Crée-les manuellement dans :"
    Write-Warn "https://github.com/$RepoOwner/$RepoName/settings/variables/actions"
    Write-Warn ""
    Write-Warn "  UE_ROOT   = $UERoot"
    Write-Warn "  SF_PROJ   = $SatisfactoryProject"
    Write-Warn "  MOD_OUTPUT = $ModOutputDir"
} else {
    $headers = @{
        "Authorization" = "Bearer $GitHubPAT"
        "Accept"        = "application/vnd.github+json"
        "X-GitHub-Api-Version" = "2022-11-28"
    }
    $baseUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/actions/variables"

    # Crée ou met à jour chaque variable
    $variables = @{
        "UE_ROOT"    = $UERoot
        "SF_PROJ"    = $SatisfactoryProject
        "MOD_OUTPUT" = $ModOutputDir
    }

    foreach ($varName in $variables.Keys) {
        $body = @{ name = $varName; value = $variables[$varName] } | ConvertTo-Json

        # Essaie PATCH (mise à jour) puis POST (création) si 404
        try {
            Invoke-RestMethod -Method PATCH -Uri "$baseUrl/$varName" `
                -Headers $headers -Body $body -ContentType "application/json" | Out-Null
            Write-OK "Variable mise à jour : $varName = $($variables[$varName])"
        } catch {
            if ($_.Exception.Response.StatusCode -eq 404) {
                Invoke-RestMethod -Method POST -Uri $baseUrl `
                    -Headers $headers -Body $body -ContentType "application/json" | Out-Null
                Write-OK "Variable créée : $varName = $($variables[$varName])"
            } else {
                Write-Warn "Impossible de créer $varName : $($_.Exception.Message)"
            }
        }
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# Résumé final
# ─────────────────────────────────────────────────────────────────────────────
Write-Host @"

╔══════════════════════════════════════════════════════════════╗
║                    ✅ SETUP TERMINÉ                          ║
╠══════════════════════════════════════════════════════════════╣
║  Runner installé dans  : $RunnerDir
║  Nom du runner         : $RunnerName
║  Labels                : self-hosted, Windows, X64, $RunnerLabel
║
║  Vérifie que le runner est en ligne sur :
║  https://github.com/$RepoOwner/$RepoName/settings/actions/runners
║
║  Pour déclencher un build maintenant :
║  → Fais un push sur main ou va dans Actions → Run workflow
╚══════════════════════════════════════════════════════════════╝
"@ -ForegroundColor Green
