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
LOG_MODULE_REGISTER(test_gatt_client, LOG_LEVEL_INF);

static uint8_t test_value[MAX_DATA_LEN];

static gatt_service_discovery_t gatt_serv;
static struct bt_gatt_chrc gatt_char;
static struct bt_gatt_attr gatt_descr;
static struct bt_gatt_subscribe_params subscribe_params;

static void gatt_client_subscribe_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params)
{
	LOG_INF("[SUBSCRIBED]");
	conn_info_t *conn_info = get_conn_info(conn);
	atomic_set_bit(conn_info->flags, CONN_INFO_SUBSCRIBED);
}

static void gatt_client_unsubscribe_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params)
{
	LOG_INF("[UNSUBSCRIBED]");

	conn_info_t *conn_info = get_conn_info(conn);
	atomic_clear_bit(conn_info->flags, CONN_INFO_SUBSCRIBED);
}

static uint8_t gatt_client_notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	uint8_t *value = (uint8_t *)data;
	if (value && length > 0) {
		LOG_INF("%s", value);

		conn_info_t *conn_info = get_conn_info(conn);
		uint8_t flag = (params->value == BT_GATT_CCC_NOTIFY) ? CONN_INFO_NOTIFIED: CONN_INFO_INDICATED;
		atomic_set_bit(conn_info->flags, flag);
	}
	return BT_GATT_ITER_CONTINUE;
}

static uint8_t gatt_discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				struct bt_gatt_discover_params *params)
{
	if (attr == NULL) {
		return BT_GATT_ITER_STOP;
	}
	conn_info_t *conn_info = get_conn_info(conn);
	char uuid_str[BT_UUID_STR_LEN];
	int state = BT_GATT_ITER_CONTINUE;
	switch (params->type) {
		case BT_GATT_DISCOVER_PRIMARY:
		case BT_GATT_DISCOVER_SECONDARY:
			struct bt_gatt_service_val *gatt_service = attr->user_data;
			gatt_serv.start_handle = attr->handle;
			gatt_serv.end_handle = gatt_service->end_handle;

			bt_uuid_to_str(gatt_service->uuid, uuid_str, sizeof(uuid_str));
			LOG_INF("GATT_DISCOVER_PRIMARY UUID: %s HANDLE-> S:%u E:%u", uuid_str, gatt_serv.start_handle, gatt_serv.end_handle);
			atomic_set_bit(conn_info->flags, CONN_INFO_DISCOVER_SERV_COMPLETED);
			break;
		case BT_GATT_DISCOVER_CHARACTERISTIC:
			struct bt_gatt_chrc *chrc = attr->user_data;
			gatt_char.value_handle = chrc->value_handle;
			gatt_char.properties = chrc->properties;
			bt_uuid_to_str(chrc->uuid, uuid_str, sizeof(uuid_str));
			LOG_INF("BT_GATT_DISCOVER_CHARACTERISTIC UUID: %s Value Handle:%u", uuid_str, gatt_char.value_handle);
			atomic_set_bit(conn_info->flags, CONN_INFO_DISCOVER_CHAR_COMPLETED);
			break;
		case BT_GATT_DISCOVER_DESCRIPTOR:
			gatt_descr.handle = attr->handle;
			bt_uuid_to_str(attr->uuid, uuid_str, sizeof(uuid_str));
			LOG_INF("BT_GATT_DISCOVER_DESCRIPTOR UUID: %s HANDLE:%u", uuid_str, gatt_descr.handle);
			atomic_set_bit(conn_info->flags, CONN_INFO_DISCOVER_DESC_COMPLETED);
			//state = BT_GATT_ITER_STOP;
			break;
		default:
			LOG_ERR("Undefined discovery:%d", params->type);
			break;
	}

	return state;
}


void test_gatt_client_init(void)
{
}

int test_gatt_client_discover(conn_info_t *conn_info)
{
	int err = 0;
	memset(&conn_info->discover_params, 0,
		   sizeof(conn_info->discover_params));

	LOG_INF("Start service discovery...");
	conn_info->discover_params.uuid = CUSTOM_SERVICE_UUID;
	conn_info->discover_params.func = gatt_discover_cb;
	conn_info->discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	conn_info->discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	conn_info->discover_params.type = BT_GATT_DISCOVER_PRIMARY;
	err = bt_gatt_discover(conn_info->conn, &conn_info->discover_params);
	if (err) {
		return err;
	}
	LOG_INF("Waiting for service discovery...");
	while (!atomic_test_bit(conn_info->flags, CONN_INFO_DISCOVER_SERV_COMPLETED)) {
		k_sleep(K_MSEC(10));
	}

	LOG_INF("Start char discovery...");
	conn_info->discover_params.uuid = NULL;
	conn_info->discover_params.func = gatt_discover_cb;
	conn_info->discover_params.start_handle = gatt_serv.start_handle;
	conn_info->discover_params.end_handle = gatt_serv.end_handle;
	conn_info->discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
	err = bt_gatt_discover(conn_info->conn, &conn_info->discover_params);
	if (err) {
		return err;
	}
	LOG_INF("Waiting for char discovery...");
	while (!atomic_test_bit(conn_info->flags, CONN_INFO_DISCOVER_CHAR_COMPLETED)) {
		k_sleep(K_MSEC(10));
	}

	LOG_INF("Start descr discovery...");
	conn_info->discover_params.uuid = NULL;
	conn_info->discover_params.func = gatt_discover_cb;
	conn_info->discover_params.start_handle = gatt_char.value_handle + 1;
	conn_info->discover_params.end_handle = gatt_serv.end_handle;
	conn_info->discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
	err = bt_gatt_discover(conn_info->conn, &conn_info->discover_params);
	if (err) {
		return err;
	}
	LOG_INF("Waiting for char discovery...");
	while (!atomic_test_bit(conn_info->flags, CONN_INFO_DISCOVER_DESC_COMPLETED)) {
		k_sleep(K_MSEC(10));
	}

	LOG_INF("Service discovery completed!");

	return 0;
}

void test_gatt_client_subscribe(conn_info_t *conn_info, uint16_t value)
{
	int err = 0;
	subscribe_params.value = value;
	subscribe_params.value_handle = gatt_char.value_handle;
	subscribe_params.ccc_handle = gatt_descr.handle;
	subscribe_params.subscribe = gatt_client_subscribe_cb,
	subscribe_params.notify = gatt_client_notify_cb;
	err = bt_gatt_subscribe(conn_info->conn, &subscribe_params);
	if (err && err != -EALREADY) {
		LOG_ERR("Subscribe failed (err %d)", err);
		return;
	}

	LOG_INF("Waiting for gatt subscribe...");
	while (!atomic_test_bit(conn_info->flags, CONN_INFO_SUBSCRIBED)) {
		k_sleep(K_MSEC(10));
	}
}

void test_gatt_client_unsubscribe(conn_info_t *conn_info)
{
	int err = 0;
	subscribe_params.value = 0;
	subscribe_params.value_handle = gatt_char.value_handle;
	subscribe_params.ccc_handle = gatt_descr.handle;
	subscribe_params.subscribe = gatt_client_unsubscribe_cb,
	subscribe_params.notify = gatt_client_notify_cb;

	err = bt_gatt_unsubscribe(conn_info->conn, &subscribe_params);
	if (err && err != -EALREADY) {
		LOG_ERR("Unsubscribe failed (err %d)", err);
		return;
	}

	LOG_INF("Waiting for gatt unsubscribe...");
	while (atomic_test_bit(conn_info->flags, CONN_INFO_SUBSCRIBED)) {
		k_sleep(K_MSEC(10));
	}
}

int test_gatt_client_write_without_resp(conn_info_t *conn_info)
{
	static uint16_t write_count = 1;
	test_common_prepare_value(2, "WRITE", write_count, test_value);
	int err = bt_gatt_write_without_response(conn_info->conn, gatt_char.value_handle, test_value,
						 MAX_DATA_LEN, false);
	if (err) {
		LOG_ERR("GATT Write without response failed:%d", err);
	}
	write_count++;

	return err;
}

void test_gatt_client_wait_for(conn_info_t *conn_info, uint8_t state)
{
	LOG_INF("%s...", (state == CONN_INFO_NOTIFIED) ? "notification": "indication");
	while (!atomic_test_bit(conn_info->flags, state)) {
		k_sleep(K_MSEC(10));
	}
	atomic_clear_bit(conn_info->flags, state);
}
