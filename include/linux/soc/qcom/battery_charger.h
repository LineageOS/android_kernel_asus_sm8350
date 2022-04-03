/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _BATTERY_CHARGER_H
#define _BATTERY_CHARGER_H

#include <linux/notifier.h>
#include <linux/device.h>

enum battery_charger_prop {
	BATTERY_RESISTANCE,
	BATTERY_CHARGER_PROP_MAX,
};

#if IS_ENABLED(CONFIG_QTI_BATTERY_CHARGER)
struct battery_chg_dev;

int battery_chg_write(struct battery_chg_dev *bcdev, void *data, int len);
void battery_chg_complete_ack(struct battery_chg_dev *bcdev);
struct device *battery_chg_device(struct battery_chg_dev *bcdev);
int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val);
void qti_charge_register_notify(struct notifier_block *nb);
void qti_charge_unregister_notify(struct notifier_block *nb);
#else
static inline int
qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	return -EINVAL;
}
void qti_charge_register_notify(struct notifier_block *nb) {}
void qti_charge_unregister_notify(struct notifier_block *nb) {}
#endif

#endif
