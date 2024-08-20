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
#include <zephyr/bluetooth/services/ias.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/check.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>
#include "test_common.h"

void test_init(void)
{
	test_connection_init();
	test_gatt_init();
}

void test_loop(void)
{
	// int ret = 0;
	while (true) {
		// ret = test_gatt_notify();
		// if (ret) break;
		k_msleep(10);
	}
}

void test_main(void)
{
	int ret = 0;

	test_init();

	struct conn_info *conn = test_peripheral_connect();
	if(!conn) return;

	ret = test_gatt_discovery(conn);
	if (ret) return;

	// test_gatt_subscribe(conn);
	// test_gatt_notify(conn);
	// test_loop();
	// test_disconnect();
}

int main(void)
{
	test_main();

	return 0;
}
