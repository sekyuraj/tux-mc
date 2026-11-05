# tux-mc installer for Windows. Run it through install.bat (double-click), or:
#   powershell -ExecutionPolicy Bypass -File install.ps1
# Whatever is missing gets set up: Prism Launcher (portable, in tux-mc's own
# folder), the Minecraft 26.3 Fabric instance, Fabric API, the tuxmc mod, the
# launcher with Start menu and desktop shortcuts, the black theme, and at the
# end the Microsoft sign-in in Prism. Nothing here needs admin rights.
# -NoShortcut, -NoSignIn, -NoStart: skip those steps (CI runs it this way)
param([switch]$NoShortcut, [switch]$NoSignIn, [switch]$NoStart)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'          # PowerShell 5's progress bar makes downloads crawl
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo    = 'sekyuraj/tux-mc'
$Inst    = '26.3'
$App     = Join-Path $env:LOCALAPPDATA 'Programs\tux-mc'
$Own     = Join-Path $env:LOCALAPPDATA 'tux-mc'
$OwnPrism = Join-Path $Own 'prism'
$UA      = @{ 'User-Agent' = 'tux-mc-installer' }

function Say($t)  { Write-Host "==> $t" -ForegroundColor White }
function Note($t) { Write-Host "    $t" -ForegroundColor Gray }

# ---------------------------------------------------------------- launcher --
Say 'Installing the tux-mc launcher'
Get-Process tux-mc -ErrorAction SilentlyContinue | Stop-Process -Force
if (Test-Path $App) { Remove-Item $App -Recurse -Force }
New-Item -ItemType Directory -Force -Path $App | Out-Null
Copy-Item (Join-Path $Here 'tux-mc\*') $App -Recurse -Force
$Exe = Join-Path $App 'bin\tux-mc.exe'

if (-not $NoShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $places = @([Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('Desktop'))
    foreach ($dir in $places) {
        $lnk = $shell.CreateShortcut((Join-Path $dir 'tux-mc.lnk'))
        $lnk.TargetPath = $Exe
        $lnk.WorkingDirectory = Split-Path $Exe
        $lnk.Description = 'Minecraft with the tux client'
        $lnk.Save()
    }
    Note 'shortcuts: Start menu and desktop'
}

# ------------------------------------------------------------------ Prism --
function Find-Prism {
    $c = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\PrismLauncher\prismlauncher.exe'),
        (Join-Path $env:ProgramFiles 'PrismLauncher\prismlauncher.exe')
    )
    if (Test-Path $OwnPrism) {
        $c += Get-ChildItem $OwnPrism -Recurse -Depth 1 -Filter prismlauncher.exe -ErrorAction SilentlyContinue |
              ForEach-Object FullName
    }
    foreach ($p in $c) { if ($p -and (Test-Path $p)) { return $p } }
    $cmd = Get-Command prismlauncher.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

$PrismExe = Find-Prism
if (-not $PrismExe) {
    Say "Prism Launcher isn't installed: downloading it"
    $rel = Invoke-RestMethod 'https://api.github.com/repos/PrismLauncher/PrismLauncher/releases/latest' -Headers $UA
    # the MinGW build carries its own runtime; the MSVC one needs the VC++ redistributable
    $asset = $rel.assets | Where-Object { $_.name -like '*Windows-MinGW-w64-Portable*.zip' -and $_.name -notlike '*arm64*' } |
             Select-Object -First 1
    if (-not $asset) {
        $asset = $rel.assets | Where-Object { $_.name -like '*Windows-MSVC-Portable*.zip' -and $_.name -notlike '*arm64*' } |
                 Select-Object -First 1
    }
    if (-not $asset) { throw "couldn't find a Prism download on GitHub" }
    Note $asset.name
    New-Item -ItemType Directory -Force -Path $OwnPrism | Out-Null
    $zip = Join-Path $env:TEMP 'PrismLauncher.zip'
    Invoke-WebRequest $asset.browser_download_url -OutFile $zip -Headers $UA -UseBasicParsing
    Expand-Archive $zip -DestinationPath $OwnPrism -Force
    Remove-Item $zip
    $PrismExe = Find-Prism
    if (-not $PrismExe) { throw "Prism was unpacked but prismlauncher.exe wasn't found" }
}
Note "Prism: $PrismExe"

# a portable Prism keeps its data next to it
$PrismDir = Split-Path $PrismExe
if (-not (Test-Path (Join-Path $PrismDir 'portable.txt'))) { $PrismDir = Join-Path $env:APPDATA 'PrismLauncher' }
New-Item -ItemType Directory -Force -Path $PrismDir | Out-Null
$PrismRunning = [bool](Get-Process prismlauncher -ErrorAction SilentlyContinue)

# ------------------------------------------------- Prism settings + theme --
$themeDir = Join-Path $PrismDir 'themes\tux'
New-Item -ItemType Directory -Force -Path $themeDir | Out-Null
Copy-Item (Join-Path $Here 'theme\prism\tux\*') $themeDir -Force
New-Item -ItemType Directory -Force -Path $Own | Out-Null
Copy-Item (Join-Path $Here 'theme\style.css') (Join-Path $Own 'style.css') -Force

$cfg = Join-Path $PrismDir 'prismlauncher.cfg'
if ($PrismRunning) {
    Note "Prism is open, so its settings are left alone (close it and run this again for the black theme)"
} else {
    $lines = if (Test-Path $cfg) { [Collections.Generic.List[string]]@(Get-Content $cfg) } else { [Collections.Generic.List[string]]@('[General]') }
    if (-not ($lines -contains '[General]')) { $lines.Insert(0, '[General]') }
    $want = [ordered]@{
        AutomaticJavaDownload = 'true'   # Prism fetches the Java 26.3 needs
        AutomaticJavaSwitch   = 'true'
        IgnoreJavaWizard      = 'true'
        Language              = 'en_US'
        ApplicationTheme      = 'tux'    # black, like the launcher
        IconTheme             = 'flat_white'
    }
    foreach ($k in $want.Keys) {
        $i = $lines.FindIndex([Predicate[string]]{ param($l) $l -like "$k=*" })
        if ($i -ge 0) {
            if ($k -in 'ApplicationTheme', 'IconTheme') { $lines[$i] = "$k=$($want[$k])" }
        } else {
            $lines.Insert($lines.IndexOf('[General]') + 1, "$k=$($want[$k])")
        }
    }
    [IO.File]::WriteAllLines($cfg, $lines)
}

# --------------------------------------------------------------- instance --
$InstDir = Join-Path $PrismDir "instances\$Inst"
$Mods = Join-Path $InstDir 'minecraft\mods'
New-Item -ItemType Directory -Force -Path $Mods | Out-Null
if (-not (Test-Path (Join-Path $InstDir 'mmc-pack.json'))) {
    Say "Creating the Prism instance `"$Inst`" (Minecraft 26.3 + Fabric)"
    [IO.File]::WriteAllText((Join-Path $InstDir 'instance.cfg'),
        "[General]`nConfigVersion=1.2`nInstanceType=OneSix`nname=$Inst`niconKey=default`n")
    Copy-Item (Join-Path $Here 'mmc-pack.json') (Join-Path $InstDir 'mmc-pack.json')
}

if (-not (Get-ChildItem $Mods -Filter 'fabric-api-*.jar' -ErrorAction SilentlyContinue)) {
    Say 'Downloading Fabric API'
    $q = 'loaders=%5B%22fabric%22%5D&game_versions=%5B%22' + $Inst + '%22%5D'
    $v = Invoke-RestMethod "https://api.modrinth.com/v2/project/fabric-api/version?$q" -Headers $UA
    if (-not $v) { throw "no Fabric API for $Inst on Modrinth" }
    $f = $v[0].files | Where-Object primary | Select-Object -First 1
    if (-not $f) { $f = $v[0].files[0] }
    $out = Join-Path $Mods $f.filename
    Invoke-WebRequest $f.url -OutFile $out -Headers $UA -UseBasicParsing
    if ((Get-FileHash $out -Algorithm SHA512).Hash -ne $f.hashes.sha512.ToUpper()) {
        Remove-Item $out
        throw 'the Fabric API download is damaged'
    }
}

Say 'Downloading the tuxmc mod (latest release)'
$jar = Join-Path $Mods 'tuxmc.jar.part'
Invoke-WebRequest "https://github.com/$Repo/releases/latest/download/tuxmc.jar" -OutFile $jar -Headers $UA -UseBasicParsing
Get-ChildItem $Mods -Filter 'tuxmc*.jar' | Remove-Item
Rename-Item $jar 'tuxmc.jar'

# ---------------------------------------------------------------- account --
$Accounts = Join-Path $PrismDir 'accounts.json'
function Signed-In { (Test-Path $Accounts) -and ((Get-Content $Accounts -Raw) -match '"profile"') }
if (-not $NoSignIn -and -not (Signed-In)) {
    Say 'Sign in to your Microsoft account in Prism'
    Note 'Prism opens now. Click "Add Microsoft account" and sign in with the account that owns Minecraft'
    Note '(or: Accounts at the top right, Manage Accounts, Add Microsoft).'
    Note "This waits until you're signed in. Closing this window skips it; tux-mc asks again when it starts."
    Start-Process $PrismExe -ArgumentList '--show-window'
    while (-not (Signed-In)) { Start-Sleep -Seconds 2 }
    $name = ((Get-Content $Accounts -Raw | ConvertFrom-Json).accounts | ForEach-Object { $_.profile.name } | Select-Object -First 1)
    Say "Signed in as $name"
}

Say 'Done. Start tux-mc from the Start menu or the desktop.'
if (-not $NoStart) {
    $go = Read-Host 'Start it now? [Y/n]'
    if ($go -notmatch '^[nN]') { Start-Process $Exe }
}
