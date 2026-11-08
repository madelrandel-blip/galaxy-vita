#!/bin/sh
# Fails when any .exe / .dll in the given folder imports something Windows 7 does not have:
# a Windows 8+ function, an api-ms-win-core-* set that came after 7, or the Universal CRT.
# usage: check_win7.sh <folder>
dir="$1"
win8='GetSystemTimePreciseAsFileTime|WaitOnAddress|WakeByAddressSingle|WakeByAddressAll|CreateFile2|GetCurrentThreadStackLimits|SetThreadDescription|GetThreadDescription|GetOverlappedResultEx|PrefetchVirtualMemory|SetThreadInformation|GetThreadInformation|CopyFile2|DiscardVirtualMemory|OfferVirtualMemory|ReclaimVirtualMemory|GetDpiForWindow|GetDpiForMonitor|SetProcessDpiAwarenessContext|MapViewOfFile3|VirtualAlloc2|CreateFileMappingFromApp|SetProcessMitigationPolicy|GetProcessMitigationPolicy'
bad=0
for f in "$dir"/*.exe "$dir"/*.dll; do
  [ -f "$f" ] || continue
  out=$(llvm-objdump -p "$f" 2>/dev/null)
  dlls=$(printf '%s\n' "$out" | sed -n 's/^ *DLL Name: //p')
  crt=$(printf '%s\n' "$dlls" | grep -iE '^(api-ms-win-crt-|ucrtbase\.dll|vcruntime140)' | sort -u | tr '\n' ' ')
  sets=$(printf '%s\n' "$dlls" | grep -iE '^api-ms-win-core-' | sort -u | tr '\n' ' ')
  funcs=$(printf '%s\n' "$out" | grep -oE "\b($win8)\b" | sort -u | tr '\n' ' ')
  if [ -n "$crt$sets$funcs" ]; then
    echo "NOT Windows 7: $(basename "$f"): ${crt:+UCRT[$crt] }${sets:+apisets[$sets] }${funcs:+win8+[$funcs]}"
    bad=1
  else
    echo "ok (Windows 7): $(basename "$f")"
  fi
done
exit $bad
