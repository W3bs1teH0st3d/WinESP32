/**
 * Win32 OS - Bluetooth File Transfer
 * BLE GATT service for file transfer between ESP32 and phone
 */

#ifndef BLUETOOTH_TRANSFER_H
#define BLUETOOTH_TRANSFER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Transfer status
typedef enum {
    BT_TRANSFER_IDLE = 0,
    BT_TRANSFER_SENDING,
    BT_TRANSFER_RECEIVING,
    BT_TRANSFER_COMPLETE,
    BT_TRANSFER_ERROR
} bt_transfer_status_t;

// Transfer direction
typedef enum {
    BT_DIR_SEND = 0,    // Send file to phone
    BT_DIR_RECEIVE = 1  // Receive file from phone
} bt_transfer_dir_t;

// Transfer info structure
typedef struct {
    char filename[64];
    uint32_t file_size;
    uint32_t transferred;
    bt_transfer_status_t status;
    bt_transfer_dir_t direction;
    uint8_t progress_percent;
} bt_transfer_info_t;

// Callback for transfer progress
typedef void (*bt_transfer_callback_t)(bt_transfer_info_t *info);

// Initialize Bluetooth subsystem
// Returns 0 on success, negative on error
int bt_init(void);

// Deinitialize Bluetooth
void bt_deinit(void);

// Check if Bluetooth is initialized and ready
bool bt_is_ready(void);

// Check if a device is connected
bool bt_is_connected(void);

// Start advertising (make device discoverable)
int bt_start_advertising(void);

// Stop advertising
int bt_stop_advertising(void);

// Get device name
const char* bt_get_device_name(void);

// Set device name
int bt_set_device_name(const char *name);

// Send a file via Bluetooth
// path: full path to file (e.g., "/littlefs/photos/photo_001.bmp")
// callback: optional progress callback
// Returns 0 on success, negative on error
int bt_send_file(const char *path, bt_transfer_callback_t callback);

// Start receiving a file via Bluetooth
// save_dir: directory to save received file (e.g., "/littlefs/received")
// callback: optional progress callback
// Returns 0 on success, negative on error
int bt_receive_file(const char *save_dir, bt_transfer_callback_t callback);

// Cancel ongoing transfer
int bt_cancel_transfer(void);

// Get current transfer info
bt_transfer_info_t* bt_get_transfer_info(void);

// Get Bluetooth MAC address as string
const char* bt_get_mac_address(void);

// Get connected device name (if any)
const char* bt_get_connected_device(void);

// Get RSSI of connected device
int bt_get_rssi(void);

#ifdef __cplusplus
}
#endif

#endif // BLUETOOTH_TRANSFER_H
