// SPDX-License-Identifier: GPL-2.0-only

#ifndef _ASUS_BATTERY_CHARGER_H
#define _ASUS_BATTERY_CHARGER_H

#include <drm/drm_panel.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/soc/qcom/battery_charger.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/pm_wakeup.h>
#include <linux/workqueue.h>

struct cycle_count_data {
	int				magic;
	int				cycle_count;
	unsigned long			battery_total_time;
	unsigned long			high_vol_total_time;
	unsigned long			high_temp_total_time;
	unsigned long			high_temp_vol_time;
	u32				reload_condition;
};

struct asus_battery_chg {
	bool				initialized;
	struct class			asuslib_class;
	unsigned long			last_battery_total_time;
	unsigned long			last_high_temp_time;
	unsigned long			last_high_vol_time;
	struct cycle_count_data		cycle_count_data;
	bool				cycle_count_data_initialized;
	struct battery_chg_dev		*bcdev;
	struct power_supply		*batt_psy;
	struct pmic_glink_client	*client;
	struct wakeup_source		*slowchg_ws;
	struct gpio_desc		*otg_switch;
	struct drm_panel		*panel;
	struct notifier_block		reboot_notif;
	struct notifier_block		drm_notif;
	bool				panel_on;
	struct delayed_work		panel_state_work;
	bool				usb_present;
	struct iio_channel		*temp_chan;
	struct delayed_work		usb_thermal_work;
	struct notifier_block		usb_online_notifier;
	bool				usb_online;
	int				thermal_threshold;
	struct delayed_work		panel_check_work;
	struct delayed_work		charger_mode_work;
	struct delayed_work		workaround_18w_work;
	struct delayed_work		thermal_policy_work;
	int				jeita_cc_state;
	struct delayed_work		jeita_rule_work;
	struct delayed_work		jeita_prechg_work;
	struct delayed_work		jeita_cc_work;
	struct delayed_work		full_cap_monitor_work;
	struct delayed_work		battery_safety_work;
};

int asus_battery_charger_init(struct asus_battery_chg *abc);
int asus_battery_charger_deinit(struct asus_battery_chg *abc);

#endif // _ASUS_BATTERY_CHARGER_H
