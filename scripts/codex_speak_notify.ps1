$originalTitle = ''
try { $originalTitle = $Host.UI.RawUI.WindowTitle } catch { }
$titleChanged = $false
$debugWindowEnumeration = $false

$exe = Join-Path $PSScriptRoot 'CodexParakeet.exe'
$log = Join-Path $PSScriptRoot 'codex_speak_notify.log'
function Log([string]$s) {
    try {
        $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'
        #Add-Content -LiteralPath $log -Value "[$timestamp] $s" -Encoding UTF8 -ErrorAction SilentlyContinue
    }
    catch {
        # Logging must never prevent the notification handler from running.
    }
}
Log "START pid=$PID args=$($args.Count)"
if ($args.Count -gt 0) {
    Log 'JSON_BEGIN'
    Log ([string]$args[0])
    Log 'JSON_END'
}
try {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WP {
 [StructLayout(LayoutKind.Sequential)] public struct R { public int L; public int T; public int Rt; public int B; }
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern int GetSystemMetrics(int nIndex);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 public delegate bool E(IntPtr h,IntPtr x);
 [DllImport("user32.dll")] public static extern bool EnumWindows(E e,IntPtr x);
 public static IntPtr Find(uint pid) { IntPtr result=IntPtr.Zero; EnumWindows(delegate(IntPtr h,IntPtr x) { uint p; R r; GetWindowThreadProcessId(h,out p); if(p==pid && IsWindowVisible(h) && GetWindowRect(h,out r) && r.Rt-r.L>16 && r.B-r.T>16) { result=h; return false; } return true; },IntPtr.Zero); return result; }
 public static IntPtr FindByTitle(string title) { IntPtr result=IntPtr.Zero; EnumWindows(delegate(IntPtr h,IntPtr x) { if(!IsWindowVisible(h)) return true; var s=new System.Text.StringBuilder(256); GetWindowText(h,s,s.Capacity); string actual=s.ToString().TrimEnd(); R r; if(String.Equals(actual,title,StringComparison.Ordinal) && GetWindowRect(h,out r) && r.Rt-r.L>16 && r.B-r.T>16) { result=h; return false; } return true; },IntPtr.Zero); return result; }
 public static string[] GetVisibleWindowTitles() { var result=new System.Collections.Generic.List<string>(); EnumWindows(delegate(IntPtr h,IntPtr x) { if(!IsWindowVisible(h)) return true; var s=new System.Text.StringBuilder(256); GetWindowText(h,s,s.Capacity); string actual=s.ToString().TrimEnd(); R r; if(GetWindowRect(h,out r) && r.Rt-r.L>16 && r.B-r.T>16) result.Add(String.Format("h={0} title={1} rect={2},{3},{4},{5}", h, actual, r.L, r.T, r.Rt, r.B)); return true; },IntPtr.Zero); return result.ToArray(); }
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
    $windowTitle = $tid
    try { $Host.UI.RawUI.WindowTitle = $windowTitle; $titleChanged = $true } catch { Log "TITLE_SET_FAILED $($_.Exception.Message)" }
    # mintty may apply the title asynchronously. Do not rely on logging latency.
    Start-Sleep -Milliseconds 100
    Log "WINDOW_TITLE original=$originalTitle temporary=$windowTitle"
    $msg = [string]$p.last_assistant_message
    if ([string]::IsNullOrWhiteSpace($msg)) { $msg = [string]$p.'last-assistant-message' }
    Log "PAYLOAD thread=$tid cwd=$([string]$p.cwd) length=$($msg.Length) exe=$(Test-Path $exe)"
    $trimmedMessage = $msg.Trim()
    $ignoreMessage = $trimmedMessage -cmatch '^\{\s*"title"\s*:'
    if ($trimmedMessage.StartsWith('{') -and $trimmedMessage.EndsWith('}')) {
        try {
            $metadata = $trimmedMessage | ConvertFrom-Json -ErrorAction Stop
            $properties = @($metadata | Get-Member -MemberType NoteProperty | Select-Object -ExpandProperty Name)
            if ($properties -contains 'title' -and $properties -contains 'description') {
                $ignoreMessage = $true
            }
            if (-not $ignoreMessage -and $properties -contains 'summary' -and $metadata.summary -is [string]) {
                $msg = $metadata.summary
                Log 'EXTRACTED summary'
            }
        }
        catch { }
    }
    if ($ignoreMessage) {
        Log 'IGNORED internal title metadata'
        if ($titleChanged) {
            try { $Host.UI.RawUI.WindowTitle = $originalTitle; $titleChanged = $false; Log "WINDOW_TITLE_RESTORED title=$originalTitle" } catch { Log "TITLE_RESTORE_FAILED $($_.Exception.Message)" }
        }
        exit 0
    }
    Log 'MESSAGE_BEGIN'
    Log $msg
    Log 'MESSAGE_END'
    $pos = ''
    if ($debugWindowEnumeration) {
        foreach ($window in [WP]::GetVisibleWindowTitles()) { Log "WINDOW_ENUM $window" }
    }
    $h = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 10 -and $h -eq [IntPtr]::Zero; $attempt++) {
        $h = [WP]::FindByTitle($windowTitle)
        if ($h -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 50 }
    }
    if ($h -ne [IntPtr]::Zero) {
        $r = New-Object WP+R
        if ([WP]::GetWindowRect($h,[ref]$r)) {
            $windowCenterX = [int](($r.L + $r.Rt) / 2)
            $virtualLeft = [WP]::GetSystemMetrics(76) # SM_XVIRTUALSCREEN
            $screenWidth = [WP]::GetSystemMetrics(78) # SM_CXVIRTUALSCREEN
            $centerX = $windowCenterX - $virtualLeft
            $pos = "$centerX,$screenWidth"
            Log "WINDOW title=$windowTitle windowCenterX=$windowCenterX virtualLeft=$virtualLeft pos=$pos"
        }
    }
    if ([string]::IsNullOrWhiteSpace($pos)) {
        Log "METHOD_A_FAILED title=$windowTitle; trying METHOD_B_PARENT_PID"
        try { $Host.UI.RawUI.WindowTitle = $originalTitle; $titleChanged = $false } catch { Log "TITLE_RESTORE_BEFORE_FALLBACK_FAILED $($_.Exception.Message)" }
        $me = Get-CimInstance Win32_Process -Filter "ProcessId=$PID"
        $id = [uint32]$me.ParentProcessId
        for ($i = 0; $i -lt 8; $i++) {
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
                        $virtualLeft = [WP]::GetSystemMetrics(76)
                        $screenWidth = [WP]::GetSystemMetrics(78)
                        $centerX = $windowCenterX - $virtualLeft
                        $pos = "$centerX,$screenWidth"
                        Log "WINDOW method=METHOD_B_PARENT_PID pid=$($q.ProcessId) windowCenterX=$windowCenterX virtualLeft=$virtualLeft pos=$pos"
                        break
                    }
                }
            }
            $id = [uint32]$q.ParentProcessId
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($pos) -and $null -ne $originalTitle) {
        try {
            $Host.UI.RawUI.WindowTitle = $originalTitle
            $titleChanged = $false
            Log "WINDOW_TITLE_RESTORED title=$originalTitle"
        }
        catch { Log "TITLE_RESTORE_FAILED $($_.Exception.Message)" }
    }
    if ([string]::IsNullOrWhiteSpace($pos)) { Log 'POSITION_NOT_FOUND' }
    if (-not [string]::IsNullOrWhiteSpace($msg) -and (Test-Path $exe)) {
        $messageBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($msg))
        Log "EXE_ARGS screenPosition=$pos threadId=$tid"
        Log "SPEAK pos=$pos encodedLength=$($messageBase64.Length)"
        & $exe "--message-base64=$messageBase64" $tid $pos
    }
}
catch {
    Log "ERROR $($_.Exception.Message)"
    if ($titleChanged) {
        try { $Host.UI.RawUI.WindowTitle = $originalTitle; Log "WINDOW_TITLE_RESTORED_AFTER_ERROR title=$originalTitle" }
        catch { Log "TITLE_RESTORE_AFTER_ERROR_FAILED $($_.Exception.Message)" }
    }
}
exit 0
