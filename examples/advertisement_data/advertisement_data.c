#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "gattlib.h"

#define BLE_SCAN_TIMEOUT   60

static void ble_advertising_device(void *adapter, const char* addr, const char* name, void *user_data) {
	gattlib_advertisement_data_t *advertisement_data = NULL;
	size_t advertisement_data_count;
	uint16_t manufacturer_id;
	uint8_t *manufacturer_data = NULL;
	size_t manufacturer_data_size;
	int ret;

	ret = gattlib_get_advertisement_data_from_mac(adapter, addr,
			&advertisement_data, &advertisement_data_count,
			&manufacturer_id, &manufacturer_data, &manufacturer_data_size);
	if (ret != 0) {
		return;
	}

	if (name) {
		printf("Device %s - '%s': ", addr, name);
	} else {
		printf("Device %s: ", addr);
	}

	for (size_t i = 0; i < manufacturer_data_size; i++) {
		printf("%02x ", manufacturer_data[i]);
	}
	printf("\n");

	for (size_t i = 0; i < advertisement_data_count; i++)
	{
		free(advertisement_data[i].data);
	}

	if(advertisement_data)
		free(advertisement_data);
	if(manufacturer_data)
		free(manufacturer_data);
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

	ret = gattlib_adapter_open(adapter_name, &adapter);
	if (ret) {
		fprintf(stderr, "ERROR: Failed to open adapter.\n");
		return 1;
	}

	ret = gattlib_adapter_scan_with_filter(adapter,
			NULL, /* Do not filter on any specific Service UUID */
			0 /* RSSI Threshold */,
			GATTLIB_DISCOVER_FILTER_NOTIFY_CHANGE, /* Notify change of advertising data/RSSI */
			ble_advertising_device,
			10, /* timeout=0 means infinite loop */
			NULL /* user_data */);
	if (ret) {
		fprintf(stderr, "ERROR: Failed to scan.\n");
		goto EXIT;
	}

	gattlib_adapter_scan_disable(adapter);

	puts("Scan completed");

EXIT:
	gattlib_adapter_close(adapter);
	return ret;
}
