# Token Dashboard Auto-start Removal Script
# Run this to disable auto-start

$ErrorActionPreference = "Stop"

Write-Host "Removing Token Dashboard auto-start..." -ForegroundColor Yellow

# Method 1: Remove Startup Folder shortcut
$StartupFolder = [Environment]::GetFolderPath("Startup")
$ShortcutPath = Join-Path $StartupFolder "Token Dashboard.lnk"

Write-Host "`nRemoving Startup Folder shortcut..." -ForegroundColor Cyan
if (Test-Path $ShortcutPath) {
    Remove-Item $ShortcutPath -Force
    Write-Host "✓ Shortcut removed" -ForegroundColor Green
} else {
    Write-Host "✗ Shortcut not found" -ForegroundColor Yellow
}

# Method 2: Remove Task Scheduler task
Write-Host "`nRemoving Task Scheduler task..." -ForegroundColor Cyan
try {
    $TaskName = "TokenDashboardAutoStart"
    $TaskExists = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue

    if ($TaskExists) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Host "✓ Task removed" -ForegroundColor Green
    } else {
        Write-Host "✗ Task not found" -ForegroundColor Yellow
    }
} catch {
    Write-Host "✗ Failed to remove task: $_" -ForegroundColor Red
}

Write-Host "`nAuto-start removed. To run manually, execute: python -m tools.token_dashboard_host.main" -ForegroundColor Cyan
