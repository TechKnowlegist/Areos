# push.ps1 - save your changes and send them to GitHub in one go.
#
# Run it from PowerShell inside your areos folder (no admin rights needed):
#     .\tools\windows\push.ps1                      commit message: "Made changes"
#     .\tools\windows\push.ps1 "Added the thing"    your own message
#
# If a new download like areos-files-v2.tar.gz is sitting in your Downloads
# folder, it is unpacked into the repository first (and then deleted), so one
# command does the whole job. Use -NoUnpack to skip that.

param(
    [Parameter(Position = 0)]
    [string]$Message = "Made changes",
    [switch]$NoUnpack
)

$ErrorActionPreference = "Continue"

# Go to the top of the repository, wherever the script was started from.
$root = (git rev-parse --show-toplevel 2>$null)
if (-not $root) { Write-Host "This isn't inside a git repository. cd into your areos folder first."; exit 1 }
Set-Location $root

if (-not $NoUnpack) {
    $downloads = Join-Path $env:USERPROFILE "Downloads"
    $found = Get-ChildItem -Path $downloads -Filter "areos-*.tar.gz" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($found) {
        Write-Host "Unpacking $($found.Name)..."
        tar -xzf $found.FullName -C $root
        if ($LASTEXITCODE -ne 0) { Write-Host "Couldn't unpack it. Nothing was committed."; exit 1 }
        Remove-Item $found.FullName
    }
}

git add -A 2>$null   # hides the harmless line-ending warnings
git diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Host "Nothing has changed, so there is nothing to push."
    exit 0
}

git commit -m $Message
if ($LASTEXITCODE -ne 0) { Write-Host "The commit failed."; exit 1 }

git push
if ($LASTEXITCODE -ne 0) { Write-Host "The push failed. If it asked you to sign in, do that and run this again."; exit 1 }

Write-Host ""
Write-Host "Pushed. GitHub is building the new disc image now (about 2 minutes)."
Write-Host "When it's done, run .\tools\windows\update-areos.ps1 to try it."
