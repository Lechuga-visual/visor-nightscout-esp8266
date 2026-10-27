@echo off
setlocal DisableDelayedExpansion
set "NIGHTSCOUT_KEY_TOOL=%~f0"
where powershell.exe >nul 2>nul
if errorlevel 1 (
  echo This tool needs Windows PowerShell, included with Windows 10 and 11.
  pause
  exit /b 1
)
start "" powershell.exe -NoLogo -NoProfile -STA -WindowStyle Hidden -Command "$source=[IO.File]::ReadAllText($env:NIGHTSCOUT_KEY_TOOL); $marker='#==NIGHTSCOUT_POWERSHELL=='; & ([scriptblock]::Create($source.Substring($source.LastIndexOf($marker)+$marker.Length)))"
exit /b
#==NIGHTSCOUT_POWERSHELL==
param([switch]$LibraryMode)
$ErrorActionPreference = 'Stop'

# Runs in a separate PowerShell runspace so that the window remains responsive.
$script:KeyExportCore = {
    param([string]$Url)
    $ErrorActionPreference = 'Stop'
    function New-DerBlock([byte]$Tag, [byte[]]$Data) {
        [byte[]]$length = @()
        if ($Data.Length -lt 128) { $length = @([byte]$Data.Length) }
        else {
            $size = $Data.Length
            while ($size -gt 0) { $length = @([byte]($size -band 255)) + $length; $size = $size -shr 8 }
            $length = @([byte](128 + $length.Length)) + $length
        }
        return ,([byte[]](@($Tag) + $length + $Data))
    }
    function ConvertTo-PublicPem([System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate) {
        $public = $Certificate.PublicKey
        [byte[]]$parameters = $public.EncodedParameters.RawData
        if ($public.Oid.Value -eq '1.2.840.113549.1.1.1') {
            [byte[]]$oid = 6,9,42,134,72,134,247,13,1,1,1
            if (!$parameters.Length) { $parameters = 5,0 }
        } elseif ($public.Oid.Value -eq '1.2.840.10045.2.1') {
            [byte[]]$oid = 6,7,42,134,72,206,61,2,1
            $curve = [Convert]::ToBase64String($parameters)
            if ($curve -notin @('BggqhkjOPQMBBw==','BgUrgQQAIg==','BgUrgQQAIw==')) {
                throw 'This key curve is not supported by the ESP8266.'
            }
        } else { throw 'The site needs an ESP8266-compatible RSA or EC public key.' }
        [byte[]]$algorithm = New-DerBlock 48 ([byte[]]($oid + $parameters))
        [byte[]]$bits = New-DerBlock 3 ([byte[]](@(0) + $public.EncodedKeyValue.RawData))
        [byte[]]$der = New-DerBlock 48 ([byte[]]($algorithm + $bits))
        $encoded = [Convert]::ToBase64String($der)
        $lines = @('-----BEGIN PUBLIC KEY-----')
        for ($i = 0; $i -lt $encoded.Length; $i += 64) {
            $lines += $encoded.Substring($i, [Math]::Min(64, $encoded.Length - $i))
        }
        $pem = ($lines + '-----END PUBLIC KEY-----' -join "`r`n") + "`r`n"
        if ($pem.Length -gt 3072) { throw 'The key exceeds the space available on the ESP8266.' }
        return $pem
    }
    [Uri]$address = $null
    $Url = $Url.Trim()
    if ($Url -notmatch '^https://' -or $Url -match '[\x00-\x20\x7f\\]' -or
        ![Uri]::TryCreate($Url, [UriKind]::Absolute, [ref]$address) -or
        $address.Scheme -ne 'https' -or !$address.Host -or $address.UserInfo -or
        $address.Query -or $address.Fragment -or $address.Port -lt 1) {
        throw 'Paste a full HTTPS URL without a username, password, token or parameters. Example: https://your-nightscout.example/'
    }
    $hostname = $address.IdnHost.TrimEnd('.')
    $tcp = New-Object Net.Sockets.TcpClient
    $tls = $null
    try {
        $connection = $tcp.BeginConnect($hostname, $address.Port, $null, $null)
        try {
            if (!$connection.AsyncWaitHandle.WaitOne(10000)) { throw 'The site did not respond. Check your connection and URL.' }
            $tcp.EndConnect($connection)
        } finally { $connection.AsyncWaitHandle.Close() }
        # No custom validation callback: the OS verifies trust, dates and hostname.
        $tls = New-Object Net.Security.SslStream($tcp.GetStream(), $false)
        $tls.ReadTimeout = 10000; $tls.WriteTimeout = 10000
        try { $tls.AuthenticateAsClient($hostname, $null, [Security.Authentication.SslProtocols]::Tls12, $false) }
        catch { throw 'Could not verify the site certificate. Check the URL and computer clock. No key was saved.' }
        $certificate = New-Object Security.Cryptography.X509Certificates.X509Certificate2($tls.RemoteCertificate)
        try { $pem = ConvertTo-PublicPem $certificate } finally { $certificate.Dispose() }
        return [pscustomobject]@{ Hostname = $hostname; Pem = $pem }
    } finally {
        if ($tls) { $tls.Dispose() }
        $tcp.Dispose()
    }
}

function Save-PublicKey([string]$Pem, [string]$Hostname, [string]$Directory) {
    $safe = [regex]::Replace($Hostname, '[^a-zA-Z0-9._-]', '_')
    if ($safe.Length -gt 60) { $safe = $safe.Substring(0,60) }
    $base = 'clave-nightscout-' + $safe + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
    for ($i = 0; $i -lt 100; $i++) {
        $suffix = if ($i -eq 0) { '' } else { '-' + $i }
        $path = Join-Path $Directory ($base + $suffix + '.txt')
        try { $file = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None) }
        catch [IO.IOException] { if ([IO.File]::Exists($path)) { continue }; throw }
        try { $data = [Text.Encoding]::ASCII.GetBytes($Pem); $file.Write($data,0,$data.Length) }
        catch { $file.Dispose(); [IO.File]::Delete($path); throw }
        finally { $file.Dispose() }
        return $path
    }
    throw 'Could not create a new file on the Desktop.'
}

function New-KeyResultWindow([string]$Path) {
    $resultForm=New-Object Windows.Forms.Form
    $resultForm.Text='Public key ready'
    $resultForm.ClientSize=New-Object Drawing.Size(680,410)
    $resultForm.StartPosition='CenterScreen'
    $resultForm.FormBorderStyle='FixedDialog';$resultForm.MaximizeBox=$false
    $instructions=New-Object Windows.Forms.Label
    $instructions.Text='Copy the key and paste it into the PEM field under HTTPS security on the Wemos.'
    $instructions.SetBounds(18,16,645,36)
    $pathLabel=New-Object Windows.Forms.Label
    $pathLabel.Text="File saved:`r`n$Path"
    $pathLabel.SetBounds(18,56,645,45)
    $keyText=New-Object Windows.Forms.TextBox
    $keyText.Multiline=$true;$keyText.ReadOnly=$true;$keyText.WordWrap=$false
    $keyText.ScrollBars='Both';$keyText.Font=New-Object Drawing.Font('Consolas',10)
    $keyText.Text=[IO.File]::ReadAllText($Path)
    $keyText.SetBounds(18,110,645,215)
    $notice=New-Object Windows.Forms.Label
    $notice.SetBounds(18,335,645,22)
    $copy=New-Object Windows.Forms.Button
    $copy.Text='Copy key';$copy.SetBounds(285,370,125,28)
    $copy.Add_Click({[Windows.Forms.Clipboard]::SetText($keyText.Text);$notice.Text='Key copied. You can now paste it into the Wemos.'}.GetNewClosure())
    $open=New-Object Windows.Forms.Button
    $open.Text='Open .txt';$open.SetBounds(420,370,110,28)
    $open.Add_Click({try {Start-Process -FilePath notepad.exe -ArgumentList ('"'+$Path+'"')}catch {$notice.Text='Open the file saved on the Desktop.'}}.GetNewClosure())
    $close=New-Object Windows.Forms.Button
    $close.Text='Close';$close.SetBounds(540,370,123,28)
    $close.Add_Click({$resultForm.Close()}.GetNewClosure())
    $resultForm.Controls.AddRange(@($instructions,$pathLabel,$keyText,$notice,$copy,$open,$close))
    $resultForm.AcceptButton=$copy;$resultForm.CancelButton=$close
    return $resultForm
}

if ($LibraryMode) { return }
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$form = New-Object Windows.Forms.Form
$form.Text = 'Nightscout public key'
$form.ClientSize = New-Object Drawing.Size(540,230)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$label = New-Object Windows.Forms.Label
$label.Text = 'Paste the full URL of your Nightscout site:'
$label.SetBounds(20,18,500,24)
$urlBox = New-Object Windows.Forms.TextBox
$urlBox.Text = 'https://'
$urlBox.SetBounds(20,48,500,28)
$help = New-Object Windows.Forms.Label
$help.Text = "Example: https://your-nightscout.example/`r`nDo not include a token or password. A .txt file will be saved on`r`nyour Desktop. Copy all its contents into the Wemos PEM field."
$help.SetBounds(20,86,500,60)
$status = New-Object Windows.Forms.Label
$status.SetBounds(20,156,500,25)
$button = New-Object Windows.Forms.Button
$button.Text = 'Get key'
$button.SetBounds(300,192,125,30)
$cancel = New-Object Windows.Forms.Button
$cancel.Text = 'Close'
$cancel.SetBounds(435,192,85,30)
$form.Controls.AddRange(@($label,$urlBox,$help,$status,$button,$cancel))
$form.AcceptButton = $button; $form.CancelButton = $cancel
$script:worker = $null; $script:job = $null
$timer = New-Object Windows.Forms.Timer
$timer.Interval = 150
$button.Add_Click({
    if ($script:job) { return }
    $button.Enabled = $false; $urlBox.Enabled = $false
    $status.Text = 'Verifying site and getting its key…'
    $script:worker = [PowerShell]::Create()
    [void]$script:worker.AddScript($script:KeyExportCore.ToString()).AddParameter('Url', $urlBox.Text)
    $script:job = $script:worker.BeginInvoke()
    $timer.Start()
})
$timer.Add_Tick({
    if (!$script:job -or !$script:job.IsCompleted) { return }
    $timer.Stop()
    try {
        $result = $script:worker.EndInvoke($script:job)
        if ($script:worker.HadErrors -or !$result.Count) { throw 'Could not verify the site. Check the URL, Internet and Windows clock.' }
        $directory = [Environment]::GetFolderPath('DesktopDirectory')
        if (!$directory -or ![IO.Directory]::Exists($directory)) { $directory = [Environment]::GetFolderPath('UserProfile') }
        $path = Save-PublicKey $result[0].Pem $result[0].Hostname $directory
        $done=New-KeyResultWindow $path
        $form.Hide()
        try { [void]$done.ShowDialog() } finally { $done.Dispose() }
        $form.Close()
    } catch {
        $status.Text = 'No new key was saved.'
        [void][Windows.Forms.MessageBox]::Show('Could not obtain a verified key. Use an HTTPS URL without a token or password, and check Internet and the Windows clock.', 'Could not get the key', 'OK', 'Warning')
        $button.Enabled = $true; $urlBox.Enabled = $true
    } finally {
        $script:worker.Dispose(); $script:worker = $null; $script:job = $null
    }
})
$cancel.Add_Click({ $form.Close() })
$form.Add_Shown({ $urlBox.Focus(); $urlBox.SelectAll() })
try { [void]$form.ShowDialog() }
finally {
    $timer.Stop(); $timer.Dispose()
    if ($script:worker) { $script:worker.Stop(); $script:worker.Dispose() }
    $form.Dispose()
}
