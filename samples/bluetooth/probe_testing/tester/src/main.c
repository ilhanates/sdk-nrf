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
	test_gatt_init();
}

void test_loop(void)
{
	while (true) {
		test_gatt_notify_all();
		k_msleep(10);
	}
}

void test_main(void)
{
	test_init();
	test_central_connect();
	test_gatt_subscribe();
	test_loop();
}

int main(void)
{
	test_main();
	return 0;
}
