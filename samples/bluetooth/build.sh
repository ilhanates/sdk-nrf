NRF_BASE=${ZEPHYR_BASE}/../nrf
PROBE_TESTING_BASE=${NRF_BASE}/samples/bluetooth
BOARD=nrf52_bsim

TESTER_BUILD_DIR=${PROBE_TESTING_BASE}/probe_tester/build1
DUT_BUILD_DIR=${PROBE_TESTING_BASE}/peripheral_hids_mouse/build2

if [ "$1" = "-c" ]; then
    echo "CLEAN BUILD"
    rm -rf ${TESTER_BUILD_DIR}
    rm -rf ${DUT_BUILD_DIR}
fi

west build -b ${BOARD} -p auto -d ${TESTER_BUILD_DIR} ${PROBE_TESTING_BASE}/probe_tester

west build -b ${BOARD} -p auto -d ${DUT_BUILD_DIR} ${PROBE_TESTING_BASE}/peripheral_hids_mouse

