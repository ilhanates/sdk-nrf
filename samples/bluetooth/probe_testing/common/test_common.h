
#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>

#define TEST_LOOP 1

#define MAX_DATA_LEN 128

#define CUSTOM_SERVICE_UUID BT_UUID_DECLARE_128(0xBB, 0x4A, 0xFF, 0x4F, 0xAD, 0x03, 0x41, 0x5D, 0xA9, 0x6C, 0x9D, 0x6C, 0xDD, 0xDA, 0x83, 0x04)

#ifdef CONFIG_SOC_SERIES_BSIM_NRFXX
#define BSIM_BUSY_WAIT do { k_msleep(10); } while(1);
#else
#define BSIM_BUSY_WAIT
#endif

#define END_TEST(DESC) 	printk("%6s ENDS ---------------\n", DESC);

enum {
	DEVICE_IS_SCANNING,
	DEVICE_IS_CONNECTING,

	/* Total number of flags - must be at the end of the enum */
	DEVICE_NUM_FLAGS,
};

enum {
	CONN_INFO_CONNECTED,
	CONN_INFO_DISCONNECTED,
	CONN_INFO_SECURITY_UPDATED,
	CONN_INFO_SENT_MTU_EXCHANGE,
	CONN_INFO_MTU_EXCHANGED,
	CONN_INFO_ID_RESOLVED,
	CONN_INFO_DISCOVERING,
	CONN_INFO_DISCOVER_PAUSED,
	CONN_INFO_DISCOVER_SERV_COMPLETED,
	CONN_INFO_DISCOVER_CHAR_COMPLETED,
	CONN_INFO_DISCOVER_DESC_COMPLETED,
	CONN_INFO_SUBSCRIBED,
	CONN_INFO_NOTIFIED,
	CONN_INFO_INDICATED,
	CONN_INFO_INDICATION_CONFIRMED,

	/* Total number of flags - must be at the end of the enum */
	CONN_INFO_NUM_FLAGS,
};

typedef struct {
	ATOMIC_DEFINE(flags, CONN_INFO_NUM_FLAGS);
	struct bt_conn *conn;
	uint32_t notify_count;
	uint32_t write_counter;
	struct bt_gatt_discover_params discover_params;
	struct bt_gatt_subscribe_params subscribe_params;
	bt_addr_le_t addr;
} conn_info_t;


typedef struct  {
	uint16_t start_handle;
	uint16_t end_handle;
} gatt_service_discovery_t;

typedef struct {
	uint16_t len;
	uint8_t buf[513];
} gatt_attr_data_t;


void test_connection_init(void);
void test_central_connect(void);
void test_disconnect_all(void);
conn_info_t *test_peripheral_connect(void);
conn_info_t *get_conn_info(struct bt_conn *conn);
void test_connection_wait_for(struct bt_conn *conn, int flag);

void test_gatt_client_init(void);
int test_gatt_client_discover(conn_info_t *conn);
void test_gatt_client_subscribe(conn_info_t *conn, uint16_t value);
void test_gatt_client_unsubscribe(conn_info_t *conn_info);
int test_gatt_client_write_without_resp(conn_info_t *conn);
void test_gatt_client_wait_for(conn_info_t *conn_info, uint8_t state);

void test_gatt_server_init(void);
void test_gatt_server_wait_for(uint8_t subscription_value);
void test_gatt_server_notify_all(void);
void test_gatt_server_indicate_all(void);

void test_common_prepare_value(uint16_t id, uint8_t *descr, uint32_t sequence, uint8_t *dst);


