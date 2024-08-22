
#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>

#define MAX_DATA_LEN 128

#define CUSTOM_SERVICE_UUID BT_UUID_DECLARE_128(0xBB, 0x4A, 0xFF, 0x4F, 0xAD, 0x03, 0x41, 0x5D, 0xA9, 0x6C, 0x9D, 0x6C, 0xDD, 0xDA, 0x83, 0x04)

#ifdef CONFIG_SOC_SERIES_BSIM_NRFXX
#define BSIM_BUSY_WAIT do { k_msleep(10); } while(1);
#else
#define BSIM_BUSY_WAIT
#endif

#define END_TEST(DESC) 	k_msleep(100); printk("%6s ENDS ---------------\n", DESC);

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
	CONN_INFO_DISCOVER_SERV_COMPLETED,
	CONN_INFO_DISCOVER_CHAR_COMPLETED,
	CONN_INFO_DISCOVER_DESC_COMPLETED,
	CONN_INFO_SUBSCRIBED,
	CONN_INFO_NOTIFIED,
	CONN_INFO_INDICATION_CONFIRMED,

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


typedef struct  {
	uint32_t conn_obj_addr;
	uint16_t start_handle;
	uint16_t end_handle;
	uint8_t perm;
	uint8_t uuid_type;
	union  {
		uint16_t uuid16;
		uint32_t uuid32;
		uint8_t uuid128[16];
	} uuid;
} gatt_service_discovery_t;

typedef struct  {
	uint32_t conn_obj_addr;
	uint16_t handle;
	uint16_t value_handle;
	uint8_t properties;
	uint8_t perm;
	uint8_t uuid_type;
	union  {
		uint16_t uuid16;
		uint32_t uuid32;
		uint8_t uuid128[16];
	} uuid;
} gatt_char_discovery_t;

typedef struct  {
	uint32_t conn_obj_addr;
	uint16_t handle;
	uint8_t perm;
	uint8_t uuid_type;
	union  {
		uint16_t uuid16;
		uint32_t uuid32;
		uint8_t uuid128[16];
	} uuid;
} gatt_descr_discovery_t;

typedef struct  {
	uint32_t conn_obj_addr;
	uint16_t handle;
	uint8_t perm;
	uint8_t uuid_type;
	union  {
		uint16_t uuid16;
		uint32_t uuid32;
		uint8_t uuid128[16];
	} uuid;
} gatt_attr_discovery_t;

typedef struct {
	uint16_t len;
	uint8_t buf[513];
} gatt_attr_data_t;


void test_connection_init(void);
void test_central_connect(void);
struct conn_info *test_peripheral_connect(void);
struct conn_info *get_conn_info_ref(struct bt_conn *conn_ref);
struct conn_info *get_connected_conn_info_ref(struct bt_conn *conn);
bool is_connected(struct bt_conn *conn);

void test_gatt_client_init(void);
int test_gatt_client_discover(struct conn_info *conn);
void test_gatt_client_subscribe(struct conn_info *conn, uint16_t value);
void test_gatt_client_unsubscribe(struct conn_info *conn_info_ref);
int test_gatt_client_write_without_resp(struct conn_info *conn);
void test_gatt_client_wait_notification(struct conn_info *conn_info_ref);

void test_gatt_server_init(void);
void test_gatt_server_wait_subscribe(uint8_t subscription_value);
void test_gatt_server_notify_all(void);
void test_gatt_server_indicate_all(void);

void test_common_prepare_value(uint16_t id, uint8_t *descr, uint32_t sequence, uint8_t *dst);


