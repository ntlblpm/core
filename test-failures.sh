#!/bin/bash
# Quick script to show only test failures from make check

echo "LibreOffice Test Failures"
echo "========================"
echo

WORKDIR="${1:-workdir}"
LOG_DIR="$WORKDIR/CppunitTest"

if [ ! -d "$LOG_DIR" ]; then
    echo "Error: Test log directory not found: $LOG_DIR"
    echo "Usage: $0 [workdir_path]"
    exit 1
fi

# Find logs that don't contain "OK (" which indicates success
failed_count=0
for logfile in "$LOG_DIR"/*.test.log; do
    if [ -f "$logfile" ] && ! grep -q "^OK (" "$logfile"; then
        testname=$(basename "$logfile" .test.log)
        echo "FAILED: $testname"
        echo "  Log file: $logfile"
        # Show last 10 lines which usually contain the error
        echo "  Last 10 lines:"
        tail -10 "$logfile" | sed 's/^/    /'
        echo
        ((failed_count++))
    fi
done

if [ "$failed_count" -eq 0 ]; then
    echo "No test failures found!"
else
    echo "Total failures: $failed_count"
fi