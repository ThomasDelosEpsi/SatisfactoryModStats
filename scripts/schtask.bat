@echo off
schtasks /create /tn "GitHubActionsRunner-Satisfactory" /tr "C:\actions-runner\run.cmd" /sc ONLOGON /f
if %ERRORLEVEL% == 0 (
    echo TACHE CREEE AVEC SUCCES
    schtasks /run /tn "GitHubActionsRunner-Satisfactory"
    echo RUNNER DEMARRE
) else (
    echo ECHEC CREATION TACHE
)
