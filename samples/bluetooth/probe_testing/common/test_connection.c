
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
LOG_MODULE_REGISTER(test_connection, LOG_LEVEL_INF);


#define LINK_COUNT 5
#define DEFAULT_CONN_INTERVAL	   20
#define PERIPHERAL_DEVICE_NAME	   "Zephyr Peripheral"
#define PERIPHERAL_DEVICE_NAME_LEN (sizeof(PERIPHERAL_DEVICE_NAME) - 1)

ATOMIC_DEFINE(status_flags, DEVICE_NUM_FLAGS);
static atomic_t conn_count;
static struct bt_conn *conn_connecting;

static struct conn_info conn_infos[CONFIG_BT_MAX_CONN] = {0};
static struct bt_gatt_exchange_params mtu_exchange_params;

static void wait_status(struct bt_conn *conn, int flag)
{
	struct conn_info *conn_info_ref = get_connected_conn_info_ref(conn);
	while(!atomic_test_bit(conn_info_ref->flags, flag)) {
		k_sleep(K_MSEC(10));
	}
}

static void set_security(struct bt_conn *conn, void *data)
{
	int level = *(int *)data;
	int err = bt_conn_set_security(conn, level);
	if (!err) {
		LOG_INF("Security level is set to : %u", level);
	} else {
		LOG_ERR("Failed to set security (%d).", err);
	}

	wait_status(conn, CONN_INFO_SECURITY_SET);
	wait_status(conn, CONN_INFO_ID_RESOLVED);
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	conn_info_ref = get_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref);

	LOG_DBG("MTU exchange addr %s conn %s", addr,
		   err == 0U ? "successful" : "failed");

	atomic_set_bit(conn_info_ref->flags, CONN_INFO_MTU_EXCHANGED);
}

static void exchange_mtu(struct bt_conn *conn, void *data)
{
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	conn_info_ref = get_connected_conn_info_ref(conn);
	if (conn_info_ref == NULL) {
		LOG_DBG("not connected: %s", addr);
		return;
	}

	if (!atomic_test_bit(conn_info_ref->flags, CONN_INFO_MTU_EXCHANGED) &&
	    !atomic_test_and_set_bit(conn_info_ref->flags, CONN_INFO_SENT_MTU_EXCHANGE)) {
		LOG_DBG("Updating MTU for %s to %u", addr, bt_gatt_get_mtu(conn));

		mtu_exchange_params.func = mtu_exchange_cb;
		err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
		if (err) {
			LOG_ERR("MTU exchange failed (err %d)", err);
			atomic_clear_bit(conn_info_ref->flags, CONN_INFO_SENT_MTU_EXCHANGE);
		} else {
			LOG_INF("MTU Exchange pending...");
		}
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	__ASSERT(!err, "Security for %s failed", addr);
	LOG_INF("Security for %s changed: level %u", addr, level);

	if (err) {
		LOG_ERR("Security failed, disconnecting");
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_POWER_OFF);
	}

	struct conn_info *conn_info_ref = get_conn_info_ref(conn);
	atomic_set_bit(conn_info_ref->flags, CONN_INFO_SECURITY_SET);
}

static void identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
			      const bt_addr_le_t *identity)
{
	char addr_identity[BT_ADDR_LE_STR_LEN];
	char addr_rpa[BT_ADDR_LE_STR_LEN];
	struct conn_info *conn_info_ref;

	bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
	bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

	LOG_INF("Identity resolved %s -> %s", addr_rpa, addr_identity);

	/* overwrite RPA */
	conn_info_ref = get_conn_info_ref(conn);
	bt_addr_le_copy(&conn_info_ref->addr, identity);

	atomic_set_bit(conn_info_ref->flags, CONN_INFO_ID_RESOLVED);
}


static void stop_scan(void)
{
	int err;

	__ASSERT(atomic_test_bit(status_flags, DEVICE_IS_SCANNING),
		 "No scanning procedure is ongoing");
	atomic_clear_bit(status_flags, DEVICE_IS_SCANNING);

	err = bt_le_scan_stop();
	__ASSERT(!err, "Stop LE scan failed (err %d)", err);

	LOG_INF("Stopped scanning");
}

static bool check_if_peer_connected(const bt_addr_le_t *addr)
{
	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (conn_infos[i].conn_ref != NULL) {
			if (!bt_addr_le_cmp(bt_conn_get_dst(conn_infos[i].conn_ref), addr)) {
				return true;
			}
		}
	}

	return false;
}

static bool parse_ad(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;

	LOG_INF("[AD]: %u data_len %u", data->type, data->data_len);

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		LOG_INF("------------------------------------------------------");
		LOG_INF("Device name : %.*s", data->data_len, data->data);

		if (check_if_peer_connected(addr)) {
			LOG_ERR("Peer is already connected or in disconnecting state");
			break;
		}

		__ASSERT(!atomic_test_bit(status_flags, DEVICE_IS_CONNECTING),
			 "A connection procedure is ongoing");
		atomic_set_bit(status_flags, DEVICE_IS_CONNECTING);

		stop_scan();

		char addr_str[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
		LOG_INF("Connecting to %s", addr_str);

		struct bt_le_conn_param *param = BT_LE_CONN_PARAM_DEFAULT;
		int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param,
					    &conn_connecting);

		__ASSERT(!err, "Create conn failed (err %d)", err);

		return false;

		break;
	}

	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char dev[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, dev, sizeof(dev));
	printk("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n", dev, type, ad->len, rssi);

	bt_data_parse(ad, parse_ad, (void *)addr);
}

static void start_scan(void)
{
	int err;
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	atomic_set_bit(status_flags, DEVICE_IS_SCANNING);

	err = bt_le_scan_start(&scan_param, device_found);
	__ASSERT(!err, "Scanning failed to start (err %d)", err);

	LOG_INF("Started scanning");
}

static struct conn_info *get_new_conn_info_ref(const bt_addr_le_t *addr)
{
	/* try to find per-addr first */
	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (bt_addr_le_eq(&conn_infos[i].addr, addr)) {
			return &conn_infos[i];
		}
	}

	/* try to allocate if addr not found */
	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (conn_infos[i].conn_ref == NULL) {
			bt_addr_le_copy(&conn_infos[i].addr, addr);

			return &conn_infos[i];
		}
	}

	__ASSERT(0, "ran out of contexts");
}

void clear_info(struct conn_info *info)
{
	/* clear everything except the address + sub params + uuid (lifetime > connection) */
	memset(&info->flags, 0, sizeof(info->flags));
	memset(&info->conn_ref, 0, sizeof(info->conn_ref));
	memset(&info->notify_counter, 0, sizeof(info->notify_counter));
	memset(&info->tx_notify_counter, 0, sizeof(info->tx_notify_counter));
}

static void connected_cb(struct bt_conn *conn, uint8_t conn_err)
{
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	__ASSERT(conn_err == BT_HCI_ERR_SUCCESS, "Failed to connect to %s (%u)", addr, conn_err);

	LOG_INF("Connection %p established : %s", conn, addr);

	atomic_inc(&conn_count);
	LOG_DBG("connected to %u devices", atomic_get(&conn_count));

	conn_info_ref = get_new_conn_info_ref(bt_conn_get_dst(conn));
	__ASSERT_NO_MSG(conn_info_ref->conn_ref == NULL);

	conn_info_ref->conn_ref = conn_connecting;

	__ASSERT_NO_MSG(conn == conn_connecting);
	if (conn == conn_connecting) {
		conn_connecting = NULL;
		atomic_clear_bit(status_flags, DEVICE_IS_CONNECTING);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

	conn_info_ref = get_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref->conn_ref != NULL);

	bool valid_reason =
		reason == BT_HCI_ERR_REMOTE_POWER_OFF ||
		reason == BT_HCI_ERR_LOCALHOST_TERM_CONN;

	__ASSERT(valid_reason, "Disconnected (reason 0x%02x)", reason);

	bt_conn_unref(conn);
	clear_info(conn_info_ref);
	atomic_dec(&conn_count);
}

struct conn_info *get_conn_info_ref(struct bt_conn *conn_ref)
{
	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (conn_ref == conn_infos[i].conn_ref) {
			return &conn_infos[i];
		}
	}

	return NULL;
}

bool is_connected(struct bt_conn *conn)
{
	struct bt_conn_info info;
	int err = bt_conn_get_info(conn, &info);

	__ASSERT(!err, "Couldn't get conn info %d", err);

	return info.state == BT_CONN_STATE_CONNECTED;
}

struct conn_info *get_connected_conn_info_ref(struct bt_conn *conn)
{
	if (is_connected(conn)) {
		return get_conn_info_ref(conn);
	}

	return NULL;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected_cb,
	.disconnected = disconnected,
	.security_changed = security_changed,
	.identity_resolved = identity_resolved,
};

void test_connection_init(void)
{
	memset(&conn_infos, 0x00, sizeof(conn_infos));

	int err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}
	LOG_DBG("Bluetooth initialized");
}

void test_central_connect(void)
{
	LOG_WRN("1- Connect ----------");
	start_scan();
	while(atomic_get(&conn_count) < LINK_COUNT) {
		k_sleep(K_MSEC(1));

		if (!atomic_test_bit(status_flags, DEVICE_IS_SCANNING) &&
			!atomic_test_bit(status_flags, DEVICE_IS_CONNECTING)) {
			start_scan();
		}
	}

	LOG_WRN("2- SECURITY ----------");
	int level = BT_SECURITY_L2;
	bt_conn_foreach(BT_CONN_TYPE_LE, set_security, &level);

	LOG_WRN("3- MTU EXC ----------");
	bt_conn_foreach(BT_CONN_TYPE_LE, exchange_mtu, NULL);
}
