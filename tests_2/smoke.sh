#!/bin/bash
# Simple smoke test
set -e  # exit on first error

echo "=== Kernel version and basic info ==="
uname -a
echo

echo "=== NUMA topology (if available) ==="
if command -v numactl >/dev/null 2>&1; then
    numactl --hardware || true
else
    echo "numactl not installed, skipping NUMA info"
fi
echo

echo "=== dmesg sanity check (boot + now) ==="
# Look for any obvious kernel errors
if dmesg | grep -i -E "BUG:|Oops:|Call Trace:|WARNING:" >/tmp/smoketest_dmesg_warn 2>&1; then
    echo "Potential kernel warnings found:"
    cat /tmp/smoketest_dmesg_warn
else
    echo "No BUG/Oops/Call Trace/WARNING found in dmesg"
fi
echo

echo "=== CPU sanity (short stress) ==="
if ! command -v stress-ng >/dev/null 2>&1; then
    echo "Installing stress-ng..."
    sudo apt-get update >/dev/null 2>&1 || true
    sudo apt-get install -y stress-ng >/dev/null 2>&1 || true
fi

if command -v stress-ng >/dev/null 2>&1; then
    echo "Running stress-ng --cpu 4 --timeout 15s"
    stress-ng --cpu 4 --timeout 15s
    echo "CPU stress completed"
else
    echo "stress-ng not available, skipping CPU stress"
fi
echo

echo "=== Memory and page-fault path sanity ==="
if command -v stress-ng >/dev/null 2>&1; then
    echo "Running stress-ng --vm 2 --vm-bytes 50% --timeout 15s"
    stress-ng --vm 2 --vm-bytes 50% --timeout 15s
    echo "VM stress completed"
else
    echo "stress-ng not available, skipping VM stress"
fi
echo

echo "=== Disk I/O sanity ==="
TMPFILE=/tmp/kernel_smoketest.bin
dd if=/dev/zero of="$TMPFILE" bs=1M count=50 oflag=direct status=none
sync
dd if="$TMPFILE" of=/dev/null bs=1M status=none
rm -f "$TMPFILE"
echo "Disk I/O (write/read) OK"
echo

echo "=== Process creation and scheduler sanity ==="
echo "Spawning multiple short-lived processes..."
for i in $(seq 1 100); do
    ( true ) &
done
wait
echo "Fork/exit/scheduling OK"
echo

echo "=== Kernel hook check ==="
if [ -d /sys/kernel/debug/ptprefetch ]; then
    echo "Found /sys/kernel/debug/ptprefetch:"
    ls -R /sys/kernel/debug/ptprefetch
    if [ -f /sys/kernel/debug/ptprefetch/stats ]; then
        echo "Current prefetcher stats:"
        cat /sys/kernel/debug/ptprefetch/stats || true
    fi
else
    echo "No /sys/kernel/debug/ptprefetch directory; skipping hook check"
fi
echo

echo "=== Final dmesg tail (check for new errors) ==="
dmesg | tail -n 40
echo
echo "Smoke test completed."