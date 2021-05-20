/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2019 Olivier Martin <olivier@labapart.org>
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

#include <stdlib.h>

#include "gattlib_internal.h"

typedef struct {
	void* caller_user_data;
	gatt_read_cb_t cb;
} gatt_async_read_data_t;

typedef struct {
	void* caller_user_data;
	gatt_write_cb_t cb;
} gatt_async_write_data_t;


const uuid_t m_battery_level_uuid = CREATE_UUID16(0x2A19);
static const uuid_t m_ccc_uuid = CREATE_UUID16(0x2902);


static bool handle_dbus_gattcharacteristic_from_path(const char* device_object_path, const uuid_t* uuid,
		struct dbus_characteristic *dbus_characteristic, const char* object_path, GError **error)
{
	OrgBluezGattCharacteristic1 *characteristic = NULL;

	*error = NULL;
	characteristic = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			error);
	if (characteristic) {
		if (uuid != NULL) {
			uuid_t characteristic_uuid;
			const gchar *characteristic_uuid_str = org_bluez_gatt_characteristic1_get_uuid(characteristic);

			gattlib_string_to_uuid(characteristic_uuid_str, strlen(characteristic_uuid_str) + 1, &characteristic_uuid);

			if (gattlib_uuid_cmp(uuid, &characteristic_uuid) != 0) {
				g_object_unref(characteristic);
				return false;
			}
		}

		// We found the right characteristic, now we check if it's the right device.
		*error = NULL;
		OrgBluezGattService1* service = org_bluez_gatt_service1_proxy_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			org_bluez_gatt_characteristic1_get_service(characteristic),
			NULL,
			error);

		if (service) {
			const bool found = !strcmp(device_object_path, org_bluez_gatt_service1_get_device(service));

			g_object_unref(service);

			if (found) {
				dbus_characteristic->gatt = characteristic;
				dbus_characteristic->type = TYPE_GATT;
				return true;
			}
		}

		g_object_unref(characteristic);
	}

	return false;
}

static bool handle_dbus_gattdescriptor_from_path(const char* device_object_path,
		struct dbus_characteristic *dbus_characteristic, const char* object_path, GError **error)
{
	OrgBluezGattDescriptor1 *descriptor = NULL;

	*error = NULL;
	descriptor = org_bluez_gatt_descriptor1_proxy_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			error);
	if (descriptor) {
		if(handle_dbus_gattcharacteristic_from_path(device_object_path, NULL,
					dbus_characteristic, org_bluez_gatt_descriptor1_get_characteristic(descriptor), error)) {
			dbus_characteristic->desc = descriptor;
			dbus_characteristic->type = TYPE_DESCRIPTOR;
			return true;
		}

		g_object_unref(descriptor);
	}

	return false;
}

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
static bool handle_dbus_battery_from_uuid(gattlib_context_t* conn_context, const uuid_t* uuid,
		struct dbus_characteristic *dbus_characteristic, const char* object_path, GError **error)
{
	OrgBluezBattery1 *battery = NULL;

	*error = NULL;
	battery = org_bluez_battery1_proxy_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			error);
	if (battery) {
		dbus_characteristic->battery = battery;
		dbus_characteristic->type = TYPE_BATTERY_LEVEL;
	}

	return false;
}
#endif

struct dbus_characteristic get_characteristic_from_uuid(gatt_connection_t* connection, const uuid_t* uuid) {
	gattlib_context_t* conn_context = connection->context;
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(conn_context->adapter);
	GError *error = NULL;
	bool is_battery_level_uuid = false;

	struct dbus_characteristic dbus_characteristic = {
			.type = TYPE_NONE
	};

	if (device_manager == NULL) {
		fprintf(stderr, "Gattlib Context not initialized.\n");
		return dbus_characteristic; // Return characteristic of type TYPE_NONE
	}

	// Some GATT Characteristics are handled by D-BUS
	if (gattlib_uuid_cmp(uuid, &m_battery_level_uuid) == 0) {
		is_battery_level_uuid = true;
	} else if (gattlib_uuid_cmp(uuid, &m_ccc_uuid) == 0) {
		fprintf(stderr, "Error: Bluez v5.42+ does not expose Client Characteristic Configuration Descriptor through DBUS interface\n");
		return dbus_characteristic;
	}

	GList *l;
	for (l = conn_context->dbus_objects; l != NULL; l = l->next)  {
		GDBusInterface *interface;
		bool found = FALSE;
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (interface) {
			g_object_unref(interface);

			found = handle_dbus_gattcharacteristic_from_path(conn_context->device_object_path, uuid, &dbus_characteristic, object_path, &error);
			if (found) {
				break;
			}
		}

		if (!found && is_battery_level_uuid) {
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
			interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.Battery1");
			if (interface) {
				g_object_unref(interface);

				found = handle_dbus_battery_from_uuid(conn_context, uuid, &dbus_characteristic, object_path, &error);
				if (found) {
					break;
				}
			}
#else
			fprintf(stderr, "You might use Bluez v5.48 with gattlib built for pre-v5.40\n");
#endif
		}
	}

	return dbus_characteristic;
}

static struct dbus_characteristic get_characteristic_from_handle_nc(GDBusObjectManager *device_manager, GList *dbus_objects, const char *device_object_path, int handle) {
	GError *error = NULL;
	int char_handle;

	struct dbus_characteristic dbus_characteristic = {
			.type = TYPE_NONE
	};

	if (device_manager == NULL) {
		fprintf(stderr, "Gattlib context not initialized.\n");
		return dbus_characteristic;
	}

	for (GList *l = dbus_objects; l != NULL; l = l->next)  {
		GDBusInterface *interface;
		bool found;
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (interface) {
			g_object_unref(interface);

			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024'.
			// We convert the last 4 hex characters into the handle
			sscanf(object_path + strlen(object_path) - 4, "%x", &char_handle);

			// value handle at char_handle+1
			if (char_handle+1 != handle) {
				continue;
			}

			found = handle_dbus_gattcharacteristic_from_path(device_object_path, NULL, &dbus_characteristic, object_path, &error);
			if (found) {
				break;
			}
		}

		interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattDescriptor1");
		if (interface) {
			g_object_unref(interface);

			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024'.
			// We convert the last 4 hex characters into the handle
			sscanf(object_path + strlen(object_path) - 4, "%x", &char_handle);

			if (char_handle != handle) {
				continue;
			}

			found = handle_dbus_gattdescriptor_from_path(device_object_path, &dbus_characteristic, object_path, &error);
			if (found) {
				break;
			}
		}
	}

	return dbus_characteristic;
}

static struct dbus_characteristic get_characteristic_from_handle(gatt_connection_t* connection, int handle) {
	gattlib_context_t* conn_context = connection->context;
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(conn_context->adapter);

	return get_characteristic_from_handle_nc(device_manager, conn_context->dbus_objects, conn_context->device_object_path, handle);
}

struct dbus_characteristic get_characteristic_from_mac_and_handle(void *adapter, const char *mac_address, int handle) {
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(adapter);
	GList *dbus_objects = g_dbus_object_manager_get_objects(device_manager);
	char object_path[100];
	get_device_path_from_mac_with_adapter(((struct gattlib_adapter *)adapter)->adapter_proxy, mac_address, object_path, sizeof(object_path));

	struct dbus_characteristic res = get_characteristic_from_handle_nc(device_manager, dbus_objects, object_path, handle);

	g_list_free_full(dbus_objects, g_object_unref);

	return res;
}

static int read_gatt_characteristic(struct dbus_characteristic *dbus_characteristic, void **buffer, size_t* buffer_len) {
	GVariant *out_value;
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)
	org_bluez_gatt_characteristic1_call_read_value_sync(
		dbus_characteristic->gatt, &out_value, NULL, &error);
#else
	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_characteristic1_call_read_value_sync(
			dbus_characteristic->gatt, g_variant_builder_end(options), &out_value, NULL, &error);
	g_variant_builder_unref(options);
#endif
	if (error != NULL) {
		fprintf(stderr, "Failed to read DBus GATT characteristic: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	gsize n_elements = 0;
	gconstpointer const_buffer = g_variant_get_fixed_array(out_value, &n_elements, sizeof(guchar));
	if (const_buffer) {
		*buffer = malloc(n_elements);
		if (*buffer == NULL) {
			ret = GATTLIB_OUT_OF_MEMORY;
			goto EXIT;
		}

		*buffer_len = n_elements;
		memcpy(*buffer, const_buffer, n_elements);
	} else {
		*buffer_len = 0;
	}

EXIT:
	g_variant_unref(out_value);
	return ret;
}

static void read_gatt_characteristic_finish_async(GObject *object, GAsyncResult *res, gpointer user_data) {
	int status = GATTLIB_SUCCESS;
	GVariant *out_value;
	GError *error = NULL;
	gsize buffer_len = 0;
	const char *buffer = NULL;

	OrgBluezGattCharacteristic1 *characteristic = (OrgBluezGattCharacteristic1*) object;
	gatt_async_read_data_t *async_data = user_data;

	org_bluez_gatt_characteristic1_call_read_value_finish(characteristic, &out_value, res, &error);

	if (error != NULL) {
		fprintf(stderr, "Failed to read DBus GATT characteristic: %s\n", error->message);
		g_error_free(error);
		status = GATTLIB_ERROR_DBUS;
		goto EXIT;
	}
	buffer = g_variant_get_fixed_array(out_value, &buffer_len, sizeof(guchar));

	g_variant_unref(out_value);

EXIT:
	async_data->cb(status, async_data->caller_user_data, buffer, buffer_len);
	g_object_unref(object);
	free(user_data);
}

static void read_gatt_characteristic_async(struct dbus_characteristic *dbus_characteristic, void *user_data, gatt_read_cb_t cb) {
	// prepare necessary user_data
	gatt_async_read_data_t *async_data = malloc(sizeof(gatt_async_read_data_t));
	async_data->cb = cb;
	async_data->caller_user_data = user_data;

	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_characteristic1_call_read_value(
			dbus_characteristic->gatt, g_variant_builder_end(options), NULL, read_gatt_characteristic_finish_async, async_data);
	g_variant_builder_unref(options);
}

static int read_gatt_descriptor(struct dbus_characteristic *dbus_characteristic, void **buffer, size_t* buffer_len) {
	GVariant *out_value;
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_descriptor1_call_read_value_sync(
			dbus_characteristic->desc, g_variant_builder_end(options), &out_value, NULL, &error);
	g_variant_builder_unref(options);
	if (error != NULL) {
		fprintf(stderr, "Failed to read DBus GATT descriptor: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	gsize n_elements = 0;
	gconstpointer const_buffer = g_variant_get_fixed_array(out_value, &n_elements, sizeof(guchar));
	if (const_buffer) {
		*buffer = malloc(n_elements);
		if (*buffer == NULL) {
			ret = GATTLIB_OUT_OF_MEMORY;
			goto EXIT;
		}

		*buffer_len = n_elements;
		memcpy(*buffer, const_buffer, n_elements);
	} else {
		*buffer_len = 0;
	}

EXIT:
	g_variant_unref(out_value);
	return ret;
}

static void read_gatt_descriptor_finish_async(GObject *object, GAsyncResult *res, gpointer user_data) {
	int status = GATTLIB_SUCCESS;
	GVariant *out_value;
	GError *error = NULL;
	gsize buffer_len = 0;
	const char *buffer = NULL;

	OrgBluezGattDescriptor1 *descriptor = (OrgBluezGattDescriptor1*) object;
	gatt_async_read_data_t *async_data = user_data;

	org_bluez_gatt_descriptor1_call_read_value_finish(descriptor, &out_value, res, &error);

	if (error != NULL) {
		fprintf(stderr, "Failed to read DBus GATT descriptor: %s\n", error->message);
		g_error_free(error);
		status = GATTLIB_ERROR_DBUS;
		goto EXIT;
	}
	buffer = g_variant_get_fixed_array(out_value, &buffer_len, sizeof(guchar));

	g_variant_unref(out_value);

EXIT:
	async_data->cb(status, async_data->caller_user_data, buffer, buffer_len);
	g_object_unref(object);
	free(user_data);
}

static void read_gatt_descriptor_async(struct dbus_characteristic *dbus_characteristic, void *user_data, gatt_read_cb_t cb) {
	// prepare necessary user_data
	gatt_async_read_data_t *async_data = malloc(sizeof(gatt_async_read_data_t));
	async_data->cb = cb;
	async_data->caller_user_data = user_data;

	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_descriptor1_call_read_value(
			dbus_characteristic->desc, g_variant_builder_end(options), NULL, read_gatt_descriptor_finish_async, async_data);
	g_variant_builder_unref(options);
}

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
static int read_battery_level(struct dbus_characteristic *dbus_characteristic, void* buffer, size_t* buffer_len) {
	guchar percentage = org_bluez_battery1_get_percentage(dbus_characteristic->battery);

	memcpy(buffer, &percentage, sizeof(uint8_t));
	*buffer_len = sizeof(uint8_t);

	g_object_unref(dbus_characteristic->battery);
	return GATTLIB_SUCCESS;
}
#endif

int gattlib_read_by_handle_from_mac(void *adapter, const char *mac_address, uint16_t handle, void** buffer, size_t* buffer_len) {
	if(!gattlib_is_connected_from_mac(adapter, mac_address)) {
		return GATTLIB_NOT_CONNECTED;
	}
	if(!gattlib_is_services_resolved_from_mac(adapter, mac_address)) {
		return GATTLIB_BUSY;
	}
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_mac_and_handle(adapter, mac_address, handle);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	}
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		return read_battery_level(&dbus_characteristic, buffer, buffer_len);
	}
#endif
	else if (dbus_characteristic.type == TYPE_DESCRIPTOR) {
		int ret = read_gatt_descriptor(&dbus_characteristic, buffer, buffer_len);
		g_object_unref(dbus_characteristic.desc);
		return ret;
	} else if (dbus_characteristic.type != TYPE_GATT) {
		return GATTLIB_NOT_SUPPORTED;
	} else {
		int ret = read_gatt_characteristic(&dbus_characteristic, buffer, buffer_len);
		g_object_unref(dbus_characteristic.gatt);
		return ret;
	}
}

void gattlib_read_by_handle_from_mac_async(void* adapter, const char *mac_address, uint16_t handle, void *user_data, gatt_read_cb_t cb) {
	if(!gattlib_is_connected_from_mac(adapter, mac_address)) {
		cb(GATTLIB_NOT_CONNECTED, user_data, NULL, 0);
		return;
	}
	if(!gattlib_is_services_resolved_from_mac(adapter, mac_address)) {
		cb(GATTLIB_BUSY, user_data, NULL, 0);
		return;
	}
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_mac_and_handle(adapter, mac_address, handle);
	if (dbus_characteristic.type == TYPE_NONE) {
		cb(GATTLIB_NOT_SUPPORTED, user_data, NULL, 0);
	}
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		//return read_battery_level(&dbus_characteristic, buffer, buffer_len);
		cb(GATTLIB_NOT_SUPPORTED, user_data, NULL, 0);
	}
#endif
	else if (dbus_characteristic.type == TYPE_DESCRIPTOR) {
		read_gatt_descriptor_async(&dbus_characteristic, user_data, cb);
	} else if (dbus_characteristic.type != TYPE_GATT) {
		cb(GATTLIB_NOT_SUPPORTED, user_data, NULL, 0);
	} else {
		read_gatt_characteristic_async(&dbus_characteristic, user_data, cb);
	}
}


int gattlib_read_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, void **buffer, size_t *buffer_len) {
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	}
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		return read_battery_level(&dbus_characteristic, buffer, buffer_len);
	}
#endif
	else if (dbus_characteristic.type != TYPE_GATT) {
		return GATTLIB_NOT_SUPPORTED;
	} else {
		int ret;

		assert(dbus_characteristic.type == TYPE_GATT);

		ret = read_gatt_characteristic(&dbus_characteristic, buffer, buffer_len);

		g_object_unref(dbus_characteristic.gatt);

		return ret;
	}
}

static void setWriteOptions(GVariantBuilder *variant_options, uint32_t options)
{
	switch(options & BLUEZ_GATT_WRITE_VALUE_TYPE_MASK) {
		case BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE:
			g_variant_builder_add(variant_options, "{sv}", "type", g_variant_new("s", "request"));
			break;
		case BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITHOUT_RESPONSE:
			g_variant_builder_add(variant_options, "{sv}", "type", g_variant_new("s", "command"));
			break;
		case BLUEZ_GATT_WRITE_VALUE_TYPE_RELIABLE_WRITE:
			g_variant_builder_add(variant_options, "{sv}", "type", g_variant_new("s", "reliable"));
			break;
	}
}

static int write_char(struct dbus_characteristic *dbus_characteristic, const void* buffer, size_t buffer_len, uint32_t options)
{
	GVariant *value = g_variant_new_from_data(G_VARIANT_TYPE ("ay"), buffer, buffer_len, TRUE, NULL, NULL);
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	GVariantBuilder *variant_options = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	setWriteOptions(variant_options, options);

	org_bluez_gatt_characteristic1_call_write_value_sync(dbus_characteristic->gatt, value, g_variant_builder_end(variant_options), NULL, &error);
	g_variant_builder_unref(variant_options);

	if (error != NULL) {
		fprintf(stderr, "Failed to write DBus GATT characteristic: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	//
	// @note: No need to free `value` has it is freed by org_bluez_gatt_characteristic1_call_write_value_sync()
	//        See: https://developer.gnome.org/gio/stable/GDBusProxy.html#g-dbus-proxy-call
	//

	return ret;
}

static void write_char_finish_async(GObject *object, GAsyncResult *res, gpointer user_data) {
	int status = GATTLIB_SUCCESS;
	GError *error = NULL;

	OrgBluezGattCharacteristic1 *characteristic = (OrgBluezGattCharacteristic1*) object;
	gatt_async_write_data_t *async_data = user_data;

	org_bluez_gatt_characteristic1_call_write_value_finish(characteristic, res, &error);

	if (error != NULL) {
		fprintf(stderr, "Failed to read DBus GATT characteristic: %s\n", error->message);
		g_error_free(error);
		status = GATTLIB_ERROR_DBUS;
		goto EXIT;
	}

EXIT:
	async_data->cb(status, async_data->caller_user_data);
	g_object_unref(object);
	free(user_data);
}

static void write_char_async(struct dbus_characteristic *dbus_characteristic, const void* buffer, size_t buffer_len, uint32_t options, void* user_data, gatt_write_cb_t cb)
{
	// prepare necessary user_data
	gatt_async_write_data_t *async_data = malloc(sizeof(gatt_async_write_data_t));
	async_data->cb = cb;
	async_data->caller_user_data = user_data;

	GVariant *value = g_variant_new_from_data(G_VARIANT_TYPE ("ay"), buffer, buffer_len, TRUE, NULL, NULL);

	GVariantBuilder *variant_options = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	setWriteOptions(variant_options, options);

	org_bluez_gatt_characteristic1_call_write_value(dbus_characteristic->gatt, value, g_variant_builder_end(variant_options), NULL, write_char_finish_async, async_data);
	g_variant_builder_unref(variant_options);

	//
	// @note: No need to free `value` has it is freed by org_bluez_gatt_characteristic1_call_write_value_sync()
	//        See: https://developer.gnome.org/gio/stable/GDBusProxy.html#g-dbus-proxy-call
	//
}

static int write_desc(struct dbus_characteristic *dbus_characteristic, const void* buffer, size_t buffer_len, uint32_t options)
{
	GVariant *value = g_variant_new_from_data(G_VARIANT_TYPE ("ay"), buffer, buffer_len, TRUE, NULL, NULL);
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	GVariantBuilder *variant_options = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	setWriteOptions(variant_options, options);

	org_bluez_gatt_descriptor1_call_write_value_sync(dbus_characteristic->desc, value, g_variant_builder_end(variant_options), NULL, &error);
	g_variant_builder_unref(variant_options);

	if (error != NULL) {
		fprintf(stderr, "Failed to write DBus GATT descriptor: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	//
	// @note: No need to free `value` has it is freed by org_bluez_gatt_descriptor1_call_write_value_sync()
	//        See: https://developer.gnome.org/gio/stable/GDBusProxy.html#g-dbus-proxy-call
	//

	return ret;
}

static void write_desc_finish_async(GObject *object, GAsyncResult *res, gpointer user_data) {
	int status = GATTLIB_SUCCESS;
	GError *error = NULL;

	OrgBluezGattDescriptor1 *descriptor = (OrgBluezGattDescriptor1*) object;
	gatt_async_write_data_t *async_data = user_data;

	org_bluez_gatt_descriptor1_call_write_value_finish(descriptor, res, &error);

	if (error != NULL) {
		fprintf(stderr, "Failed to read DBus GATT descriptor: %s\n", error->message);
		g_error_free(error);
		status = GATTLIB_ERROR_DBUS;
		goto EXIT;
	}

EXIT:
	async_data->cb(status, async_data->caller_user_data);
	g_object_unref(object);
	free(user_data);
}

static void write_desc_async(struct dbus_characteristic *dbus_characteristic, const void* buffer, size_t buffer_len, uint32_t options, void* user_data, gatt_write_cb_t cb)
{
	// prepare necessary user_data
	gatt_async_write_data_t *async_data = malloc(sizeof(gatt_async_write_data_t));
	async_data->cb = cb;
	async_data->caller_user_data = user_data;

	GVariant *value = g_variant_new_from_data(G_VARIANT_TYPE ("ay"), buffer, buffer_len, TRUE, NULL, NULL);

	GVariantBuilder *variant_options = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	setWriteOptions(variant_options, options);

	org_bluez_gatt_descriptor1_call_write_value(dbus_characteristic->desc, value, g_variant_builder_end(variant_options), NULL, write_desc_finish_async, async_data);
	g_variant_builder_unref(variant_options);

	//
	// @note: No need to free `value` has it is freed by org_bluez_gatt_descriptor1_call_write_value_sync()
	//        See: https://developer.gnome.org/gio/stable/GDBusProxy.html#g-dbus-proxy-call
	//
}


int gattlib_write_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len)
{
	int ret;

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	} else if(dbus_characteristic.type != TYPE_GATT) {
		return GATTLIB_NOT_SUPPORTED;
	}

	ret = write_char(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE);

	g_object_unref(dbus_characteristic.gatt);
	return ret;
}

int gattlib_write_by_handle_from_mac(void *adapter, const char *mac_address, uint16_t handle, const void* buffer, size_t buffer_len) {
	if(!gattlib_is_connected_from_mac(adapter, mac_address)) {
		return GATTLIB_NOT_CONNECTED;
	}
	if(!gattlib_is_services_resolved_from_mac(adapter, mac_address)) {
		return GATTLIB_BUSY;
	}
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_mac_and_handle(adapter, mac_address, handle);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	} else if (dbus_characteristic.type == TYPE_DESCRIPTOR) {
		int ret = write_desc(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE);
		g_object_unref(dbus_characteristic.desc);
		return ret;
	} else if (dbus_characteristic.type != TYPE_GATT) {
		return GATTLIB_NOT_SUPPORTED;
	}
	else {
		int ret = write_char(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE);
		g_object_unref(dbus_characteristic.gatt);
		return ret;
	}
}

void gattlib_write_by_handle_from_mac_async(void *adapter, const char *mac_address, uint16_t handle, const void* buffer, size_t buffer_len, void *user_data, gatt_write_cb_t cb) {
	if(!gattlib_is_connected_from_mac(adapter, mac_address)) {
		cb(GATTLIB_NOT_CONNECTED, user_data);
		return;
	}
	if(!gattlib_is_services_resolved_from_mac(adapter, mac_address)) {
		cb(GATTLIB_BUSY, user_data);
		return;
	}
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_mac_and_handle(adapter, mac_address, handle);
	if (dbus_characteristic.type == TYPE_NONE) {
		cb(GATTLIB_NOT_FOUND, user_data);
	} else if (dbus_characteristic.type == TYPE_DESCRIPTOR) {
		write_desc_async(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE, user_data, cb);
	} else if (dbus_characteristic.type != TYPE_GATT) {
		cb(GATTLIB_NOT_SUPPORTED, user_data);
	} else {
		write_char_async(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE, user_data, cb);
	}
}

int gattlib_write_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len)
{
	int ret;

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_handle(connection, handle);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	} else if (dbus_characteristic.type != TYPE_GATT) {
		return GATTLIB_NOT_SUPPORTED;
	}

	ret = write_char(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE);

	g_object_unref(dbus_characteristic.gatt);
	return ret;
}

int gattlib_write_without_response_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len)
{
	int ret;

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	} else if (dbus_characteristic.type != TYPE_GATT) {
		return GATTLIB_NOT_SUPPORTED;
	}

	ret = write_char(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITHOUT_RESPONSE);

	g_object_unref(dbus_characteristic.gatt);
	return ret;
}

int gattlib_write_without_response_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len)
{
	int ret;

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_handle(connection, handle);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	} else if (dbus_characteristic.type != TYPE_GATT) {
		return GATTLIB_NOT_SUPPORTED;
	}

	ret = write_char(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITHOUT_RESPONSE);

	g_object_unref(dbus_characteristic.gatt);
	return ret;
}

