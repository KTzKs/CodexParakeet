$exe = Join-Path $PSScriptRoot 'CodexParakeet.exe'
$log = Join-Path $PSScriptRoot 'codex_speak_notify.log'
function Log([string]$s) {
    try {
        $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'
        # Add-Content -LiteralPath $log -Value "[$timestamp] $s" -Encoding UTF8 -ErrorAction SilentlyContinue
    }
    catch {
        # Logging must never prevent the notification handler from running.
    }
}
Log "START pid=$PID args=$($args.Count)"
try {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WP {
 [StructLayout(LayoutKind.Sequential)] public struct R { public int L; public int T; public int Rt; public int B; }
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern int GetSystemMetrics(int nIndex);
 public delegate bool E(IntPtr h,IntPtr x);
 [DllImport("user32.dll")] public static extern bool EnumWindows(E e,IntPtr x);
 public static IntPtr Find(uint pid) { IntPtr result=IntPtr.Zero; EnumWindows(delegate(IntPtr h,IntPtr x) { uint p; R r; GetWindowThreadProcessId(h,out p); if(p==pid && IsWindowVisible(h) && GetWindowRect(h,out r) && r.Rt-r.L>16 && r.B-r.T>16) { result=h; return false; } return true; },IntPtr.Zero); return result; }
}
'@ -ErrorAction Stop
    if ($args.Count -eq 0) { Log 'NO_ARGS'; exit 0 }
    $p = $args[0] | ConvertFrom-Json
    $type = [string]$p.type
    $status = [string]$p.status
    $event = [string]$p.event
    $eventType = [string]$p.'event-type'
    Log "NOTIFICATION type=$type status=$status event=$event eventType=$eventType properties=$((($p | Get-Member -MemberType NoteProperty).Name -join ','))"
    $tid = [string]$p.'thread-id'
    $msg = [string]$p.last_assistant_message
    if ([string]::IsNullOrWhiteSpace($msg)) { $msg = [string]$p.'last-assistant-message' }
    Log "PAYLOAD thread=$tid cwd=$([string]$p.cwd) length=$($msg.Length) exe=$(Test-Path $exe)"
    $trimmedMessage = $msg.Trim()
    if ($trimmedMessage.StartsWith('{') -and $trimmedMessage.EndsWith('}')) {
        try {
            $metadata = $trimmedMessage | ConvertFrom-Json -ErrorAction Stop
            $properties = @($metadata | Get-Member -MemberType NoteProperty | Select-Object -ExpandProperty Name)
            if ($properties -contains 'title' -and $properties -contains 'description') {
                Log 'IGNORED internal title/description metadata'
                exit 0
            }
        }
        catch { }
    }
    Log 'MESSAGE_BEGIN'
    Log $msg
    Log 'MESSAGE_END'
    $pos = ''
    $me = Get-CimInstance Win32_Process -Filter "ProcessId=$PID"
    $id = [uint32]$me.ParentProcessId
    for ($i=0; $i -lt 8; $i++) {
        if ($id -eq 0) { break }
        $q = Get-CimInstance Win32_Process -Filter "ProcessId=$id"
        if ($null -eq $q) { break }
        Log "PARENT depth=$i pid=$($q.ProcessId) name=$($q.Name)"
        if ($q.Name -ne 'explorer.exe' -and $q.Name -ne 'dwm.exe') {
            $h = [WP]::Find([uint32]$q.ProcessId)
            if ($h -ne [IntPtr]::Zero) {
                $r = New-Object WP+R
                if ([WP]::GetWindowRect($h,[ref]$r)) {
                    $windowCenterX = [int](($r.L + $r.Rt) / 2)
                    $virtualLeft = [WP]::GetSystemMetrics(76) # SM_XVIRTUALSCREEN
                    $screenWidth = [WP]::GetSystemMetrics(78) # SM_CXVIRTUALSCREEN
                    $centerX = $windowCenterX - $virtualLeft
                    $pos = "$centerX,$screenWidth"
                    Log "WINDOW pid=$($q.ProcessId) windowCenterX=$windowCenterX virtualLeft=$virtualLeft pos=$pos"
                    break
                }
            }
        }
        $id = [uint32]$q.ParentProcessId
    }
    if ([string]::IsNullOrWhiteSpace($pos)) { Log 'POSITION_NOT_FOUND' }
    if (-not [string]::IsNullOrWhiteSpace($msg) -and (Test-Path $exe)) {
        $messageBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($msg))
        Log "EXE_ARGS screenPosition=$pos threadId=$tid"
        Log "SPEAK pos=$pos encodedLength=$($messageBase64.Length)"
        & $exe "--message-base64=$messageBase64" $tid $pos
    }
}
catch { Log "ERROR $($_.Exception.Message)" }
exit 0
