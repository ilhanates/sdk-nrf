/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/check.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>
#include "test_common.h"


void test_init(void)
{
	test_connection_init();
	test_gatt_server_init();
}

void test_loop(void)
{
	test_gatt_server_wait_subscribe(BT_GATT_CCC_NOTIFY);
	test_gatt_server_notify_all();
	test_gatt_server_wait_subscribe(0);

	test_gatt_server_wait_subscribe(BT_GATT_CCC_INDICATE);
	test_gatt_server_indicate_all();

	while (true) {
		k_sleep(K_MSEC(1));
	}
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
