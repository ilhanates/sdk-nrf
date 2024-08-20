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

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_gatt_server, LOG_LEVEL_INF);

#define CHARACTERISTIC_DATA_MAX_LEN 260
#define NOTIFICATION_DATA_LEN	    MAX(200, (CONFIG_BT_L2CAP_TX_MTU - 4))

static struct bt_gatt_attr *custom_attr;
static uint8_t test_value[CHARACTERISTIC_DATA_MAX_LEN];

static struct bt_uuid_16 custom_char_uuid = BT_UUID_INIT_16(0x1845);
#define CUSTOM_CHAR_UUID ((struct bt_uuid *)&custom_char_uuid)
static gatt_attr_data_t char_attr_data;
volatile bool gatt_subscribed = false;

void on_att_mtu_updated_cb(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	LOG_INF("Updated MTU: TX: %d RX: %d bytes", tx, rx);

	if (tx == CONFIG_BT_L2CAP_TX_MTU && rx == CONFIG_BT_L2CAP_TX_MTU) {
		char addr[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		struct conn_info *conn_info_ref = get_conn_info_ref(conn);
		atomic_set_bit(conn_info_ref->flags, CONN_INFO_MTU_EXCHANGED);
		LOG_INF("Updating MTU succeeded %s", addr);
	}
}

static void on_gatt_ccc_changed_cb(const struct bt_gatt_attr *attr, uint16_t value)
{
	LOG_INF("CCC changed: %u", value);

	gatt_subscribed = true;
}

static ssize_t on_gatt_attr_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				     const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	gatt_attr_data_t *data = attr->user_data;
	uint8_t *value = data->buf;
	data->len = len + offset;

	memcpy(value + offset, buf, len);

	/* TODO: Update statistics*/

	return len;
}

BT_GATT_SERVICE_DEFINE(_gatt_service, BT_GATT_PRIMARY_SERVICE(CUSTOM_SERVICE_UUID),
	BT_GATT_CHARACTERISTIC(CUSTOM_CHAR_UUID,
				BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
					BT_GATT_CHRC_NOTIFY |
					BT_GATT_CHRC_WRITE_WITHOUT_RESP,
				BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL,
				on_gatt_attr_write_cb, &char_attr_data),
	BT_GATT_CCC(on_gatt_ccc_changed_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = on_att_mtu_updated_cb
};

void test_gatt_server_init(void)
{
	bt_gatt_cb_register(&gatt_callbacks);

	custom_attr = bt_gatt_find_by_uuid(_gatt_service.attrs, _gatt_service.attr_count, CUSTOM_CHAR_UUID);
	LOG_ERR("custom_attr attr %u", custom_attr->handle);
}

void test_gatt_server_wait_subscribe(void)
{
	LOG_INF("Waiting GATT subscription...");
	while (!gatt_subscribed) {
		k_sleep(K_MSEC(10));
	}
}

static void test_gatt_server_notify(struct bt_conn *conn, void *data)
{
	int err;

	LOG_INF("Notify...");
	uint8_t *value = (uint8_t *)data;
	err = bt_gatt_notify(conn, custom_attr, value, NOTIFICATION_DATA_LEN);
	if (err) {
		LOG_ERR("Couldn't send GATT notification");
		return;
	}
}

void test_gatt_server_notify_all(void)
{
	static int counter = 0;
	memset(test_value, 0x00, NOTIFICATION_DATA_LEN);
	snprintk(test_value, NOTIFICATION_DATA_LEN, "NOTIFY:%u", ++counter);
	bt_conn_foreach(BT_CONN_TYPE_LE, test_gatt_server_notify, test_value);
}
