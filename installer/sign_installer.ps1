# Builds output\OpenPhantom_Installer.exe signed, and checks the signature on the result.
#
#   powershell -ExecutionPolicy Bypass -File sign_installer.ps1 -Certificate <file.cer or thumbprint>
#
# The private key never passes through here. It stays wherever the certificate's issuer keeps it, a
# smart card or a cloud signing service such as Certum's SimplySign, and that has to be connected
# first: the certificate then sits in the current user's personal store with its key attached, and
# signtool picks it by thumbprint. The .cer file is only the public half and is read for that
# thumbprint alone.
#
# -Certificate can also come from the environment as OPENPHANTOM_SIGN_CERT, so a machine that signs
# regularly does not need it on every call. -OutputDir builds somewhere other than output\.

param(
    [string]$Certificate = $env:OPENPHANTOM_SIGN_CERT,
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Script = Join-Path $PSScriptRoot 'openphantom_installer.iss'

# Certum's RFC 3161 server. The timestamp is what keeps a signature valid after the certificate
# expires, which is a year after issue, so an installer signed without one would stop verifying
# while it is still on the releases page.
$TimestampUrl = 'http://time.certum.pl'

function Find-Thumbprint([string]$value) {
    if (-not $value) {
        throw 'No certificate given. Pass -Certificate with the .cer file or its thumbprint, or set OPENPHANTOM_SIGN_CERT.'
    }
    if (Test-Path -LiteralPath $value -PathType Leaf) {
        $path = (Resolve-Path -LiteralPath $value).Path
        return (New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 $path).Thumbprint
    }
    $thumbprint = ($value -replace '\s', '').ToUpperInvariant()
    if ($thumbprint -notmatch '^[0-9A-F]{40}$') {
        throw "'$value' is neither a certificate file nor a 40 digit thumbprint."
    }
    return $thumbprint
}

# The newest Windows SDK wins. The folder layout is Windows Kits\10\bin\<version>\<arch>\signtool.exe.
function Find-SignTool {
    $arch = if ([Environment]::Is64BitOperatingSystem) { 'x64' } else { 'x86' }
    $bin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $found = Get-ChildItem -LiteralPath $bin -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+(\.\d+){3}$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "$arch\signtool.exe" } |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if (-not $found) {
        throw "signtool.exe not found under $bin. It comes with the Windows SDK, in its signing tools."
    }
    return $found
}

# Inno Setup installs per user by default, into AppData, so Program Files is the wrong place to
# look. Its uninstall key records where it went either way.
function Find-Iscc {
    $keys = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup *_is1',
            'Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup *_is1'
    $found = foreach ($hive in 'HKCU:', 'HKLM:') {
        foreach ($key in $keys) {
            Get-ItemProperty -Path (Join-Path $hive $key) -ErrorAction SilentlyContinue
        }
    }
    $iscc = $found |
        Sort-Object { [version]$_.DisplayVersion } -Descending |
        ForEach-Object { Join-Path $_.InstallLocation 'ISCC.exe' } |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if (-not $iscc) {
        $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($command) { $iscc = $command.Source }
    }
    if (-not $iscc) {
        throw 'ISCC.exe not found. Install Inno Setup or put ISCC.exe on the PATH.'
    }
    return $iscc
}

$thumbprint = Find-Thumbprint $Certificate

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $thumbprint }
if (-not $cert) {
    throw "Certificate $thumbprint is not in the personal store. Connect the signing service (SimplySign Desktop) and run this again."
}
if (-not $cert.HasPrivateKey) {
    throw "Certificate $thumbprint is in the personal store without its private key. The signing service is not connected."
}
if ($cert.NotAfter -lt (Get-Date)) {
    throw "Certificate $thumbprint expired on $($cert.NotAfter)."
}

$signtool = Find-SignTool
$iscc = Find-Iscc

# The script names its own output. Read from it so the two cannot disagree.
$match = Select-String -LiteralPath $Script -Pattern '^OutputBaseFilename=(.+)$' | Select-Object -First 1
if (-not $match) {
    throw "No OutputBaseFilename in $Script."
}
$outName = $match.Matches[0].Groups[1].Value.Trim() + '.exe'

if ($OutputDir) {
    # A trailing backslash would escape the closing quote PowerShell puts around the argument.
    $outDir = [IO.Path]::GetFullPath($OutputDir).TrimEnd('\')
} else {
    $outDir = Join-Path $PSScriptRoot 'output'
}
$exe = Join-Path $outDir $outName

# $q and $f are Inno's, not PowerShell's: a quote character and the quoted file being signed. The
# single quotes keep PowerShell from expanding them.
$command = '$q' + $signtool + '$q sign /sha1 ' + $thumbprint + ' /fd sha256 /tr ' + $TimestampUrl + ' /td sha256 $f'

$arguments = @('/DSIGN', "/Ssigntool=$command")
if ($OutputDir) { $arguments += "/O$outDir" }
$arguments += $Script

Write-Host "Signing with $thumbprint ($($cert.Subject))"
Write-Host "signtool: $signtool"
Write-Host "ISCC:     $iscc"
& $iscc @arguments
if ($LASTEXITCODE -ne 0) {
    throw "ISCC failed with exit code $LASTEXITCODE."
}

# Checked on the file that came out, not assumed from the exit code: a signature from another
# certificate, or one without a timestamp, is a failure here even though ISCC reports success.
$signature = Get-AuthenticodeSignature -LiteralPath $exe
if ($signature.Status -ne 'Valid') {
    throw "$exe does not verify: $($signature.Status). $($signature.StatusMessage)"
}
if ($signature.SignerCertificate.Thumbprint -ne $thumbprint) {
    throw "$exe is signed by $($signature.SignerCertificate.Thumbprint), not by $thumbprint."
}
if (-not $signature.TimeStamperCertificate) {
    throw "$exe is signed but carries no timestamp."
}

$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host ''
Write-Host "Signed:      $exe"
Write-Host "Signer:      $($signature.SignerCertificate.Subject)"
Write-Host "Timestamped: $($signature.TimeStamperCertificate.Subject)"
Write-Host "SHA256:      $hash"
