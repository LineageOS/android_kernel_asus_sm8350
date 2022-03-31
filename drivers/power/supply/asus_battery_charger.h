// SPDX-License-Identifier: GPL-2.0-only

#ifndef _ASUS_BATTERY_CHARGER_H
#define _ASUS_BATTERY_CHARGER_H

#include <linux/soc/qcom/battery_charger.h>

struct asus_battery_chg {
	struct battery_chg_dev		*bcdev;
};

int asus_battery_charger_init(struct asus_battery_chg *abc);
int asus_battery_charger_deinit(struct asus_battery_chg *abc);

#endif // _ASUS_BATTERY_CHARGER_H
