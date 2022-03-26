/*
 * Copyright (c) 2019-2020, The ASUS Company. All rights reserved.
 */

#define pr_fmt(fmt) "BATTERY_CHG: %s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/soc/qcom/battery_charger.h>

#include <linux/of_gpio.h>
#include "asus_battery_charger_ZF8.h"
#include <drm/drm_panel.h>
#include <linux/reboot.h>
#include <linux/syscalls.h>
#include <linux/rtc.h>

#include "../../thermal/qcom/adc-tm.h"
#include <dt-bindings/iio/qcom,spmi-vadc.h>
#include <dt-bindings/iio/qcom,spmi-adc7-pm8350.h>
#include <dt-bindings/iio/qcom,spmi-adc7-pm8350b.h>
#include <linux/iio/consumer.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include <linux/extcon.h>
#include <../../extcon/extcon.h>

bool g_asuslib_init = false;

struct iio_channel *usb_conn_temp_vadc_chan;
static int asus_usb_online = 0;
static struct wakeup_source *slowchg_ws;

extern int battery_chg_write(struct battery_chg_dev *bcdev, void *data,
			     int len);
extern int asus_extcon_set_state_sync(struct extcon_dev *edev, int cable_state);
extern int asus_extcon_get_state(struct extcon_dev *edev);
extern bool g_Charger_mode;

extern void qti_charge_notify_device_charge(void);
extern void qti_charge_notify_device_not_charge(void);

#define CYCLE_COUNT_DATA_MAGIC 0x85
#define CYCLE_COUNT_FILE_NAME "/batinfo/.bs"
#define CYCLE_COUNT_DATA_OFFSET 0x0
#define FILE_OP_READ 0
#define FILE_OP_WRITE 1
#define BATTERY_SAFETY_UPGRADE_TIME 1 * 60

static bool g_cyclecount_initialized = false;

struct bat_safety_condition {
	unsigned long condition1_battery_time;
	unsigned long condition2_battery_time;
	unsigned long condition3_battery_time;
	unsigned long condition4_battery_time;
	int condition1_cycle_count;
	int condition2_cycle_count;
	unsigned long condition1_temp_vol_time;
	unsigned long condition2_temp_vol_time;
	unsigned long condition1_temp_time;
	unsigned long condition2_temp_time;
	unsigned long condition1_vol_time;
	unsigned long condition2_vol_time;
};

#define BAT_HEALTH_NUMBER_MAX 21
struct BAT_HEALTH_DATA {
	int magic;
	int bat_health;
	int charge_counter_begin;
	int charge_counter_end;
};

struct BAT_HEALTH_DATA_BACKUP {
	char date[20];
	int health;
};

#define HIGH_TEMP 350
#define HIGHER_TEMP 450
#define FULL_CAPACITY_VALUE 100
#define BATTERY_USE_TIME_CONDITION1 (1 * 30 * 24 * 60 * 60) //1Months
#define BATTERY_USE_TIME_CONDITION2 (3 * 30 * 24 * 60 * 60) //3Months
#define BATTERY_USE_TIME_CONDITION3 (12 * 30 * 24 * 60 * 60) //12Months
#define BATTERY_USE_TIME_CONDITION4 (18 * 30 * 24 * 60 * 60) //18Months
#define CYCLE_COUNT_CONDITION1 100
#define CYCLE_COUNT_CONDITION2 400
#define HIGH_TEMP_VOL_TIME_CONDITION1 (15 * 24 * 60 * 60) //15Days
#define HIGH_TEMP_VOL_TIME_CONDITION2 (30 * 24 * 60 * 60) //30Days
#define HIGH_TEMP_TIME_CONDITION1 (6 * 30 * 24 * 60 * 60) //6Months
#define HIGH_TEMP_TIME_CONDITION2 (12 * 30 * 24 * 60 * 60) //12Months
#define HIGH_VOL_TIME_CONDITION1 (6 * 30 * 24 * 60 * 60) //6Months
#define HIGH_VOL_TIME_CONDITION2 (12 * 30 * 24 * 60 * 60) //12Months

enum calculation_time_type {
	TOTOL_TIME_CAL_TYPE,
	HIGH_VOL_CAL_TYPE,
	HIGH_TEMP_CAL_TYPE,
	HIGH_TEMP_VOL_CAL_TYPE,
};

struct CYCLE_COUNT_DATA {
	int magic;
	int cycle_count;
	unsigned long battery_total_time;
	unsigned long high_vol_total_time;
	unsigned long high_temp_total_time;
	unsigned long high_temp_vol_time;
	u32 reload_condition;
};

static struct bat_safety_condition safety_cond;

static struct CYCLE_COUNT_DATA g_cycle_count_data = {
	.magic = CYCLE_COUNT_DATA_MAGIC,
	.cycle_count = 0,
	.battery_total_time = 0,
	.high_vol_total_time = 0,
	.high_temp_total_time = 0,
	.high_temp_vol_time = 0,
	.reload_condition = 0
};
struct delayed_work battery_safety_work;

#if defined ASUS_VODKA_PROJECT
#define INIT_FV 4360
#else
#define INIT_FV 4450
#endif

#define BATTERY_HEALTH_UPGRADE_TIME 1
#define BATTERY_METADATA_UPGRADE_TIME 60
#define BAT_HEALTH_DATA_OFFSET 0x0
#define BAT_HEALTH_DATA_MAGIC 0x86
#define BAT_HEALTH_DATA_BACKUP_MAGIC 0x87
#define BAT_HEALTH_DATA_FILE_NAME "/batinfo/bat_health"
#define BAT_HEALTH_START_LEVEL 70
#define BAT_HEALTH_END_LEVEL 100
static bool g_bathealth_initialized = false;
static bool g_bathealth_trigger = false;
static bool g_last_bathealth_trigger = false;
static int g_health_upgrade_index = 0;
static int g_health_upgrade_start_level = BAT_HEALTH_START_LEVEL;
static int g_health_upgrade_end_level = BAT_HEALTH_END_LEVEL;
static int g_health_upgrade_upgrade_time = BATTERY_HEALTH_UPGRADE_TIME;
static int g_bat_health_avg;

#if defined ASUS_VODKA_PROJECT
#define ZF8_DESIGNED_CAPACITY 4750 //mAh //Design Capcaity *0.95 = 4750
#else
#define ZF8_DESIGNED_CAPACITY 3800 //mAh //Design Capcaity *0.95 = 3800
#endif

static struct BAT_HEALTH_DATA g_bat_health_data = {
	.magic = BAT_HEALTH_DATA_MAGIC,
	.bat_health = 0,
	.charge_counter_begin = 0,
	.charge_counter_end = 0
};
static struct BAT_HEALTH_DATA_BACKUP
	g_bat_health_data_backup[BAT_HEALTH_NUMBER_MAX] = {
		{ "", 0 }, { "", 0 }, { "", 0 }, { "", 0 }, { "", 0 },
		{ "", 0 }, { "", 0 }, { "", 0 }, { "", 0 }, { "", 0 },
		{ "", 0 }, { "", 0 }, { "", 0 }, { "", 0 }, { "", 0 },
		{ "", 0 }, { "", 0 }, { "", 0 }, { "", 0 }, { "", 0 },
		{ "", 0 }
	};
struct delayed_work battery_health_work;

static struct notifier_block fb_notif;
static struct drm_panel *active_panel;
struct delayed_work asus_set_panelonoff_current_work;
int g_drm_blank = 0;

int asus_set_panelonoff_charging_current_limit(u32 panelOn)
{
	int rc;
	u32 tmp = panelOn;

	rc = oem_prop_write(BATTMAN_OEM_Panel_Check, &tmp, 1);
	if (rc < 0) {
		pr_err("Failed to set BATTMAN_OEM_Panel_Check rc=%d\n", rc);
		return rc;
	}
	return 0;
}

static int drm_check_dt(struct device_node *np)
{
	int i = 0;
	int count = 0;
	struct device_node *node = NULL;
	struct drm_panel *panel = NULL;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0) {
		pr_err("find drm_panel count(%d) fail", count);
		return -ENODEV;
	}

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			pr_err("find drm_panel successfully");
			active_panel = panel;
			return 0;
		}
	}

	pr_err("no find drm_panel");

	return -ENODEV;
}

void asus_set_panelonoff_current_worker(struct work_struct *work)
{
	if (g_drm_blank == DRM_PANEL_BLANK_UNBLANK) {
		asus_set_panelonoff_charging_current_limit(true);
	} else if (g_drm_blank == DRM_PANEL_BLANK_POWERDOWN) {
		asus_set_panelonoff_charging_current_limit(false);
	}
}

static int drm_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct drm_panel_notifier *evdata = data;
	int *blank = NULL;

	if (!evdata) {
		printk("[BAT][CHG]drm_notifier_callback: evdata is null");
		return 0;
	}

	if (!((event == DRM_PANEL_EARLY_EVENT_BLANK) ||
	      (event == DRM_PANEL_EVENT_BLANK))) {
		pr_err("event(%lu) do not need process", event);
		return 0;
	}

	blank = evdata->data;
	g_drm_blank = *blank;

	switch (*blank) {
	case DRM_PANEL_BLANK_UNBLANK:
		printk("[BAT][CHG] DRM_PANEL_BLANK_UNBLANK,Display on");
		if (DRM_PANEL_EARLY_EVENT_BLANK == event) {
			//pr_debug("resume: event = %lu, not care", event);
		} else if (DRM_PANEL_EVENT_BLANK == event) {
			printk("[BAT][CHG] asus_set_panelonoff_charging_current_limit = true");
			schedule_delayed_work(&asus_set_panelonoff_current_work,
					      0);
		}
		break;
	case DRM_PANEL_BLANK_POWERDOWN:
		printk("[BAT][CHG] DRM_PANEL_BLANK_POWERDOWN,Display off");
		if (DRM_PANEL_EARLY_EVENT_BLANK == event) {
			;
		} else if (DRM_PANEL_EVENT_BLANK == event) {
			printk("[BAT][CHG] asus_set_panelonoff_charging_current_limit = false");
			schedule_delayed_work(&asus_set_panelonoff_current_work,
					      0);
		}
		break;
	case DRM_PANEL_BLANK_LP:
		printk("[BAT][CHG] DRM_PANEL_BLANK_LP,Display resume into LP1/LP2");
		break;
	case DRM_PANEL_BLANK_FPS_CHANGE:
		break;
	default:
		break;
	}

	return 0;
}

void RegisterDRMCallback(void)
{
	int ret = 0;

	pr_err("[BAT][CHG] RegisterDRMCallback");
	ret = drm_check_dt(g_bcdev->dev->of_node);
	if (ret) {
		pr_err("[BAT][CHG] parse drm-panel fail");
	}

	fb_notif.notifier_call = drm_notifier_callback;

	if (active_panel) {
		pr_err("[BAT][CHG] RegisterDRMCallback: registering fb notification");
		ret = drm_panel_notifier_register(active_panel, &fb_notif);
		if (ret)
			pr_err("[BAT][CHG] drm_panel_notifier_register fail: %d",
			       ret);
	}

	return;
}

ssize_t oem_prop_read(enum battman_oem_property prop, size_t count)
{
	struct battman_oem_read_buffer_req_msg req_msg = { { 0 } };
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OPCODE_READ_BUFFER;
	req_msg.oem_property_id = prop;
	req_msg.data_size = count;

	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to read buffer rc=%d\n", rc);
		return rc;
	}

	return count;
}

ssize_t oem_prop_write(enum battman_oem_property prop, u32 *buf, size_t count)
{
	struct battman_oem_write_buffer_req_msg req_msg = { { 0 } };
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OPCODE_WRITE_BUFFER;
	req_msg.oem_property_id = prop;
	memcpy(req_msg.data_buffer, buf, sizeof(u32) * count);
	req_msg.data_size = count;

	if (g_bcdev == NULL) {
		pr_err("g_bcdev is null\n");
		return -1;
	}
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to write buffer rc=%d\n", rc);
		return rc;
	}

	return count;
}

static ssize_t set_virtualthermal_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int mask;
	mask = simple_strtol(buf, NULL, 16);
	ChgPD_Info.thermel_threshold = mask;
	CHG_DBG_E("%s thermel threshold=%d", __func__, mask);

	return count;
}

static ssize_t set_virtualthermal_show(struct class *c,
				       struct class_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "No function\n");
}
static CLASS_ATTR_RW(set_virtualthermal);

static struct attribute *asuslib_class_attrs[] = {
	&class_attr_set_virtualthermal.attr,
	NULL,
};
ATTRIBUTE_GROUPS(asuslib_class);

struct class asuslib_class = {
	.name = "asuslib",
	.class_groups = asuslib_class_groups,
};

int g_temp_Triger = 70000;
int g_temp_Release = 60000;

void asus_usb_thermal_worker(struct work_struct *work)
{
	int rc;
	u32 tmp;
	int conn_temp;

	rc = iio_read_channel_processed(usb_conn_temp_vadc_chan, &conn_temp);
	if (rc < 0)
		CHG_DBG_E("%s: iio_read_channel_processed fail\n", __func__);
	else
		CHG_DBG_E("%s: usb_conn_temp = %d\n", __func__, conn_temp);

	if (conn_temp > g_temp_Triger) {
		if (ChgPD_Info.usb_present) {
			tmp = THERMAL_ALERT_WITH_AC;
			CHG_DBG("%s. set BATTMAN_OEM_THERMAL_ALERT : %d",
				__func__, tmp);
			rc = oem_prop_write(BATTMAN_OEM_THERMAL_ALERT, &tmp, 1);
			if (rc < 0) {
				pr_err("Failed to set BATTMAN_OEM_THERMAL_ALERT rc=%d\n",
				       rc);
			}
		} else {
			tmp = THERMAL_ALERT_NO_AC;
			CHG_DBG("%s. set BATTMAN_OEM_THERMAL_ALERT : %d",
				__func__, tmp);
			rc = oem_prop_write(BATTMAN_OEM_THERMAL_ALERT, &tmp, 1);
			if (rc < 0) {
				pr_err("Failed to set BATTMAN_OEM_THERMAL_ALERT rc=%d\n",
				       rc);
			}
		}

		CHG_DBG("conn_temp(%d) >= 700, usb thermal alert\n", conn_temp);
	} else if (!ChgPD_Info.usb_present && conn_temp < g_temp_Release) {
		tmp = THERMAL_ALERT_NONE;
		CHG_DBG("%s. set BATTMAN_OEM_THERMAL_ALERT : %d", __func__,
			tmp);
		rc = oem_prop_write(BATTMAN_OEM_THERMAL_ALERT, &tmp, 1);
		if (rc < 0) {
			pr_err("Failed to set BATTMAN_OEM_THERMAL_ALERT rc=%d\n",
			       rc);
		}

		CHG_DBG("conn_temp(%d) <= 600, disable usb suspend\n",
			conn_temp);
	}

	schedule_delayed_work(&g_bcdev->asus_usb_thermal_work,
			      msecs_to_jiffies(60000));
}

void asus_thermal_policy_worker(struct work_struct *work)
{
	int rc;
	u32 tmp;

	tmp = ChgPD_Info.thermel_threshold;
	CHG_DBG("%s. set BATTMAN_OEM_THERMAL_THRESHOLD : %d", __func__, tmp);
	rc = oem_prop_write(BATTMAN_OEM_THERMAL_THRESHOLD, &tmp, 1);
	if (rc < 0) {
		pr_err("Failed to set BATTMAN_OEM_THERMAL_THRESHOLD rc=%d\n",
		       rc);
	}

	schedule_delayed_work(&asus_thermal_policy_work, 10 * HZ);
}

int asus_init_power_supply_prop(void)
{
	// Initialize the power supply for battery properties
	if (!qti_phy_bat)
		qti_phy_bat = power_supply_get_by_name("battery");

	if (!qti_phy_bat) {
		pr_err("Failed to get battery power supply, rc=%d\n");
		return -ENODEV;
	}
	return 0;
};

static void handle_notification(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct evtlog_context_resp_msg3 *evtlog_msg;
	struct oem_enable_change_msg *enable_change_msg;
	struct asus_notify_work_event_msg *work_event_msg;
	struct oem_jeita_cc_state_msg *jeita_cc_state_msg;
	struct pmic_glink_hdr *hdr = data;
	int rc;

	switch (hdr->opcode) {
	case OEM_ASUS_EVTLOG_IND:
		if (len == sizeof(*evtlog_msg)) {
			evtlog_msg = data;
			pr_err("[adsp] evtlog= %s\n", evtlog_msg->buf);
		}
		break;
	case OEM_PD_EVTLOG_IND:
		if (len == sizeof(*evtlog_msg)) {
			evtlog_msg = data;
			pr_err("[PD] %s\n", evtlog_msg->buf);
		}
		break;
	case OEM_SET_OTG_WA:
		if (len == sizeof(*enable_change_msg)) {
			enable_change_msg = data;
			CHG_DBG("%s OEM_SET_OTG_WA. enable : %d, HWID : %d\n",
				__func__, enable_change_msg->enable,
				g_ASUS_hwID);
			if (g_ASUS_hwID >= HW_REV_ER) {
				if (gpio_is_valid(OTG_LOAD_SWITCH_GPIO)) {
					rc = gpio_direction_output(
						OTG_LOAD_SWITCH_GPIO,
						enable_change_msg->enable);
					if (rc)
						pr_err("%s. Failed to control OTG_Load_Switch\n",
						       __func__);
				} else {
					CHG_DBG_E(
						"%s. OTG_LOAD_SWITCH_GPIO is invalid\n",
						__func__);
				}
			}
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_OTG_WA\n",
			       len);
		}
		break;
	case OEM_USB_PRESENT:
		if (len == sizeof(*enable_change_msg)) {
			enable_change_msg = data;
			CHG_DBG("%s OEM_USB_PRESENT enable : %d\n", __func__,
				enable_change_msg->enable);
			ChgPD_Info.usb_present = enable_change_msg->enable;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_OTG_WA\n",
			       len);
		}
		break;
	case OEM_ASUS_WORK_EVENT_REQ:
		if (len == sizeof(*work_event_msg)) {
			work_event_msg = data;
			CHG_DBG_E(
				"%s OEM_ASUS_WORK_EVENT_REQ. work=%d, enable=%d\n",
				__func__, work_event_msg->work,
				work_event_msg->data_buffer[0]);
			if (work_event_msg->work == WORK_JEITA_PRECHG) {
				if (work_event_msg->data_buffer[0] == 1) {
					cancel_delayed_work_sync(
						&asus_jeita_prechg_work);
					schedule_delayed_work(
						&asus_jeita_prechg_work, 0);
				} else {
					cancel_delayed_work_sync(
						&asus_jeita_prechg_work);
				}
			} else if (work_event_msg->work == WORK_JEITA_CC) {
				if (work_event_msg->data_buffer[0] == 1) {
					cancel_delayed_work_sync(
						&asus_jeita_cc_work);
					schedule_delayed_work(
						&asus_jeita_cc_work, 5 * HZ);
				} else {
					cancel_delayed_work_sync(
						&asus_jeita_cc_work);
				}
			}
		} else {
			pr_err("Incorrect response length %zu for OEM_ASUS_WORK_EVENT_REQ\n",
			       len);
		}
		break;
	case OEM_JEITA_CC_STATE_REQ:
		if (len == sizeof(*jeita_cc_state_msg)) {
			jeita_cc_state_msg = data;
			CHG_DBG("%s jeita cc state : %d\n", __func__,
				jeita_cc_state_msg->state);
			ChgPD_Info.jeita_cc_state = jeita_cc_state_msg->state;
		} else {
			pr_err("Incorrect response length %zu for OEM_JEITA_CC_STATE_REQ\n",
			       len);
		}
		break;
	default:
		pr_err("Unknown opcode: %u\n", hdr->opcode);
		break;
	}
}

static void handle_message(struct battery_chg_dev *bcdev, void *data,
			   size_t len)
{
	struct battman_oem_read_buffer_resp_msg *oem_read_buffer_resp_msg;
	struct battman_oem_write_buffer_resp_msg *oem_write_buffer_resp_msg;

	struct pmic_glink_hdr *hdr = data;
	bool ack_set = false;

	switch (hdr->opcode) {
	case OEM_OPCODE_READ_BUFFER:
		if (len == sizeof(*oem_read_buffer_resp_msg)) {
			oem_read_buffer_resp_msg = data;
			switch (oem_read_buffer_resp_msg->oem_property_id) {
			default:
				ack_set = true;
				pr_err("Unknown property_id: %u\n",
				       oem_read_buffer_resp_msg
					       ->oem_property_id);
			}
		} else {
			pr_err("Incorrect response length %zu for OEM_OPCODE_READ_BUFFER\n",
			       len);
		}
		break;
	case OEM_OPCODE_WRITE_BUFFER:
		if (len == sizeof(*oem_write_buffer_resp_msg)) {
			oem_write_buffer_resp_msg = data;
			switch (oem_write_buffer_resp_msg->oem_property_id) {
			case BATTMAN_OEM_Panel_Check:
			case BATTMAN_OEM_WORK_EVENT:
			case BATTMAN_OEM_THERMAL_ALERT:
			case BATTMAN_OEM_Batt_Protection:
			case BATTMAN_OEM_CHG_MODE:
			case BATTMAN_OEM_THERMAL_THRESHOLD:
			case BATTMAN_OEM_FV:
				CHG_DBG("%s set property:%d successfully\n",
					__func__,
					oem_write_buffer_resp_msg
						->oem_property_id);
				ack_set = true;
				break;
			default:
				ack_set = true;
				pr_err("Unknown property_id: %u\n",
				       oem_write_buffer_resp_msg
					       ->oem_property_id);
			}
		} else {
			pr_err("Incorrect response length %zu for OEM_OPCODE_READ_BUFFER\n",
			       len);
		}
		break;
	default:
		pr_err("Unknown opcode: %u\n", hdr->opcode);
		ack_set = true;
		break;
	}

	if (ack_set)
		complete(&bcdev->ack);
}

static int asusBC_msg_cb(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;

	// pr_err("owner: %u type: %u opcode: %u len: %zu\n", hdr->owner, hdr->type, hdr->opcode, len);

	if (hdr->owner == PMIC_GLINK_MSG_OWNER_OEM) {
		if (hdr->type == MSG_TYPE_NOTIFY)
			handle_notification(g_bcdev, data, len);
		else
			handle_message(g_bcdev, data, len);
	}
	return 0;
}

static void asusBC_state_cb(void *priv, enum pmic_glink_state state)
{
	pr_err("Enter asusBC_state_cb\n");
}

static void asus_jeita_rule_worker(struct work_struct *dat)
{
	int rc;
	u32 tmp;

	tmp = WORK_JEITA_RULE;
	printk(KERN_ERR
	       "[BAT][CHG]%s set BATTMAN_OEM_WORK_EVENT : WORK_JEITA_RULE",
	       __func__);
	rc = oem_prop_write(BATTMAN_OEM_WORK_EVENT, &tmp, 1);
	if (rc < 0) {
		pr_err("Failed to set BATTMAN_OEM_WORK_EVENT WORK_JEITA_RULE rc=%d\n",
		       rc);
	}

	schedule_delayed_work(&asus_jeita_rule_work, 60 * HZ);
	if (ChgPD_Info.jeita_cc_state > JETA_NONE &&
	    ChgPD_Info.jeita_cc_state < JETA_CV) {
		__pm_wakeup_event(slowchg_ws, 60 * 1000);
	}
}

static void asus_jeita_prechg_worker(struct work_struct *dat)
{
	int rc;
	u32 tmp;

	tmp = WORK_JEITA_PRECHG;
	printk(KERN_ERR
	       "[BAT][CHG]%s set BATTMAN_OEM_WORK_EVENT : WORK_JEITA_PRECHG",
	       __func__);
	rc = oem_prop_write(BATTMAN_OEM_WORK_EVENT, &tmp, 1);
	if (rc < 0) {
		pr_err("Failed to set BATTMAN_OEM_WORK_EVENT WORK_JEITA_PRECHG rc=%d\n",
		       rc);
	}

	schedule_delayed_work(&asus_jeita_prechg_work, HZ);
}

static void asus_jeita_cc_worker(struct work_struct *dat)
{
	int rc;
	u32 tmp;

	tmp = WORK_JEITA_CC;
	printk(KERN_ERR
	       "[BAT][CHG]%s set BATTMAN_OEM_WORK_EVENT : WORK_JEITA_CC",
	       __func__);
	rc = oem_prop_write(BATTMAN_OEM_WORK_EVENT, &tmp, 1);
	if (rc < 0) {
		pr_err("Failed to set BATTMAN_OEM_WORK_EVENT WORK_JEITA_CC rc=%d\n",
		       rc);
	}

	schedule_delayed_work(&asus_jeita_cc_work, 5 * HZ);
}

static void asus_panel_check_worker(struct work_struct *dat)
{
	int rc;
	u32 tmp;

	tmp = WORK_PANEL_CHECK;
	printk(KERN_ERR
	       "[BAT][CHG]%s set BATTMAN_OEM_WORK_EVENT : WORK_PANEL_CHECK",
	       __func__);
	rc = oem_prop_write(BATTMAN_OEM_WORK_EVENT, &tmp, 1);
	if (rc < 0) {
		pr_err("Failed to set BATTMAN_OEM_WORK_EVENT WORK_PANEL_CHECK rc=%d\n",
		       rc);
	}

	schedule_delayed_work(&asus_panel_check_work, 10 * HZ);
}

static void asus_charger_mode_worker(struct work_struct *dat)
{
	int rc;
	u32 tmp;

	tmp = g_Charger_mode;
	CHG_DBG("%s. set BATTMAN_OEM_CHG_MODE : %d", __func__, tmp);
	rc = oem_prop_write(BATTMAN_OEM_CHG_MODE, &tmp, 1);
	if (rc < 0) {
		pr_err("Failed to set BATTMAN_OEM_CHG_MODE rc=%d\n", rc);
	}
}

static void asus_long_full_cap_monitor_worker(struct work_struct *dat)
{
	int rc;
	u32 tmp;

	tmp = WORK_LONG_FULL_CAP;
	CHG_DBG("[BAT][CHG]%s set BATTMAN_OEM_WORK_EVENT : WORK_LONG_FULL_CAP",
		__func__);
	rc = oem_prop_write(BATTMAN_OEM_WORK_EVENT, &tmp, 1);
	if (rc < 0) {
		pr_err("Failed to set BATTMAN_OEM_WORK_EVENT WORK_LONG_FULL_CAP rc=%d\n",
		       rc);
	}

	schedule_delayed_work(&asus_long_full_cap_monitor_work, 30 * HZ);
}

static void asus_18W_workaround_worker(struct work_struct *dat)
{
	int rc;
	u32 tmp;

	tmp = WORK_18W_WORKAROUND;
	printk(KERN_ERR
	       "[BAT][CHG]%s set BATTMAN_OEM_WORK_EVENT : WORK_18W_WORKAROUND",
	       __func__);
	rc = oem_prop_write(BATTMAN_OEM_WORK_EVENT, &tmp, 1);
	if (rc < 0) {
		pr_err("Failed to set BATTMAN_OEM_WORK_EVENT WORK_18W_WORKAROUND rc=%d\n",
		       rc);
	}
}

//start plugin work +++
void asus_monitor_start(int status)
{
	if (!g_asuslib_init)
		return;
	if (asus_usb_online == status)
		return;

	asus_usb_online = status;
	printk(KERN_ERR "[BAT][CHG] asus_monitor_start %d\n", asus_usb_online);
	if (asus_usb_online) {
		cancel_delayed_work_sync(&asus_jeita_rule_work);
		schedule_delayed_work(&asus_jeita_rule_work, 0);

		if (!g_Charger_mode) {
			cancel_delayed_work_sync(&asus_panel_check_work);
			schedule_delayed_work(&asus_panel_check_work, 62 * HZ);
		}

		cancel_delayed_work_sync(&asus_18W_workaround_work);
		schedule_delayed_work(&asus_18W_workaround_work, 26 * HZ);

		cancel_delayed_work_sync(&asus_thermal_policy_work);
		schedule_delayed_work(&asus_thermal_policy_work, 68 * HZ);

		qti_charge_notify_device_charge();
		__pm_wakeup_event(slowchg_ws, 60 * 1000);
	} else {
		cancel_delayed_work_sync(&asus_jeita_rule_work);
		cancel_delayed_work_sync(&asus_jeita_prechg_work);
		cancel_delayed_work_sync(&asus_jeita_cc_work);
		cancel_delayed_work_sync(&asus_panel_check_work);
		cancel_delayed_work_sync(&asus_18W_workaround_work);
		cancel_delayed_work_sync(&asus_thermal_policy_work);
		qti_charge_notify_device_not_charge();
	}
}

static void init_battery_safety(struct bat_safety_condition *cond)
{
	cond->condition1_battery_time = BATTERY_USE_TIME_CONDITION1;
	cond->condition2_battery_time = BATTERY_USE_TIME_CONDITION2;
	cond->condition3_battery_time = BATTERY_USE_TIME_CONDITION3;
	cond->condition4_battery_time = BATTERY_USE_TIME_CONDITION4;
	cond->condition1_cycle_count = CYCLE_COUNT_CONDITION1;
	cond->condition2_cycle_count = CYCLE_COUNT_CONDITION2;
	cond->condition1_temp_vol_time = HIGH_TEMP_VOL_TIME_CONDITION1;
	cond->condition2_temp_vol_time = HIGH_TEMP_VOL_TIME_CONDITION2;
	cond->condition1_temp_time = HIGH_TEMP_TIME_CONDITION1;
	cond->condition2_temp_time = HIGH_TEMP_TIME_CONDITION2;
	cond->condition1_vol_time = HIGH_VOL_TIME_CONDITION1;
	cond->condition2_vol_time = HIGH_VOL_TIME_CONDITION2;
}

static void set_full_charging_voltage(void)
{
	int rc;
	u32 tmp;

#if defined ASUS_VODKA_PROJECT
	if (0 == g_cycle_count_data.reload_condition) {
	} else if (1 == g_cycle_count_data.reload_condition) {
		tmp = INIT_FV;
	} else if (2 == g_cycle_count_data.reload_condition) {
		tmp = INIT_FV;
	} else if (3 == g_cycle_count_data.reload_condition) {
		tmp = INIT_FV - 50;
	} else if (4 == g_cycle_count_data.reload_condition) {
		tmp = INIT_FV - 100;
	}
#else
	if (0 == g_cycle_count_data.reload_condition) {
	} else if (1 == g_cycle_count_data.reload_condition) {
		tmp = INIT_FV - 20;
	} else if (2 == g_cycle_count_data.reload_condition) {
		tmp = INIT_FV - 50;
	} else if (3 == g_cycle_count_data.reload_condition) {
		tmp = INIT_FV - 100;
	} else if (4 == g_cycle_count_data.reload_condition) {
		tmp = INIT_FV - 150;
	}
#endif

	if (0 != g_cycle_count_data.reload_condition) {
		CHG_DBG("%s. set BATTMAN_OEM_FV : %d", __func__, tmp);
		rc = oem_prop_write(BATTMAN_OEM_FV, &tmp, 1);
		if (rc < 0) {
			pr_err("Failed to set BATTMAN_OEM_FV rc=%d\n", rc);
		}
	}
}

static int file_op(const char *filename, loff_t offset, char *buf, int length,
		   int operation)
{
	int filep;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	if (FILE_OP_READ == operation)
		filep = ksys_open(filename, O_RDONLY | O_CREAT | O_SYNC, 0666);
	else if (FILE_OP_WRITE == operation)
		filep = ksys_open(filename, O_RDWR | O_CREAT | O_SYNC, 0666);
	else {
		pr_err("Unknown partition op err!\n");
		return -1;
	}
	if (filep < 0) {
		pr_err("open %s err! error code:%d\n", filename, filep);
		return -1;
	} else
		CHG_DBG("[BAT][CHG]%s open %s success!\n", __func__, filename);

	ksys_lseek(filep, offset, SEEK_SET);
	if (FILE_OP_READ == operation)
		ksys_read(filep, buf, length);
	else if (FILE_OP_WRITE == operation) {
		ksys_write(filep, buf, length);
		ksys_sync();
	}
	ksys_close(filep);
	set_fs(old_fs);
	return length;
}

static int init_batt_cycle_count_data(void)
{
	int rc = 0;
	struct CYCLE_COUNT_DATA buf;

	/* Read cycle count data from emmc */
	rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
		     (char *)&buf, sizeof(struct CYCLE_COUNT_DATA),
		     FILE_OP_READ);
	if (rc < 0) {
		pr_err("Read cycle count file failed!\n");
		return rc;
	}

	/* Check data validation */
	if (buf.magic != CYCLE_COUNT_DATA_MAGIC) {
		pr_err("data validation!\n");
		file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
			(char *)&g_cycle_count_data,
			sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
		return -1;
	} else {
		/* Update current value */
		CHG_DBG("[BAT][CHG]%s Update current value!\n", __func__);
		g_cycle_count_data.cycle_count = buf.cycle_count;
		g_cycle_count_data.high_temp_total_time =
			buf.high_temp_total_time;
		g_cycle_count_data.high_temp_vol_time = buf.high_temp_vol_time;
		g_cycle_count_data.high_vol_total_time =
			buf.high_vol_total_time;
		g_cycle_count_data.reload_condition = buf.reload_condition;
		g_cycle_count_data.battery_total_time = buf.battery_total_time;

		CHG_DBG("[BAT][CHG]%s cycle_count=%d;reload_condition=%d;high_temp_total_time=%lu;high_temp_vol_time=%lu;high_vol_total_time=%lu;battery_total_time=%lu\n",
			__func__, buf.cycle_count, buf.reload_condition,
			buf.high_temp_total_time, buf.high_temp_vol_time,
			buf.high_vol_total_time, buf.battery_total_time);
	}
	CHG_DBG("[BAT][CHG]%s Cycle count data initialize success!\n",
		__func__);
	g_cyclecount_initialized = true;
	set_full_charging_voltage();
	return 0;
}

static void write_back_cycle_count_data(void)
{
	int rc;

	rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
		     (char *)&g_cycle_count_data,
		     sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
	if (rc < 0)
		pr_err("%s:Write file:%s err!\n", __func__,
		       CYCLE_COUNT_FILE_NAME);
}

static void asus_reload_battery_profile(int value)
{
	//save current status
	write_back_cycle_count_data();

	//reloade battery
	//reload_battery_profile(chip);
	set_full_charging_voltage();

	CHG_DBG("[BAT][CHG]%s !!new profile is value=%d\n", __func__, value);
}

static void
asus_judge_reload_condition(struct bat_safety_condition *safety_cond)
{
	int temp_condition = 0;
	int cycle_count = 0;
	// bool full_charge;
	unsigned long local_high_vol_time =
		g_cycle_count_data.high_vol_total_time;
	unsigned long local_high_temp_time =
		g_cycle_count_data.high_temp_total_time;
	//unsigned long local_high_temp_vol_time = g_cycle_count_data.high_temp_vol_time;
	unsigned long local_battery_total_time =
		g_cycle_count_data.battery_total_time;

	temp_condition = g_cycle_count_data.reload_condition;
	if (temp_condition >= 4) { //if condition=2 will return
		return;
	}

	//only full charger can load new profile
	// full_charge = fg->charge_done;
	// if(!full_charge)
	//     return ;

	//1.judge battery using total time
	if (local_battery_total_time >= safety_cond->condition4_battery_time) {
		g_cycle_count_data.reload_condition = 4;
		goto DONE;
	} else if (local_battery_total_time >=
			   safety_cond->condition3_battery_time &&
		   local_battery_total_time <
			   safety_cond->condition4_battery_time) {
		g_cycle_count_data.reload_condition = 3;
	}
#if defined ASUS_SAKE_PROJECT
	else if (local_battery_total_time >=
			 safety_cond->condition2_battery_time &&
		 local_battery_total_time <
			 safety_cond->condition3_battery_time) {
		g_cycle_count_data.reload_condition = 2;
	} else if (local_battery_total_time >=
			   safety_cond->condition1_battery_time &&
		   local_battery_total_time <
			   safety_cond->condition2_battery_time) {
		g_cycle_count_data.reload_condition = 1;
	}
#endif

	//2. judge battery cycle count
	cycle_count = g_cycle_count_data.cycle_count;

	//4. judge high temp condition
	if (local_high_temp_time >= safety_cond->condition2_temp_time) {
		g_cycle_count_data.reload_condition = 4;
		goto DONE;
	} else if (local_high_temp_time >= safety_cond->condition1_temp_time &&
		   local_high_temp_time < safety_cond->condition2_temp_time) {
		g_cycle_count_data.reload_condition = 3;
	}

	//5. judge high voltage condition
	if (local_high_vol_time >= safety_cond->condition2_vol_time) {
		g_cycle_count_data.reload_condition = 4;
		goto DONE;
	} else if (local_high_vol_time >= safety_cond->condition1_vol_time &&
		   local_high_vol_time < safety_cond->condition2_vol_time) {
		g_cycle_count_data.reload_condition = 3;
	}

DONE:
	if (temp_condition != g_cycle_count_data.reload_condition)
		asus_reload_battery_profile(
			g_cycle_count_data.reload_condition);
}

unsigned long last_battery_total_time = 0;
unsigned long last_high_temp_time = 0;
unsigned long last_high_vol_time = 0;
unsigned long last_high_temp_vol_time = 0;

static void calculation_time_fun(int type)
{
	unsigned long now_time;
	unsigned long temp_time = 0;
	struct timespec64 mtNow;

	ktime_get_coarse_real_ts64(&mtNow);

	now_time = mtNow.tv_sec;

	if (now_time < 0) {
		pr_err("asus read rtc time failed!\n");
		return;
	}

	switch (type) {
	case TOTOL_TIME_CAL_TYPE:
		if (0 == last_battery_total_time) {
			last_battery_total_time = now_time;
			CHG_DBG("[BAT][CHG]%s now_time=%lu;last_battery_total_time=%lu\n",
				__func__, now_time,
				g_cycle_count_data.battery_total_time);
		} else {
			temp_time = now_time - last_battery_total_time;
			if (temp_time > 0)
				g_cycle_count_data.battery_total_time +=
					temp_time;
			last_battery_total_time = now_time;
		}
		break;

	case HIGH_VOL_CAL_TYPE:
		if (0 == last_high_vol_time) {
			last_high_vol_time = now_time;
			CHG_DBG("[BAT][CHG]%s now_time=%lu;high_vol_total_time=%lu\n",
				__func__, now_time,
				g_cycle_count_data.high_vol_total_time);
		} else {
			temp_time = now_time - last_high_vol_time;
			if (temp_time > 0)
				g_cycle_count_data.high_vol_total_time +=
					temp_time;
			last_high_vol_time = now_time;
		}
		break;

	case HIGH_TEMP_CAL_TYPE:
		if (0 == last_high_temp_time) {
			last_high_temp_time = now_time;
			CHG_DBG("[BAT][CHG]%s now_time=%lu;high_temp_total_time=%lu\n",
				__func__, now_time,
				g_cycle_count_data.high_temp_total_time);
		} else {
			temp_time = now_time - last_high_temp_time;
			if (temp_time > 0)
				g_cycle_count_data.high_temp_total_time +=
					temp_time;
			last_high_temp_time = now_time;
		}
		break;

	case HIGH_TEMP_VOL_CAL_TYPE:
		if (0 == last_high_temp_vol_time) {
			last_high_temp_vol_time = now_time;
			CHG_DBG("[BAT][CHG]%s now_time=%lu;high_temp_vol_time=%lu\n",
				__func__, now_time,
				g_cycle_count_data.high_temp_vol_time);
		} else {
			temp_time = now_time - last_high_temp_vol_time;
			if (temp_time > 0)
				g_cycle_count_data.high_temp_vol_time +=
					temp_time;
			last_high_temp_vol_time = now_time;
		}
		break;
	}
}

static void update_battery_safe()
{
	int rc;
	int temp;
	int capacity;
	union power_supply_propval prop = {};

	CHG_DBG("[BAT][CHG]%s +++", __func__);

	if (g_asuslib_init != true) {
		pr_err("asuslib init is not ready");
		return;
	}

	if (g_cyclecount_initialized != true) {
		rc = init_batt_cycle_count_data();
		if (rc < 0) {
			pr_err("cyclecount is not initialized");
			return;
		}
	}

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_TEMP,
				       &prop);
	if (rc < 0) {
		pr_err("Error in getting battery temp, rc=%d\n", rc);
	}
	temp = prop.intval;

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_CAPACITY,
				       &prop);
	if (rc < 0) {
		pr_err("Error in getting capacity, rc=%d\n", rc);
	}
	capacity = prop.intval;

	rc = power_supply_get_property(qti_phy_bat,
				       POWER_SUPPLY_PROP_CYCLE_COUNT, &prop);
	if (rc < 0) {
		pr_err("Error in getting cycle count, rc=%d\n", rc);
	}
	g_cycle_count_data.cycle_count = prop.intval;

	CHG_DBG("[BAT][CHG]%s temp=%d, capacity=%d, cycle_count=%d", __func__,
		temp, capacity, g_cycle_count_data.cycle_count);

	//check data
	calculation_time_fun(TOTOL_TIME_CAL_TYPE);

	if (capacity == FULL_CAPACITY_VALUE) {
		calculation_time_fun(HIGH_VOL_CAL_TYPE);
	} else {
		last_high_vol_time = 0; //exit high vol
	}

	if (temp >= HIGHER_TEMP) {
		calculation_time_fun(HIGH_TEMP_CAL_TYPE);
	} else {
		last_high_temp_time = 0; //exit high temp
	}

	if (temp >= HIGH_TEMP && capacity == FULL_CAPACITY_VALUE) {
		calculation_time_fun(HIGH_TEMP_VOL_CAL_TYPE);
	} else {
		last_high_temp_vol_time = 0; //exit high temp and vol
	}

	asus_judge_reload_condition(&safety_cond);
	write_back_cycle_count_data();
	CHG_DBG("[BAT][CHG]%s ---", __func__);
}

void battery_safety_worker(struct work_struct *work)
{
	update_battery_safe();

	schedule_delayed_work(&battery_safety_work,
			      BATTERY_SAFETY_UPGRADE_TIME * HZ);
}
//ASUS_BSP battery safety upgrade ---

//ASUS_BS battery health upgrade +++
static void battery_health_data_reset(void)
{
	g_bat_health_data.charge_counter_begin = 0;
	g_bat_health_data.charge_counter_end = 0;
	g_bathealth_trigger = false;
	g_last_bathealth_trigger = false;
}

static int restore_bat_health(void)
{
	int i = 0, rc = 0, count = 0;

	memset(&g_bat_health_data_backup, 0,
	       sizeof(struct BAT_HEALTH_DATA_BACKUP) * BAT_HEALTH_NUMBER_MAX);

	/* Read cycle count data from emmc */
	rc = file_op(BAT_HEALTH_DATA_FILE_NAME, BAT_HEALTH_DATA_OFFSET,
		     (char *)&g_bat_health_data_backup,
		     sizeof(struct BAT_HEALTH_DATA_BACKUP) *
			     BAT_HEALTH_NUMBER_MAX,
		     FILE_OP_READ);
	if (rc < 0) {
		pr_err("Read bat health file failed!\n");
		return -1;
	}

	for (i = 1; i < BAT_HEALTH_NUMBER_MAX; i++) {
		CHG_DBG("[BAT][CHG]%s %s %d", __func__,
			g_bat_health_data_backup[i].date,
			g_bat_health_data_backup[i].health);

		if (g_bat_health_data_backup[i].health != 0) {
			count++;
		}
	}

	if (count >= BAT_HEALTH_NUMBER_MAX - 1) {
		g_health_upgrade_index = BAT_HEALTH_NUMBER_MAX - 1;
		g_bat_health_data_backup[0].health = BAT_HEALTH_NUMBER_MAX - 1;
	}

	CHG_DBG("[BAT][CHG]%s index(%d)\n", __func__,
		g_bat_health_data_backup[0].health);

	g_health_upgrade_index = g_bat_health_data_backup[0].health;
	g_bathealth_initialized = true;

	return 0;
}

static void resequencing_bat_health_data(void)
{
	int i;

	for (i = 1; i < BAT_HEALTH_NUMBER_MAX - 1; i++) {
		memcpy(&g_bat_health_data_backup[i],
		       &g_bat_health_data_backup[i + 1],
		       sizeof(struct BAT_HEALTH_DATA_BACKUP));
	}
}

static int backup_bat_health(void)
{
	int bat_health, rc;
	struct timespec ts;
	struct rtc_time tm;
	int health_t;
	int count = 0, i = 0;
	unsigned long long bat_health_accumulate = 0;

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);

	bat_health = g_bat_health_data.bat_health;

	if (g_health_upgrade_index >= BAT_HEALTH_NUMBER_MAX - 1) {
		g_health_upgrade_index = BAT_HEALTH_NUMBER_MAX - 1;
	} else {
		g_health_upgrade_index++;
	}

	if (g_health_upgrade_index >= BAT_HEALTH_NUMBER_MAX - 1) {
		resequencing_bat_health_data();
	}

	sprintf(g_bat_health_data_backup[g_health_upgrade_index].date,
		"%d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1,
		tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	g_bat_health_data_backup[g_health_upgrade_index].health = bat_health;
	g_bat_health_data_backup[0].health = g_health_upgrade_index;

	CHG_DBG("[BAT][CHG]%s ===== Health history ====\n", __func__);
	for (i = 1; i < BAT_HEALTH_NUMBER_MAX; i++) {
		if (g_bat_health_data_backup[i].health != 0) {
			count++;
			bat_health_accumulate +=
				g_bat_health_data_backup[i].health;
			CHG_DBG("[BAT][CHG]%s %02d:%d\n", __func__, i,
				g_bat_health_data_backup[i].health);
		}
	}
	CHG_DBG("[BAT][CHG]%s  ========================\n", __func__);
	if (count == 0) {
		CHG_DBG("[BAT][CHG]%s battery health value is empty\n",
			__func__);
		return -1;
	}
	health_t = bat_health_accumulate * 10 / count;
	g_bat_health_avg = (int)(health_t + 5) / 10;
	g_bat_health_data_backup[g_health_upgrade_index].health =
		g_bat_health_avg;

	rc = file_op(BAT_HEALTH_DATA_FILE_NAME, BAT_HEALTH_DATA_OFFSET,
		     (char *)&g_bat_health_data_backup,
		     sizeof(struct BAT_HEALTH_DATA_BACKUP) *
			     BAT_HEALTH_NUMBER_MAX,
		     FILE_OP_WRITE);
	if (rc < 0) {
		pr_err("%s:Write file:%s err!\n", __func__,
		       BAT_HEALTH_DATA_FILE_NAME);
	}

	return rc;
}

static void update_battery_health()
{
	int bat_capacity, rc;
	union power_supply_propval prop = {};

	if (g_bathealth_initialized != true) {
		restore_bat_health();
		return;
	}

	if (!asus_usb_online) {
		if (g_bathealth_trigger == true) {
			battery_health_data_reset();
		}
		return;
	}

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_CAPACITY,
				       &prop);
	if (rc < 0) {
		pr_err("Error in getting capacity, rc=%d\n", rc);
	}
	bat_capacity = prop.intval;

	if (bat_capacity == g_health_upgrade_start_level &&
	    !g_bathealth_trigger) {
		g_bathealth_trigger = true;

		rc = power_supply_get_property(
			qti_phy_bat, POWER_SUPPLY_PROP_CHARGE_COUNTER, &prop);
		if (rc < 0) {
			pr_err("Error in getting current, rc=%d\n", rc);
		}
		g_bat_health_data.charge_counter_begin = prop.intval;

		CHG_DBG("[BAT][CHG]%s battery health begin charge_counter=%d",
			__func__, g_bat_health_data.charge_counter_begin);
	} else if (bat_capacity == g_health_upgrade_end_level &&
		   g_bathealth_trigger) {
		g_bathealth_trigger = false;

		rc = power_supply_get_property(
			qti_phy_bat, POWER_SUPPLY_PROP_CHARGE_COUNTER, &prop);
		if (rc < 0) {
			pr_err("Error in getting current, rc=%d\n", rc);
		}
		g_bat_health_data.charge_counter_end = prop.intval;

		//(coulomb100%-coulomb70%)/1000/30/(Realcapacity/100)*100%
		g_bat_health_data.bat_health =
			(g_bat_health_data.charge_counter_end -
			 g_bat_health_data.charge_counter_begin) *
			10 /
			(ZF8_DESIGNED_CAPACITY *
			 (g_health_upgrade_end_level -
			  g_health_upgrade_start_level));
		if (g_bat_health_data.bat_health < 0)
			g_bat_health_data.bat_health = 0;
		if (g_bat_health_data.bat_health > 100)
			g_bat_health_data.bat_health = 100;

		backup_bat_health();
		CHG_DBG("[BAT][CHG]%s battery health = (%d,%d), charge_counter begin=%d, charge_counter end=%d",
			__func__, g_bat_health_data.bat_health,
			g_bat_health_avg,
			g_bat_health_data.charge_counter_begin,
			g_bat_health_data.charge_counter_end);

		battery_health_data_reset();
	} else if (bat_capacity >= g_health_upgrade_start_level &&
		   bat_capacity <= g_health_upgrade_end_level) {
	} else {
		g_bathealth_trigger = false;
		battery_health_data_reset();
	}
}

void battery_health_worker(struct work_struct *work)
{
	update_battery_health();

	schedule_delayed_work(&battery_health_work,
			      g_health_upgrade_upgrade_time * HZ);
}

static int reboot_shutdown_prep(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	switch (event) {
	case SYS_RESTART:
	case SYS_POWER_OFF:
		/* Write data back to emmc */
		CHG_DBG_E("[BAT][CHG]%s John", __func__);
		write_back_cycle_count_data();
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}
/*  Call back function for reboot notifier chain  */
static struct notifier_block reboot_blk = {
	.notifier_call = reboot_shutdown_prep,
};

int asuslib_init(void)
{
	int rc = 0;
	struct pmic_glink_client_data client_data = {};
	struct pmic_glink_client *client;

	printk(KERN_ERR "%s +++\n", __func__);
	// Initialize the necessary power supply
	rc = asus_init_power_supply_prop();
	if (rc < 0) {
		pr_err("Failed to init power_supply chains\n");
		return rc;
	}

	// Register the class node
	rc = class_register(&asuslib_class);
	if (rc) {
		pr_err("%s: Failed to register asuslib class\n", __func__);
		return -1;
	}

	if (g_ASUS_hwID >= HW_REV_ER) {
		OTG_LOAD_SWITCH_GPIO = of_get_named_gpio(g_bcdev->dev->of_node,
							 "OTG_LOAD_SWITCH", 0);
		rc = gpio_request(OTG_LOAD_SWITCH_GPIO, "OTG_LOAD_SWITCH");
		if (rc) {
			pr_err("%s: Failed to initalize the OTG_LOAD_SWITCH\n",
			       __func__);
			return -1;
		}

		if (gpio_is_valid(OTG_LOAD_SWITCH_GPIO)) {
			rc = gpio_direction_output(OTG_LOAD_SWITCH_GPIO, 0);
			if (rc)
				pr_err("%s. Failed to control OTG_Load_Switch\n",
				       __func__);
		} else {
			CHG_DBG_E("%s. OTG_LOAD_SWITCH_GPIO is invalid\n",
				  __func__);
		}
	}

	client_data.id = PMIC_GLINK_MSG_OWNER_OEM;
	client_data.name = "asus_BC";
	client_data.msg_cb = asusBC_msg_cb;
	client_data.priv = g_bcdev;
	client_data.state_cb = asusBC_state_cb;
	client = pmic_glink_register_client(g_bcdev->dev, &client_data);
	if (IS_ERR(client)) {
		rc = PTR_ERR(client);
		if (rc != -EPROBE_DEFER)
			dev_err(g_bcdev->dev,
				"Error in registering with pmic_glink %d\n",
				rc);
		return rc;
	}

	slowchg_ws = wakeup_source_register(g_bcdev->dev, "Slowchg_wakelock");

	usb_conn_temp_vadc_chan =
		iio_channel_get(g_bcdev->dev, "pm8350b_amux_thm4");
	if (IS_ERR_OR_NULL(usb_conn_temp_vadc_chan)) {
		CHG_DBG_E("%s: usb_conn_temp iio_channel_get fail\n", __func__);
	}

	INIT_DELAYED_WORK(&g_bcdev->asus_usb_thermal_work,
			  asus_usb_thermal_worker);
	schedule_delayed_work(&g_bcdev->asus_usb_thermal_work,
			      msecs_to_jiffies(0));

	//thermal policy
	INIT_DELAYED_WORK(&asus_thermal_policy_work,
			  asus_thermal_policy_worker);

	//register drm notifier
	INIT_DELAYED_WORK(&asus_set_panelonoff_current_work,
			  asus_set_panelonoff_current_worker);
	RegisterDRMCallback();

	//jeita rule work
	INIT_DELAYED_WORK(&asus_jeita_rule_work, asus_jeita_rule_worker);
	INIT_DELAYED_WORK(&asus_jeita_prechg_work, asus_jeita_prechg_worker);
	INIT_DELAYED_WORK(&asus_jeita_cc_work, asus_jeita_cc_worker);

	//panel check work
	INIT_DELAYED_WORK(&asus_panel_check_work, asus_panel_check_worker);

	INIT_DELAYED_WORK(&asus_charger_mode_work, asus_charger_mode_worker);
	schedule_delayed_work(&asus_charger_mode_work, 0);

	//implement the asus owns algorithm of detection full capacity
	INIT_DELAYED_WORK(&asus_long_full_cap_monitor_work,
			  asus_long_full_cap_monitor_worker);
	schedule_delayed_work(&asus_long_full_cap_monitor_work,
			      msecs_to_jiffies(0));

	//18W workaround work
	INIT_DELAYED_WORK(&asus_18W_workaround_work,
			  asus_18W_workaround_worker);

	//battery safety upgrade
	INIT_DELAYED_WORK(&battery_safety_work, battery_safety_worker);
	init_battery_safety(&safety_cond);
	register_reboot_notifier(&reboot_blk);
	schedule_delayed_work(&battery_safety_work, 30 * HZ);

	INIT_DELAYED_WORK(&battery_health_work, battery_health_worker);
	battery_health_data_reset();
	schedule_delayed_work(&battery_health_work, 30 * HZ);

	CHG_DBG_E("Load the asuslib_init Succesfully\n");
	g_asuslib_init = true;
	return rc;
}

int asuslib_deinit(void)
{
	g_asuslib_init = false;
	class_unregister(&asuslib_class);
	wakeup_source_unregister(slowchg_ws);
	return 0;
}
