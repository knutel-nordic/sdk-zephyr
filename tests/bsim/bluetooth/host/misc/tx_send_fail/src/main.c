/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

#include "babblekit/flags.h"
#include "babblekit/testcase.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tx_send_fail, LOG_LEVEL_INF);

DEFINE_FLAG_STATIC(flag_connected);
DEFINE_FLAG_STATIC(flag_disconnected);
DEFINE_FLAG_STATIC(flag_fail_acl_tx);

static struct bt_conn *g_conn;

int __real_bt_send(struct net_buf *buf);

/* Emulate a failing HCI transport/controller (e.g. `ll_tx_mem_enqueue()`
 * returning -EINVAL for a handle that has just been invalidated).
 * Only ACL data is failed, commands still go through.
 */
int __wrap_bt_send(struct net_buf *buf)
{
	if (IS_FLAG_SET(flag_fail_acl_tx) && buf->data[0] == BT_HCI_H4_ACL) {
		LOG_INF("Injecting ACL TX failure");
		UNSET_FLAG(flag_fail_acl_tx);
		return -EINVAL;
	}

	return __real_bt_send(buf);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	TEST_ASSERT(!err, "Connection failed (err 0x%02x)", err);

	LOG_INF("Connected");
	g_conn = bt_conn_ref(conn);
	SET_FLAG(flag_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason 0x%02x)", reason);
	bt_conn_unref(g_conn);
	g_conn = NULL;
	UNSET_FLAG(flag_connected);
	SET_FLAG(flag_disconnected);
}

BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	struct bt_conn *conn;
	int err;

	err = bt_le_scan_stop();
	TEST_ASSERT(!err, "Failed to stop scan (err %d)", err);

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &conn);
	TEST_ASSERT(!err, "Failed to create connection (err %d)", err);
}

static void mtu_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
	LOG_INF("MTU exchange done (err 0x%02x)", err);
}

static void test_central_main(void)
{
	static struct bt_gatt_exchange_params mtu_params = {.func = mtu_cb};
	int err;

	/* Test purpose:
	 * Verify the host recovers when the HCI send of an ACL fragment fails.
	 * The TX processor must not disconnect inline, as the HCI Disconnect
	 * command can only be sent by the TX processor thread itself.
	 *
	 * Test procedure:
	 * - Connect to the peripheral
	 * - Arm a one-shot failure of the next ACL data send
	 * - Trigger an ATT request, so the TX processor attempts to send it
	 *
	 * Verdict:
	 * - The `disconnected` callback is called. Without the fix, the TX
	 *   processor stalls on the HCI Disconnect command and BT_ASSERT fires
	 *   after the 10 s HCI command timeout.
	 */
	err = bt_enable(NULL);
	TEST_ASSERT(!err, "Failed to enable Bluetooth (err %d)", err);

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
	TEST_ASSERT(!err, "Failed to start scan (err %d)", err);

	WAIT_FOR_FLAG(flag_connected);

	SET_FLAG(flag_fail_acl_tx);
	err = bt_gatt_exchange_mtu(g_conn, &mtu_params);
	TEST_ASSERT(!err, "Failed to start MTU exchange (err %d)", err);

	WAIT_FOR_FLAG(flag_disconnected);

	TEST_PASS("Central: disconnected after ACL TX failure");
}

static void test_peripheral_main(void)
{
	int err;

	err = bt_enable(NULL);
	TEST_ASSERT(!err, "Failed to enable Bluetooth (err %d)", err);

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, NULL, 0, NULL, 0);
	TEST_ASSERT(!err, "Failed to start advertising (err %d)", err);

	WAIT_FOR_FLAG(flag_connected);
	WAIT_FOR_FLAG(flag_disconnected);

	TEST_PASS("Peripheral: disconnected");
}

static const struct bst_test_instance test_to_add[] = {
	{
		.test_id = "central",
		.test_descr = "Connects, then fails an ACL send",
		.test_main_f = test_central_main,
	},
	{
		.test_id = "peripheral",
		.test_descr = "Accepts a connection",
		.test_main_f = test_peripheral_main,
	},
	BSTEST_END_MARKER,
};

static struct bst_test_list *install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_to_add);
};

bst_test_install_t test_installers[] = {install, NULL};

int main(void)
{
	bst_main();

	return 0;
}
