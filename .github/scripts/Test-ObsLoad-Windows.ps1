[CmdletBinding()]
param(
    [ValidateSet('RelWithDebInfo', 'Release')]
    [string] $Configuration = 'RelWithDebInfo',
    [string] $ArchivePath,
    [string] $PackagePath,
    [string] $OutputRoot,
    [ValidateRange(20, 300)]
    [int] $TimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Normal-install verification requires a fresh GitHub-hosted runner.'
}
$ConfigDir = Join-Path $env:APPDATA 'obs-studio'
$SystemPlugins = Join-Path $env:ProgramData 'obs-studio/plugins'
$ProductInstall = Join-Path $SystemPlugins 'simple-drop-shadow'
$ProbeInstall = Join-Path $SystemPlugins 'drop-shadow-install-probe'
foreach ($Existing in @($ConfigDir, $ProductInstall, $ProbeInstall)) {
    if (Test-Path -LiteralPath $Existing) { throw "Fresh runner required; existing state: $Existing" }
}
if (@(Get-Process -Name obs64 -ErrorAction SilentlyContinue).Count) { throw 'An OBS process already exists.' }
foreach ($Variable in @('OBS_PLUGINS_PATH', 'OBS_LEGACY_PLUGINS_PATH', 'OBS_LEGACY_PLUGINS_DATA_PATH')) {
    if ([Environment]::GetEnvironmentVariable($Variable)) { throw "External plugin search path is set: $Variable" }
}

$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$OutputRoot) { $OutputRoot = Join-Path $ProjectRoot '.deps/obs-load-windows' }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'OutputRoot already exists. Use a new directory to preserve evidence.' }
$BuildSpec = Get-Content -LiteralPath (Join-Path $ProjectRoot 'buildspec.json') -Raw | ConvertFrom-Json
if (!$PackagePath) { $PackagePath = Join-Path $ProjectRoot "release/$($BuildSpec.name)-$($BuildSpec.version)-windows-x64.zip" }
$ProbeDll = Join-Path $ProjectRoot "build_x64/install-probe/$Configuration/drop-shadow-install-probe.dll"
foreach ($File in @($PackagePath, $ProbeDll)) {
    if (!(Test-Path -LiteralPath $File -PathType Leaf)) { throw "Missing built module: $File" }
}
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$PackageDir = Join-Path $OutputRoot 'package'
Expand-Archive -LiteralPath $PackagePath -DestinationPath $PackageDir
$ProductDll = Join-Path $PackageDir 'simple-drop-shadow/simple-drop-shadow.dll'
$ProductData = Join-Path $PackageDir 'simple-drop-shadow/data'
if (!(Test-Path -LiteralPath $ProductDll -PathType Leaf) -or !(Test-Path -LiteralPath $ProductData -PathType Container)) { throw 'Unexpected product package layout.' }
if (Test-Path -LiteralPath (Join-Path $PackageDir 'simple-drop-shadow/bin/64bit')) { throw 'Legacy binary layout found in the new distribution.' }
$OldPackagePath = Join-Path $OutputRoot 'simple-drop-shadow.zip'
Invoke-WebRequest -Uri 'https://github.com/AllegroMoltoV/obs-simple-drop-shadow/releases/download/v0.1.1/simple-drop-shadow.zip' -OutFile $OldPackagePath
$OldPackageHash = (Get-FileHash -LiteralPath $OldPackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($OldPackageHash -ne 'b744e120a498aed68e1f6f42b8cbfaebc29f48eee9146b03dd3005aed5547e25') { throw 'Official v0.1.1 package checksum mismatch.' }
$OldPackageDir = Join-Path $OutputRoot 'old-package'
Expand-Archive -LiteralPath $OldPackagePath -DestinationPath $OldPackageDir
$OldProductDll = Join-Path $OldPackageDir 'simple-drop-shadow/bin/64bit/simple-drop-shadow.dll'
if (!(Test-Path -LiteralPath $OldProductDll -PathType Leaf)) { throw 'Unexpected v0.1.1 package layout.' }
$ResultDir = Join-Path $OutputRoot 'results'
New-Item -ItemType Directory -Path $ResultDir | Out-Null
if (!$ArchivePath) {
    $ArchivePath = Join-Path $OutputRoot 'OBS-Studio-33.0.1-Windows-x64.zip'
    Invoke-WebRequest -Uri 'https://github.com/obsproject/obs-studio/releases/download/33.0.1/OBS-Studio-33.0.1-Windows-x64.zip' -OutFile $ArchivePath
}
$ExpectedHash = '0d9436451c2d772b33428e3fdae9776f071895c335de17283176306fb72651d3'
$ActualHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualHash -ne $ExpectedHash) { throw 'Official OBS runtime archive checksum mismatch.' }
$Runtime = Join-Path $OutputRoot 'runtime'
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $Runtime
$BinaryDir = Join-Path $Runtime 'bin/64bit'
$ObsExe = Join-Path $BinaryDir 'obs64.exe'
if (!(Test-Path -LiteralPath $ObsExe -PathType Leaf)) { throw 'Unexpected OBS archive layout.' }
New-Item -ItemType Directory -Path $SystemPlugins -Force | Out-Null
# Install the distribution's normal ProgramData layout on the disposable runner.
Copy-Item -LiteralPath (Join-Path $PackageDir 'simple-drop-shadow') -Destination $ProductInstall -Recurse
New-Item -ItemType Directory -Path $ProbeInstall -Force | Out-Null
Copy-Item -LiteralPath $ProbeDll -Destination $ProbeInstall
$InstalledDll = Join-Path $ProductInstall 'simple-drop-shadow.dll'
$OldInstalledDll = Join-Path $ProductInstall 'bin/64bit/simple-drop-shadow.dll'
if ((Get-FileHash -LiteralPath $InstalledDll).Hash -ne (Get-FileHash -LiteralPath $ProductDll).Hash) {
    throw 'Installed DLL differs from the distribution.'
}
$ProfileDir = Join-Path $ConfigDir 'basic/profiles/Baseline'
$SceneDir = Join-Path $ConfigDir 'basic/scenes'
function Initialize-TestConfig {
New-Item -ItemType Directory -Path $ProfileDir, $SceneDir -Force | Out-Null
$UserIni = "[General]`nLanguage=en-US`nFirstRun=true`n[Basic]`nProfile=Baseline`nProfileDir=Baseline`nSceneCollection=Baseline`nSceneCollectionFile=Baseline.json`nConfigOnNewProfile=false`n[BasicWindow]`nPreviewEnabled=false`n"
Set-Content -LiteralPath (Join-Path $ConfigDir 'user.ini') -Value $UserIni -Encoding utf8
Set-Content -LiteralPath (Join-Path $ConfigDir 'global.ini') -Value "[General]`nEnableAutoUpdates=false`n" -Encoding utf8
$ProfileIni = "[General]`nName=Baseline`n[Audio]`nSampleRate=48000`nChannelSetup=Stereo`nMonitoringDeviceId=default`n[Video]`nBaseCX=640`nBaseCY=360`nOutputCX=640`nOutputCY=360`nFPSType=0`nFPSCommon=30`n"
Set-Content -LiteralPath (Join-Path $ProfileDir 'basic.ini') -Value $ProfileIni -Encoding utf8
$InitialScene = @{
    name = 'Baseline'
    sources = @(@{ name = 'Scene'; id = 'scene'; settings = @{ items = @() } })
    scene_order = @(@{ name = 'Scene' })
    current_scene = 'Scene'
    current_program_scene = 'Scene'
} | ConvertTo-Json -Depth 8
Set-Content -LiteralPath (Join-Path $SceneDir 'Baseline.json') -Value $InitialScene -Encoding utf8
}
Initialize-TestConfig
$PreviousResultDir = $env:DROP_SHADOW_INSTALL_RESULT_DIR
$PreviousPhase = $env:DROP_SHADOW_INSTALL_PHASE
$Process = $null
try {
    foreach ($Stage in @('fresh', 'old-setup', 'upgrade')) {
        $Phase = switch ($Stage) { 'fresh' { 'setup' }; 'old-setup' { 'setup' }; 'upgrade' { 'restore' } }
        if ($Stage -eq 'old-setup') {
            # Preserve the fresh-install evidence and start an independent legacy configuration.
            $FreshConfig = [IO.Path]::GetFullPath((Join-Path $OutputRoot 'fresh-config'))
            $ResolvedConfig = [IO.Path]::GetFullPath($ConfigDir)
            $ExpectedConfig = [IO.Path]::GetFullPath((Join-Path $env:APPDATA 'obs-studio'))
            if ($ResolvedConfig -ne $ExpectedConfig -or
                [IO.Path]::GetDirectoryName($FreshConfig) -ne $OutputRoot -or
                (Test-Path -LiteralPath $FreshConfig)) { throw 'Unexpected disposable configuration paths.' }
            Move-Item -LiteralPath $ResolvedConfig -Destination $FreshConfig
            Initialize-TestConfig
            # Remove only the new-layout binaries inside this disposable installation.
            $InstallRoot = [IO.Path]::GetFullPath($ProductInstall)
            foreach ($Name in @('simple-drop-shadow.dll', 'simple-drop-shadow.pdb')) {
                $RemovePath = [IO.Path]::GetFullPath((Join-Path $InstallRoot $Name))
                if ([IO.Path]::GetDirectoryName($RemovePath) -ne $InstallRoot) { throw 'Binary removal escaped the disposable product directory.' }
                if (Test-Path -LiteralPath $RemovePath -PathType Leaf) { Remove-Item -LiteralPath $RemovePath }
            }
            Get-ChildItem -LiteralPath (Join-Path $OldPackageDir 'simple-drop-shadow') | Copy-Item -Destination $ProductInstall -Recurse -Force
        } elseif ($Stage -eq 'upgrade') {
            # Keep the legacy bin tree to verify new-layout precedence during upgrades.
            Get-ChildItem -LiteralPath (Join-Path $PackageDir 'simple-drop-shadow') | Copy-Item -Destination $ProductInstall -Recurse -Force
        }
        $ExpectedDll = if ($Stage -eq 'old-setup') { $OldInstalledDll } else { $InstalledDll }
        $SourceDll = if ($Stage -eq 'old-setup') { $OldProductDll } else { $ProductDll }
        if ((Get-FileHash -LiteralPath $ExpectedDll).Hash -ne (Get-FileHash -LiteralPath $SourceDll).Hash) { throw "Installed DLL differs from the distribution: $Stage" }
        if ($Stage -in @('upgrade') -and (Get-FileHash -LiteralPath $OldInstalledDll).Hash -ne (Get-FileHash -LiteralPath $OldProductDll).Hash) { throw 'Legacy DLL was not preserved during upgrade.' }
        $PhaseDir = Join-Path $ResultDir $Stage
        New-Item -ItemType Directory -Path $PhaseDir | Out-Null
        $env:DROP_SHADOW_INSTALL_RESULT_DIR = $PhaseDir
        $env:DROP_SHADOW_INSTALL_PHASE = $Phase
        $Process = Start-Process -FilePath $ObsExe -WorkingDirectory $BinaryDir -ArgumentList @('--disable-updater', '--multi') -WindowStyle Hidden -PassThru
        if (!$Process.WaitForExit($TimeoutSeconds * 1000)) { throw "OBS baseline probe timed out: $Phase" }
        if ($Process.ExitCode -ne 0) { throw "OBS exited with code $($Process.ExitCode): $Phase" }
        $ResultPath = Join-Path $PhaseDir 'result.json'
        if (!(Test-Path -LiteralPath $ResultPath)) { throw "Probe result missing: $Phase" }
        $Result = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
        if ($Result.passed -ne $true -or $Result.obs -ne '33.0.1' -or $Result.phase -ne $Phase) { throw "Probe failed: $Phase $($Result.reason)" }
        if (!$Result.product_binary_path -or [IO.Path]::GetFullPath($Result.product_binary_path, [IO.Path]::GetFullPath($BinaryDir)) -ne [IO.Path]::GetFullPath($ExpectedDll)) { throw "Unexpected loaded product DLL: $($Result.product_binary_path)" }
        $Logs = @(Get-ChildItem -LiteralPath (Join-Path $ConfigDir 'logs') -Filter '*.txt' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
        if (!$Logs.Count) { throw 'OBS log missing.' }
        $LogText = ($Logs | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
        if ([regex]::Matches($LogText, '\[simple-drop-shadow\] plugin loaded successfully').Count -ne 1) { throw 'Expected exactly one product module load.' }
        if ($LogText -match '(?i)(duplicate|already registered).*drop_shadow_filter|drop_shadow_filter.*(duplicate|already registered)') { throw 'Duplicate filter registration detected.' }
        foreach ($Required in @('Portable mode: false', 'plugin loaded successfully', "DROP_SHADOW_INSTALL PASS phase=$Phase")) {
            if (!$LogText.Contains($Required)) { throw "Required log marker missing: $Required" }
        }
        foreach ($Resource in @('effect_path', 'locale_path')) {
            $Relative = if ($Resource -eq 'effect_path') { 'data/effects/drop-shadow.effect' } else { 'data/locale/en-US.ini' }
            $ExpectedResource = [IO.Path]::GetFullPath((Join-Path $ProductInstall $Relative))
            $ObservedResource = $Result.evidence.$Resource
            if (!$ObservedResource -or [IO.Path]::GetFullPath($ObservedResource, $BinaryDir) -ne $ExpectedResource) { throw "Unexpected product resource: $Resource" }
        }
        if ($Result.evidence.pixel_result -ne 0 -or $Result.evidence.filter_name -ne 'Drop Shadow') { throw 'Pixel or locale verification failed.' }
        $SavedScene = Get-Content -LiteralPath (Join-Path $SceneDir 'Baseline.json') -Raw | ConvertFrom-Json
        $SavedSource = @($SavedScene.sources | Where-Object { $_.name -eq 'Drop shadow install impulse' })
        $SavedFilter = @($SavedSource.filters | Where-Object { $_.id -eq 'drop_shadow_filter' })
        if ($SavedSource.Count -ne 1 -or $SavedFilter.Count -ne 1 -or
            $SavedFilter[0].settings.offset_x -ne 0 -or $SavedFilter[0].settings.offset_y -ne 0 -or
            $SavedFilter[0].settings.blur_radius -ne 1 -or $SavedFilter[0].settings.opacity -ne 1 -or
            $SavedFilter[0].settings.color -ne 4278190080) { throw 'Saved filter settings mismatch.' }
        Copy-Item -LiteralPath $ConfigDir -Destination (Join-Path $PhaseDir 'saved-config') -Recurse
        Write-Output "OBS 33.0.1 normal installation verification passed: $Stage"
    }
} finally {
    if ($Process -and !$Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        $Process.WaitForExit()
    }
    $env:DROP_SHADOW_INSTALL_RESULT_DIR = $PreviousResultDir
    $env:DROP_SHADOW_INSTALL_PHASE = $PreviousPhase
    $LogDir = Join-Path $ConfigDir 'logs'
    if (Test-Path -LiteralPath $LogDir) { Copy-Item -LiteralPath $LogDir -Destination (Join-Path $ResultDir 'obs-logs') -Recurse }
    @{
        runtime_version = '33.0.1'
        runtime_archive_sha256 = $ActualHash
        package_sha256 = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash
        old_package_sha256 = $OldPackageHash
        product_sha256 = (Get-FileHash -LiteralPath $ProductDll -Algorithm SHA256).Hash
        product_installed_path = $InstalledDll
        config_directory = $ConfigDir
        probe_sha256 = (Get-FileHash -LiteralPath $ProbeDll -Algorithm SHA256).Hash
        process_exit_code = $(if ($Process -and $Process.HasExited) { $Process.ExitCode } else { $null })
        runner_image = $env:ImageVersion
        architecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ResultDir 'environment.json') -Encoding utf8
}
