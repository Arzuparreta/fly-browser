# Provision only a disposable GitHub runner. Never invoked by local bootstrap.
$ErrorActionPreference = 'Stop'
$engineRoot = Join-Path $env:RUNNER_TEMP 'fly-chromium'
$evidenceRoot = Join-Path $engineRoot 'evidence'
New-Item -ItemType Directory -Force -Path $evidenceRoot | Out-Null
# Installer for the SDK revision required by the pinned Chromium build scripts:
# https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads
$sdkInstaller = Join-Path $env:RUNNER_TEMP 'fly-windows-sdk-setup.exe'
$sdkUrl = 'https://download.microsoft.com/download/78b533c3-6724-4e28-a984-b9fa93265781/KIT_BUNDLE_WINDOWSSDK_MEDIACREATION/winsdksetup.exe'
Invoke-WebRequest -Uri $sdkUrl -OutFile $sdkInstaller
$signature = Get-AuthenticodeSignature $sdkInstaller
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    throw 'The SDK installer has no valid Microsoft signature.'
}
Get-FileHash -Algorithm SHA256 $sdkInstaller | Format-List | Out-File (Join-Path $evidenceRoot 'windows-sdk-installer.txt')
$signature | Format-List | Out-File -Append (Join-Path $evidenceRoot 'windows-sdk-installer.txt')
$sdkLog = Join-Path $evidenceRoot 'windows-sdk-install.log'
$result = Start-Process -FilePath $sdkInstaller -ArgumentList @('/quiet', '/norestart', '/ceip', 'off', '/features', '+', '/log', $sdkLog) -Wait -PassThru
if ($result.ExitCode -notin @(0, 3010)) {
    throw "Windows SDK installation failed with exit code $($result.ExitCode)."
}
Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' | Select-Object Name | ConvertTo-Json | Out-File (Join-Path $evidenceRoot 'windows-sdk-installed.json')
