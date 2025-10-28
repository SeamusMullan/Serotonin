#!/bin/bash
# build/run-tests.sh
# Run kernel tests in QEMU

OS_TYPE="$(uname)"

echo "Running Serotonin Kernel Tests..."
echo ""

if [ "$OS_TYPE" = "Darwin" ]; then
    # macOS
    timeout 30 qemu-system-i386 \
        -cdrom serotonin.iso \
        -serial stdio \
        -display none \
        -no-reboot \
        2>&1 | tee test_output.log
else
    # Linux
    timeout 30 qemu-system-i386 \
        -cdrom serotonin.iso \
        -serial stdio \
        -nographic \
        -no-reboot \
        2>&1 | tee test_output.log
fi

echo ""
echo "==============================================="
echo "Test run complete. Output saved to test_output.log"
echo ""

# Parse results
if grep -q "All kernel tests completed successfully" test_output.log; then
    echo "✓ TESTS PASSED"
    exit 0
elif grep -q "KERNEL TEST SUMMARY" test_output.log; then
    echo "✗ SOME TESTS FAILED"
    echo ""
    echo "Summary from log:"
    grep -A 10 "KERNEL TEST SUMMARY" test_output.log
    exit 1
else
    echo "⚠ Could not determine test results"
    echo "Check test_output.log for details"
    exit 2
fi
