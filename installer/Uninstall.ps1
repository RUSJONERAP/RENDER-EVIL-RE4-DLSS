[CmdletBinding()]
param([string]$Manifest)
$ErrorActionPreference='Stop'
if(Get-Process re4 -ErrorAction SilentlyContinue){throw 'Close RE4 before rollback.'}
Write-Host 'RE:NDER EVIL (DLSS SR/NR) - Restore previous installation'
if(!$Manifest){
 $index=Join-Path $env:LOCALAPPDATA 'RENDER-EVIL\last-install.json'
 if(Test-Path -LiteralPath $index){$Manifest=(Get-Content -LiteralPath $index -Raw|ConvertFrom-Json).manifest}
 if(!$Manifest){$Manifest=Read-Host 'Full path to installed.json printed by the installer'}
}
$state=Get-Content -LiteralPath $Manifest -Raw|ConvertFrom-Json
if($state.status -ne 'installed'){throw "No active installation in this manifest: $($state.status)"}
$game=[IO.Path]::GetFullPath($state.game).TrimEnd('\')+'\'
$backup=[IO.Path]::GetFullPath($state.backup).TrimEnd('\')+'\'
function Target([string]$relative){$p=[IO.Path]::GetFullPath((Join-Path $game $relative));if(!$p.StartsWith($game,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe rollback path'};return $p}
function Original([string]$relative){$p=[IO.Path]::GetFullPath((Join-Path $backup ('originals\'+$relative)));if(!$p.StartsWith($backup,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe backup path'};return $p}
function Hash([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
foreach($entry in $state.files){
 $dst=Target $entry.path
 if($entry.existed -and (Hash (Original $entry.path)) -ne $entry.originalHash){throw "Damaged backup: $($entry.path)"}
 if(!$entry.mutable -and !$entry.remove -and (Test-Path -LiteralPath $dst) -and (Hash $dst) -ne $entry.newHash){throw "An installed component was changed externally: $dst"}
 if($entry.remove -and (Test-Path -LiteralPath $dst)){throw "An archived script was recreated externally: $dst"}
}
$current=Join-Path $backup ('before_rollback_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
foreach($entry in $state.files){
 $dst=Target $entry.path
 if(Test-Path -LiteralPath $dst){$saved=Join-Path $current $entry.path;New-Item -ItemType Directory -Force -Path (Split-Path $saved)|Out-Null;Copy-Item -LiteralPath $dst -Destination $saved;[IO.File]::SetAttributes($dst,[IO.FileAttributes]::Normal)}
 if($entry.existed){
  New-Item -ItemType Directory -Force -Path (Split-Path $dst)|Out-Null;Copy-Item -LiteralPath (Original $entry.path) -Destination $dst -Force
  if((Hash $dst) -ne $entry.originalHash){throw "Restore failed: $dst"}
  [IO.File]::SetAttributes($dst,[IO.FileAttributes]$entry.attributes)
 }elseif(Test-Path -LiteralPath $dst){Remove-Item -LiteralPath $dst -Force}
}
$state.status='rolled_back';$state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $Manifest -Encoding UTF8
Write-Output "Previous installation restored; hashes verified. Settings from this build were saved to $current"
