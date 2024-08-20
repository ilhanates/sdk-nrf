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
LOG_MODULE_REGISTER(test_gatt, LOG_LEVEL_INF);

#define CHARACTERISTIC_DATA_MAX_LEN 260
#define NOTIFICATION_DATA_LEN	    MAX(200, (CONFIG_BT_L2CAP_TX_MTU - 4))


static gatt_service_discovery_t service_discovery;
static gatt_char_discovery_t char_discovery;
static gatt_descr_discovery_t descr_discovery;
static gatt_attr_discovery_t attr_discovery;
static struct bt_gatt_subscribe_params subscribe_params;
static struct bt_gatt_attr *custom_attr;
static uint32_t notification_size = NOTIFICATION_DATA_LEN;
static uint8_t test_value[CHARACTERISTIC_DATA_MAX_LEN];

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
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
}

static ssize_t on_attribute_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				     const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	gatt_attr_data_t *data = attr->user_data;
	uint8_t *value = data->buf;
	data->len = len + offset;

	memcpy(value + offset, buf, len);

	/* TODO: Update statistics*/

	return len;
}

#define CUSTOM_SERVICE_UUID BT_UUID_DECLARE_128(0xBB, 0x4A, 0xFF, 0x4F, 0xAD, 0x03, 0x41, 0x5D, 0xA9, 0x6C, 0x9D, 0x6C, 0xDD, 0xDA, 0x83, 0x04)
static struct bt_uuid_16 custom_char_uuid = BT_UUID_INIT_16(0x1845);
#define CUSTOM_CHAR_UUID ((struct bt_uuid *)&custom_char_uuid)
static gatt_attr_data_t char_attr_data;

BT_GATT_SERVICE_DEFINE(_gatt_service, BT_GATT_PRIMARY_SERVICE(CUSTOM_SERVICE_UUID),
	BT_GATT_CHARACTERISTIC(CUSTOM_CHAR_UUID,
				BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
					BT_GATT_CHRC_NOTIFY |
					BT_GATT_CHRC_WRITE_WITHOUT_RESP,
				BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL,
				on_attribute_write_cb, &char_attr_data),
	BT_GATT_CCC(on_gatt_ccc_changed_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));


static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	const char *data_ptr = (const char *)data;
	uint32_t received_counter;
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!data) {
		LOG_INF("[UNSUBSCRIBED] addr %s", addr);
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	conn_info_ref = get_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref);

	received_counter = strtoul(data_ptr, NULL, 0);

	LOG_DBG("[NOTIFICATION] addr %s data %s length %u cnt %u",
		addr, data, length, received_counter);

	LOG_HEXDUMP_DBG(data, length, "RX");

	// __ASSERT(conn_info_ref->notify_counter == received_counter,
	// 	 "addr %s expected counter : %u , received counter : %u",
	// 	 addr, conn_info_ref->notify_counter, received_counter);
	// conn_info_ref->notify_counter++;

	return BT_GATT_ITER_CONTINUE;
}

static void populate_uuid(void *d_uuid, const struct bt_uuid *s_uuid)
{
	switch (s_uuid->type) {
	case BT_UUID_TYPE_16:
		memcpy(d_uuid, &(BT_UUID_16(s_uuid)->val), 2);
		break;

	case BT_UUID_TYPE_32:
		memcpy(d_uuid, &(BT_UUID_32(s_uuid)->val), 4);
		break;

	case BT_UUID_TYPE_128:
		memcpy(d_uuid, BT_UUID_128(s_uuid)->val, 16);
		break;
	default:
	}
}

static uint8_t gatt_discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				struct bt_gatt_discover_params *params)
{
	struct bt_gatt_service_val *gatt_service;
	struct bt_gatt_chrc *gatt_chrc;

	if (attr == NULL) {
		return BT_GATT_ITER_STOP;
	}
	struct conn_info *conn_info_ref = get_conn_info_ref(conn);
	char uuid_str[BT_UUID_STR_LEN];
	int state = BT_GATT_ITER_CONTINUE;
	switch (params->type) {
	case BT_GATT_DISCOVER_PRIMARY:
	case BT_GATT_DISCOVER_SECONDARY:
		gatt_service = attr->user_data;
		service_discovery.conn_obj_addr = (uint32_t)conn;
		service_discovery.start_handle = attr->handle;
		service_discovery.end_handle = gatt_service->end_handle;
		service_discovery.perm = attr->perm;
		service_discovery.uuid_type = gatt_service->uuid->type;
		populate_uuid(&(service_discovery.uuid), gatt_service->uuid);

		bt_uuid_to_str(gatt_service->uuid, uuid_str, sizeof(uuid_str));
		LOG_INF("GATT_DISCOVER_PRIMARY UUID: %s HANDLE-> S:%u E:%u", uuid_str, service_discovery.start_handle, service_discovery.end_handle);
		atomic_set_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_SERV_COMPLETED);
		break;
	case BT_GATT_DISCOVER_CHARACTERISTIC:
		gatt_chrc = attr->user_data;
		char_discovery.conn_obj_addr = (uint32_t)conn;
		char_discovery.handle = attr->handle;
		char_discovery.value_handle = gatt_chrc->value_handle;
		char_discovery.properties = gatt_chrc->properties;
		char_discovery.perm = attr->perm;
		char_discovery.uuid_type = gatt_chrc->uuid->type;
		populate_uuid(&(char_discovery.uuid), gatt_chrc->uuid);
		bt_uuid_to_str(gatt_chrc->uuid, uuid_str, sizeof(uuid_str));
		LOG_INF("BT_GATT_DISCOVER_CHARACTERISTIC UUID: %s HANDLE-> %u Value Handle:%u", uuid_str, char_discovery.handle, char_discovery.value_handle);
		atomic_set_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_CHAR_COMPLETED);
		break;
	case BT_GATT_DISCOVER_DESCRIPTOR:
		descr_discovery.conn_obj_addr = (uint32_t)conn;
		descr_discovery.handle = attr->handle;
		descr_discovery.perm = attr->perm;
		descr_discovery.uuid_type = attr->uuid->type;
		populate_uuid(&(descr_discovery.uuid), attr->uuid);

		bt_uuid_to_str(attr->uuid, uuid_str, sizeof(uuid_str));
		LOG_INF("BT_GATT_DISCOVER_DESCRIPTOR UUID: %s HANDLE:%u", uuid_str, descr_discovery.handle);
		atomic_set_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_DESC_COMPLETED);
		//state = BT_GATT_ITER_STOP;
		break;
	case BT_GATT_DISCOVER_ATTRIBUTE:
		LOG_WRN("BT_GATT_DISCOVER_ATTRIBUTE");
		attr_discovery.conn_obj_addr = (uint32_t)conn;
		attr_discovery.handle = attr->handle;
		attr_discovery.perm = attr->perm;
		attr_discovery.uuid_type = attr->uuid->type;
		populate_uuid(&(attr_discovery.uuid), attr->uuid);
		break;
	default:
		LOG_ERR("Undefined discovery:%d", params->type);
		break;
	}

	return state;
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

void test_gatt_init(void)
{
	bt_gatt_cb_register(&gatt_callbacks);

}

int test_gatt_discovery(struct conn_info *conn_info_ref)
{
	int err = 0;
	memcpy(&conn_info_ref->uuid, CUSTOM_SERVICE_UUID,
		   sizeof(conn_info_ref->uuid));
	memset(&conn_info_ref->discover_params, 0,
		   sizeof(conn_info_ref->discover_params));

	LOG_INF("Start service discovery...");
	conn_info_ref->discover_params.uuid = &conn_info_ref->uuid.uuid;
	conn_info_ref->discover_params.func = gatt_discover_cb;
	conn_info_ref->discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	conn_info_ref->discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	conn_info_ref->discover_params.type = BT_GATT_DISCOVER_PRIMARY;
	err = bt_gatt_discover(conn_info_ref->conn_ref, &conn_info_ref->discover_params);
	if (err) {
		return err;
	}
	LOG_INF("Waiting for service discovery...");
	while (!atomic_test_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_SERV_COMPLETED)) {
		k_sleep(K_MSEC(10));
	}

	LOG_INF("Start char discovery...");
	conn_info_ref->discover_params.uuid = NULL;
	conn_info_ref->discover_params.func = gatt_discover_cb;
	conn_info_ref->discover_params.start_handle = service_discovery.start_handle;
	conn_info_ref->discover_params.end_handle = service_discovery.end_handle;
	conn_info_ref->discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
	err = bt_gatt_discover(conn_info_ref->conn_ref, &conn_info_ref->discover_params);
	if (err) {
		return err;
	}
	LOG_INF("Waiting for char discovery...");
	while (!atomic_test_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_CHAR_COMPLETED)) {
		k_sleep(K_MSEC(10));
	}

	LOG_INF("Start descr discovery...");
	conn_info_ref->discover_params.uuid = NULL;
	conn_info_ref->discover_params.func = gatt_discover_cb;
	conn_info_ref->discover_params.start_handle = char_discovery.value_handle + 1;
	conn_info_ref->discover_params.end_handle = service_discovery.end_handle;
	conn_info_ref->discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
	err = bt_gatt_discover(conn_info_ref->conn_ref, &conn_info_ref->discover_params);
	if (err) {
		return err;
	}
	LOG_INF("Waiting for char discovery...");
	while (!atomic_test_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_DESC_COMPLETED)) {
		k_sleep(K_MSEC(10));
	}

	LOG_INF("Service discovery completed!");

	return 0;
}

void test_gatt_subscribe(struct conn_info *conn_info_ref)
{
	int err = 0;
	subscribe_params.notify = notify_cb;
	subscribe_params.value = BT_GATT_CCC_NOTIFY;
	subscribe_params.ccc_handle = descr_discovery.handle;
	err = bt_gatt_subscribe(conn_info_ref->conn_ref, &subscribe_params);
	if (err && err != -EALREADY) {
		LOG_ERR("Subscribe failed (err %d)", err);
		return;
	}
	atomic_set_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED);
	LOG_INF("[SUBSCRIBED]");
}

void test_gatt_notify(struct conn_info *conn_info_ref)
{
	int err;
	if (!atomic_test_bit(conn_info_ref->flags, CONN_INFO_MTU_EXCHANGED)) {
		LOG_DBG("can't notify: MTU not yet exchanged");
		/* sleep a bit to allow the exchange to take place */
		k_msleep(100);
		return;
	}

	memset(test_value, 0x00, sizeof(test_value));
	snprintk(test_value, notification_size, "NOT:%u", conn_info_ref->tx_notify_counter);

	LOG_INF("Notify...");
	err = bt_gatt_notify(conn_info_ref->conn_ref, custom_attr, test_value, notification_size);
	if (err) {
		LOG_ERR("Couldn't send GATT notification");
		return;
	}

	LOG_DBG("central notified: %d", conn_info_ref->tx_notify_counter);

	conn_info_ref->tx_notify_counter++;
}

