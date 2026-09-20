@echo off
:: Starts run_tests.ps1, which holds the logic. A .ps1 cannot be run directly under the default execution policy,
:: and CI and the project's rules name run_tests.bat. Windows PowerShell, not pwsh: it is present on every machine.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_tests.ps1" %*
