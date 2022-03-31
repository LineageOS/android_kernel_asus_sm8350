// SPDX-License-Identifier: GPL-2.0-only

#include "asus_battery_charger.h"

#define MSG_OWNER_OEM			32782
#define MSG_TYPE_REQ_RESP		1
#define MSG_TYPE_NOTIFY			2

#define OEM_THERMAL_ALERT_SET		17
#define THERMAL_ALERT_NONE		0
#define THERMAL_ALERT_NO_AC		1
#define THERMAL_ALERT_WITH_AC		2

#define OEM_WORK_EVENT			16
#define WORK_PANEL_CHECK		3
#define WORK_18W_WORKAROUND		5

#define OEM_THERMAL_THRESHOLD		23

#define OEM_OPCODE_WRITE_BUFFER		0x10001

#define OEM_PROPERTY_MAX_DATA_SIZE	16

#define OEM_SET_OTG_WA			0x2107
#define OEM_USB_PRESENT			0x2108

#define dwork_to_abc(work, member) \
	container_of(to_delayed_work(work), struct asus_battery_chg, member)

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

extern bool g_Charger_mode;

static int handle_usb_online(struct notifier_block *nb, unsigned long status,
			     void *unused)
{
	struct asus_battery_chg *abc = container_of(nb, struct asus_battery_chg,
						    usb_online_notifier);

	if (abc->usb_online == status)
		return 0;

	abc->usb_online = status;

	if (status) {
		if (!g_Charger_mode) {
			cancel_delayed_work_sync(&abc->panel_check_work);
			schedule_delayed_work(&abc->panel_check_work, 62 * HZ);
		}

		cancel_delayed_work_sync(&abc->workaround_18w_work);
		schedule_delayed_work(&abc->workaround_18w_work, 26 * HZ);

		cancel_delayed_work_sync(&abc->thermal_policy_work);
		schedule_delayed_work(&abc->thermal_policy_work, 68 * HZ);

		__pm_wakeup_event(abc->slowchg_ws, 60 * 1000);
	} else {
		cancel_delayed_work_sync(&abc->panel_check_work);
		cancel_delayed_work_sync(&abc->workaround_18w_work);
		cancel_delayed_work_sync(&abc->thermal_policy_work);
	}

	return 0;
}

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

static int write_property_work_event(struct asus_battery_chg *abc, u32 event)
{
	struct device *dev = battery_chg_device(abc->bcdev);
	int rc;

	rc = write_property_id_oem(abc, OEM_WORK_EVENT, &event, 1);
	if (rc)
		dev_err(dev, "Failed to write work event %u, rc=%d\n", event,
			rc);

	return rc;
}

#define TEMP_TRIGGER			70000
#define TEMP_RELEASE			60000
#define USB_THERMAL_WORK_MSECS		60000

static void usb_thermal_worker(struct work_struct *work)
{
	struct asus_battery_chg *abc = dwork_to_abc(work, usb_thermal_work);
	struct device *dev = battery_chg_device(abc->bcdev);
	int temp;
	u32 tmp;
	int rc;

	rc = iio_read_channel_processed(abc->temp_chan, &temp);
	if (rc < 0) {
		dev_err(dev, "Failed to read temperature, rc=%d\n", rc);
		goto out;
	}

	if (abc->usb_present && temp > TEMP_TRIGGER)
		tmp = THERMAL_ALERT_WITH_AC;
	else if (temp > TEMP_TRIGGER)
		tmp = THERMAL_ALERT_NO_AC;
	else if (temp < TEMP_RELEASE)
		tmp = THERMAL_ALERT_NONE;
	else
		goto out;

	rc = write_property_id_oem(abc, OEM_THERMAL_ALERT_SET, &tmp, 1);
	if (rc)
		dev_err(dev, "Failed to write thermal alert %u, rc=%d\n",
			tmp, rc);

out:
	schedule_delayed_work(&abc->usb_thermal_work,
			      msecs_to_jiffies(USB_THERMAL_WORK_MSECS));
}

static void panel_check_worker(struct work_struct *work)
{
	struct asus_battery_chg *abc = dwork_to_abc(work, panel_check_work);

	write_property_work_event(abc, WORK_PANEL_CHECK);

	schedule_delayed_work(&abc->panel_check_work, 10 * HZ);
}

static void workaround_18w_worker(struct work_struct *work)
{
	struct asus_battery_chg *abc = dwork_to_abc(work, workaround_18w_work);

	write_property_work_event(abc, WORK_18W_WORKAROUND);
}

static void thermal_policy_worker(struct work_struct *work)
{
	struct asus_battery_chg *abc = dwork_to_abc(work, thermal_policy_work);
	struct device *dev = battery_chg_device(abc->bcdev);
	u32 tmp = abc->thermal_threshold;
	int rc;

	rc = write_property_id_oem(abc, OEM_THERMAL_THRESHOLD, &tmp, 1);
	if (rc)
		dev_err(dev, "Failed to write thermal threshold %u, rc=%d\n",
			tmp, rc);

	schedule_delayed_work(&abc->thermal_policy_work, 10 * HZ);
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
	case OEM_USB_PRESENT:
		CHECK_SET_DATA(enable_change_msg);

		abc->usb_present = enable_change_msg->enable;
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
		case OEM_THERMAL_ALERT_SET:
		case OEM_THERMAL_THRESHOLD:
		case OEM_WORK_EVENT:
			ack_set = true;
			break;
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

static ssize_t set_virtualthermal_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct asus_battery_chg *abc = container_of(c, struct asus_battery_chg,
						    asuslib_class);

	if (kstrtoint(buf, 0, &abc->thermal_threshold))
		return -EINVAL;

	return count;
}
static CLASS_ATTR_WO(set_virtualthermal);

static struct attribute *asuslib_class_attrs[] = {
	&class_attr_set_virtualthermal.attr,
	NULL,
};
ATTRIBUTE_GROUPS(asuslib_class);

void unregister_pmic_glink_client(void *data)
{
	struct pmic_glink_client *client = data;

	pmic_glink_unregister_client(client);
}

void unregister_usb_online_notifier(void *data)
{
	struct notifier_block *nb = data;

	qti_charge_unregister_notify(nb);
}

void unregister_asuslib_class(void *data)
{
	struct class *class = data;

	class_unregister(class);
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

	abc->slowchg_ws = wakeup_source_register(dev, "Slowchg_wakelock");
	if (!abc->slowchg_ws) {
		dev_err(dev, "Failed to register wakeup source\n");
		return -EINVAL;
	}

	abc->temp_chan = devm_iio_channel_get(dev, "pm8350b_amux_thm4");
	if (IS_ERR(abc->temp_chan)) {
		rc = PTR_ERR(abc->temp_chan);
		dev_err(dev, "Failed to get temp channel, rc=%d\n", rc);
		return rc;
	}

	abc->usb_online_notifier.notifier_call = handle_usb_online;
	qti_charge_register_notify(&abc->usb_online_notifier);
	rc = devm_add_action_or_reset(dev, unregister_usb_online_notifier,
				      &abc->usb_online_notifier);
	if (rc)
		return rc;

	abc->asuslib_class.name = "asuslib";
	abc->asuslib_class.class_groups = asuslib_class_groups;
	rc = class_register(&abc->asuslib_class);
	if (rc) {
		dev_err(dev, "Failed to create asuslib_class rc=%d\n", rc);
		return rc;
	}

	rc = devm_add_action_or_reset(dev, unregister_asuslib_class,
				      &abc->asuslib_class);
	if (rc)
		return rc;

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

	INIT_DELAYED_WORK(&abc->usb_thermal_work, usb_thermal_worker);
	schedule_delayed_work(&abc->usb_thermal_work, 0);

	INIT_DELAYED_WORK(&abc->panel_check_work, panel_check_worker);
	INIT_DELAYED_WORK(&abc->workaround_18w_work, workaround_18w_worker);
	INIT_DELAYED_WORK(&abc->thermal_policy_work, thermal_policy_worker);

	abc->initialized = true;

	return 0;
}

int asus_battery_charger_deinit(struct asus_battery_chg *abc)
{
	cancel_delayed_work_sync(&abc->usb_thermal_work);
	cancel_delayed_work_sync(&abc->panel_check_work);
	cancel_delayed_work_sync(&abc->workaround_18w_work);
	cancel_delayed_work_sync(&abc->thermal_policy_work);

	return 0;
}
