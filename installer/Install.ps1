[CmdletBinding()]
param([string]$GameDirectory,[ValidateSet('auto','off','native','legacy')][string]$Mode='auto',[int]$GpuIndex=-1,[ValidateSet('preserve','on','off')][string]$NrMode='preserve',[switch]$AcknowledgeExperimental,[switch]$CheckOnly)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Installer.Core.ps1')
$hardware=Get-InstallerHardware
if(!$hardware.is64Bit){throw 'Windows x64 is required.'}
if(!$GameDirectory){$games=@(Get-GameCandidates|Select-Object -Unique);if($games.Count -eq 1){$GameDirectory=$games[0]}else{throw 'Specify -GameDirectory or use Install.cmd to select the game folder.'}}
$gpus=@($hardware.gpus);$gpu=$null
if($GpuIndex -ge 0){if($GpuIndex -ge $gpus.Count){throw 'GPU index out of range.'};$gpu=$gpus[$GpuIndex]}else{
 $rtx=@($gpus|Where-Object generation -In @(20,30,40,50))
 if($rtx.Count -eq 1){$gpu=$rtx[0]}elseif($rtx.Count -gt 1){throw 'Multiple RTX GPUs detected; select one in the GUI or provide -GpuIndex.'}elseif($gpus.Count -eq 1){$gpu=$gpus[0]}
}
$plan=Get-InstallPlan -PackageRoot $PSScriptRoot -GameDirectory $GameDirectory -Gpu $gpu -Mode $Mode -Hags $hardware.hags -AcceptedExperimental ([bool]$AcknowledgeExperimental) -NrMode $NrMode
if($CheckOnly){$plan|Select-Object version,game,profile,gpu,hags,nrMode,warnings,bytes|ConvertTo-Json -Depth 8;exit 0}
$result=Invoke-InstallPlan $plan
$result|ConvertTo-Json
Write-Host 'Installed. Keep in-game FSR 2 ON and FSR 1 OFF. Insert opens the panel. NR is independent.'
Write-Host ('Rollback manifest: '+$result.manifest)
