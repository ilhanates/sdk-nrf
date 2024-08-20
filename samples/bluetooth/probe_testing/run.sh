NEF_BASE=${ZEPHYR_BASE}/../nrf
PROBE_TESTING_BASE=${NRF_BASE}/samples/bluetooth/probe_testing
TESTER_BUILD_DIR=${PROBE_TESTING_BASE}/tester/build
DUT_BUILD_DIR=${PROBE_TESTING_BASE}/dut/build
SIM_ID="PROBE_TESTING"

cd ${BSIM_OUT_PATH}/bin/

device_count=1
${TESTER_BUILD_DIR}/tester/zephyr/zephyr.exe -s=${SIM_ID} -d=0 &
for i in $(seq 1 $device_count);
do
    ${DUT_BUILD_DIR}/dut/zephyr/zephyr.exe -s=${SIM_ID} -d=$i &
done

((device_count++))
./bs_2G4_phy_v1 -s=${SIM_ID} -D=$device_count -sim_length=10e6 &
