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

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "gattlib_internal.h"

#define CONNECT_TIMEOUT  4

static const char *m_dbus_error_unknown_object = "GDBus.Error:org.freedesktop.DBus.Error.UnknownObject";

gboolean on_handle_device_property_change(
	    OrgBluezGattCharacteristic1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	gatt_connection_t* connection = user_data;
	gattlib_context_t* conn_context = connection->context;

	// Retrieve 'Value' from 'arg_changed_properties'
	if (g_variant_n_children (arg_changed_properties) > 0) {
		GVariantIter *iter;
		const gchar *key;
		GVariant *value;

		g_variant_get (arg_changed_properties, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
			if (strcmp(key, "Connected") == 0) {
				if (!g_variant_get_boolean(value)) {
					// Disconnection case
					if (gattlib_has_valid_handler(&connection->disconnection)) {
						gattlib_call_disconnection_handler(&connection->disconnection);
					}
				}
			} else if (strcmp(key, "ServicesResolved") == 0) {
				if (g_variant_get_boolean(value)) {
					// Stop the timeout for connection
					g_source_remove(conn_context->connection_timeout);

					// Tell we are now connected
					g_main_loop_quit(conn_context->connection_loop);
				}
			}
		}
		g_variant_iter_free(iter);
	}
	return TRUE;
}

void get_device_path_from_mac_with_adapter(OrgBluezAdapter1* adapter, const char *mac_address, char *object_path, size_t object_path_len)
{
	char device_address_str[20 + 1];
	const char* adapter_path = g_dbus_proxy_get_object_path((GDBusProxy *)ORG_BLUEZ_ADAPTER1_PROXY(adapter));

	// Transform string from 'DA:94:40:95:E0:87' to 'dev_DA_94_40_95_E0_87'
	strncpy(device_address_str, mac_address, sizeof(device_address_str) - 1);
	for (int i = 0; i < strlen(device_address_str); i++) {
		if (device_address_str[i] == ':') {
			device_address_str[i] = '_';
		}
	}

	// Force a null-terminated character
	device_address_str[20] = '\0';

	// Generate object path like: /org/bluez/hci0/dev_DA_94_40_95_E0_87
	snprintf(object_path, object_path_len, "%s/dev_%s", adapter_path, device_address_str);
}


void get_device_path_from_mac(const char *adapter_name, const char *mac_address, char *object_path, size_t object_path_len)
{
	char device_address_str[20 + 1];
	const char* adapter;

	if (adapter_name) {
		adapter = adapter_name;
	} else {
		adapter = "hci0";
	}

	// Transform string from 'DA:94:40:95:E0:87' to 'dev_DA_94_40_95_E0_87'
	strncpy(device_address_str, mac_address, sizeof(device_address_str) - 1);
	for (int i = 0; i < strlen(device_address_str); i++) {
		if (device_address_str[i] == ':') {
			device_address_str[i] = '_';
		}
	}

	// Force a null-terminated character
	device_address_str[20] = '\0';

	// Generate object path like: /org/bluez/hci0/dev_DA_94_40_95_E0_87
	snprintf(object_path, object_path_len, "/org/bluez/%s/dev_%s", adapter, device_address_str);
}

/**
 * @param src		Local Adaptater interface
 * @param dst		Remote Bluetooth address
 * @param dst_type	Set LE address type (either BDADDR_LE_PUBLIC or BDADDR_LE_RANDOM)
 * @param sec_level	Set security level (either BT_IO_SEC_LOW, BT_IO_SEC_MEDIUM, BT_IO_SEC_HIGH)
 * @param psm       Specify the PSM for GATT/ATT over BR/EDR
 * @param mtu       Specify the MTU size
 */
gatt_connection_t *gattlib_connect(void* adapter, const char *dst, unsigned long options)
{
	struct gattlib_adapter *gattlib_adapter = adapter;
	const char* adapter_name = NULL;
	GDBusObjectManager *device_manager;
	GError *error = NULL;
	char object_path[100];

	// In case NULL is passed, we initialized default adapter
	if (gattlib_adapter == NULL) {
		gattlib_adapter = init_default_adapter();
	} else {
		adapter_name = gattlib_adapter->adapter_name;
	}

	get_device_path_from_mac(adapter_name, dst, object_path, sizeof(object_path));

	gattlib_context_t* conn_context = calloc(sizeof(gattlib_context_t), 1);
	if (conn_context == NULL) {
		return NULL;
	}
	conn_context->adapter = gattlib_adapter;

	gatt_connection_t* connection = calloc(sizeof(gatt_connection_t), 1);
	if (connection == NULL) {
		goto FREE_CONN_CONTEXT;
	} else {
		connection->context = conn_context;
	}

	OrgBluezDevice1* device = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			&error);
	if (device == NULL) {
		if (error) {
			fprintf(stderr, "Failed to connect to DBus Bluez Device: %s\n", error->message);
			g_error_free(error);
		}
		goto FREE_CONNECTION;
	} else {
		conn_context->device = device;
		conn_context->device_object_path = strdup(object_path);
	}

	// Register a handle for notification
	g_signal_connect(device,
		"g-properties-changed",
		G_CALLBACK (on_handle_device_property_change),
		connection);

	error = NULL;
	org_bluez_device1_call_connect_sync(device, NULL, &error);
	if (error) {
		if (strncmp(error->message, m_dbus_error_unknown_object, strlen(m_dbus_error_unknown_object)) == 0) {
			// You might have this error if the computer has not scanned or has not already had
			// pairing information about the targetted device.
			fprintf(stderr, "Device '%s' cannot be found\n", dst);
		}  else {
			fprintf(stderr, "Device connected error (device:%s): %s\n",
				conn_context->device_object_path,
				error->message);
		}

		g_error_free(error);
		goto FREE_DEVICE;
	}

	// Wait for the property 'UUIDs' to be changed. We assume 'org.bluez.GattService1
	// and 'org.bluez.GattCharacteristic1' to be advertised at that moment.
	conn_context->connection_loop = g_main_loop_new(NULL, 0);

	conn_context->connection_timeout = g_timeout_add_seconds(CONNECT_TIMEOUT, stop_scan_func,
								 conn_context->connection_loop);
	g_main_loop_run(conn_context->connection_loop);
	g_main_loop_unref(conn_context->connection_loop);
	// Set the attribute to NULL even if not required
	conn_context->connection_loop = NULL;

	// Get list of objects belonging to Device Manager
	device_manager = get_device_manager_from_adapter(conn_context->adapter);
	conn_context->dbus_objects = g_dbus_object_manager_get_objects(device_manager);

	return connection;

FREE_DEVICE:
	free(conn_context->device_object_path);
	g_object_unref(conn_context->device);

FREE_CONNECTION:
	free(connection);

FREE_CONN_CONTEXT:
	free(conn_context);
	return NULL;
}

gatt_connection_t *gattlib_connect_async(void *adapter, const char *dst,
				unsigned long options,
				gatt_connect_cb_t connect_cb, void* data)
{
	gatt_connection_t *connection;

	connection = gattlib_connect(adapter, dst, options);
	if ((connection != NULL) && (connect_cb != NULL)) {
		connect_cb(connection, data);
	}

	return connection;
}

int gattlib_disconnect(gatt_connection_t* connection) {
	gattlib_context_t* conn_context = connection->context;
	GError *error = NULL;

	org_bluez_device1_call_disconnect_sync(conn_context->device, NULL, &error);
	if (error) {
		fprintf(stderr, "Failed to disconnect DBus Bluez Device: %s\n", error->message);
		g_error_free(error);
	}

	free(conn_context->device_object_path);
	g_object_unref(conn_context->device);
	g_list_free_full(conn_context->dbus_objects, g_object_unref);
	disconnect_all_notifications(conn_context);

	free(connection->context);
	free(connection);
	return GATTLIB_SUCCESS;
}

static void sort_services(gattlib_primary_service_t *services, int services_count) {
	for(int i=0; i<services_count-1; i++) {
		uint16_t smallestHandle = services[i].attr_handle_start;
		int smallestIdx = i;
		for(int j=i+1; j<services_count; j++) {
			if(services[j].attr_handle_start < smallestHandle) {
				smallestHandle = services[j].attr_handle_start;
				smallestIdx    = j;
			}
		}
		// swap i and smallestIdx
		gattlib_primary_service_t tmp = services[i];
		services[i] = services[smallestIdx];
		services[smallestIdx] = tmp;
	}
}

int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count) {
	gattlib_context_t* conn_context = connection->context;
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(conn_context->adapter);
	OrgBluezDevice1* device = conn_context->device;
	const gchar* const* service_str;
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	const gchar* const* service_strs = org_bluez_device1_get_uuids(device);

	if (device_manager == NULL) {
		fprintf(stderr, "Gattlib context not initialized.\n");
		return GATTLIB_INVALID_PARAMETER;
	}

	if (service_strs == NULL) {
		if (services != NULL) {
			*services       = NULL;
		}
		if (services_count != NULL) {
			*services_count = 0;
		}
		return GATTLIB_SUCCESS;
	}

	// Maximum number of primary services
	int count_max = 0, count = 0;
	for (service_str = service_strs; *service_str != NULL; service_str++) {
		count_max++;
	}

	gattlib_primary_service_t* primary_services = malloc(count_max * sizeof(gattlib_primary_service_t));
	if (primary_services == NULL) {
		return GATTLIB_OUT_OF_MEMORY;
	}

	for (GList *l = conn_context->dbus_objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattService1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (service_proxy == NULL) {
			if (error) {
				fprintf(stderr, "Failed to open service '%s': %s\n", object_path, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open service '%s'.\n", object_path);
			}
			continue;
		}

		// Ensure the service is attached to this device
		if (strcmp(conn_context->device_object_path, org_bluez_gatt_service1_get_device(service_proxy))) {
			g_object_unref(service_proxy);
			continue;
		}

		if (org_bluez_gatt_service1_get_primary(service_proxy)) {
			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024'.
			// We convert the last 4 hex characters into the handle
			int service_handle;
			sscanf(object_path + strlen(object_path) - 4, "%x", &service_handle);

			primary_services[count].attr_handle_start = service_handle;
			primary_services[count].attr_handle_end   = service_handle;

			for (GList *m = conn_context->dbus_objects; m != NULL; m = m->next)  {
				GDBusObject *characteristic_object = m->data;
				const char* characteristic_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(characteristic_object));
				if (strncmp(object_path, characteristic_path, strlen(object_path)) != 0) {
					continue;
				}

				interface = g_dbus_object_manager_get_interface(device_manager, characteristic_path, "org.bluez.GattCharacteristic1");
				if (!interface) {
					continue;
				} else {
					int char_handle;

					g_object_unref(interface);

					// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
					// We convert the last 4 hex characters into the handle
					sscanf(characteristic_path + strlen(characteristic_path) - 4, "%x", &char_handle);

					primary_services[count].attr_handle_start = MIN(primary_services[count].attr_handle_start, char_handle  );
					primary_services[count].attr_handle_end   = MAX(primary_services[count].attr_handle_end,   char_handle+1);
				}
			}

			gattlib_string_to_uuid(
					org_bluez_gatt_service1_get_uuid(service_proxy),
					MAX_LEN_UUID_STR + 1,
					&primary_services[count].uuid);
			count++;
		}

		g_object_unref(service_proxy);
	}

	if (services != NULL) {
		sort_services(primary_services, count);
		*services       = primary_services;
	}
	if (services_count != NULL) {
		*services_count = count;
	}

	if (ret != GATTLIB_SUCCESS) {
		free(primary_services);
	}
	return ret;
}

int gattlib_discover_primary_from_mac(void* adapter, const char *mac_address, gattlib_primary_service_t** services, int* services_count) {
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(adapter);
	GList *dbus_objects = g_dbus_object_manager_get_objects(device_manager);
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;
	OrgBluezDevice1* device;
	gchar * device_object_path;
	gattlib_primary_service_t* primary_services = NULL;

	ret = get_bluez_device_from_mac(adapter, mac_address, &device);
	if(ret != GATTLIB_SUCCESS) {
		ret = GATTLIB_NOT_CONNECTED;
		goto FREE_OBJECTS;
	}
	g_object_get(G_OBJECT(device), "g-object-path", &device_object_path, NULL);

	if(!org_bluez_device1_get_services_resolved(device))
	{
		if(org_bluez_device1_get_connected(device)) {
			ret = GATTLIB_BUSY;
		} else {
			ret = GATTLIB_NOT_CONNECTED;
		}
		goto FREE_DEVICE;
	}

	const gchar* const* service_strs = org_bluez_device1_get_uuids(device);

	if (service_strs == NULL) {
		ret = GATTLIB_NOT_FOUND;
		goto FREE_DEVICE;
	}

	// Maximum number of primary services
	int count_max = 0, count = 0;
	for (const gchar* const* service_str = service_strs; *service_str != NULL; service_str++) {
		count_max++;
	}

	primary_services = malloc(count_max * sizeof(gattlib_primary_service_t));
	if (primary_services == NULL) {
		ret = GATTLIB_OUT_OF_MEMORY;
		goto FREE_DEVICE;
	}

	for (GList *l = dbus_objects; l != NULL; l = l->next)  {
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(l->data));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattService1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (service_proxy == NULL) {
			if (error) {
				fprintf(stderr, "Failed to open service '%s': %s\n", object_path, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open service '%s'.\n", object_path);
			}
			continue;
		}

		// Ensure the service is attached to this device
		if (strcmp(device_object_path, org_bluez_gatt_service1_get_device(service_proxy))) {
			g_object_unref(service_proxy);
			continue;
		}

		if (org_bluez_gatt_service1_get_primary(service_proxy)) {
			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024'.
			// We convert the last 4 hex characters into the handle
			int service_handle;
			sscanf(object_path + strlen(object_path) - 4, "%x", &service_handle);

			primary_services[count].attr_handle_start = service_handle;
			primary_services[count].attr_handle_end   = service_handle;

			for (GList *m = dbus_objects; m != NULL; m = m->next)  {
				const char* characteristic_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(m->data));
				if (strncmp(object_path, characteristic_path, strlen(object_path)) != 0) {
					continue;
				}

				interface = g_dbus_object_manager_get_interface(device_manager, characteristic_path, "org.bluez.GattCharacteristic1");
				if (!interface) {
					continue;
				} else {
					int char_handle;

					g_object_unref(interface);

					// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
					// We convert the last 4 hex characters into the handle
					sscanf(characteristic_path + strlen(characteristic_path) - 4, "%x", &char_handle);

					primary_services[count].attr_handle_start = MIN(primary_services[count].attr_handle_start, char_handle  );
					primary_services[count].attr_handle_end   = MAX(primary_services[count].attr_handle_end,   char_handle+1);
				}
			}

			gattlib_string_to_uuid(
					org_bluez_gatt_service1_get_uuid(service_proxy),
					MAX_LEN_UUID_STR + 1,
					&primary_services[count].uuid);
			count++;
		}

		g_object_unref(service_proxy);
	}

FREE_DEVICE:
	g_object_unref(device);
	g_free(device_object_path);
FREE_OBJECTS:
	g_list_free_full(dbus_objects, g_object_unref);

	if(ret != GATTLIB_SUCCESS) {
		if(primary_services)
			free(primary_services);
		*services       = NULL;
		*services_count = 0;
	} else {
		sort_services(primary_services, count);
		*services       = primary_services;
		*services_count = count;
	}

	return ret;
}

static void sort_characteristics(gattlib_characteristic_t *characteristics, int characteristics_count) {
	for(int i=0; i<characteristics_count-1; i++) {
		uint16_t smallestHandle = characteristics[i].handle;
		int smallestIdx = i;
		for(int j=i+1; j<characteristics_count; j++) {
			if(characteristics[j].handle < smallestHandle) {
				smallestHandle = characteristics[j].handle;
				smallestIdx    = j;
			}
		}
		// swap i and smallestIdx
		gattlib_characteristic_t tmp = characteristics[i];
		characteristics[i] = characteristics[smallestIdx];
		characteristics[smallestIdx] = tmp;
	}
}

int gattlib_discover_char_from_mac(void* adapter, const char *mac_address, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(adapter);
	GList *dbus_objects = g_dbus_object_manager_get_objects(device_manager);
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;
	OrgBluezDevice1* device;
	gchar *device_object_path;
	gattlib_characteristic_t* characteristic_list = NULL;

	ret = get_bluez_device_from_mac(adapter, mac_address, &device);
	if(ret != GATTLIB_SUCCESS) {
		ret = GATTLIB_NOT_CONNECTED;
		goto FREE_OBJECTS;
	}
	g_object_get(G_OBJECT(device), "g-object-path", &device_object_path, NULL);

	if(!org_bluez_device1_get_services_resolved(device))
	{
		if(org_bluez_device1_get_connected(device)) {
			ret = GATTLIB_BUSY;
		} else {
			ret = GATTLIB_NOT_CONNECTED;
		}
		goto FREE_DEVICE;
	}

	// Count the maximum number of characteristic to allocate the array
	int count_max = 0, count = 0;
	for (GList *l = dbus_objects; l != NULL; l = l->next)  {
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(l->data));
		if(strncmp(object_path, device_object_path, strlen(device_object_path)) != 0) {
			continue;
		}
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		count_max++;
	}

	characteristic_list = malloc(count_max * sizeof(gattlib_characteristic_t));
	if (characteristic_list == NULL) {
		ret = GATTLIB_OUT_OF_MEMORY;
		goto FREE_DEVICE;
	}

	for (GList *l = dbus_objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
		if(strncmp(object_path, device_object_path, strlen(device_object_path)) != 0) {
			continue;
		}
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		OrgBluezGattCharacteristic1* characteristic = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (characteristic == NULL) {
			if (error) {
				fprintf(stderr, "Failed to open characteristic '%s': %s\n", object_path, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open characteristic '%s'.\n", object_path);
			}
			continue;
		}

		int handle;

		// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
		// We convert the last 4 hex characters into the handle
		sscanf(object_path + strlen(object_path) - 4, "%x", &handle);

		characteristic_list[count].handle = handle;
		characteristic_list[count].value_handle = handle+1;
		characteristic_list[count].properties = 0;
		const gchar *const * flags = org_bluez_gatt_characteristic1_get_flags(characteristic);
		for (; *flags != NULL; flags++) {
			if (strcmp(*flags,"broadcast") == 0) {
				characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_BROADCAST;
			} else if (strcmp(*flags,"read") == 0) {
				characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_READ;
			} else if (strcmp(*flags,"write") == 0) {
				characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_WRITE;
			} else if (strcmp(*flags,"write-without-response") == 0) {
				characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP;
			} else if (strcmp(*flags,"notify") == 0) {
				characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_NOTIFY;
			} else if (strcmp(*flags,"indicate") == 0) {
				characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_INDICATE;
			}
		}

		gattlib_string_to_uuid(
				org_bluez_gatt_characteristic1_get_uuid(characteristic),
				MAX_LEN_UUID_STR + 1,
				&characteristic_list[count].uuid);
		count = count + 1;

		g_object_unref(characteristic);
	}

FREE_DEVICE:
	g_object_unref(device);
	g_free(device_object_path);
FREE_OBJECTS:
	g_list_free_full(dbus_objects, g_object_unref);

	if(ret != GATTLIB_SUCCESS) {
		if(characteristic_list)
			free(characteristic_list);
		*characteristics       = NULL;
		*characteristics_count = 0;
	} else {
		sort_characteristics(characteristic_list, count);
		*characteristics       = characteristic_list;
		*characteristics_count = count;
	}

	return ret;
}

static void add_characteristics_from_service(gattlib_context_t* conn_context, GDBusObjectManager *device_manager,
			const char* service_object_path,
			int start, int end,
			gattlib_characteristic_t* characteristic_list, int* count)
{
	GError *error = NULL;

	for (GList *l = conn_context->dbus_objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		OrgBluezGattCharacteristic1* characteristic = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (characteristic == NULL) {
			if (error) {
				fprintf(stderr, "Failed to open characteristic '%s': %s\n", object_path, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open characteristic '%s'.\n", object_path);
			}
			continue;
		}

		if (strcmp(org_bluez_gatt_characteristic1_get_service(characteristic), service_object_path)) {
			g_object_unref(characteristic);
			continue;
		} else {
			int handle;

			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
			// We convert the last 4 hex characters into the handle
			sscanf(object_path + strlen(object_path) - 4, "%x", &handle);

			// Check if handle is in range
			if ((handle < start) || (handle > end)) {
				continue;
			}

			characteristic_list[*count].handle = handle;
			characteristic_list[*count].value_handle = handle+1;
			characteristic_list[*count].properties = 0;
			const gchar *const * flags = org_bluez_gatt_characteristic1_get_flags(characteristic);
			for (; *flags != NULL; flags++) {
				if (strcmp(*flags,"broadcast") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_BROADCAST;
				} else if (strcmp(*flags,"read") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_READ;
				} else if (strcmp(*flags,"write") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_WRITE;
				} else if (strcmp(*flags,"write-without-response") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP;
				} else if (strcmp(*flags,"notify") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_NOTIFY;
				} else if (strcmp(*flags,"indicate") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_INDICATE;
				}
			}

			gattlib_string_to_uuid(
					org_bluez_gatt_characteristic1_get_uuid(characteristic),
					MAX_LEN_UUID_STR + 1,
					&characteristic_list[*count].uuid);
			*count = *count + 1;
		}

		g_object_unref(characteristic);
	}
}

int gattlib_discover_char_range(gatt_connection_t* connection, int start, int end, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	gattlib_context_t* conn_context = connection->context;
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(conn_context->adapter);
	GError *error = NULL;

	if (device_manager == NULL) {
		fprintf(stderr, "Gattlib context not initialized.\n");
		return GATTLIB_INVALID_PARAMETER;
	}

	// Count the maximum number of characteristic to allocate the array (we count all the characterstic for all devices)
	int count_max = 0, count = 0;
	for (GList *l = conn_context->dbus_objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		count_max++;
	}

	gattlib_characteristic_t* characteristic_list = malloc(count_max * sizeof(gattlib_characteristic_t));
	if (characteristic_list == NULL) {
		return GATTLIB_OUT_OF_MEMORY;
	}

	// List all services for this device
	for (GList *l = conn_context->dbus_objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattService1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (service_proxy == NULL) {
			if (error) {
				fprintf(stderr, "Failed to open service '%s': %s\n", object_path, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open service '%s'.\n", object_path);
			}
			continue;
		}

		// Ensure the service is attached to this device
		const char* service_object_path = org_bluez_gatt_service1_get_device(service_proxy);
		if (strcmp(conn_context->device_object_path, service_object_path)) {
			g_object_unref(service_proxy);
			continue;
		}

		// Add all characteristics attached to this service
		add_characteristics_from_service(conn_context, device_manager, object_path, start, end, characteristic_list, &count);
		g_object_unref(service_proxy);
	}

	sort_characteristics(characteristic_list, count);
	*characteristics       = characteristic_list;
	*characteristics_count = count;
	return GATTLIB_SUCCESS;
}

int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristics_count)
{
	return gattlib_discover_char_range(connection, 0x00, 0xFF, characteristics, characteristics_count);
}

int gattlib_discover_desc_range(gatt_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_discover_desc(gatt_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return GATTLIB_NOT_SUPPORTED;
}

static void sort_descriptors(gattlib_descriptor_t *descriptors, int descriptors_count) {
	for(int i=0; i<descriptors_count-1; i++) {
		uint16_t smallestHandle = descriptors[i].handle;
		int smallestIdx = i;
		for(int j=i+1; j<descriptors_count; j++) {
			if(descriptors[j].handle < smallestHandle) {
				smallestHandle = descriptors[j].handle;
				smallestIdx    = j;
			}
		}
		// swap i and smallestIdx
		gattlib_descriptor_t tmp = descriptors[i];
		descriptors[i] = descriptors[smallestIdx];
		descriptors[smallestIdx] = tmp;
	}
}

int gattlib_discover_desc_from_mac(void* adapter, const char *mac_address, gattlib_descriptor_t** descriptors, int* descriptors_count) {
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(adapter);
	GList *dbus_objects = g_dbus_object_manager_get_objects(device_manager);
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;
	OrgBluezDevice1* device;
	gchar *device_object_path;
	gattlib_descriptor_t* descriptor_list = NULL;
	gattlib_primary_service_t* services = NULL;
	gattlib_characteristic_t* characteristics = NULL;
	int services_count, characteristics_count;
	int count = 0;
	
	ret = gattlib_discover_primary_from_mac(adapter, mac_address, &services, &services_count);
	if(ret != GATTLIB_SUCCESS) {
		goto FREE_OBJECTS;
	}

	ret = gattlib_discover_char_from_mac(adapter, mac_address, &characteristics, &characteristics_count);
	if(ret != GATTLIB_SUCCESS) {
		goto FREE_OBJECTS;
	}

	ret = get_bluez_device_from_mac(adapter, mac_address, &device);
	if(ret != GATTLIB_SUCCESS) {
		ret = GATTLIB_NOT_CONNECTED;
		goto FREE_OBJECTS;
	}
	g_object_get(G_OBJECT(device), "g-object-path", &device_object_path, NULL);

	if(!org_bluez_device1_get_services_resolved(device))
	{
		if(org_bluez_device1_get_connected(device)) {
			ret = GATTLIB_BUSY;
		} else {
			ret = GATTLIB_NOT_CONNECTED;
		}
		goto FREE_DEVICE;
	}

	// Count the maximum number of descriptors to allocate the array
	int count_max = services_count + characteristics_count*2;
	for (GList *l = dbus_objects; l != NULL; l = l->next)  {
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(l->data));
		if(strncmp(object_path, device_object_path, strlen(device_object_path)) != 0) {
			continue;
		}
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattDescriptor1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		count_max++;
	}

	descriptor_list = malloc(count_max * sizeof(gattlib_descriptor_t));
	if (descriptor_list == NULL) {
		ret = GATTLIB_OUT_OF_MEMORY;
		goto FREE_DEVICE;
	}

	for(int i=0; i<services_count; i++) {
		descriptor_list[count].handle = services[i].attr_handle_start;
		descriptor_list[count].uuid16 = GATT_PRIM_SVC_UUID;
		descriptor_list[count].uuid   = CREATE_UUID16(GATT_PRIM_SVC_UUID);
		count++;
	}

	for(int i=0; i<characteristics_count; i++) {
		descriptor_list[count].handle = characteristics[i].handle;
		descriptor_list[count].uuid16 = GATT_CHARAC_UUID;
		descriptor_list[count].uuid   = CREATE_UUID16(GATT_CHARAC_UUID);
		count++;
		descriptor_list[count].handle = characteristics[i].value_handle;
		descriptor_list[count].uuid16 = 0xFFFF;
		descriptor_list[count].uuid   = characteristics[i].uuid;
		count++;
	}

	for (GList *l = dbus_objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
		if(strncmp(object_path, device_object_path, strlen(device_object_path)) != 0) {
			continue;
		}
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattDescriptor1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		OrgBluezGattDescriptor1* descriptor = org_bluez_gatt_descriptor1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (descriptor == NULL) {
			if (error) {
				fprintf(stderr, "Failed to open descriptor '%s': %s\n", object_path, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open descriptor '%s'.\n", object_path);
			}
			continue;
		}

		int handle;

		// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029/desc002b'.
		// We convert the last 4 hex characters into the handle
		sscanf(object_path + strlen(object_path) - 4, "%x", &handle);

		descriptor_list[count].handle = handle;

		gattlib_string_to_uuid(
				org_bluez_gatt_descriptor1_get_uuid(descriptor),
				MAX_LEN_UUID_STR + 1,
				&descriptor_list[count].uuid);
		descriptor_list[count].uuid16 = descriptor_list[count].uuid.value.uuid16;
		count = count + 1;

		g_object_unref(descriptor);
	}

FREE_DEVICE:
	g_object_unref(device);
	g_free(device_object_path);
FREE_OBJECTS:
	g_list_free_full(dbus_objects, g_object_unref);
	if(services)
		free(services);
	if(characteristics)
		free(characteristics);

	if(ret != GATTLIB_SUCCESS) {
		if(descriptor_list)
			free(descriptor_list);
		*descriptors       = NULL;
		*descriptors_count = 0;
	} else {
		sort_descriptors(descriptor_list, count);
		*descriptors       = descriptor_list;
		*descriptors_count = count;
	}

	return ret;
}

int get_bluez_device_from_mac(struct gattlib_adapter *adapter, const char *mac_address, OrgBluezDevice1 **bluez_device1)
{
	GError *error = NULL;
	char object_path[100];

	if (adapter != NULL) {
		get_device_path_from_mac_with_adapter(adapter->adapter_proxy, mac_address, object_path, sizeof(object_path));
	} else {
		get_device_path_from_mac(NULL, mac_address, object_path, sizeof(object_path));
	}

	*bluez_device1 = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			&error);
	if (error) {
		fprintf(stderr, "Failed to connection to new DBus Bluez Device: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	return GATTLIB_SUCCESS;
}

bool gattlib_is_public_address_type_from_mac(void *adapter, const char *mac_address)
{
	OrgBluezDevice1 *bluez_device1;
	const char *address_type;
	int ret;

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		g_object_unref(bluez_device1);
		return FALSE;
	}

	address_type = org_bluez_device1_get_address_type(bluez_device1);
	bool is_public_addr = address_type != NULL && 0 == strcmp(address_type, "public");

	g_object_unref(bluez_device1);
	return is_public_addr;
}

bool gattlib_is_connected_from_mac(void *adapter, const char *mac_address)
{
	OrgBluezDevice1 *bluez_device1;
	int ret;

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		g_object_unref(bluez_device1);
		return FALSE;
	}

	bool connected = org_bluez_device1_get_connected(bluez_device1);

	g_object_unref(bluez_device1);
	return connected;
}

bool gattlib_is_services_resolved_from_mac(void *adapter, const char *mac_address)
{
	OrgBluezDevice1 *bluez_device1;
	int ret;

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		g_object_unref(bluez_device1);
		return FALSE;
	}

	bool resolved = org_bluez_device1_get_services_resolved(bluez_device1);

	g_object_unref(bluez_device1);
	return resolved;
}

#if 0 // Disable until https://github.com/labapart/gattlib/issues/75 is resolved
int gattlib_get_rssi(gatt_connection_t *connection, int16_t *rssi)
{
	gattlib_context_t* conn_context = connection->context;

	if (rssi == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	*rssi = org_bluez_device1_get_rssi(conn_context->device);

	return GATTLIB_SUCCESS;
}
#endif

int gattlib_get_rssi_from_mac(void *adapter, const char *mac_address, int16_t *rssi)
{
	OrgBluezDevice1 *bluez_device1;
	int ret;

	if (rssi == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		g_object_unref(bluez_device1);
		return ret;
	}

	*rssi = org_bluez_device1_get_rssi(bluez_device1);

	g_object_unref(bluez_device1);
	return GATTLIB_SUCCESS;
}

int gattlib_get_advertising_flags_from_mac(void *adapter, const char *mac_address, char *flags)
{
	OrgBluezDevice1 *bluez_device1;
	int ret;

	if (flags == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		g_object_unref(bluez_device1);
		return ret;
	}

	GVariant *device_flags = org_bluez_device1_get_advertising_flags(bluez_device1);
	if(device_flags != NULL)
	{
		gsize flag_size;
		const guint8* flags_data = g_variant_get_fixed_array(device_flags, &flag_size, sizeof(guint8));
		if(flag_size == 1)
		{
			*flags = *flags_data;
		}
		else
		{
			ret = GATTLIB_NOT_FOUND;
		}
	}
	else
	{
		ret = GATTLIB_NOT_FOUND;
	}

	g_object_unref(bluez_device1);
	return ret;
}

int gattlib_get_raw_advertising_data_from_mac(void *adapter, const char *mac_address, uint8_t *out, size_t *out_size, size_t max_out)
{
	OrgBluezDevice1 *bluez_device1;
	int ret;

	if (out == NULL && max_out != 0) {
		return GATTLIB_INVALID_PARAMETER;
	}
	*out_size = 0;

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		g_object_unref(bluez_device1);
		return ret;
	}

	GVariant *ad_list = org_bluez_device1_get_advertising_data(bluez_device1);
	if(ad_list != NULL)
	{
		GVariantIter ad_iter;
		gchar ad_type;
		GVariant *ad_val;

		g_variant_iter_init(&ad_iter, ad_list);
		while(g_variant_iter_loop(&ad_iter, "{yv}", &ad_type, &ad_val))
		{
			gsize ad_size;
			const guint8* ad_data = g_variant_get_fixed_array(ad_val, &ad_size, sizeof(guint8));

			if(max_out < sizeof(uint8_t)+sizeof(uint8_t)+ad_size)
			{
				ret = GATTLIB_OUT_OF_MEMORY;
				continue; // if we break out of loop, data is not free'd
			}

			out[(*out_size)++] = ad_size+1;
			out[(*out_size)++] = ad_type;
			memcpy(&out[*out_size], ad_data, ad_size);
			(*out_size) += ad_size;
		}
	}
	else
	{
		ret = GATTLIB_NOT_FOUND;
	}

	g_object_unref(bluez_device1);
	return ret;
}

gboolean services_resolved_cb_handler(
	    OrgBluezDevice1 *device,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	services_resolved_cb cb = user_data;

	// Retrieve 'Value' from 'arg_changed_properties'
	if (g_variant_n_children (arg_changed_properties) > 0) {
		GVariantIter *iter;
		const gchar *key;
		GVariant *value;

		g_variant_get (arg_changed_properties, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
			if (strcmp(key, "ServicesResolved") == 0) {
				// call cb
				const char* address = org_bluez_device1_get_address(device);
				const char* address_type = org_bluez_device1_get_address_type(device);
				bool is_public_addr = address_type != NULL && 0 == strcmp(address_type, "public");
				bool services_resolved = g_variant_get_boolean(value);
				if(address != NULL)
					cb(address, is_public_addr, services_resolved);
			}
		}
		g_variant_iter_free(iter);
	}
	return TRUE;
}

void* gattlib_add_services_resolved_cb(void* adapter, const char *mac, services_resolved_cb cb)
{
	struct gattlib_adapter *gattlib_adapter = adapter;
	const char* adapter_name = NULL;
	char object_path[100];
	GError *error = NULL;

	// In case NULL is passed, we initialized default adapter
	if (gattlib_adapter == NULL) {
		gattlib_adapter = init_default_adapter();
	} else {
		adapter_name = gattlib_adapter->adapter_name;
	}

	get_device_path_from_mac(adapter_name, mac, object_path, sizeof(object_path));

	OrgBluezDevice1* device = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			&error);
	if (device == NULL) {
		if (error) {
			fprintf(stderr, "Failed to connect to DBus Bluez Device: %s\n", error->message);
			g_error_free(error);
			return NULL;
		}
	}

	// Register a handle for notification
	g_signal_connect(device,
		"g-properties-changed",
		G_CALLBACK (services_resolved_cb_handler),
		cb);

	return device;
}

void gattlib_remove_services_resolved_cb(void* handle)
{
	g_object_unref(handle);
}

