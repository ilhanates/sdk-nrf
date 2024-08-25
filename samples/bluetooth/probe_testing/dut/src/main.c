/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

conn_info_t *conn_info = NULL;

void test_init(void)
{
	test_connection_init();
	test_gatt_client_init();
}

void test_loop(void)
{
	test_gatt_client_subscribe(conn_info, BT_GATT_CCC_NOTIFY);
	test_gatt_client_wait_for(conn_info, CONN_INFO_NOTIFIED);
	test_gatt_client_unsubscribe(conn_info);

	test_gatt_client_subscribe(conn_info, BT_GATT_CCC_INDICATE);
	test_gatt_client_wait_for(conn_info, CONN_INFO_INDICATED);
	test_gatt_client_unsubscribe(conn_info);

	test_gatt_client_write_without_resp(conn_info);
}

void test_main(void)
{
	int ret = 0;

	test_init();

	conn_info = test_peripheral_connect();
	if(!conn_info) return;

	ret = test_gatt_client_discover(conn_info);
	if (ret) return;

	for (int i = 0; i < TEST_LOOP; i++) {
		test_loop();
	}

	test_connection_wait_for(conn_info->conn, CONN_INFO_DISCONNECTED);

	END_TEST("DUT")
	BSIM_BUSY_WAIT
}

int main(void)
{
	test_main();

	return 0;
}
