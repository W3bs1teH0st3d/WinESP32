/**
 * Win32 OS - Bluetooth File Transfer
 * BLE GATT service for file transfer using NimBLE via ESP-Hosted
 * 
 * When CONFIG_BT_ENABLED is not set, stub functions are provided.
 */

#include "bluetooth_transfer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "system_settings.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

// Check if BT is enabled in config
#include "sdkconfig.h"

static const char *TAG = "BT_TRANSFER";

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BT_NIMBLE_ENABLED)

// Full BT implementation
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_hosted.h"

// BLE UUIDs for File Transfer Service
#define FILE_TRANSFER_SERVICE_UUID      0x1234
#define FILE_INFO_CHAR_UUID             0x1235
#define FILE_DATA_CHAR_UUID             0x1236
#define FILE_CONTROL_CHAR_UUID          0x1237

// Static UUID variables for C++ compatibility (BLE_UUID16_DECLARE macro doesn't work in C++ struct init)
static ble_uuid16_t svc_uuid = BLE_UUID16_INIT(FILE_TRANSFER_SERVICE_UUID);
static ble_uuid16_t file_info_uuid = BLE_UUID16_INIT(FILE_INFO_CHAR_UUID);
static ble_uuid16_t file_data_uuid = BLE_UUID16_INIT(FILE_DATA_CHAR_UUID);
static ble_uuid16_t file_control_uuid = BLE_UUID16_INIT(FILE_CONTROL_CHAR_UUID);

// State
static bool bt_initialized = false;
static bool bt_advertising = false;
static uint16_t bt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static char bt_device_name[32] = "WinEsp32-PDA";
static char bt_mac_str[18] = {0};
static char bt_connected_device[32] = {0};

// Transfer state
static bt_transfer_info_t current_transfer = {0};
static bt_transfer_callback_t transfer_callback = NULL;
static FILE *transfer_file = NULL;
static uint8_t *transfer_buffer = NULL;
static const size_t CHUNK_SIZE = 512;
static char receive_save_dir[128] = "/littlefs/received";

// GATT attribute handles
static uint16_t file_info_handle;
static uint16_t file_data_handle;
static uint16_t file_control_handle;

// Forward declarations
static void bt_host_task(void *param);
static void bt_on_reset(int reason);
static void bt_on_sync(void);
static int bt_gap_event(struct ble_gap_event *event, void *arg);
static int gatt_svr_init(void);

// GATT callbacks
static int file_info_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char info[128];
        snprintf(info, sizeof(info), "%s|%lu|%d",
                 current_transfer.filename,
                 (unsigned long)current_transfer.file_size,
                 current_transfer.status);
        int rc = os_mbuf_append(ctxt->om, info, strlen(info));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int file_data_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (current_transfer.status != BT_TRANSFER_SENDING || !transfer_file) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (!transfer_buffer) {
            transfer_buffer = (uint8_t *)malloc(CHUNK_SIZE);
            if (!transfer_buffer) return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        size_t bytes_read = fread(transfer_buffer, 1, CHUNK_SIZE, transfer_file);
        if (bytes_read > 0) {
            current_transfer.transferred += bytes_read;
            current_transfer.progress_percent = 
                (current_transfer.file_size > 0) ?
                (current_transfer.transferred * 100 / current_transfer.file_size) : 0;
            if (transfer_callback) transfer_callback(&current_transfer);
            int rc = os_mbuf_append(ctxt->om, transfer_buffer, bytes_read);
            if (current_transfer.transferred >= current_transfer.file_size) {
                current_transfer.status = BT_TRANSFER_COMPLETE;
                fclose(transfer_file);
                transfer_file = NULL;
            }
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        current_transfer.status = BT_TRANSFER_COMPLETE;
        if (transfer_file) { fclose(transfer_file); transfer_file = NULL; }
        return 0;
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (current_transfer.status != BT_TRANSFER_RECEIVING || !transfer_file) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0) {
            uint8_t *data = (uint8_t *)malloc(len);
            if (!data) return BLE_ATT_ERR_INSUFFICIENT_RES;
            uint16_t copied = 0;
            ble_hs_mbuf_to_flat(ctxt->om, data, len, &copied);
            size_t written = fwrite(data, 1, copied, transfer_file);
            free(data);
            current_transfer.transferred += written;
            current_transfer.progress_percent = 
                (current_transfer.file_size > 0) ?
                (current_transfer.transferred * 100 / current_transfer.file_size) : 0;
            if (transfer_callback) transfer_callback(&current_transfer);
            if (current_transfer.transferred >= current_transfer.file_size) {
                current_transfer.status = BT_TRANSFER_COMPLETE;
                fclose(transfer_file);
                transfer_file = NULL;
            }
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int file_control_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t status = (uint8_t)current_transfer.status;
        int rc = os_mbuf_append(ctxt->om, &status, 1);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len >= 1) {
            uint8_t *data = (uint8_t *)malloc(len);
            if (!data) return BLE_ATT_ERR_INSUFFICIENT_RES;
            uint16_t copied = 0;
            ble_hs_mbuf_to_flat(ctxt->om, data, len, &copied);
            
            uint8_t cmd = data[0];
            if (cmd == 0) {
                // Cancel transfer
                bt_cancel_transfer();
            } else if (cmd == 1 && len > 1) {
                // Start receive: cmd=1, then filename|size
                // Format: 0x01 filename|size
                char *info = (char *)(data + 1);
                info[copied - 1] = '\0';
                
                char *sep = strchr(info, '|');
                if (sep) {
                    *sep = '\0';
                    uint32_t file_size = atoi(sep + 1);
                    
                    // Create receive directory if needed
                    mkdir(receive_save_dir, 0755);
                    
                    // Open file for writing
                    char full_path[256];
                    snprintf(full_path, sizeof(full_path), "%s/%s", receive_save_dir, info);
                    
                    if (transfer_file) fclose(transfer_file);
                    transfer_file = fopen(full_path, "wb");
                    
                    if (transfer_file) {
                        memset(&current_transfer, 0, sizeof(current_transfer));
                        snprintf(current_transfer.filename, sizeof(current_transfer.filename), "%s", info);
                        current_transfer.file_size = file_size;
                        current_transfer.status = BT_TRANSFER_RECEIVING;
                        current_transfer.direction = BT_DIR_RECEIVE;
                        ESP_LOGI(TAG, "Receiving: %s (%lu bytes)", info, (unsigned long)file_size);
                    }
                }
            }
            free(data);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// GATT characteristics definition (separate for C++ compatibility)
static struct ble_gatt_chr_def file_transfer_chars[] = {
    {
        .uuid = &file_info_uuid.u,
        .access_cb = file_info_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &file_info_handle,
    },
    {
        .uuid = &file_data_uuid.u,
        .access_cb = file_data_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &file_data_handle,
    },
    {
        .uuid = &file_control_uuid.u,
        .access_cb = file_control_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        .val_handle = &file_control_handle,
    },
    { 0 } // Terminator
};

// GATT service definition
static struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = file_transfer_chars,
    },
    { 0 } // Terminator
};

static int bt_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                bt_conn_handle = event->connect.conn_handle;
                bt_advertising = false;
                if (ble_gap_conn_find(bt_conn_handle, &desc) == 0) {
                    snprintf(bt_connected_device, sizeof(bt_connected_device),
                             "%02X:%02X:%02X:%02X:%02X:%02X",
                             desc.peer_ota_addr.val[5], desc.peer_ota_addr.val[4],
                             desc.peer_ota_addr.val[3], desc.peer_ota_addr.val[2],
                             desc.peer_ota_addr.val[1], desc.peer_ota_addr.val[0]);
                }
                ESP_LOGI(TAG, "Connected: %s", bt_connected_device);
            } else {
                bt_start_advertising();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected");
            bt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            bt_connected_device[0] = '\0';
            if (current_transfer.status == BT_TRANSFER_SENDING ||
                current_transfer.status == BT_TRANSFER_RECEIVING) {
                bt_cancel_transfer();
            }
            bt_start_advertising();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            bt_advertising = false;
            break;
    }
    return 0;
}

static void bt_on_reset(int reason) {
    ESP_LOGW(TAG, "BLE host reset; reason=%d", reason);
}

static void bt_on_sync(void) {
    ESP_LOGI(TAG, "BLE host synced");
    uint8_t addr[6];
    if (ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr, NULL) == 0) {
        snprintf(bt_mac_str, sizeof(bt_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
        ESP_LOGI(TAG, "BLE MAC: %s", bt_mac_str);
    }
    bt_start_advertising();
}

static void bt_host_task(void *param) {
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static int gatt_svr_init(void) {
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) return rc;
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    return rc;
}

int bt_init(void) {
    if (bt_initialized) {
        ESP_LOGI(TAG, "Bluetooth already initialized");
        return 0;
    }
    ESP_LOGI(TAG, "Initializing Bluetooth...");
    
    system_settings_t *s = settings_get_global();
    if (s && s->bt_name[0]) {
        snprintf(bt_device_name, sizeof(bt_device_name), "%s", s->bt_name);
    }
    
    // Check if BT controller is already initialized (error 262 = ESP_ERR_INVALID_STATE)
    esp_err_t ret = esp_hosted_bt_controller_init();
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "BT controller already initialized, continuing...");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init BT controller: %d", ret);
        // Don't fail completely - BT is optional feature
        ESP_LOGW(TAG, "Bluetooth will be unavailable");
        return -1;
    }
    
    ret = esp_hosted_bt_controller_enable();
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "BT controller already enabled, continuing...");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable BT controller: %d", ret);
        ESP_LOGW(TAG, "Bluetooth will be unavailable");
        return -2;
    }
    
    ret = nimble_port_init();
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "NimBLE already initialized, continuing...");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NimBLE: %d", ret);
        ESP_LOGW(TAG, "Bluetooth will be unavailable");
        return -3;
    }
    
    ble_hs_cfg.reset_cb = bt_on_reset;
    ble_hs_cfg.sync_cb = bt_on_sync;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    
    if (gatt_svr_init() != 0) {
        ESP_LOGE(TAG, "Failed to init GATT server");
        ESP_LOGW(TAG, "Bluetooth will be unavailable");
        return -4;
    }
    ble_svc_gap_device_name_set(bt_device_name);
    nimble_port_freertos_init(bt_host_task);
    
    bt_initialized = true;
    ESP_LOGI(TAG, "Bluetooth initialized successfully");
    return 0;
}

void bt_deinit(void) {
    if (!bt_initialized) return;
    bt_cancel_transfer();
    bt_stop_advertising();
    if (bt_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(bt_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    nimble_port_stop();
    nimble_port_deinit();
    esp_hosted_bt_controller_disable();
    esp_hosted_bt_controller_deinit(true);
    bt_initialized = false;
    bt_advertising = false;
    bt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

bool bt_is_ready(void) { return bt_initialized; }
bool bt_is_connected(void) { return bt_conn_handle != BLE_HS_CONN_HANDLE_NONE; }

int bt_start_advertising(void) {
    if (!bt_initialized || bt_advertising) return 0;
    if (bt_conn_handle != BLE_HS_CONN_HANDLE_NONE) return 0;
    
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)bt_device_name;
    fields.name_len = strlen(bt_device_name);
    fields.name_is_complete = 1;
    fields.uuids16 = &svc_uuid;  // Use static UUID variable
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    
    if (ble_gap_adv_set_fields(&fields) != 0) return -2;
    
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    if (ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &adv_params, bt_gap_event, NULL) != 0) return -3;
    
    bt_advertising = true;
    ESP_LOGI(TAG, "Advertising as '%s'", bt_device_name);
    return 0;
}

int bt_stop_advertising(void) {
    if (!bt_advertising) return 0;
    ble_gap_adv_stop();
    bt_advertising = false;
    return 0;
}

const char* bt_get_device_name(void) { return bt_device_name; }

int bt_set_device_name(const char *name) {
    if (!name || !name[0]) return -1;
    strncpy(bt_device_name, name, sizeof(bt_device_name) - 1);
    if (bt_initialized) {
        ble_svc_gap_device_name_set(bt_device_name);
        if (bt_advertising) {
            bt_stop_advertising();
            bt_start_advertising();
        }
    }
    return 0;
}

int bt_send_file(const char *path, bt_transfer_callback_t callback) {
    if (!bt_initialized) return -1;
    if (bt_conn_handle == BLE_HS_CONN_HANDLE_NONE) return -2;
    if (current_transfer.status == BT_TRANSFER_SENDING ||
        current_transfer.status == BT_TRANSFER_RECEIVING) return -3;
    
    struct stat st;
    if (stat(path, &st) != 0) return -4;
    
    transfer_file = fopen(path, "rb");
    if (!transfer_file) return -5;
    
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    
    memset(&current_transfer, 0, sizeof(current_transfer));
    strncpy(current_transfer.filename, filename, sizeof(current_transfer.filename) - 1);
    current_transfer.file_size = st.st_size;
    current_transfer.status = BT_TRANSFER_SENDING;
    current_transfer.direction = BT_DIR_SEND;
    transfer_callback = callback;
    
    ESP_LOGI(TAG, "Sending: %s (%lu bytes)", current_transfer.filename, (unsigned long)st.st_size);
    return 0;
}

int bt_cancel_transfer(void) {
    if (transfer_file) { fclose(transfer_file); transfer_file = NULL; }
    if (transfer_buffer) { free(transfer_buffer); transfer_buffer = NULL; }
    current_transfer.status = BT_TRANSFER_IDLE;
    transfer_callback = NULL;
    return 0;
}

bt_transfer_info_t* bt_get_transfer_info(void) { return &current_transfer; }
const char* bt_get_mac_address(void) { return bt_mac_str; }
const char* bt_get_connected_device(void) { return bt_connected_device; }

int bt_get_rssi(void) {
    if (bt_conn_handle == BLE_HS_CONN_HANDLE_NONE) return 0;
    int8_t rssi = 0;
    ble_gap_conn_rssi(bt_conn_handle, &rssi);
    return rssi;
}

int bt_receive_file(const char *save_dir, bt_transfer_callback_t callback) {
    if (!bt_initialized) return -1;
    if (bt_conn_handle == BLE_HS_CONN_HANDLE_NONE) return -2;
    if (current_transfer.status == BT_TRANSFER_SENDING ||
        current_transfer.status == BT_TRANSFER_RECEIVING) return -3;
    
    // Set save directory
    if (save_dir && save_dir[0]) {
        snprintf(receive_save_dir, sizeof(receive_save_dir), "%s", save_dir);
    }
    
    // Create directory if needed
    mkdir(receive_save_dir, 0755);
    
    transfer_callback = callback;
    
    // Ready to receive - the actual file info comes via control characteristic
    ESP_LOGI(TAG, "Ready to receive files to: %s", receive_save_dir);
    return 0;
}

#else // CONFIG_BT_ENABLED not defined - stub implementation

// Stub functions when BT is disabled
int bt_init(void) {
    ESP_LOGW(TAG, "Bluetooth not enabled in config");
    return -1;
}
void bt_deinit(void) {}
bool bt_is_ready(void) { return false; }
bool bt_is_connected(void) { return false; }
int bt_start_advertising(void) { return -1; }
int bt_stop_advertising(void) { return 0; }
const char* bt_get_device_name(void) { return "BT Disabled"; }
int bt_set_device_name(const char *name) { return -1; }
int bt_send_file(const char *path, bt_transfer_callback_t callback) { return -1; }
int bt_receive_file(const char *save_dir, bt_transfer_callback_t callback) { return -1; }
int bt_cancel_transfer(void) { return 0; }
bt_transfer_info_t* bt_get_transfer_info(void) { 
    static bt_transfer_info_t dummy = {0};
    return &dummy;
}
const char* bt_get_mac_address(void) { return ""; }
const char* bt_get_connected_device(void) { return ""; }
int bt_get_rssi(void) { return 0; }

#endif // CONFIG_BT_ENABLED
