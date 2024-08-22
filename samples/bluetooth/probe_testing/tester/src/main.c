/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

void test_init(void)
{
	test_connection_init();
	test_gatt_server_init();
}

void test_loop(void)
{
	test_gatt_server_wait_for(BT_GATT_CCC_NOTIFY);
	test_gatt_server_notify_all();
	test_gatt_server_wait_for(0);

	test_gatt_server_wait_for(BT_GATT_CCC_INDICATE);
	test_gatt_server_indicate_all();
	test_gatt_server_wait_for(0);

	END_TEST("TESTER")
	BSIM_BUSY_WAIT
}

void test_main(void)
{
	test_init();

	test_central_connect();

	test_loop();
}

int main(void)
{
	test_main();
	return 0;
}
