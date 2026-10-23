<#
.SYNOPSIS
  Puts the Archipelago logo on the Gizmos & Gadgets title screen: builds GIZMOAP1.DAT in your game folder.

.DESCRIPTION
  Runs tools/title_logo.py from this repository on your own game files. It reads gizmo001.dat (and ssg.exe, whose
  own routine unpacks the game's resources) from the game folder and writes GIZMOAP1.DAT next to them; your
  gizmo001.dat is not changed. The Archipelago mod uses GIZMOAP1.DAT when it is there.

  Needs Python 3.8 or newer (python.org or the Microsoft Store). Pillow is installed for you if it is missing
  (pip install --user pillow).

  The logo is (c) the Archipelago project, CC BY-NC 4.0 (see COPYING.md), so GIZMOAP1.DAT is for non-commercial
  use too. It also contains data from your copy of the game: keep it to yourself.

.PARAMETER GameDir
  The game folder (the one with gizmo001.dat and ssg.exe). Without it, a folder dialog asks.

.PARAMETER Preview
  Also save a PNG preview of the new title sprite to this path.

.EXAMPLE
  .\title-logo.ps1 -GameDir C:\DOS\Gizmos
#>
param(
    [string]$GameDir,
    [string]$Preview
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$msg) { Write-Host ''; Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg)   { Write-Host "    $msg" -ForegroundColor Green }
function Fail([string]$msg) {
    Write-Host ''
    Write-Host "ERROR: $msg" -ForegroundColor Red
    throw $msg
}

$script = Join-Path $PSScriptRoot 'title_logo.py'
$logo   = Join-Path $PSScriptRoot '..\assets\archipelago_logo.png'
if (-not (Test-Path $script) -or -not (Test-Path $logo)) {
    Fail "run this from a copy of the gizmos-and-gadgets-ap repository (tools\title_logo.py and assets\archipelago_logo.png are missing)"
}

# ---- Python ---------------------------------------------------------------------------------------
# Native commands run with $ErrorActionPreference = 'Continue': Windows PowerShell 5.1 would otherwise turn
# anything they write to stderr into a terminating error.
function Get-PythonOutput([string]$exe, [string[]]$pyArgs) {
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { $out = & $exe @pyArgs 2>$null; return @{ Code = $LASTEXITCODE; Out = "$out".Trim() } }
    catch { return @{ Code = 1; Out = '' } }
    finally { $ErrorActionPreference = $old }
}

Write-Step 'Looking for Python 3.8+'
$python = $null
foreach ($cand in @('py -3', 'python', 'python3')) {
    $parts = $cand -split ' '
    $exe = $parts[0]
    $pre = @($parts | Select-Object -Skip 1)
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { continue }
    # The Microsoft Store placeholder "python.exe" prints nothing here, so it is skipped too.
    $r = Get-PythonOutput $exe ($pre + @('-c', 'import sys; print(int(sys.version_info >= (3, 8)))'))
    if ($r.Out -eq '1') { $python = @{ Exe = $exe; Pre = $pre }; break }
}
if (-not $python) {
    Fail 'Python 3.8 or newer not found. Install it from python.org (tick "Add python.exe to PATH") or the Microsoft Store, then run this again.'
}
$version = (Get-PythonOutput $python.Exe ($python.Pre + @('-c', 'import sys; print(sys.version.split()[0])'))).Out
Write-Ok "Python $version ($($python.Exe))"

# runs Python with its output shown; returns the exit code
function Invoke-Python([string[]]$pyArgs) {
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { & $python.Exe @($python.Pre + $pyArgs) 2>&1 | Out-Host; return $LASTEXITCODE }
    finally { $ErrorActionPreference = $old }
}

# ---- Pillow -----------------------------------------------------------------------------------------
Write-Step 'Checking for Pillow (image library)'
if ((Get-PythonOutput $python.Exe ($python.Pre + @('-c', 'import PIL'))).Code -ne 0) {
    Write-Host '    not installed: installing it (pip install --user pillow)'
    if ((Invoke-Python @('-m', 'pip', 'install', '--user', '--quiet', 'pillow')) -ne 0) {
        Fail 'installing Pillow failed. Try "python -m pip install pillow" yourself, then run this again.'
    }
}
Write-Ok 'Pillow is there'

# ---- the game folder ------------------------------------------------------------------------------------
Write-Step 'Game folder'
if (-not $GameDir) {
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Pick your Gizmos & Gadgets game folder (the one with gizmo001.dat and ssg.exe)'
    $dlg.ShowNewFolderButton = $false
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Fail 'no game folder picked' }
    $GameDir = $dlg.SelectedPath
}
$GameDir = (Resolve-Path $GameDir).Path
foreach ($f in @('gizmo001.dat', 'ssg.exe')) {
    if (-not (Test-Path (Join-Path $GameDir $f))) { Fail "$GameDir has no ${f}: pick the folder with the game's files" }
}
Write-Ok $GameDir

# ---- build it ------------------------------------------------------------------------------------------
Write-Step 'Building GIZMOAP1.DAT'
$pyArgs = @($script, $GameDir, $logo)
if ($Preview) { $pyArgs += @('--preview', $Preview) }
if ((Invoke-Python $pyArgs) -ne 0) { Fail 'title_logo.py failed (see above)' }

Write-Host ''
Write-Host 'Done. Start gizmos.exe: the console should say "ap: title logo on (archive 1 = GIZMOAP1.DAT)".' -ForegroundColor Green
Write-Host 'To turn the logo off again, delete GIZMOAP1.DAT from the game folder.'
