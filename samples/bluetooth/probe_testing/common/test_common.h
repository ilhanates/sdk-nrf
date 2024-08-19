

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

enum {
	DEVICE_IS_SCANNING,
	DEVICE_IS_CONNECTING,

	/* Total number of flags - must be at the end of the enum */
	DEVICE_NUM_FLAGS,
};

enum {
	CONN_INFO_CONNECTED,
	CONN_INFO_SECURITY_UPDATED,
	CONN_INFO_SENT_MTU_EXCHANGE,
	CONN_INFO_ID_RESOLVED,
	CONN_INFO_MTU_EXCHANGED,
	CONN_INFO_DISCOVERING,
	CONN_INFO_DISCOVER_PAUSED,
	CONN_INFO_DISCOVER_COMPLETED,
	CONN_INFO_SUBSCRIBED,

	/* Total number of flags - must be at the end of the enum */
	CONN_INFO_NUM_FLAGS,
};

struct conn_info {
	ATOMIC_DEFINE(flags, CONN_INFO_NUM_FLAGS);
	struct bt_conn *conn_ref;
	uint32_t notify_counter;
	uint32_t tx_notify_counter;
	struct bt_uuid_128 uuid;
	struct bt_gatt_discover_params discover_params;
	struct bt_gatt_subscribe_params subscribe_params;
	struct bt_conn_le_data_len_param le_data_len_param;
	bt_addr_le_t addr;
};

extern uint8_t central_subscription;

void test_connection_init(void);
void test_central_connect(void);
struct conn_info *test_peripheral_connect(void);
struct conn_info *get_conn_info_ref(struct bt_conn *conn_ref);
struct conn_info *get_connected_conn_info_ref(struct bt_conn *conn);
bool is_connected(struct bt_conn *conn);

void test_gatt_init(void);
void test_gatt_discovery(struct conn_info *conn);
void test_gatt_subscribe(struct conn_info *conn);
void test_gatt_notify(struct conn_info *conn);



