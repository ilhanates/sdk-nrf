NEF_BASE=${ZEPHYR_BASE}/../nrf
PROBE_TESTING_BASE=${NRF_BASE}/samples/bluetooth/probe_testing
BOARD=nrf52_bsim

TESTER_BUILD_DIR=${PROBE_TESTING_BASE}/tester/build
DUT_BUILD_DIR=${PROBE_TESTING_BASE}/dut/build

if [ "$1" = "-c" ]; then
    echo "CLEAN BUILD"
    rm -rf ${TESTER_BUILD_DIR}
    rm -rf ${DUT_BUILD_DIR}
fi

west build -b ${BOARD} -p auto -d ${TESTER_BUILD_DIR} ${PROBE_TESTING_BASE}/tester

west build -b ${BOARD} -p auto -d ${DUT_BUILD_DIR} ${PROBE_TESTING_BASE}/dut

