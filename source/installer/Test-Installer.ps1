$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Installer.Core.ps1')
$script:assertions=0;$script:scenarios=New-Object Collections.Generic.List[string]
function Assert([bool]$Value,[string]$Label){$script:assertions++;if(!$Value){throw ('FAIL: '+$Label)}}
function Reject([scriptblock]$Action,[string]$Label){$caught=$false;try{$null=& $Action}catch{$caught=$true};Assert $caught $Label}
function Gpu([int]$Generation=50,[int]$Driver=62101){[pscustomobject]@{name=('NVIDIA GeForce RTX '+$Generation+'70');generation=$Generation;driverNumber=$Driver;driver='32.0.16.2101';pnp='PCI\VEN_10DE'}}
foreach($g in @(40,50)){Assert ((Resolve-InstallProfile (Gpu $g) auto enabled $false).mode -eq 'native') ('native auto '+$g)}
foreach($g in @(20,30,0)){Assert ((Resolve-InstallProfile (Gpu $g) auto enabled $false).mode -eq 'off') ('safe auto '+$g)}
Assert ((Resolve-InstallProfile (Gpu 50) auto disabled $false).mode -eq 'off') 'HAGS disabled falls back to no FG'
Assert ((Resolve-InstallProfile (Gpu 50) native disabled $false).errors.Count -gt 0) 'HAGS disabled blocks explicit FG'
Assert ((Resolve-InstallProfile (Gpu 50) native unknown $false).warnings.Count -gt 0) 'Missing registry key is unknown'
Assert ((Resolve-InstallProfile (Gpu 30) legacy enabled $false).errors.Count -gt 0) 'Legacy acknowledgement mandatory'
Assert ((Resolve-InstallProfile (Gpu 30 57999) legacy enabled $true).errors.Count -gt 0) 'Driver floor enforced'
Assert ((Resolve-InstallProfile (Gpu 30 58000) legacy enabled $true).errors.Count -eq 0) 'R580 boundary accepted'
Assert ((Resolve-InstallProfile (Gpu 20) legacy enabled $true).errors.Count -eq 0) 'Explicit RTX20 experiment allowed'
Assert ((Resolve-InstallProfile (Gpu 50) legacy enabled $true).errors.Count -gt 0) 'Legacy is not installed on RTX50'
Assert ((Resolve-InstallProfile (Gpu 30) native enabled $true).errors.Count -gt 0) 'Native FG not offered on RTX30'
Assert ((Get-NvidiaDriverNumber '32.0.16.2101') -eq 62101) 'CIM driver 621.01 decoded'
Assert ((Get-NvidiaDriverNumber '32.0.15.8000') -eq 58000) 'CIM R580 boundary decoded'
Assert ((Get-GpuGeneration 'NVIDIA GeForce RTX 3060 Ti') -eq 30) 'RTX30 detection'
Assert ((Get-GpuGeneration 'AMD Radeon RX 7900') -eq 0) 'Non-RTX detection'
$script:scenarios.Add('hardware/driver/HAGS policy matrix')
$package=Join-Path $PSScriptRoot 'tests\fixture_package'
$suite=Join-Path $PSScriptRoot ('tests\run_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$null=New-Item -ItemType Directory -Path $suite
function Fixture([string]$Name){
 $path=Join-Path $suite $Name;$null=New-Item -ItemType Directory -Path $path
 Write-Utf8 (Join-Path $path 're4.exe') 'NONEXECUTABLE GAME HASH FIXTURE'
 Write-Utf8 (Join-Path $path 'dinput8.dll') 'previous framework'
 Write-Utf8 (Join-Path $path 'local_config.ini') "[AppRender]`r`nAppUpScalingTypeFSR1=1`r`nAppUpScalingTypeFSR2=0`r`nAppFrameRate=1`r`nAppTextureQuality=7"
 Write-Utf8 (Join-Path $path 'OptiScaler.ini') "[DlssNr]`r`nEnabled=true`r`nPreset=1`r`nIntensity=1.37`r`n[Other]`r`nKeep=42"
 Write-Utf8 (Join-Path $path 'reframework/data/re4unified/ui.json') '{"quality":2,"language":"ru","fpsLimitEnabled":true,"fpsLimit":80,"customValue":"keep"}'
 Write-Utf8 (Join-Path $path 'reframework/autorun/re4dlss_ui.lua') 'old author menu'
 Write-Utf8 (Join-Path $path 'saves-do-not-touch.txt') 'unrelated data'
 return $path
}
function Snapshot([string]$Game){$result=@{};foreach($p in Get-ChildItem -LiteralPath $Game -Recurse -File){$result[$p.FullName.Substring($Game.Length+1)]=Get-FileDigest $p.FullName};return $result}
function Same([string]$Game,$Before,[string]$Label){$after=Snapshot $Game;Assert ($after.Count -eq $Before.Count) ($Label+' file count');foreach($key in $Before.Keys){Assert ($after[$key] -eq $Before[$key]) ($Label+' '+$key)}}
function Plan([string]$Game,[string]$Mode='native',[int]$Generation=50,[bool]$Ack=$false){Get-InstallPlan -PackageRoot $package -GameDirectory $Game -Gpu (Gpu $Generation) -Mode $Mode -Hags enabled -AcceptedExperimental $Ack}
foreach($mode in @('off','native','legacy')){
 $game=Fixture ('roundtrip_'+$mode);$before=Snapshot $game;$generation=50;$ack=$false;if($mode -eq 'legacy'){$generation=30;$ack=$true}
 $p=Plan $game $mode $generation $ack;$result=Invoke-InstallPlan $p -StateRoot (Join-Path $suite ('state_'+$mode))
 Assert ((Get-Content -LiteralPath $result.manifest -Raw|ConvertFrom-Json).status -eq 'installed') ($mode+' transaction committed')
 Assert ((Read-GameText $game 'OptiScaler.ini') -match 'Intensity=1.37') ($mode+' NR preference preserved')
 Assert ((Read-GameText $game 'local_config.ini') -match 'AppTextureQuality=7') ($mode+' unrelated graphics setting preserved')
 Assert ((Read-GameText $game 'OptiScaler.ini') -match 'Keep=42') ($mode+' unrelated INI preserved')
 $ui=(Read-GameText $game 'reframework/data/re4unified/ui.json')|ConvertFrom-Json
 Assert ($ui.quality -eq 2 -and $ui.language -eq 'ru' -and $ui.customValue -eq 'keep') ($mode+' UI preferences preserved')
 Assert ($ui.fpsLimitEnabled -eq ($mode -eq 'off')) ($mode+' custom limiter state')
 Assert (!(Test-Path -LiteralPath (Join-Path $game 'reframework/autorun/re4dlss_ui.lua'))) ($mode+' duplicate UI archived')
 Assert ((Test-Path -LiteralPath (Join-Path $game 'winmm.dll')) -eq ($mode -eq 'legacy')) ($mode+' exactly correct adapter')
 Assert ((Test-Path -LiteralPath (Join-Path $game 'reframework/plugins/re4dlss_fg/sl.interposer.dll')) -eq ($mode -ne 'off')) ($mode+' correct FG components')
 if($mode -eq 'legacy'){Assert ((Read-GameText $game 'dlssg_sm86.ini') -match 'MaxGeneratedFrames=1') 'Legacy conservative 2x';Assert ((Read-GameText $game 'dlssg_sm86.ini') -match 'Mode=Off') 'Adapter SR disabled'}
 $null=Restore-SmartInstall $result.manifest;Same $game $before ($mode+' restore')
 $script:scenarios.Add($mode+' install and exact rollback')
}
$game=Fixture 'profile_changes';$before=Snapshot $game;$stateRoot=Join-Path $suite 'state_profile_changes'
$native=Invoke-InstallPlan (Plan $game native) $stateRoot
$off=Invoke-InstallPlan (Plan $game off) $stateRoot
Assert (!(Test-Path -LiteralPath (Join-Path $game 'reframework/plugins/re4dlss_fg/sl.dlss_g.dll'))) 'Native->off removes owned FG library'
$legacy=Invoke-InstallPlan (Plan $game legacy 30 $true) $stateRoot
Assert (Test-Path -LiteralPath (Join-Path $game 'winmm.dll')) 'Off->legacy installs single proxy'
$null=Restore-SmartInstall $legacy.manifest
Assert (!(Test-Path -LiteralPath (Join-Path $game 'winmm.dll'))) 'Legacy undo removes adapter'
$null=Restore-SmartInstall $off.manifest
Assert (Test-Path -LiteralPath (Join-Path $game 'reframework/plugins/re4dlss_fg/sl.dlss_g.dll')) 'Off undo restores native FG'
$null=Restore-SmartInstall $native.manifest;Same $game $before 'Nested profile restore'
$script:scenarios.Add('native->off->legacy and reverse rollback/index chain')
$game=Fixture 'foreign_proxy';Write-Utf8 (Join-Path $game 'winmm.dll') 'foreign audio/injector proxy';$before=Snapshot $game
Reject {Plan $game legacy 30 $true} 'Unknown winmm refused';Same $game $before 'Foreign proxy untouched'
$game=Fixture 'wrong_game';Write-Utf8 (Join-Path $game 're4.exe') 'unsupported EXE';$before=Snapshot $game
Reject {Plan $game} 'Wrong game hash refused';Same $game $before 'Wrong game untouched'
$game=Fixture 'race';$p=Plan $game;Write-Utf8 (Join-Path $game 'local_config.ini') 'modified after preview';$before=Snapshot $game
Reject {Invoke-InstallPlan $p (Join-Path $suite 'state_race')} 'Changed file after preview refused';Same $game $before 'Race rejected without changes'
Reject {Join-SafePath $game '..\escape.dll'} 'Traversal refused'
Reject {Join-SafePath $game 'C:\outside.dll'} 'Absolute target refused'
Reject {Join-SafePath $game 'x.dll:stream'} 'Alternate data stream refused'
$script:scenarios.Add('foreign injector/wrong EXE/race/path guards')
$game=Fixture 'nested_old_upscaler';Write-Utf8 (Join-Path $game 'reframework/plugins/PDPerfPlugin.dll') 'old conflicting upscaler';$before=Snapshot $game
Reject {Plan $game} 'Old upscaler in plugins detected';Same $game $before 'Nested old upscaler untouched'
$game=Fixture 'junction';$outside=Join-Path $suite 'outside';$null=New-Item -ItemType Directory -Path $outside
$null=New-Item -ItemType Junction -Path (Join-Path $game 'redirected') -Target $outside
Reject {Join-SafePath $game 'redirected/escape.dll'} 'Nested junction target refused'
$script:scenarios.Add('nested conflicting plugins and junction escape protection')
$game=Fixture 'hardlink';$outsideFile=Join-Path $suite 'manager-copy.dll';Write-Utf8 $outsideFile 'previous framework'
Remove-Item -LiteralPath (Join-Path $game 'dinput8.dll') -Force
$null=New-Item -ItemType HardLink -Path (Join-Path $game 'dinput8.dll') -Target $outsideFile
$before=Snapshot $game;$result=Invoke-InstallPlan (Plan $game native) (Join-Path $suite 'state_hardlink')
Assert ([IO.File]::ReadAllText($outsideFile) -eq 'previous framework') 'Install leaves mod-manager hardlink peer unchanged'
$null=Restore-SmartInstall $result.manifest;Same $game $before 'Hardlink original restored'
Assert ([IO.File]::ReadAllText($outsideFile) -eq 'previous framework') 'Rollback leaves hardlink peer unchanged'
$script:scenarios.Add('atomic replacement protects mod-manager hardlink peers')
$game=Fixture 'modified_component';$p=Plan $game;$result=Invoke-InstallPlan $p (Join-Path $suite 'state_tamper')
Write-Utf8 (Join-Path $game 'dinput8.dll') 'third-party modification after install';$before=Snapshot $game
Reject {Restore-SmartInstall $result.manifest} 'Changed immutable component blocks destructive rollback';Same $game $before 'Modified component untouched'
$script:scenarios.Add('rollback protects externally changed components')
$game=Fixture 'transaction_failure';$p=Plan $game;$before=Snapshot $game;$script:injectRoot=$game;$script:writes=0;$script:fired=$false
function Copy-Item {
 [CmdletBinding()]param([string]$LiteralPath,[string]$Destination,[switch]$Force)
 if(!$script:fired -and $Destination.StartsWith($script:injectRoot,[StringComparison]::OrdinalIgnoreCase)){
  $script:writes++;if($script:writes -eq 3){$script:fired=$true;throw 'INJECTED COPY FAILURE'}
 }
 Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Force:$Force
}
Reject {Invoke-InstallPlan $p (Join-Path $suite 'state_failure')} 'Injected mid-commit failure surfaced'
Remove-Item Function:\Copy-Item
Assert $script:fired 'Failure injection reached live commit';Same $game $before 'Failed transaction auto-restored'
$script:scenarios.Add('mid-commit failure automatically restores originals')
$game=Fixture 'fresh_old_gpu';$p=Plan $game auto 30
Assert ($p.profile.mode -eq 'off') 'Fresh RTX30 defaults to no FG'
$gpu50=Gpu 50;$hardware=Get-InstallerHardware
Assert ($null -ne $hardware.hags) 'Hardware probe returns explicit HAGS state'
$game=Fixture 'tampered_package';$before=Snapshot $game
$catalog=Get-Content -LiteralPath (Join-Path $package 'catalog.json') -Raw|ConvertFrom-Json
$file=Join-SafePath $package $catalog.files[0].source;$original=[IO.File]::ReadAllBytes($file)
try{[IO.File]::WriteAllText($file,'corrupt');Reject {Plan $game} 'Package corruption detected before backup/commit';Same $game $before 'Corrupt package leaves game untouched'}finally{[IO.File]::WriteAllBytes($file,$original)}
$script:scenarios.Add('package checksum validation')
$report=[ordered]@{passed=$true;assertions=$script:assertions;scenarios=@($script:scenarios.ToArray());testRoot=$suite;powerShell=$PSVersionTable.PSVersion.ToString();gameExecuted=$false;realGameModified=$false;at=(Get-Date -Format o)}
Write-Utf8 (Join-Path $PSScriptRoot 'test_results.json') ($report|ConvertTo-Json -Depth 6)
$report|ConvertTo-Json -Depth 6
