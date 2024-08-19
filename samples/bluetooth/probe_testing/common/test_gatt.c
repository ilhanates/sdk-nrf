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

#define NOTIFICATION_DATA_PREFIX     "Counter:"
#define NOTIFICATION_DATA_PREFIX_LEN (sizeof(NOTIFICATION_DATA_PREFIX) - 1)

#define CHARACTERISTIC_DATA_MAX_LEN 260
#define NOTIFICATION_DATA_LEN	    MAX(200, (CONFIG_BT_L2CAP_TX_MTU - 4))
BUILD_ASSERT(NOTIFICATION_DATA_LEN <= CHARACTERISTIC_DATA_MAX_LEN);

#define CUSTOM_SERVICE_UUID_VAL                                                                \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define CUSTOM_CHARACTERISTIC_UUID_VAL                                                         \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

#define CUSTOM_SERVICE_UUID	       BT_UUID_DECLARE_128(CUSTOM_SERVICE_UUID_VAL)
#define CUSTOM_CHARACTERISTIC_UUID BT_UUID_DECLARE_128(CUSTOM_CHARACTERISTIC_UUID_VAL)

static struct bt_uuid_128 vnd_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdea0));

static struct bt_uuid_128 vnd_enc_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdea1));


uint8_t central_subscription;
static uint16_t value_handle = 0;
static uint16_t ccc_handle = 0;
static struct bt_gatt_attr *vnd_attr;
static uint32_t notification_size = NOTIFICATION_DATA_LEN;
static uint8_t vnd_value[CHARACTERISTIC_DATA_MAX_LEN];

static struct bt_uuid_128 uuid = BT_UUID_INIT_128(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

static const struct bt_uuid_16 ccc_uuid = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);

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

static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	central_subscription = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
	LOG_INF("CCC changed: %u", value);
}

/* Vendor Primary Service Declaration */
BT_GATT_SERVICE_DEFINE(
	vnd_svc, BT_GATT_PRIMARY_SERVICE(&vnd_uuid),
	BT_GATT_CHARACTERISTIC(&vnd_enc_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL, NULL,
			       NULL),
	BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);


static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	const char *data_ptr = (const char *)data + NOTIFICATION_DATA_PREFIX_LEN;
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

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;
	char uuid_str[BT_UUID_STR_LEN];
	struct conn_info *conn_info_ref;

	if (!attr) {
		/* We might be called from the ATT disconnection callback if we
		 * have an ongoing procedure. That is ok.
		 */
		// __ASSERT_NO_MSG(!is_connected(conn));
		return BT_GATT_ITER_STOP;
	}
	__ASSERT(attr, "Did not find requested UUID");

	bt_uuid_to_str(params->uuid, uuid_str, sizeof(uuid_str));
	LOG_DBG("UUID found : %s", uuid_str);

	LOG_INF("[ATTRIBUTE] handle %u", attr->handle);

	conn_info_ref = get_connected_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref);

	atomic_clear_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_PAUSED);

	if (conn_info_ref->discover_params.type == BT_GATT_DISCOVER_PRIMARY) {
		LOG_DBG("Primary Service Found");
		memcpy(&conn_info_ref->uuid,
		       CUSTOM_CHARACTERISTIC_UUID,
		       sizeof(conn_info_ref->uuid));
		conn_info_ref->discover_params.uuid = &conn_info_ref->uuid.uuid;
		conn_info_ref->discover_params.start_handle = attr->handle + 1;
		conn_info_ref->discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &conn_info_ref->discover_params);
		if (err == -ENOMEM || err == -ENOTCONN) {
			goto retry;
		}

		__ASSERT(!err, "Discover failed (err %d)", err);

	} else if (conn_info_ref->discover_params.type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		LOG_DBG("Service Characteristic Found");

		conn_info_ref->discover_params.uuid = &ccc_uuid.uuid;
		conn_info_ref->discover_params.start_handle = attr->handle + 2;
		conn_info_ref->discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &conn_info_ref->discover_params);
		if (err == -ENOMEM || err == -ENOTCONN) {
			goto retry;
		}

		__ASSERT(!err, "Discover failed (err %d)", err);

	} else {
		ccc_handle = attr->handle;
		atomic_set_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_COMPLETED);
	}

	return BT_GATT_ITER_STOP;

retry:
	/* if we're out of buffers or metadata contexts, continue discovery
	 * later.
	 */
	LOG_INF("out of memory/not connected, continuing sub later");
	atomic_set_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_PAUSED);

	return BT_GATT_ITER_STOP;
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

void test_gatt_init(void)
{
	bt_gatt_cb_register(&gatt_callbacks);

	char str[BT_UUID_STR_LEN];
	vnd_attr = bt_gatt_find_by_uuid(vnd_svc.attrs, vnd_svc.attr_count, &vnd_enc_uuid.uuid);
	bt_uuid_to_str(&vnd_enc_uuid.uuid, str, sizeof(str));
	LOG_DBG("Indicate VND attr %p (UUID %s)", vnd_attr, str);
}

void test_gatt_discovery(struct conn_info *conn_info_ref)
{
	memcpy(&conn_info_ref->uuid, CUSTOM_SERVICE_UUID,
		   sizeof(conn_info_ref->uuid));
	memset(&conn_info_ref->discover_params, 0,
		   sizeof(conn_info_ref->discover_params));

	conn_info_ref->discover_params.uuid = &conn_info_ref->uuid.uuid;
	conn_info_ref->discover_params.func = discover_cb;
	conn_info_ref->discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	conn_info_ref->discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	conn_info_ref->discover_params.type = BT_GATT_DISCOVER_PRIMARY;
	LOG_INF("Start discovery...");
	int err = bt_gatt_discover(conn_info_ref->conn_ref, &conn_info_ref->discover_params);
	if (err == -ENOMEM) {
		atomic_clear_bit(conn_info_ref->flags, CONN_INFO_DISCOVERING);
	}

	LOG_INF("Waiting for service discovery...");
	while (!atomic_test_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_COMPLETED)) {
		k_sleep(K_MSEC(10));
	}
	LOG_INF("Service discovery completed!");
}

void test_gatt_subscribe(struct conn_info *conn_info_ref)
{
	int err = 0;
	subscribe_params.notify = notify_cb;
	subscribe_params.value = BT_GATT_CCC_NOTIFY;
	subscribe_params.ccc_handle = ccc_handle;
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

	memset(vnd_value, 0x00, sizeof(vnd_value));
	snprintk(vnd_value, notification_size, "%s%u", NOTIFICATION_DATA_PREFIX,
		 conn_info_ref->tx_notify_counter);

	LOG_INF("Notify...");
	err = bt_gatt_notify(conn_info_ref->conn_ref, vnd_attr, vnd_value, notification_size);
	if (err) {
		LOG_ERR("Couldn't send GATT notification");
		return;
	}

	LOG_DBG("central notified: %d", conn_info_ref->tx_notify_counter);

	conn_info_ref->tx_notify_counter++;
}

