/*
 * Copyright (c) 2019-2020, The ASUS Company. All rights reserved.
 */

#define MSG_OWNER_BC 32778

struct battery_charger_req_msg {
	struct pmic_glink_hdr hdr;
	u32 battery_id;
	u32 property_id;
	u32 value;
};

#define PMIC_GLINK_MSG_OWNER_OEM 32782

#define MSG_TYPE_REQ_RESP 1
#define MSG_TYPE_NOTIFY 2

#define OEM_OPCODE_READ_BUFFER 0x10000
#define OEM_OPCODE_WRITE_BUFFER 0x10001

#define OEM_ASUS_EVTLOG_IND 0x1002
#define OEM_PD_EVTLOG_IND 0x1003
#define OEM_SET_OTG_WA 0x2107
#define OEM_USB_PRESENT 0x2108
#define OEM_ASUS_WORK_EVENT_REQ 0x2110
#define OEM_JEITA_CC_STATE_REQ 0x2112

#define MAX_OEM_PROPERTY_DATA_SIZE 16

struct ADSP_ChargerPD_Info {
	bool usb_present;
	int jeita_cc_state;
	int thermel_threshold;
};

struct battman_oem_read_buffer_req_msg {
	struct pmic_glink_hdr hdr;
	u32 oem_property_id;
	u32 data_size;
};

struct battman_oem_read_buffer_resp_msg {
	struct pmic_glink_hdr hdr;
	u32 oem_property_id;
	u32 data_buffer[MAX_OEM_PROPERTY_DATA_SIZE];
	u32 data_size; //size = 0 if failed, otherwise should be data_size.
};

struct battman_oem_write_buffer_req_msg {
	struct pmic_glink_hdr hdr;
	u32 oem_property_id;
	u32 data_buffer[MAX_OEM_PROPERTY_DATA_SIZE];
	u32 data_size;
};

struct battman_oem_write_buffer_resp_msg {
	struct pmic_glink_hdr hdr;
	u32 oem_property_id;
	u32 return_status;
};

struct evtlog_context_resp_msg3 {
	struct pmic_glink_hdr hdr;
	u8 buf[128];
	u32 reserved;
};

struct oem_enable_change_msg {
	struct pmic_glink_hdr hdr;
	u32 enable;
};

struct asus_notify_work_event_msg {
	struct pmic_glink_hdr header;
	u32 work;
	u32 data_buffer[MAX_OEM_PROPERTY_DATA_SIZE];
	u32 data_size;
};

struct oem_jeita_cc_state_msg {
	struct pmic_glink_hdr header;
	u32 state;
};

enum battman_oem_property {
	BATTMAN_OEM_ADSP_PLATFORM_ID,
	BATTMAN_OEM_BATT_ID,
	BATTMAN_OEM_CHG_LIMIT_EN,
	BATTMAN_OEM_CHG_LIMIT_CAP,
	BATTMAN_OEM_USBIN_SUSPEND,
	BATTMAN_OEM_CHARGING_SUSPNED,
	BATTMAN_OEM_CHGPD_FW_VER,
	BATTMAN_OEM_FW_VERSION,
	BATTMAN_OEM_BATT_TEMP,
	BATTMAN_OEM_PM8350B_ICL,
	BATTMAN_OEM_SMB1396_ICL,
	BATTMAN_OEM_FCC,
	BATTMAN_OEM_DEBUG_MASK,
	BATTMAN_OEM_AdapterVID,
	BATTMAN_OEM_SMB_Setting,
	BATTMAN_OEM_Panel_Check,
	BATTMAN_OEM_WORK_EVENT,
	BATTMAN_OEM_THERMAL_ALERT,
	BATTMAN_OEM_Write_PM8350B_Register,
	BATTMAN_OEM_Slow_Chg,
	BATTMAN_OEM_Batt_Protection,
	BATTMAN_OEM_CHG_Disable_Jeita,
	BATTMAN_OEM_CHG_MODE,
	BATTMAN_OEM_THERMAL_THRESHOLD,
	BATTMAN_OEM_THERMAL_SENSOR,
	BATTMAN_OEM_FV,
	BATTMAN_OEM_In_Call,
	BATTMAN_OEM_PROPERTY_MAX,
};

enum Work_ID {
	WORK_JEITA_RULE,
	WORK_JEITA_PRECHG,
	WORK_JEITA_CC,
	WORK_PANEL_CHECK,
	WORK_LONG_FULL_CAP,
	WORK_18W_WORKAROUND,
	WORK_MAX
};

enum thermal_alert_state {
	THERMAL_ALERT_NONE,
	THERMAL_ALERT_NO_AC,
	THERMAL_ALERT_WITH_AC,
	THERMAL_ALERT_MAX
};

enum JETA_CURR {
	JETA_NONE,
	JETA_PRECHG,
	JETA_CC1,
	JETA_CC2,
	JETA_CV,
	JETA_MAX
};

#define CHARGER_TAG "[BAT][CHG]"
#define ERROR_TAG "[ERR]"
#define CHG_DBG(...) printk(KERN_INFO CHARGER_TAG __VA_ARGS__)
#define CHG_DBG_E(...) printk(KERN_ERR CHARGER_TAG ERROR_TAG __VA_ARGS__)

struct delayed_work asus_jeita_rule_work;
struct delayed_work asus_jeita_prechg_work;
struct delayed_work asus_jeita_cc_work;
struct delayed_work asus_panel_check_work;
struct delayed_work asus_charger_mode_work;
struct delayed_work asus_long_full_cap_monitor_work;
struct delayed_work asus_18W_workaround_work;
struct delayed_work asus_thermal_policy_work;

extern struct battery_chg_dev *g_bcdev;
struct power_supply *qti_phy_bat;
int OTG_LOAD_SWITCH_GPIO;
struct ADSP_ChargerPD_Info ChgPD_Info;

ssize_t oem_prop_read(enum battman_oem_property prop, size_t count);
ssize_t oem_prop_write(enum battman_oem_property prop, u32 *buf, size_t count);
//[---] Add the global variables
