[CmdletBinding()]
param([string]$GameDirectory)
$ErrorActionPreference='Stop'
Write-Host 'RE:NDER EVIL (DLSS SR/NR) - Installer'
if(!$GameDirectory){
 $steamRoots=New-Object Collections.Generic.List[string]
 foreach($key in @('HKCU:\Software\Valve\Steam','HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')){
  $steam=Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
  if($steam.SteamPath){$steamRoots.Add($steam.SteamPath)}elseif($steam.InstallPath){$steamRoots.Add($steam.InstallPath)}
 }
 $libraries=New-Object Collections.Generic.List[string]
 foreach($steamRoot in $steamRoots){
  $libraries.Add($steamRoot)
  $vdf=Join-Path $steamRoot 'steamapps\libraryfolders.vdf'
  if(Test-Path -LiteralPath $vdf){foreach($match in [regex]::Matches([IO.File]::ReadAllText($vdf),'"path"\s+"([^"]+)"')){$libraries.Add($match.Groups[1].Value.Replace('\\','\'))}}
 }
 $candidates=@(foreach($library in ($libraries|Select-Object -Unique)){
  $app=Join-Path $library 'steamapps\appmanifest_2050650.acf'
  if(Test-Path -LiteralPath $app){$match=[regex]::Match([IO.File]::ReadAllText($app),'"installdir"\s+"([^"]+)"');if($match.Success){$candidate=Join-Path $library ('steamapps\common\'+$match.Groups[1].Value);if(Test-Path -LiteralPath (Join-Path $candidate 're4.exe')){$candidate}}}
 })
 if($candidates.Count -eq 1){$GameDirectory=$candidates[0];Write-Host "Found RE4: $GameDirectory"}
 else{$GameDirectory=Read-Host 'Paste the RE4 folder containing re4.exe (Steam > Manage > Browse local files)'}
}
$game=[IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')+'\'
if(Get-Process re4 -ErrorAction SilentlyContinue){throw 'Close RE4 before installation.'}
$package=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw|ConvertFrom-Json
$utf8=New-Object Text.UTF8Encoding($false)
function Hash([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
function Target([string]$relative){$p=[IO.Path]::GetFullPath((Join-Path $game $relative));if(!$p.StartsWith($game,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe target path'};return $p}
if(!(Test-Path -LiteralPath (Target 're4.exe'))){throw 're4.exe not found.'}
if((Hash (Target 're4.exe')) -ne $package.supportedGameSha256){throw 'This release candidate supports only the documented RE4 1.5.9.0 executable. No files changed.'}
foreach($file in $package.files){if((Hash (Join-Path $PSScriptRoot ('payload\'+$file.path))) -ne $file.sha256){throw "Package checksum mismatch: $($file.path)"}}
if(Test-Path -LiteralPath (Target 'dxgi.dll')){throw 'An existing dxgi.dll injector needs manual review. No files changed.'}
if(Test-Path -LiteralPath (Target 'PDPerfPlugin.dll')){throw 'An old PD Upscaler is present. Back it up and remove it before this native-FSR2 integration.'}
foreach($relative in @('version.dll','reframework\plugins\re4dlss.dll')){
 $entry=$package.files|Where-Object path -eq $relative
 if((Test-Path -LiteralPath (Target $relative)) -and (Hash (Target $relative)) -ne $entry.sha256){throw "An unsupported existing component occupies $relative. No files changed."}
}
function IniSet([string]$source,[string]$section,[string]$key,[string]$value){
 $lines=[regex]::Split($source,'\r?\n');$output=New-Object Collections.Generic.List[string]
 $current='';$found=$false;$seen=($section -eq '')
 foreach($line in $lines){
  if($line -match '^\s*\[([^\]]+)\]\s*$'){
   if($current -ieq $section -and !$found){$output.Add("$key=$value");$found=$true}
   $current=$Matches[1];if($current -ieq $section){$seen=$true}
  }
  if($current -ieq $section -and $line -match ('^\s*'+[regex]::Escape($key)+'\s*=')){
   if(!$found){$output.Add("$key=$value");$found=$true}
  }else{$output.Add($line)}
 }
 if(!$found){if(!$seen){$output.Add("[$section]")};$output.Add("$key=$value")}
 return $output -join "`r`n"
}
function Read([string]$relative){$p=Target $relative;if(Test-Path -LiteralPath $p){return [IO.File]::ReadAllText($p)};return ''}
$backupRoot=Join-Path $env:LOCALAPPDATA 'RENDER-EVIL\Backups'
$backup=Join-Path $backupRoot (Get-Date -Format 'yyyyMMdd_HHmmss_fff')
if([IO.Path]::GetFullPath($backup).StartsWith($game,[StringComparison]::OrdinalIgnoreCase)){throw 'Backup must be outside the game.'}
New-Item -ItemType Directory -Force -Path $backup|Out-Null
$changes=New-Object Collections.Generic.List[object]
foreach($file in $package.files){
 # Existing user configuration and the acknowledged quality command stay intact.
 if($file.preserveExisting -and (Test-Path -LiteralPath (Target $file.path))){continue}
 $changes.Add(@{path=$file.path;source=(Join-Path $PSScriptRoot ('payload\'+$file.path));remove=$false;mutable=$file.mutable})
}
function ConfigChange([string]$relative,[string]$text){
 $source=Join-Path $backup ('candidate\'+$relative);New-Item -ItemType Directory -Force -Path (Split-Path $source)|Out-Null
 [IO.File]::WriteAllText($source,$text,$utf8)
 $changes.Add(@{path=$relative;source=$source;remove=$false;mutable=$true})
}
$config=Read 're2_fw_config.txt'
foreach($key in @('TemporalUpscaler_Enabled','TemporalUpscaler_HUDTrace','REFrameworkConfig_MenuOpen','REFrameworkConfig_RememberMenuState')){$config=IniSet $config '' $key 'false'}
$config=IniSet $config '' 'REFrameworkConfig_MenuKey_V2' '45';ConfigChange 're2_fw_config.txt' $config
$config=Read 'local_config.ini'
foreach($pair in @(@('AppUpScalingTypeFSR1','0'),@('AppUpScalingTypeFSR2','1'),@('AppFrameRate','3'))){$config=IniSet $config 'AppRender' $pair[0] $pair[1]}
ConfigChange 'local_config.ini' $config
$config=Read 'OptiScaler.ini'
if(!$config){$config=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'defaults\OptiScaler.ini'))}
foreach($pair in @(@('DlssNr','Placement','evaluate'),@('DlssNr','LiveReload','true'),@('DlssNr','PanelKey','0x79'),@('Menu','ShortcutKey','-1'),@('FrameGen','Enabled','false'),@('Framerate','FramerateLimit','0'),@('Pacing','FpsLimit','off'))){$config=IniSet $config $pair[0] $pair[1] $pair[2]}
ConfigChange 'OptiScaler.ini' $config
foreach($script in @('reframework\autorun\re4dlss_ui.lua','reframework\autorun\0_hudlab_probe.lua','reframework\autorun\1_hudlab_menu.lua')){
 if(Test-Path -LiteralPath (Target $script)){$changes.Add(@{path=$script;remove=$true;mutable=$true})}
}
$entries=@()
foreach($change in $changes){
 $dst=Target $change.path;$entry=@{path=$change.path;existed=(Test-Path -LiteralPath $dst);remove=$change.remove;mutable=$change.mutable}
 if(!$change.remove){$entry.source=$change.source;$entry.newHash=Hash $change.source}
 if($entry.existed){
  $original=Join-Path $backup ('originals\'+$change.path);New-Item -ItemType Directory -Force -Path (Split-Path $original)|Out-Null
  Copy-Item -LiteralPath $dst -Destination $original
  $entry.originalHash=Hash $dst;$entry.attributes=[int][IO.File]::GetAttributes($dst)
  if((Hash $original) -ne $entry.originalHash){throw "Backup verification failed: $dst"}
 }
 $entries+=,$entry
}
$state=@{game=$game;backup=$backup;version=$package.version;created=(Get-Date -Format o);status='prepared';files=$entries}
$manifest=Join-Path $backup 'installed.json';$state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifest -Encoding UTF8
if(Get-Process re4 -ErrorAction SilentlyContinue){throw 'RE4 started during preparation. No game files changed.'}
$applied=New-Object Collections.Generic.List[object]
try{
 foreach($entry in $entries){
  $dst=Target $entry.path
  if($entry.existed -and (Hash $dst) -ne $entry.originalHash){throw "File changed during preparation: $dst"}
  $applied.Add($entry)
  if(Test-Path -LiteralPath $dst){[IO.File]::SetAttributes($dst,[IO.FileAttributes]::Normal)}
  if($entry.remove){Remove-Item -LiteralPath $dst -Force}else{
   New-Item -ItemType Directory -Force -Path (Split-Path $dst)|Out-Null
   Copy-Item -LiteralPath $entry.source -Destination $dst -Force
   if((Hash $dst) -ne $entry.newHash){throw "Installed checksum mismatch: $dst"}
  }
 }
 $state.status='installed'
 $state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifest -Encoding UTF8
}catch{
 $failure=$_
 foreach($entry in $applied){
  $dst=Target $entry.path
  if(Test-Path -LiteralPath $dst){[IO.File]::SetAttributes($dst,[IO.FileAttributes]::Normal)}
  if($entry.existed){Copy-Item -LiteralPath (Join-Path $backup ('originals\'+$entry.path)) -Destination $dst -Force;[IO.File]::SetAttributes($dst,[IO.FileAttributes]$entry.attributes)}elseif(Test-Path -LiteralPath $dst){Remove-Item -LiteralPath $dst -Force}
 }
 $state.status='failed_restored';$state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifest -Encoding UTF8
 throw $failure
}
Write-Output "Installed $($package.version). FSR 2 must stay enabled; FSR 1 off. Insert opens the RU/EN panel."
Write-Output "Verified backup / rollback manifest: $manifest"
$index=Join-Path $env:LOCALAPPDATA 'RENDER-EVIL\last-install.json'
try{[IO.File]::WriteAllText($index,(@{manifest=$manifest;game=$game;version=$package.version}|ConvertTo-Json),$utf8)}catch{Write-Warning "Keep the backup path above; could not save the uninstall shortcut: $_"}
