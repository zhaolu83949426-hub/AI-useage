# Token Dashboard Auto-start Setup Script
# Run this script as Administrator to configure auto-start on Windows login

$ErrorActionPreference = "Stop"

# Get the script directory
$ScriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$PythonExe = (Get-Command python).Source
$MainPy = Join-Path $ScriptPath "main.py"

# Check if files exist
if (-not (Test-Path $MainPy)) {
    Write-Error "main.py not found at: $MainPy"
    exit 1
}

Write-Host "Configuring Token Dashboard auto-start..." -ForegroundColor Green
Write-Host "Python: $PythonExe"
Write-Host "Script: $MainPy"

# Method 1: Startup Folder (current user)
$StartupFolder = [Environment]::GetFolderPath("Startup")
$ShortcutPath = Join-Path $StartupFolder "Token Dashboard.lnk"

Write-Host "`nMethod 1: Creating Startup Folder shortcut..." -ForegroundColor Cyan
try {
    $WshShell = New-Object -ComObject WScript.Shell
    $Shortcut = $WshShell.CreateShortcut($ShortcutPath)
    $Shortcut.TargetPath = $PythonExe
    $Shortcut.Arguments = "-m tools.token_dashboard_host.main"
    $Shortcut.WorkingDirectory = $ScriptPath
    $Shortcut.Description = "Token Usage Dashboard - Auto-start on login"
    $Shortcut.Save()
    Write-Host "✓ Shortcut created: $ShortcutPath" -ForegroundColor Green
} catch {
    Write-Host "✗ Failed to create shortcut: $_" -ForegroundColor Red
}

# Method 2: Task Scheduler (more reliable, runs as background task)
Write-Host "`nMethod 2: Creating Task Scheduler task..." -ForegroundColor Cyan
try {
    $TaskName = "TokenDashboardAutoStart"
    $TaskExists = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue

    if ($TaskExists) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Host "Removed existing task" -ForegroundColor Yellow
    }

    $Action = New-ScheduledTaskAction -Execute $PythonExe -Argument "-m tools.token_dashboard_host.main" -WorkingDirectory $ScriptPath
    $Trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
    $Settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable
    $Principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Highest

    Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger -Settings $Settings -Principal $Principal -Description "Token Usage Dashboard - Auto-start on login" | Out-Null
    Write-Host "✓ Task Scheduler task created: $TaskName" -ForegroundColor Green
} catch {
    Write-Host "✗ Failed to create scheduled task: $_" -ForegroundColor Red
}

Write-Host "`nAuto-start configuration complete!" -ForegroundColor Green
Write-Host "The dashboard will start automatically when you log in." -ForegroundColor Cyan
Write-Host "`nTo test now, run: python -m tools.token_dashboard_host.main" -ForegroundColor Yellow
Write-Host "To stop auto-start, delete the shortcut from Startup folder or unregister the scheduled task." -ForegroundColor Yellow
