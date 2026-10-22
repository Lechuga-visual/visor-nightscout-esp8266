param([Parameter(Mandatory=$true)][string]$Tool,
      [string]$Url, [string]$Directory, [string]$TrustCertificate,
      [switch]$Screenshot)
$ErrorActionPreference='Stop'
$source=[IO.File]::ReadAllText($Tool)
$marker='#==NIGHTSCOUT_POWERSHELL=='
$payload=$source.Substring($source.LastIndexOf($marker)+$marker.Length)
$parseErrors=$null;$tokens=$null
[void][System.Management.Automation.Language.Parser]::ParseInput($payload,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw ($parseErrors | Out-String)}
. ([scriptblock]::Create($payload)) -LibraryMode
if($Screenshot){
    $start=$payload.IndexOf('Add-Type -AssemblyName System.Windows.Forms')
    $end=$payload.IndexOf('$button.Add_Click(')
    . ([scriptblock]::Create($payload.Substring($start,$end-$start)))
    $urlBox.Text='https://tu-nightscout.example/'
    $form.Show();[Windows.Forms.Application]::DoEvents()
    $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
    try {
        $form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$form.Width,$form.Height)))
        $bitmap.Save((Join-Path $Directory 'clave-windows.png'),[Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose();$form.Close();$form.Dispose();$timer.Dispose() }
    # Render the actual result window with an in-memory, synthetic certificate.
    try { . $script:KeyExportCore -Url 'http://invalid' } catch { }
    $rsa=[Security.Cryptography.RSA]::Create();$rsa.KeySize=2048
    $request=New-Object Security.Cryptography.X509Certificates.CertificateRequest('CN=example.invalid',$rsa,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $sample=$request.CreateSelfSigned([DateTimeOffset]::Now.AddDays(-1),[DateTimeOffset]::Now.AddDays(1))
    try {
        $pem=ConvertTo-PublicPem $sample
        $file=Save-PublicKey $pem 'ejemplo' $Directory
        $done=New-KeyResultWindow $file;$done.Show();[Windows.Forms.Application]::DoEvents()
        $bitmap=New-Object Drawing.Bitmap($done.Width,$done.Height)
        try {
            $done.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$done.Width,$done.Height)))
            $bitmap.Save((Join-Path $Directory 'clave-windows-resultado.png'),[Drawing.Imaging.ImageFormat]::Png)
        } finally { $bitmap.Dispose();$done.Close();$done.Dispose() }
        Remove-Item -LiteralPath $file
    } finally {$sample.Dispose();$rsa.Dispose()}
    Write-Output 'Windows forms rendered'
    exit 0
}
$store=$null;$certificate=$null
try {
    if($TrustCertificate){
        $certificate=New-Object Security.Cryptography.X509Certificates.X509Certificate2($TrustCertificate)
        $store=New-Object Security.Cryptography.X509Certificates.X509Store('Root','CurrentUser')
        $store.Open('ReadWrite');$store.Add($certificate)
    }
    $result=& $script:KeyExportCore -Url $Url
    if(!$result.Pem.StartsWith('-----BEGIN PUBLIC KEY-----')){throw 'Missing PEM'}
    Save-PublicKey $result.Pem $result.Hostname $Directory
} finally {
    if($store){$store.Remove($certificate);$store.Close();$store.Dispose()}
    if($certificate){$certificate.Dispose()}
}
