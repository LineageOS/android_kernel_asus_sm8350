// SPDX-License-Identifier: GPL-2.0-only

#ifndef _ASUS_BATTERY_CHARGER_H
#define _ASUS_BATTERY_CHARGER_H

#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/soc/qcom/battery_charger.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/workqueue.h>

struct asus_battery_chg {
	bool				initialized;
	struct class			asuslib_class;
	struct battery_chg_dev		*bcdev;
	struct pmic_glink_client	*client;
	struct gpio_desc		*otg_switch;
	bool				usb_present;
	struct iio_channel		*temp_chan;
	struct delayed_work		usb_thermal_work;
	struct notifier_block		usb_online_notifier;
	bool				usb_online;
	int				thermal_threshold;
	struct delayed_work		thermal_policy_work;
};

int asus_battery_charger_init(struct asus_battery_chg *abc);
int asus_battery_charger_deinit(struct asus_battery_chg *abc);

#endif // _ASUS_BATTERY_CHARGER_H
