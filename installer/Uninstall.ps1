[CmdletBinding()]
param([string]$GameDirectory,[string]$Manifest)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Installer.Core.ps1')
if(!$Manifest){
 if(!$GameDirectory){$games=@(Get-GameCandidates|Select-Object -Unique);if($games.Count -eq 1){$GameDirectory=$games[0]}else{$GameDirectory=Read-Host 'RE4 game folder / папка игры'}}
 $index=Get-InstallIndex $GameDirectory (Join-Path $env:LOCALAPPDATA 'RENDER-EVIL\SmartInstaller')
 if(!(Test-Path -LiteralPath $index)){throw 'No smart-installer backup found for this game folder. Use the uninstall tool of the previous package.'}
 $Manifest=(Get-Content -LiteralPath $index -Raw -Encoding UTF8|ConvertFrom-Json).manifest
}
Restore-SmartInstall $Manifest|ConvertTo-Json
