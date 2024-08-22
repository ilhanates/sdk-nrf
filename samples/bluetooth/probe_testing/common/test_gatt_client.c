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

static gatt_service_discovery_t service_discovery;
static gatt_char_discovery_t char_discovery;
static gatt_descr_discovery_t descr_discovery;
static gatt_attr_discovery_t attr_discovery;
static struct bt_gatt_subscribe_params subscribe_params;

static void gatt_client_subscribe_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params)
{
	LOG_INF("[SUBSCRIBED]");
	struct conn_info *conn_info_ref = get_conn_info_ref(conn);
	atomic_set_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED);
}

static void gatt_client_unsubscribe_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params)
{
	LOG_INF("[UNSUBSCRIBED]");

	struct conn_info *conn_info_ref = get_conn_info_ref(conn);
	atomic_clear_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED);
}

static uint8_t gatt_client_notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	uint8_t *value = (uint8_t *)data;
	if (value && length > 0) {
		LOG_INF("%s", value);

		struct conn_info *conn_info_ref = get_conn_info_ref(conn);
		uint8_t flag = (params->value == BT_GATT_CCC_NOTIFY) ? CONN_INFO_NOTIFIED: CONN_INFO_INDICATED;
		atomic_set_bit(conn_info_ref->flags, flag);
	}
	return BT_GATT_ITER_CONTINUE;
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

			bt_uuid_to_str(gatt_chrc->uuid, uuid_str, sizeof(uuid_str));
			LOG_INF("BT_GATT_DISCOVER_CHARACTERISTIC UUID: %s HANDLE-> %u Value Handle:%u", uuid_str, char_discovery.handle, char_discovery.value_handle);
			atomic_set_bit(conn_info_ref->flags, CONN_INFO_DISCOVER_CHAR_COMPLETED);
			break;
		case BT_GATT_DISCOVER_DESCRIPTOR:
			descr_discovery.conn_obj_addr = (uint32_t)conn;
			descr_discovery.handle = attr->handle;
			descr_discovery.perm = attr->perm;
			descr_discovery.uuid_type = attr->uuid->type;

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

int test_gatt_client_discover(struct conn_info *conn_info_ref)
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

void test_gatt_client_subscribe(struct conn_info *conn_info_ref, uint16_t value)
{
	int err = 0;
	subscribe_params.value = value;
	subscribe_params.value_handle = char_discovery.value_handle;
	subscribe_params.ccc_handle = descr_discovery.handle;
	subscribe_params.subscribe = gatt_client_subscribe_cb,
	subscribe_params.notify = gatt_client_notify_cb;
	err = bt_gatt_subscribe(conn_info_ref->conn_ref, &subscribe_params);
	if (err && err != -EALREADY) {
		LOG_ERR("Subscribe failed (err %d)", err);
		return;
	}

	LOG_INF("Waiting for gatt subscribe...");
	while (!atomic_test_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED)) {
		k_sleep(K_MSEC(10));
	}
}

void test_gatt_client_unsubscribe(struct conn_info *conn_info_ref)
{
	int err = 0;
	subscribe_params.value = 0;
	subscribe_params.value_handle = char_discovery.value_handle;
	subscribe_params.ccc_handle = descr_discovery.handle;
	subscribe_params.subscribe = gatt_client_unsubscribe_cb,
	subscribe_params.notify = gatt_client_notify_cb;

	err = bt_gatt_unsubscribe(conn_info_ref->conn_ref, &subscribe_params);
	if (err && err != -EALREADY) {
		LOG_ERR("Unsubscribe failed (err %d)", err);
		return;
	}

	LOG_INF("Waiting for gatt unsubscribe...");
	while (atomic_test_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED)) {
		k_sleep(K_MSEC(10));
	}
}

int test_gatt_client_write_without_resp(struct conn_info *conn_info_ref)
{
	static uint16_t write_count = 1;
	test_common_prepare_value(2, "WRITE", write_count, test_value);
	int err = bt_gatt_write_without_response(conn_info_ref->conn_ref, char_discovery.value_handle, test_value,
						 MAX_DATA_LEN, false);
	if (err) {
		LOG_ERR("GATT Write without response failed:%d", err);
	}
	write_count++;

	return err;
}

void test_gatt_client_wait_for(struct conn_info *conn_info_ref, uint8_t state)
{
	LOG_INF("%s...", (state == CONN_INFO_NOTIFIED) ? "notification": "indication");
	while (!atomic_test_bit(conn_info_ref->flags, state)) {
		k_sleep(K_MSEC(10));
	}
	atomic_clear_bit(conn_info_ref->flags, state);
}
