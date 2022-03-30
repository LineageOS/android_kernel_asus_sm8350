// SPDX-License-Identifier: GPL-2.0-only

#include "asus_battery_charger.h"

#define MSG_OWNER_OEM			32782
#define MSG_TYPE_REQ_RESP		1
#define MSG_TYPE_NOTIFY			2

#define OEM_OPCODE_WRITE_BUFFER		0x10001

#define OEM_PROPERTY_MAX_DATA_SIZE	16

#define OEM_SET_OTG_WA			0x2107

struct oem_write_buffer_req_msg {
	struct pmic_glink_hdr hdr;
	u32 oem_property_id;
	u32 data_buffer[OEM_PROPERTY_MAX_DATA_SIZE];
	u32 data_size;
};

struct oem_write_buffer_resp_msg {
	struct pmic_glink_hdr hdr;
	u32 oem_property_id;
	u32 return_status;
};

struct oem_enable_change_msg {
	struct pmic_glink_hdr hdr;
	u32 enable;
};

static int write_property_id_oem(struct asus_battery_chg *abc, u32 prop_id,
				 u32 *buf, size_t count)
{
	struct oem_write_buffer_req_msg req_msg = { { 0 } };

	req_msg.hdr.owner = MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OPCODE_WRITE_BUFFER;
	req_msg.oem_property_id = prop_id;
	req_msg.data_size = count;

	memcpy(req_msg.data_buffer, buf, sizeof(u32) * count);

	return battery_chg_write(abc->bcdev, &req_msg, sizeof(req_msg));
}

#define CHECK_SET_DATA(msg)						\
	if (len != sizeof(*msg)) {					\
		dev_err(dev, "Bad response length %zu for opcode %u\n",	\
		       len, hdr->opcode);				\
		break;							\
	}								\
	msg = data;

static void handle_notification(struct asus_battery_chg *abc, void *data,
				size_t len)
{
	struct device *dev = battery_chg_device(abc->bcdev);
	struct oem_enable_change_msg *enable_change_msg;
	struct pmic_glink_hdr *hdr = data;

	switch (hdr->opcode) {
	case OEM_SET_OTG_WA:
		CHECK_SET_DATA(enable_change_msg);

		gpiod_set_value(abc->otg_switch, enable_change_msg->enable);
		break;
	default:
		dev_err(dev, "Unknown opcode: %u\n", hdr->opcode);
		break;
	}
}

static void handle_message(struct asus_battery_chg *abc, void *data,
			   size_t len)
{
	struct device *dev = battery_chg_device(abc->bcdev);
	struct oem_write_buffer_resp_msg *resp_msg;
	struct pmic_glink_hdr *hdr = data;
	bool ack_set = false;

	switch (hdr->opcode) {
	case OEM_OPCODE_WRITE_BUFFER:
		CHECK_SET_DATA(resp_msg);

		resp_msg = data;

		switch (resp_msg->oem_property_id) {
		default:
			ack_set = true;
			dev_err(dev, "Unknown property_id: %u\n",
			       resp_msg->oem_property_id);
			break;
		}
		break;
	default:
		dev_err(dev, "Unknown opcode: %u\n", hdr->opcode);
		ack_set = true;
		break;
	}

	if (ack_set)
		battery_chg_complete_ack(abc->bcdev);
}

static void pmic_glink_state_cb(void *priv, enum pmic_glink_state state)
{
}

static int pmic_glink_cb(void *priv, void *data, size_t len)
{
	struct asus_battery_chg *abc = priv;
	struct pmic_glink_hdr *hdr = data;

	if (!abc->initialized || hdr->owner != MSG_OWNER_OEM)
		return 0;

	if (hdr->type == MSG_TYPE_NOTIFY)
		handle_notification(abc, data, len);
	else
		handle_message(abc, data, len);

	return 0;
}

void unregister_pmic_glink_client(void *data)
{
	struct pmic_glink_client *client = data;

	pmic_glink_unregister_client(client);
}

int asus_battery_charger_init(struct asus_battery_chg *abc)
{
	struct device *dev = battery_chg_device(abc->bcdev);
	struct pmic_glink_client_data client_data = { };
	int rc;

	abc->otg_switch = devm_gpiod_get(dev, "otg-load-switch", GPIOD_OUT_LOW);
	if (IS_ERR(abc->otg_switch)) {
		rc = PTR_ERR(abc->otg_switch);
		dev_err(dev, "Failed to get otg switch gpio, rc=%d\n", rc);
		return rc;
	}

	client_data.id = MSG_OWNER_OEM;
	client_data.name = "asus_BC";
	client_data.priv = abc;
	client_data.msg_cb = pmic_glink_cb;
	client_data.state_cb = pmic_glink_state_cb;

	abc->client = pmic_glink_register_client(dev, &client_data);
	if (IS_ERR(abc->client)) {
		rc = PTR_ERR(abc->client);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Error in registering with pmic_glink %d\n",
				rc);
		return rc;
	}

	rc = devm_add_action_or_reset(dev, unregister_pmic_glink_client,
				      &abc->client);
	if (rc)
		return rc;

	abc->initialized = true;

	return 0;
}

int asus_battery_charger_deinit(struct asus_battery_chg *abc)
{
	return 0;
}
