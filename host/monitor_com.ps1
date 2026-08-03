# Raw byte-level monitor for the STM32 hub's ST-LINK Virtual COM Port.
# Prints every received byte as hex + ASCII (non-printable shown as '.'),
# so framing/noise issues are visible even when ReadLine() would hide them.
# Usage: powershell -ExecutionPolicy Bypass -File host\monitor_com.ps1

$dev = Get-CimInstance -Class Win32_PnPEntity | Where-Object { $_.Name -match 'STLink Virtual COM Port \((COM\d+)\)' }
if (-not $dev) {
    Write-Output "No ST-LINK Virtual COM Port found. Check 'usbipd list' -- if busid shows Attached, run 'usbipd detach --busid <id>' first."
    exit 1
}
$portName = [regex]::Match($dev.Name, 'COM\d+').Value
Write-Output "Using $portName -- press NRST on the STM32 now"

# The ST-LINK VCP drops and re-enumerates unpredictably while bound to
# usbipd (even when not currently attached to WSL), independent of any
# actual target reset. Auto-reconnect on "port closed"/IO errors instead
# of dying on the first drop.
$port = $null
$buf = New-Object byte[] 256
$sw = [Diagnostics.Stopwatch]::StartNew()
Write-Output "Listening for 20 seconds..."
while ($sw.Elapsed.TotalSeconds -lt 20) {
    if (-not $port -or -not $port.IsOpen) {
        try {
            if ($port) { $port.Dispose() }
            $port = New-Object System.IO.Ports.SerialPort $portName,115200,None,8,One
            $port.ReadTimeout = 500
            $port.Open()
        } catch {
            "{0:HH:mm:ss.fff} reconnect failed: {1}" -f (Get-Date), $_.Exception.Message
            Start-Sleep -Milliseconds 300
            continue
        }
    }
    try {
        $n = $port.Read($buf, 0, $buf.Length)
        if ($n -gt 0) {
            $hex = ($buf[0..($n-1)] | ForEach-Object { $_.ToString("X2") }) -join ' '
            $ascii = -join ($buf[0..($n-1)] | ForEach-Object { if ($_ -ge 32 -and $_ -le 126) { [char]$_ } else { '.' } })
            "{0:HH:mm:ss.fff} [{1} bytes] HEX: {2}" -f (Get-Date), $n, $hex
            "                      ASCII: {0}" -f $ascii
        }
    } catch [System.TimeoutException] {
    } catch {
        "{0:HH:mm:ss.fff} dropped: {1}" -f (Get-Date), $_.Exception.Message
        try { $port.Close() } catch {}
    }
}
if ($port -and $port.IsOpen) { $port.Close() }
Write-Output "Done."
