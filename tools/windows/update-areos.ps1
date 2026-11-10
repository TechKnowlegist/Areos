# update-areos.ps1 - get the newest AREOS build from GitHub and start it in QEMU.
#
# Run it from PowerShell (no admin rights needed):
#     .\update-areos.ps1
# Options:
#     -NoRun      only download, don't start QEMU
#     -WindowsQemu  use the Windows QEMU even if QEMU is installed in WSL
#     -Iso PATH   start a disc image you already have, don't download
#     -Repo NAME  another GitHub repository (default TechKnowlegist/areos)

param(
    [switch]$NoRun,
    [switch]$WindowsQemu,
    [string]$Iso,
    [string]$Repo = "TechKnowlegist/areos"
)

$ErrorActionPreference = "Stop"
$folder = Join-Path $env:LOCALAPPDATA "AREOS"
New-Item -ItemType Directory -Force -Path $folder | Out-Null

if (-not $Iso) {
    $Iso = Join-Path $folder "areos-x86_64.iso"
    $url = "https://github.com/$Repo/releases/download/latest/areos-x86_64.iso"
    Write-Host "Checking for the newest AREOS build..."
    # Only download when the build on GitHub is different from the one we have.
    $stampFile = Join-Path $folder "last-build.txt"
    $remote = $null
    try {
        $head = Invoke-WebRequest -Uri $url -Method Head -UseBasicParsing
        $remote = "$($head.Headers['ETag'])|$($head.Headers['Content-Length'])"
    } catch { }
    $local = if (Test-Path $stampFile) { Get-Content $stampFile -Raw } else { "" }
    if ((Test-Path $Iso) -and $remote -and ($remote.Trim() -eq $local.Trim())) {
        Write-Host "You already have the newest build."
    } else {
        Write-Host "Downloading..."
        $tmp = "$Iso.part"
        try {
            $ProgressPreference = "SilentlyContinue"
            Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
        } catch {
            Remove-Item $tmp -ErrorAction SilentlyContinue
            Write-Host ""
            Write-Host "There's no build to download yet."
            Write-Host "Either GitHub is still building it (give it a couple more minutes),"
            Write-Host "or the build failed. Check: https://github.com/$Repo/actions"
            exit 1
        }
        Move-Item -Force $tmp $Iso
        if ($remote) { Set-Content -Path $stampFile -Value $remote }
        Write-Host "Downloaded: $Iso"
    }
}

if ($NoRun) { Write-Host "Done."; exit 0 }

# On a Snapdragon (ARM) laptop the Windows build of QEMU can crash, so if QEMU is
# installed inside WSL (Ubuntu), use that one: it runs natively and the window
# still opens on your Windows desktop.
if (-not $WindowsQemu -and (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    $ErrorActionPreference = "Continue"
    wsl.exe -e sh -c "command -v qemu-system-x86_64" *> $null
    if ($LASTEXITCODE -eq 0) {
        # C:\Users\me\file.iso  ->  /mnt/c/Users/me/file.iso
        $full = (Resolve-Path $Iso).Path
        $linuxIso = "/mnt/" + $full.Substring(0, 1).ToLower() + $full.Substring(2).Replace("\", "/")
        $helpText = (wsl.exe -e qemu-system-x86_64 -audiodev help 2>&1 | Out-String)
        $wslArgs = @("-M", "q35", "-m", "2G", "-cdrom", $linuxIso, "-boot", "d",
                     "-rtc", "base=localtime", "-nic", "user,model=e1000", "-accel", "tcg")
        if ($helpText -match "(?m)^\s*pa\s*$") {
            $wslArgs += @("-audiodev", "pa,id=sound", "-device", "AC97,audiodev=sound")
        } else {
            Write-Host "No sound driver in WSL's QEMU, so AREOS will start without sound."
        }
        Write-Host "Starting AREOS (QEMU inside WSL)..."
        wsl.exe -e qemu-system-x86_64 @wslArgs
        exit
    }
    $ErrorActionPreference = "Stop"
}

# Find QEMU
$qemu = $null
$cmd = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
if ($cmd) { $qemu = $cmd.Source }
if (-not $qemu) {
    foreach ($dir in @("$env:ProgramFiles\qemu", "${env:ProgramFiles(x86)}\qemu", "$env:LOCALAPPDATA\Programs\qemu")) {
        $try = Join-Path $dir "qemu-system-x86_64.exe"
        if (Test-Path $try) { $qemu = $try; break }
    }
}
if (-not $qemu) {
    Write-Host "QEMU isn't installed. Install it once with:"
    Write-Host "    winget install SoftwareFreedomConservancy.QEMU"
    Write-Host "then close and reopen PowerShell and run this again."
    exit 1
}

Write-Host "Starting AREOS..."
# Pick a sound driver this QEMU has, so the screen reader can speak.
$drivers = (& $qemu -audiodev help 2>&1 | Out-String)
$audio = $null
foreach ($name in @("dsound", "wasapi", "sdl")) {
    if ($drivers -match "(?m)^\s*$name\s*$") { $audio = $name; break }
}
# The same settings the AREOS makefile uses.
$arguments = @(
    "-M", "q35",
    "-m", "2G",
    "-cdrom", $Iso,
    "-boot", "d",
    "-rtc", "base=localtime",
    "-nic", "user,model=e1000",
    "-accel", "tcg"
)
if ($audio) {
    $arguments += @("-audiodev", "$audio,id=sound", "-device", "AC97,audiodev=sound")
} else {
    Write-Host "No sound driver found in this QEMU, so AREOS will start without sound."
}
& $qemu @arguments
