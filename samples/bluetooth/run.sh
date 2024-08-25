NRF_BASE=${ZEPHYR_BASE}/../nrf
PROBE_TESTING_BASE=${NRF_BASE}/samples/bluetooth
TESTER_BUILD_DIR=${PROBE_TESTING_BASE}/probe_tester/build1
DUT_BUILD_DIR=${PROBE_TESTING_BASE}/peripheral_hids_mouse/build2
SIM_ID="PROBE_TESTING"

cd ${BSIM_OUT_PATH}/bin/

device_count=8
${TESTER_BUILD_DIR}/probe_tester/zephyr/zephyr.exe -s=${SIM_ID} -d=0 &
for i in $(seq 1 $device_count);
do
    ${DUT_BUILD_DIR}/peripheral_hids_mouse/zephyr/zephyr.exe -s=${SIM_ID} -d=$i &
done

((device_count++))
./bs_2G4_phy_v1 -s=${SIM_ID} -D=$device_count -sim_length=50e6 &
