#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>

#include "gattlib.h"

#define BLE_SCAN_TIMEOUT   4

typedef void (*ble_discovered_device_t)(const char* addr, const char* name);

// We use a mutex to make the BLE connections synchronous
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

LIST_HEAD(listhead, connection_t) g_ble_connections;
struct connection_t {
	pthread_t thread;
	void* adapter;
	char* addr;
	LIST_ENTRY(connection_t) entries;
};

static void *ble_connect_device(void *arg) {
	struct connection_t *connection = arg;
	void* adapter = connection->adapter;
	char* addr = connection->addr;
	gatt_connection_t* gatt_connection;
	gattlib_primary_service_t* services;
	gattlib_characteristic_t* characteristics;
	gattlib_descriptor_t* descriptors;
	int services_count, characteristics_count, descriptors_count;
	char uuid_str[MAX_LEN_UUID_STR + 1];
	int ret, i;

	pthread_mutex_lock(&g_mutex);

	printf("------------START %s ---------------\n", addr);

	gatt_connection = gattlib_connect(adapter, addr, GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT);
	if (gatt_connection == NULL) {
		fprintf(stderr, "Fail to connect to the bluetooth device.\n");
		goto connection_exit;
	} else {
		puts("Succeeded to connect to the bluetooth device.");
	}

	//ret = gattlib_discover_primary(gatt_connection, &services, &services_count);
	ret = gattlib_discover_primary_from_mac(adapter, addr, &services, &services_count);
	if (ret != 0) {
		fprintf(stderr, "Fail to discover primary services.\n");
		goto disconnect_exit;
	}

	for (i = 0; i < services_count; i++) {
		gattlib_uuid_to_string(&services[i].uuid, uuid_str, sizeof(uuid_str));

		printf("service[%d] start_handle:%02x end_handle:%02x uuid:%s\n", i,
				services[i].attr_handle_start, services[i].attr_handle_end,
				uuid_str);
	}
	free(services);

	// ret = gattlib_discover_char(gatt_connection, &characteristics, &characteristics_count);
	ret = gattlib_discover_char_from_mac(adapter, addr, &characteristics, &characteristics_count);
	if (ret != 0) {
		fprintf(stderr, "Fail to discover characteristics.\n");
		goto disconnect_exit;
	}
	for (i = 0; i < characteristics_count; i++) {
		gattlib_uuid_to_string(&characteristics[i].uuid, uuid_str, sizeof(uuid_str));

		printf("characteristic[%d] properties:%02x handle:%04x value_handle:%04x uuid:%s\n", i,
				characteristics[i].properties, characteristics[i].handle, characteristics[i].value_handle,
				uuid_str);
	}
	free(characteristics);

	// ret = gattlib_discover_desc(gatt_connection, &descriptors, &descriptors_count);
	ret = gattlib_discover_desc_from_mac(adapter, addr, &descriptors, &descriptors_count);
	if (ret != 0) {
		fprintf(stderr, "Fail to discover descriptors.\n");
		goto disconnect_exit;
	}
	for (i = 0; i < descriptors_count; i++) {
		gattlib_uuid_to_string(&descriptors[i].uuid, uuid_str, sizeof(uuid_str));

		printf("descriptor[%d] handle:%04x uuid:%s\n", i,
				descriptors[i].handle, uuid_str);
	}
	free(descriptors);

disconnect_exit:
	gattlib_disconnect(gatt_connection);

connection_exit:
	printf("------------DONE %s ---------------\n", addr);
	pthread_mutex_unlock(&g_mutex);
	return NULL;
}

static void ble_discovered_device(void *adapter, const char* addr, const char* name, void *user_data) {
	struct connection_t *connection;
	int ret;

	if (name) {
		printf("Discovered %s - '%s'\n", addr, name);
	} else {
		printf("Discovered %s\n", addr);
	}

	connection = malloc(sizeof(struct connection_t));
	if (connection == NULL) {
		fprintf(stderr, "Failt to allocate connection.\n");
		return;
	}
	connection->addr = strdup(addr);
	connection->adapter = adapter;

	ret = pthread_create(&connection->thread, NULL,	ble_connect_device, connection);
	if (ret != 0) {
		fprintf(stderr, "Failt to create BLE connection thread.\n");
		free(connection);
		return;
	}
	LIST_INSERT_HEAD(&g_ble_connections, connection, entries);
}

int main(int argc, const char *argv[]) {
	const char* adapter_name;
	void* adapter;
	int ret;

	if (argc == 1) {
		adapter_name = NULL;
	} else if (argc == 2) {
		adapter_name = argv[1];
	} else {
		fprintf(stderr, "%s [<bluetooth-adapter>]\n", argv[0]);
		return 1;
	}

	LIST_INIT(&g_ble_connections);

	ret = gattlib_adapter_open(adapter_name, &adapter);
	if (ret) {
		fprintf(stderr, "ERROR: Failed to open adapter.\n");
		return 1;
	}

	pthread_mutex_lock(&g_mutex);
	ret = gattlib_adapter_scan(adapter, ble_discovered_device, BLE_SCAN_TIMEOUT, NULL /* user_data */);
	if (ret) {
		fprintf(stderr, "ERROR: Failed to scan.\n");
		goto EXIT;
	}

	gattlib_adapter_scan_disable(adapter);

	puts("Scan completed");
	pthread_mutex_unlock(&g_mutex);

	// Wait for the thread to complete
	while (g_ble_connections.lh_first != NULL) {
		struct connection_t* connection = g_ble_connections.lh_first;
		pthread_join(connection->thread, NULL);
		LIST_REMOVE(g_ble_connections.lh_first, entries);
		free(connection->addr);
		free(connection);
	}

EXIT:
	gattlib_adapter_close(adapter);
	return ret;
}
