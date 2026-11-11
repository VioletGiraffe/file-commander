@echo off
:: Starts build_app.ps1, which holds the logic: a .ps1 cannot be run directly under the default execution policy.
:: Windows PowerShell, not pwsh: it is present on every machine.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_app.ps1" %*
