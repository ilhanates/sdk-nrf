/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

conn_info_t *conn = NULL;

void test_init(void)
{
	test_connection_init();
	test_gatt_client_init();
}

void test_loop(void)
{
	test_gatt_client_subscribe(conn, BT_GATT_CCC_NOTIFY);
	test_gatt_client_wait_for(conn, CONN_INFO_NOTIFIED);
	test_gatt_client_unsubscribe(conn);

	test_gatt_client_subscribe(conn, BT_GATT_CCC_INDICATE);
	test_gatt_client_wait_for(conn, CONN_INFO_INDICATED);
	test_gatt_client_unsubscribe(conn);

	test_gatt_client_write_without_resp(conn);

	END_TEST("DUT")
	BSIM_BUSY_WAIT
}

void test_main(void)
{
	int ret = 0;

	test_init();

	conn= test_peripheral_connect();
	if(!conn) return;

	ret = test_gatt_client_discover(conn);
	if (ret) return;

	test_loop();
	// test_disconnect();
}

int main(void)
{
	test_main();

	return 0;
}
