# vcpkg asset-source adapter for GitHub archives. Keep the pinned upstream
# archive and expected SHA512; only avoid the unreliable redirect endpoint.
param(
    [Parameter(Mandatory = $true)][string]$Url,
    [Parameter(Mandatory = $true)][string]$Sha512,
    [Parameter(Mandatory = $true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
if ($Url -notmatch '^https://github\.com/(?<owner>[A-Za-z0-9_.-]+)/(?<repo>[A-Za-z0-9_.-]+)/archive/(?<ref>[^?#]+)\.tar\.gz$') {
    exit 1 # Let vcpkg use its normal source for other asset types.
}
$archiveUrl = "https://codeload.github.com/$($Matches.owner)/$($Matches.repo)/tar.gz/$($Matches.ref)"
if ($Sha512 -notmatch '^[A-Fa-f0-9]{128}$') { exit 1 }
& curl.exe --fail --location --retry 3 --retry-all-errors --retry-delay 2 `
    --connect-timeout 20 --max-time 180 --output $Destination $archiveUrl
if ($LASTEXITCODE -ne 0) { exit 1 }
# vcpkg removes PowerShell module search paths from its build environment.
# Use .NET directly rather than relying on the Get-FileHash cmdlet/module.
$stream = [System.IO.File]::OpenRead($Destination)
$algorithm = [System.Security.Cryptography.SHA512]::Create()
try {
    $actualHash = [System.BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '')
}
finally {
    $algorithm.Dispose()
    $stream.Dispose()
}
if ($actualHash -ine $Sha512) {
    [System.IO.File]::Delete($Destination)
    exit 1
}
exit 0
