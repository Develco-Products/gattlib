/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2020 Olivier Martin <olivier@labapart.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __GATTLIB_H__
#define __GATTLIB_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#ifndef BDADDR_BREDR
  /* GattLib note: BD Address have only been introduced into Bluez v4.100.   */
  /*               Prior to this version, only BDADDR_BREDR can be supported */

  /* BD Address type */
  #define BDADDR_BREDR           0x00
  #define BDADDR_LE_PUBLIC       0x01
  #define BDADDR_LE_RANDOM       0x02
#endif

#if BLUEZ_VERSION_MAJOR == 5
  #define ATT_MAX_MTU ATT_MAX_VALUE_LEN
#endif

/**
 * @name Gattlib errors
 */
//@{
#define GATTLIB_SUCCESS             0
#define GATTLIB_INVALID_PARAMETER   1
#define GATTLIB_NOT_FOUND           2
#define GATTLIB_OUT_OF_MEMORY       3
#define GATTLIB_NOT_SUPPORTED       4
#define GATTLIB_DEVICE_ERROR        5
#define GATTLIB_ERROR_DBUS          6
#define GATTLIB_ERROR_BLUEZ         7
#define GATTLIB_ERROR_INTERNAL      8
#define GATTLIB_NOT_CONNECTED       9
#define GATTLIB_BUSY               10
//@}

/**
 * @name GATT Characteristic Properties Bitfield values
 */
//@{
#define GATTLIB_CHARACTERISTIC_BROADCAST			0x01
#define GATTLIB_CHARACTERISTIC_READ					0x02
#define GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP	0x04
#define GATTLIB_CHARACTERISTIC_WRITE				0x08
#define GATTLIB_CHARACTERISTIC_NOTIFY				0x10
#define GATTLIB_CHARACTERISTIC_INDICATE				0x20
//@}

/**
 * @name GATT write option values
 */
//@{
#define BLUEZ_GATT_WRITE_VALUE_TYPE_MASK                    (0x7)
#define BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE     (1 << 0)
#define BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITHOUT_RESPONSE  (1 << 1)
#define BLUEZ_GATT_WRITE_VALUE_TYPE_RELIABLE_WRITE          (1 << 2)
//@}

/**
 * Helper function to create UUID16 from a 16bit integer
 */
#define CREATE_UUID16(value16) (uuid_t){ .type=SDP_UUID16, .value.uuid16=(value16) }

/**
 * @name Options for gattlib_connect()
 *
 * @note Options with the prefix `GATTLIB_CONNECTION_OPTIONS_LEGACY_`
 *       is for Bluez prior to v5.42 (before Bluez) support
 */
//@{
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC  (1 << 0)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM  (1 << 1)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW        (1 << 2)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_MEDIUM     (1 << 3)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_HIGH       (1 << 4)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_PSM(value)        (((value) & 0x3FF) << 11) //< We encode PSM on 10 bits (up to 1023)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_MTU(value)        (((value) & 0x3FF) << 21) //< We encode MTU on 10 bits (up to 1023)

#define GATTLIB_CONNECTION_OPTIONS_LEGACY_GET_PSM(options)  (((options) >> 11) && 0x3FF)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_GET_MTU(options)  (((options) >> 21) && 0x3FF)

#define GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT \
		GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC | \
		GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM | \
		GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW
//@}

/**
 * @name Discover filter
 */
//@{
#define GATTLIB_DISCOVER_FILTER_USE_NONE                    0
#define GATTLIB_DISCOVER_FILTER_USE_UUID                    (1 << 0)
#define GATTLIB_DISCOVER_FILTER_USE_RSSI                    (1 << 1)
#define GATTLIB_DISCOVER_FILTER_NOTIFY_CHANGE               (1 << 2)
//@}

/**
 * @name Gattlib Eddystone types
 */
//@{
#define GATTLIB_EDDYSTONE_TYPE_UID                          (1 << 0)
#define GATTLIB_EDDYSTONE_TYPE_URL                          (1 << 1)
#define GATTLIB_EDDYSTONE_TYPE_TLM                          (1 << 2)
#define GATTLIB_EDDYSTONE_TYPE_EID                          (1 << 3)
#define GATTLIB_EDDYSTONE_LIMIT_RSSI                        (1 << 4)
//@}

/**
 * @name Eddystone ID types defined by its specification: https://github.com/google/eddystone
 */
//@{
#define EDDYSTONE_TYPE_UID                                  0x00
#define EDDYSTONE_TYPE_URL                                  0x10
#define EDDYSTONE_TYPE_TLM                                  0x20
#define EDDYSTONE_TYPE_EID                                  0x30
//@}


typedef struct _gatt_connection_t gatt_connection_t;
typedef struct _gatt_stream_t gatt_stream_t;

/**
 * Structure to represent a GATT Service and its data in the BLE advertisement packet
 */
typedef struct {
	uuid_t   uuid;         /**< UUID of the GATT Service */
	uint8_t* data;         /**< Data attached to the GATT Service */
	size_t   data_length;  /**< Length of data attached to the GATT Service */
} gattlib_advertisement_data_t;

typedef void (*gattlib_event_handler_t)(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data);

/**
 * @brief Handler called on disconnection
 *
 * @param connection Connection that is disconnecting
 * @param user_data  Data defined when calling `gattlib_register_on_disconnect()`
 */
typedef void (*gattlib_disconnection_handler_t)(void* user_data);

/**
 * @brief Handler called on new discovered BLE device
 *
 * @param adapter is the adapter that has found the BLE device
 * @param addr is the MAC address of the BLE device
 * @param name is the name of BLE device if advertised
 * @param user_data  Data defined when calling `gattlib_register_on_disconnect()`
 */
typedef void (*gattlib_discovered_device_t)(void *adapter, const char* addr, const char* name, void *user_data);

/**
 * @brief Handler called on new discovered BLE device
 *
 * @param adapter is the adapter that has found the BLE device
 * @param addr is the MAC address of the BLE device
 * @param name is the name of BLE device if advertised
 * @param advertisement_data is an array of Service UUID and their respective data
 * @param advertisement_data_count is the number of elements in the advertisement_data array
 * @param manufacturer_id is the ID of the Manufacturer ID
 * @param manufacturer_data is the data following Manufacturer ID
 * @param manufacturer_data_size is the size of manufacturer_data
 * @param user_data  Data defined when calling `gattlib_register_on_disconnect()`
 */
typedef void (*gattlib_discovered_device_with_data_t)(void *adapter, const char* addr, const char* name,
		gattlib_advertisement_data_t *advertisement_data, size_t advertisement_data_count,
		uint16_t manufacturer_id, uint8_t *manufacturer_data, size_t manufacturer_data_size,
		void *user_data);

/**
 * @brief Handler called on asynchronous connection when connection is ready
 *
 * @param connection Connection that is disconnecting
 * @param user_data  Data defined when calling `gattlib_register_on_disconnect()`
 */
typedef void (*gatt_connect_cb_t)(gatt_connection_t* connection, void* user_data);

/**
 * @brief Callback called when GATT characteristic read value has been received
 *
 * @param status is the result of the operation
 * @param buffer contains the value to read.
 * @param buffer_len Length of the read data
 *
 */
typedef void (*gatt_read_cb_t)(int status, void* user_data, const void *buffer, size_t buffer_len);

/**
 * @brief Callback called when GATT characteristic write value has been completed
 *
 * @param status is the result of the operation
 *
 */
typedef void (*gatt_write_cb_t)(int status, void* user_data);

/**
 * @brief Constant defining Eddystone common data UID in Advertisement data
 */
extern const uuid_t gattlib_eddystone_common_data_uuid;

/**
 * @brief List of prefix for Eddystone URL Scheme
 */
extern const char *gattlib_eddystone_url_scheme_prefix[];


/**
 * @brief Open Bluetooth adapter
 *
 * @param adapter_name    With value NULL, the default adapter will be selected.
 * @param adapter is the context of the newly opened adapter
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_adapter_open(const char* adapter_name, void** adapter);

/**
 * @brief Get Bluetooth adapter address
 *
 * @param adapter is the context of the newly opened adapter
 *
 * @return adapters bluetooth address on success or NULL on error
 */
const char *gattlib_adapter_get_address(void* adapter);

/**
 * @brief Enable Bluetooth scanning on a given adapter
 *
 * @param adapter is the context of the newly opened adapter
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_adapter_scan_enable(void* adapter);

/**
 * @brief Bluetooth scanning on a given adapter
 *
 * @param adapter is the context of the newly opened adapter
 * @param discovered_device_cb is the function callback called for each new Bluetooth device discovered
 * @param timeout defines the duration of the Bluetooth scanning. When timeout=0, we scan indefinitely.
 * @param user_data is the data passed to the callback `discovered_device_cb()`
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_adapter_scan(void* adapter, gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data);

/**
 * @brief Enable Bluetooth scanning on a given adapter
 *
 * @param adapter is the context of the newly opened adapter
 * @param uuid_list is a NULL-terminated list of UUIDs to filter. The rule only applies to advertised UUID.
 *        Returned devices would match any of the UUIDs of the list.
 * @param rssi_threshold is the imposed RSSI threshold for the returned devices.
 * @param enabled_filters defines the parameters to use for filtering. There are selected by using the macros
 *        GATTLIB_DISCOVER_FILTER_USE_UUID and GATTLIB_DISCOVER_FILTER_USE_RSSI.
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_adapter_scan_enable_with_filter(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters);

/**
 * @brief Bluetooth scanning on a given adapter
 *
 * @param adapter is the context of the newly opened adapter
 * @param uuid_list is a NULL-terminated list of UUIDs to filter. The rule only applies to advertised UUID.
 *        Returned devices would match any of the UUIDs of the list.
 * @param rssi_threshold is the imposed RSSI threshold for the returned devices.
 * @param enabled_filters defines the parameters to use for filtering. There are selected by using the macros
 *        GATTLIB_DISCOVER_FILTER_USE_UUID and GATTLIB_DISCOVER_FILTER_USE_RSSI.
 * @param discovered_device_cb is the function callback called for each new Bluetooth device discovered
 * @param timeout defines the duration of the Bluetooth scanning. When timeout=0, we scan indefinitely.
 * @param user_data is the data passed to the callback `discovered_device_cb()`
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_adapter_scan_enable_with_filter(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters);
int gattlib_adapter_scan_with_filter(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
		gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data);

/**
 * @brief Enable Eddystone Bluetooth Device scanning on a given adapter
 *
 * @param adapter is the context of the newly opened adapter
 * @param rssi_threshold is the imposed RSSI threshold for the returned devices.
 * @param eddystone_types defines the type(s) of Eddystone advertisement data type to select.
 *        The types are defined by the macros `GATTLIB_EDDYSTONE_TYPE_*`. The macro `GATTLIB_EDDYSTONE_LIMIT_RSSI`
 *        can also be used to limit RSSI with rssi_threshold.
 * @param discovered_device_cb is the function callback called for each new Bluetooth device discovered
 * @param timeout defines the duration of the Bluetooth scanning. When timeout=0, we scan indefinitely.
 * @param user_data is the data passed to the callback `discovered_device_cb()`
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_adapter_scan_eddystone(void *adapter, int16_t rssi_threshold, uint32_t eddystone_types,
		gattlib_discovered_device_with_data_t discovered_device_cb, size_t timeout, void *user_data);

/**
 * @brief Disable Bluetooth scanning on a given adapter
 *
 * @param adapter is the context of the newly opened adapter
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_adapter_scan_disable(void* adapter);

/**
 * @brief Close Bluetooth adapter context
 *
 * @param adapter is the context of the newly opened adapter
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_adapter_close(void* adapter);

/* TBD */
void gattlib_register_default_agent(void);

/**
 * @brief Function to connect to a BLE device
 *
 * @param adapter	Local Adaptater interface. When passing NULL, we use default adapter.
 * @param dst		Remote Bluetooth address
 * @param options	Options to connect to BLE device. See `GATTLIB_CONNECTION_OPTIONS_*`
 */
gatt_connection_t *gattlib_connect(void *adapter, const char *dst, unsigned long options);

/**
 * @brief Function to asynchronously connect to a BLE device
 *
 * @note This function is mainly used before Bluez v5.42 (prior to D-BUS support)
 *
 * @param adapter	Local Adaptater interface. When passing NULL, we use default adapter.
 * @param dst		Remote Bluetooth address
 * @param options	Options to connect to BLE device. See `GATTLIB_CONNECTION_OPTIONS_*`
 * @param connect_cb is the callback to call when the connection is established
 * @param user_data is the user specific data to pass to the callback
 */
gatt_connection_t *gattlib_connect_async(void *adapter, const char *dst,
		unsigned long options,
		gatt_connect_cb_t connect_cb, void* user_data);

/**
 * @brief Function to disconnect the GATT connection
 *
 * @param connection Active GATT connection
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_disconnect(gatt_connection_t* connection);

/**
 * @brief Function to register a callback on GATT disconnection
 *
 * @param connection Active GATT connection
 * @param handler is the callaback to invoke on disconnection
 * @param user_data is user specific data to pass to the callaback
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
void gattlib_register_on_disconnect(gatt_connection_t *connection, gattlib_disconnection_handler_t handler, void* user_data);

/**
 * Structure to represent GATT Primary Service
 */
typedef struct {
	uint16_t  attr_handle_start; /**< First attribute handle of the GATT Primary Service */
	uint16_t  attr_handle_end;   /**< Last attibute handle of the GATT Primary Service */
	uuid_t    uuid;              /**< UUID of the Primary Service */
} gattlib_primary_service_t;

/**
 * Structure to represent GATT Characteristic
 */
typedef struct {
	uint16_t  handle;        /**< Handle of the GATT characteristic */
	uint8_t   properties;    /**< Property of the GATT characteristic */
	uint16_t  value_handle;  /**< Handle for the value of the GATT characteristic */
	uuid_t    uuid;          /**< UUID of the GATT characteristic */
} gattlib_characteristic_t;

/**
 * Structure to represent GATT Descriptor
 */
typedef struct {
	uint16_t handle;        /**< Handle of the GATT Descriptor */
	uint16_t uuid16;        /**< UUID16 of the GATT Descriptor */
	uuid_t   uuid;          /**< UUID of the GATT Descriptor */
} gattlib_descriptor_t;

/**
 * @brief Function to discover GATT Services
 *
 * @note This function can be used to force GATT services/characteristic discovery
 *
 * @param connection Active GATT connection
 * @param services array of GATT services allocated by the function. Can be NULL.
 * @param services_count Number of GATT services discovered. Can be NULL
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count);
int gattlib_discover_primary_from_mac(void* adapter, const char *mac_address, gattlib_primary_service_t** services, int* services_count);

/**
 * @brief Function to discover GATT Characteristic
 *
 * @note This function can be used to force GATT services/characteristic discovery
 *
 * @param connection Active GATT connection
 * @param start is the index of the first handle of the range
 * @param end is the index of the last handle of the range
 * @param characteristics array of GATT characteristics allocated by the function. Can be NULL.
 * @param characteristics_count Number of GATT characteristics discovered. Can be NULL
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_discover_char_range(gatt_connection_t* connection, int start, int end, gattlib_characteristic_t** characteristics, int* characteristics_count);

/**
 * @brief Function to discover GATT Characteristic
 *
 * @note This function can be used to force GATT services/characteristic discovery
 *
 * @param connection Active GATT connection
 * @param characteristics array of GATT characteristics allocated by the function. Can be NULL.
 * @param characteristics_count Number of GATT characteristics discovered. Can be NULL
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristics_count);
int gattlib_discover_char_from_mac(void* adapter, const char *mac_address, gattlib_characteristic_t** characteristics, int* characteristics_count);

/**
 * @brief Function to discover GATT Descriptors in a range of handles
 *
 * @param connection Active GATT connection
 * @param start is the index of the first handle of the range
 * @param end is the index of the last handle of the range
 * @param descriptors array of GATT descriptors allocated by the function. Can be NULL.
 * @param descriptors_count Number of GATT descriptors discovered. Can be NULL
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_discover_desc_range(gatt_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptors_count);

/**
 * @brief Function to discover GATT Descriptor
 *
 * @param connection Active GATT connection
 * @param descriptors array of GATT descriptors allocated by the function. Can be NULL.
 * @param descriptors_count Number of GATT descriptors discovered. Can be NULL
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_discover_desc(gatt_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptors_count);
int gattlib_discover_desc_from_mac(void* adapter, const char *mac_address, gattlib_descriptor_t** descriptors, int* descriptors_count);

/**
 * @brief Function to read GATT characteristic
 *
 * @note buffer is allocated by the function. It is the responsibility of the caller to free the buffer.
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the GATT characteristic to read
 * @param buffer contains the value to read. It is allocated by the function.
 * @param buffer_len Length of the read data
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_read_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, void** buffer, size_t* buffer_len);

/**
 * @brief Function to read GATT characteristic
 *
 * @note buffer is allocated by the function. It is the responsibility of the caller to free the buffer.
 *
 * @param adapter is the gattlib adapter to use
 * @param mac_address is the address of the device
 * @param handle is the handle of the characteristic
 * @param buffer contains the value to read. It is allocated by the function.
 * @param buffer_len Length of the read data
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_read_by_handle_from_mac(void* adapter, const char *mac_address, uint16_t handle, void** buffer, size_t* buffer_len);

/**
 * @brief Function to read GATT characteristic
 *
 * @note buffer is allocated by the function. It is the responsibility of the caller to free the buffer.
 *
 * @param adapter is the gattlib adapter to use
 * @param mac_address is the address of the device
 * @param handle is the handle of the characteristic
 * @param user_data is the user data to include in the callback
 * @param cb is the callback to be called when read is complete
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
void gattlib_read_by_handle_from_mac_async(void* adapter, const char *mac_address, uint16_t handle, void *user_data, gatt_read_cb_t cb);

/**
 * @brief Function to write to the GATT characteristic UUID
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the GATT characteristic to read
 * @param buffer contains the values to write to the GATT characteristic
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len);

/**
 * @brief Function to write to the GATT characteristic handle
 *
 * @param connection Active GATT connection
 * @param handle is the handle of the GATT characteristic
 * @param buffer contains the values to write to the GATT characteristic
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len);

/**
 * @brief Function to write to the GATT characteristic handle
 *
 * @param adapter is the gattlib adapter to use
 * @param mac_address is the address of the device
 * @param handle is the handle of the GATT characteristic
 * @param buffer contains the values to write to the GATT characteristic
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_by_handle_from_mac(void *adapter, const char *mac_address, uint16_t handle, const void* buffer, size_t buffer_len);
//TBD
void gattlib_write_by_handle_from_mac_async(void *adapter, const char *mac_address, uint16_t handle, const void* buffer, size_t buffer_len, void *user_data, gatt_write_cb_t cb);

/**
 * @brief Function to write without response to the GATT characteristic UUID
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the GATT characteristic to read
 * @param buffer contains the values to write to the GATT characteristic
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_without_response_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len);

/**
 * @brief Create a stream to a GATT characteristic to write data in continue
 *
 * @note: The GATT characteristic must support 'Write-Without-Response'
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the GATT characteristic to write
 * @param stream is the object that is attached to the GATT characteristic that is used to write data to
 * @param mtu is the MTU of the GATT connection to optimise the stream writting
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_char_by_uuid_stream_open(gatt_connection_t* connection, uuid_t* uuid, gatt_stream_t **stream, uint16_t *mtu);

/**
 * @brief Write data to the stream previously created with `gattlib_write_char_by_uuid_stream_open()`
 *
 * @param stream is the object that is attached to the GATT characteristic that is used to write data to
 * @param buffer is the data to write to the stream
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_char_stream_write(gatt_stream_t *stream, const void *buffer, size_t buffer_len);

/**
 * @brief Close the stream previously created with `gattlib_write_char_by_uuid_stream_open()`
 *
 * @param stream is the object that is attached to the GATT characteristic that is used to write data to
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_char_stream_close(gatt_stream_t *stream);

/**
 * @brief Function to write without response to the GATT characteristic handle
 *
 * @param connection Active GATT connection
 * @param handle is the handle of the GATT characteristic
 * @param buffer contains the values to write to the GATT characteristic
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_without_response_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len);

/*
 * @brief Enable notification on GATT characteristic represented by its UUID
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the characteristic that will trigger the notification
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_notification_start(gatt_connection_t* connection, const uuid_t* uuid);

/*
 * @brief Disable notification on GATT characteristic represented by its UUID
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the characteristic that will trigger the notification
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_notification_stop(gatt_connection_t* connection, const uuid_t* uuid);

/*
 * @brief Enable indication on GATT characteristic represented by its UUID
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the characteristic that will trigger the indication
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_indication_start(gatt_connection_t* connection, const uuid_t* uuid);

/*
 * @brief Disable indication on GATT characteristic represented by its UUID
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the characteristic that will trigger the indication
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_indication_stop(gatt_connection_t* connection, const uuid_t* uuid);

/*
 * @brief Register a handle for the GATT notifications
 *
 * @param connection Active GATT connection
 * @param notification_handler is the handler to call on notification
 * @param user_data if the user specific data to pass to the handler
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
void gattlib_register_notification(gatt_connection_t* connection, gattlib_event_handler_t notification_handler, void* user_data);

/*
 * @brief Register a handle for the GATT indications
 *
 * @param connection Active GATT connection
 * @param notification_handler is the handler to call on indications
 * @param user_data if the user specific data to pass to the handler
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
void gattlib_register_indication(gatt_connection_t* connection, gattlib_event_handler_t indication_handler, void* user_data);

/*
 * @brief Checks whether the device has a public or randomly assigned address
 *
 * @param adapter is the adapter to where the device has been seen
 * @param mac_address is the MAC address of the device to look up
 *
 * @return TRUE if device has a public address. FALSE on error, or if device has randomly assigned address
 */
bool gattlib_is_public_address_type_from_mac(void *adapter, const char *mac_address);

/*
 * @brief Checks whether the device is connected
 *
 * @param adapter is the adapter to where the device has been seen
 * @param mac_address is the MAC address of the device to look up
 *
 * @return TRUE if device is connected. FALSE on error, or if device is not connected
 */
bool gattlib_is_connected_from_mac(void *adapter, const char *mac_address);

/*
 * @brief Checks whether the services has been resolved for the device
 *
 * @param adapter is the adapter to where the device has been seen
 * @param mac_address is the MAC address of the device to look up
 *
 * @return TRUE if service is resolved. FALSE on error, or if services is not resolved.
 */
bool gattlib_is_services_resolved_from_mac(void *adapter, const char *mac_address);

#if 0 // Disable until https://github.com/labapart/gattlib/issues/75 is resolved
/**
 * @brief Function to retrieve RSSI from a GATT connection
 *
 * @param connection Active GATT connection
 * @param rssi is the Received Signal Strength Indicator of the remote device
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_get_rssi(gatt_connection_t *connection, int16_t *rssi);
#endif

/**
 * @brief Function to retrieve RSSI from a MAC Address
 *
 * @note: This function is mainly used before a connection is established. Once the connection
 * established, the function `gattlib_get_rssi()` should be preferred.
 *
 * @param adapter is the adapter the new device has been seen
 * @param mac_address is the MAC address of the device to get the RSSI
 * @param rssi is the Received Signal Strength Indicator of the remote device
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_get_rssi_from_mac(void *adapter, const char *mac_address, int16_t *rssi);

/**
 * @brief Function to retrieve Advertising Flags from a MAC Address
 *
 * @note: This function uses an experimental D-Bus API
 *
 * @param adapter is the adapter the new device has been seen
 * @param mac_address is the MAC address of the device to get the RSSI
 * @param flags is the flags from the devices AD entry with type 0x01
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_get_advertising_flags_from_mac(void *adapter, const char *mac_address, char *flags);

/**
 * @brief Function to retrieve raw Advertising Data from a MAC Address
 *
 * @note: This function uses an experimental D-Bus API
 *
 * @param adapter is the adapter the new device has been seen
 * @param mac_address is the MAC address of the device to get the RSSI
 * @param out is the buffer that the AD entries are packed into
 * @param out_size is the size of AD entries added to out
 * @param max_out is the maximum size added to out
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_get_raw_advertising_data_from_mac(void *adapter, const char *mac_address, uint8_t *out, size_t *out_size, size_t max_out);

/**
 * @brief Function to retrieve Advertisement Data from a MAC Address
 *
 * @param connection Active GATT connection
 * @param advertisement_data is an array of Service UUID and their respective data
 * @param advertisement_data_count is the number of elements in the advertisement_data array
 * @param manufacturer_id is the ID of the Manufacturer ID
 * @param manufacturer_data is the data following Manufacturer ID
 * @param manufacturer_data_size is the size of manufacturer_data
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_get_advertisement_data(gatt_connection_t *connection,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size);

/**
 * @brief Function to retrieve Advertisement Data from a MAC Address
 *
 * @param adapter is the adapter the new device has been seen
 * @param mac_address is the MAC address of the device to get the RSSI
 * @param advertisement_data is an array of Service UUID and their respective data
 * @param advertisement_data_count is the number of elements in the advertisement_data array
 * @param manufacturer_id is the ID of the Manufacturer ID
 * @param manufacturer_data is the data following Manufacturer ID
 * @param manufacturer_data_size is the size of manufacturer_data
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_get_advertisement_data_from_mac(void *adapter, const char *mac_address,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size);

/**
 * @brief Function to process pending glib events
 */
void gattlib_process_events(void);

/**
 * @brief Function to add a callback when services_resolved has been updated for a device
 *
 * @param adapter is the adapter the new device has been seen
 * @param mac_address is the MAC address of the device
 * @param cb is the callback to be called when services_resolved is updated for the device
 *
 * @return handle to be used for removing the callback once it is no longer needed
 */
typedef void (*services_resolved_cb)(const char* mac_address, bool is_public_address, bool resolved);
void* gattlib_add_services_resolved_cb(void* adapter, const char *mac, services_resolved_cb cb);

/**
 * @brief Function to remove a services resolved callback again
 *
 * @param handle is the handle received when adding the callback
*/
void gattlib_remove_services_resolved_cb(void* handle);


/**
 * @brief Convert a UUID into a string
 *
 * @param uuid is the UUID to convert
 * @param str is the buffer that will contain the string
 * @param size is the size of the buffer
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_uuid_to_string(const uuid_t *uuid, char *str, size_t size);

/**
 * @brief Convert a string representing a UUID into a UUID structure
 *
 * @param str is the buffer containing the string
 * @param size is the size of the buffer
 * @param uuid is the UUID structure that would receive the UUID
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_string_to_uuid(const char *str, size_t size, uuid_t *uuid);

/**
 * @brief Compare two UUIDs
 *
 * @param uuid1 is the one of the UUID to compare with
 * @param uuid2 is the other UUID to compare with
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_uuid_cmp(const uuid_t *uuid1, const uuid_t *uuid2);

#ifdef __cplusplus
}
#endif

#endif
