/* SPDX-License-Identifier: GPL-2.0
 * aw882xx_device.h
 *
 * Copyright (c) 2020 AWINIC Technology CO., LTD
 *
 * Author: Nick Li <liweilei@awinic.com.cn>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __AW882XX_DEVICE_FILE_H__
#define __AW882XX_DEVICE_FILE_H__
#include <linux/i2c.h>
#include "aw882xx_data_type.h"
#include "aw882xx_calib.h"
#include "aw882xx_monitor.h"
#include "aw882xx_dsp.h"

#define AW_VOLUME_STEP_DB	(6 * 2)
#define AW_REG_NONE		(0xFF)
#define AW_NAME_MAX		(50)
#define ALGO_VERSION_MAX	(80)

#define AW_GET_MIN_VALUE(value1, value2) \
	((value1) > (value2) ? (value2) : (value1))

#define AW_GET_MAX_VALUE(value1, value2)  \
	((value1) > (value2) ? (value1) : (value2))

#define AW_LOCK_SEQUENCE_MAX (2)

extern int g_algo_auth_st;

enum {
	AW_1000_US = 1000,
	AW_2000_US = 2000,
	AW_3000_US = 3000,
	AW_4000_US = 4000,
	AW_5000_US = 5000,
	AW_10000_US = 10000,
	AW_32000_US = 32000,
	AW_70000_US = 70000,
	AW_100000_US = 100000,
};

struct aw_device;

enum {
	AW_DEV_TYPE_NONE = 0,
	AW_DEV_TYPE_OK,
};

enum {
	AW_EF_AND_CHECK = 0,
	AW_EF_OR_CHECK,
};

enum {
	AW_DEV_CH_PRI_L = 0,
	AW_DEV_CH_PRI_R = 1,
	AW_DEV_CH_SEC_L = 2,
	AW_DEV_CH_SEC_R = 3,
	AW_DEV_CH_TERT_L = 4,
	AW_DEV_CH_TERT_R = 5,
	AW_DEV_CH_QUAT_L = 6,
	AW_DEV_CH_QUAT_R = 7,
	AW_DEV_CH_MAX,
};

enum AW_DEV_INIT {
	AW_DEV_INIT_ST = 0,
	AW_DEV_INIT_OK = 1,
	AW_DEV_INIT_NG = 2,
};

enum AW_DEV_STATUS {
	AW_DEV_PW_OFF = 0,
	AW_DEV_PW_ON,
};

enum AW_DEV_FW_STATUS {
	AW_DEV_FW_FAILED = 0,
	AW_DEV_FW_OK,
};



enum AW_SPIN_KCONTROL_STATUS {
	AW_SPIN_KCONTROL_DISABLE = 0,
	AW_SPIN_KCONTROL_ENABLE,
};

enum AW_ALGO_AUTH_MODE {
	AW_ALGO_AUTH_DISABLE = 0,
	AW_ALGO_AUTH_MODE_MAGIC_ID,
	AW_ALGO_AUTH_MODE_REG_CRC,
};

enum AW_ALGO_AUTH_ID {
	AW_ALGO_AUTH_MAGIC_ID = 0x4157,
};

enum AW_ALGO_AUTH_STATUS {
	AW_ALGO_AUTH_WAIT = 0,
	AW_ALGO_AUTH_OK = 1,
};

struct aw_device_ops {
	int (*aw_get_version)(char *buf, int size);
	int (*aw_get_dev_num)(void);
	int (*aw_i2c_write)(struct i2c_client *i2c, unsigned char reg_addr, unsigned int reg_data);
	int (*aw_i2c_read)(struct i2c_client *i2c, unsigned char reg_addr, unsigned int *reg_data);
	int (*aw_i2c_write_bits)(struct i2c_client *i2c, unsigned char reg_addr, unsigned int mask, unsigned int reg_data);
	unsigned int (*aw_reg_val_to_db)(unsigned int value);
	unsigned int (*aw_db_val_to_reg)(unsigned int value);
	unsigned int (*aw_get_irq_type)(struct aw_device *aw_dev, unsigned int value);
	void (*aw_reg_force_set)(struct aw_device *aw_dev);
	int (*aw_frcset_check)(struct aw_device *aw_dev);
	int (*aw_get_voltage_offset)(struct aw_device *aw_dev, int *offset);
};

struct aw_int_desc {
	unsigned int mask_reg;			/*interrupt mask reg*/
	unsigned int st_reg;			/*interrupt status reg*/
	unsigned int mask_default;		/*default mask close all*/
	unsigned int int_mask;			/*set mask*/
};

struct aw_work_mode {
	unsigned int reg;
	unsigned int mask;
	unsigned int spk_val;
	unsigned int rcv_val;
};

struct aw_soft_rst {
	int reg;
	int reg_value;
};

struct aw_switch_desc {
	char *name;

	unsigned int reg;
	unsigned int mask;
	unsigned int enable;
	unsigned int disable;

};

struct aw_vcalb_desc {
	unsigned int icalk_reg;
	unsigned int icalk_reg_mask;
	unsigned int icalk_shift;
	unsigned int icalkl_reg;
	unsigned int icalkl_reg_mask;
	unsigned int icalkl_shift;
	unsigned int icalk_sign_mask;
	unsigned int icalk_neg_mask;
	int icalk_value_factor;

	unsigned int vcalk_reg;
	unsigned int vcalk_reg_mask;
	unsigned int vcalk_shift;
	unsigned int vcalkl_reg;
	unsigned int vcalkl_reg_mask;
	unsigned int vcalkl_shift;
	unsigned int vcalk_sign_mask;
	unsigned int vcalk_neg_mask;
	int vcalk_value_factor;

	unsigned int vcalb_reg;
	int cabl_base_value;
	int vcal_factor;
};



struct aw_txen_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int enable;
	unsigned int disable;
	unsigned int cfg_val;
};

struct aw_sysst_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int st_check;
	unsigned int st_sws_check;
	unsigned int pll_check;
};

struct aw_profctrl_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int spk_mode;
	unsigned int cfg_mode;
};

struct aw_bstctrl_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int frc_bst;
	unsigned int tsp_type;
	unsigned int cfg_type;
};

struct aw_cco_mux_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int divided_val;
	unsigned int bypass_val;
};

struct aw_volume_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int shift;
	int init_volume;
	int mute_volume;
	int ctl_volume;
	int monitor_volume;
};

struct aw_voltage_desc {
	unsigned int reg;
	unsigned int vbat_range;
	unsigned int int_bit;
};

struct aw_temperature_desc {
	unsigned int reg;
	unsigned int sign_mask;
	unsigned int neg_mask;
};

struct aw_ipeak_desc {
	unsigned int reg;
	unsigned int mask;
};

struct aw_spin_ch {
	uint16_t rx_val;
};

struct aw_reg_ch {
	unsigned int reg;
	unsigned int mask;
	unsigned int left_val;
	unsigned int right_val;
};

struct aw_spin_desc {
	int aw_spin_kcontrol_st;
	struct aw_spin_ch spin_table[AW_SPIN_MAX];
	struct aw_reg_ch rx_desc;
};

struct aw_ef_unit {
	unsigned int reg;
	unsigned int mask;
	unsigned int check_val;
};
struct aw_ef_desc {
	unsigned int count;
	struct aw_ef_unit sequence[AW_LOCK_SEQUENCE_MAX];
};

struct aw_efcheck_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int and_val;
	unsigned int or_val;
};


struct aw_noise_gate_desc {
	unsigned int reg;
	unsigned int mask;



};

struct aw_auth_desc {
	uint8_t reg_in;
	uint8_t reg_out;
	int32_t auth_mode;
	int32_t reg_crc;
	int32_t random;
	int32_t chip_id;
	int32_t check_result;
};

struct algo_auth_data {
	int32_t auth_mode;  /* 0: disable  1 : chip ID  2 : reg crc */
	int32_t reg_crc;
	int32_t random;
	int32_t chip_id;
	int32_t check_result;
};

#define AW_IOCTL_MAGIC_S			'w'
#define AW_IOCTL_GET_ALGO_AUTH			_IOWR(AW_IOCTL_MAGIC_S, 1, struct algo_auth_data)
#define AW_IOCTL_SET_ALGO_AUTH			_IOWR(AW_IOCTL_MAGIC_S, 2, struct algo_auth_data)


struct aw_device {
	int status;
	unsigned int chip_id;
	unsigned int monitor_start;
	int frcset_en;
	int bop_en;
	int efuse_check;
	int fade_en;
	int reg_num;
	unsigned int mute_st;
	unsigned int amppd_st;
	unsigned int dither_st;
	unsigned int txen_st;

	unsigned char cur_prof;  /*current profile index*/
	unsigned char set_prof;  /*set profile index*/
	unsigned int channel;    /*pa channel select*/
	unsigned int vol_step;
	unsigned int re_max;
	unsigned int re_min;
	int voltage_offset_debug;
	int32_t voltage_offset;

	struct device *dev;
	struct i2c_client *i2c;
	char monitor_name[AW_NAME_MAX];
	struct workqueue_struct *work_queue;

	struct aw_ef_desc ef_desc;
	struct aw_efcheck_desc efcheck_desc;
	struct aw_profctrl_desc profctrl_desc;
	struct aw_bstctrl_desc bstctrl_desc;
	struct aw_int_desc int_desc;
	struct aw_work_mode work_mode;
	struct aw_switch_desc pwd_desc;
	struct aw_switch_desc amppd_desc;
	struct aw_switch_desc mute_desc;
	struct aw_switch_desc uls_hmute_desc;
	struct aw_txen_desc txen_desc;
	struct aw_vcalb_desc vcalb_desc;
	struct aw_sysst_desc sysst_desc;

	struct aw_cco_mux_desc cco_mux_desc;
	struct aw_voltage_desc voltage_desc;
	struct aw_temperature_desc temp_desc;
	struct aw_ipeak_desc ipeak_desc;
	struct aw_volume_desc volume_desc;
	struct aw_prof_info prof_info;
	struct aw_cali_desc cali_desc;
	struct aw_monitor_desc monitor_desc;
	struct aw_soft_rst soft_rst;
	struct aw_spin_desc spin_desc;
	struct aw_switch_desc bop_desc;

	struct aw_switch_desc dither_desc;
	struct aw_switch_desc noise_gate_desc;

	struct aw_switch_desc psm_desc;
	struct aw_switch_desc mpd_desc;
	struct aw_switch_desc dsmzth_desc;
	struct aw_auth_desc auth_desc;

	struct aw_device_ops ops;
	struct list_head list_node;
};


/*start/stop*/
int aw882xx_device_start(struct aw_device *aw_dev);
int aw882xx_device_stop(struct aw_device *aw_dev);
int aw882xx_dev_reg_update(struct aw_device *aw_dev, bool force);
int aw882xx_dev_get_list_head(struct list_head **head);
void aw882xx_dev_mute(struct aw_device *aw_dev, bool mute);
int aw882xx_dev_algo_auth_set(struct aw_device *aw_dev, struct algo_auth_data *algo_data);
int aw882xx_dev_algo_auth_get(struct aw_device *aw_dev, struct algo_auth_data *algo_data);
int aw882xx_dev_set_volume(struct aw_device *aw_dev, unsigned int set_vol);
int aw882xx_dev_get_volume(struct aw_device *aw_dev, unsigned int *get_vol);

/*re*/
int aw882xx_dev_get_cali_re(struct aw_device *aw_dev, int32_t *cali_re);
int aw882xx_dev_init_cali_re(struct aw_device *aw_dev);
int aw882xx_dev_dc_status(struct aw_device *aw_dev);
void aw882xx_dev_iv_forbidden_output(struct aw_device *aw_dev, bool power_waste);

/*interrupt*/
int aw882xx_dev_status(struct aw_device *aw_dev);
int aw882xx_dev_get_int_status(struct aw_device *aw_dev, uint16_t *int_status);
void aw882xx_dev_clear_int_status(struct aw_device *aw_dev);
int aw882xx_dev_set_intmask(struct aw_device *aw_dev, bool flag);
int aw882xx_dev_irq_reinit(struct aw_device *aw_dev);

/*volume /fade in-out*/
void aw882xx_dev_set_fade_vol_step(struct aw_device *aw_dev, unsigned int step);
int aw882xx_dev_get_fade_vol_step(struct aw_device *aw_dev);
void aw882xx_dev_get_fade_time(unsigned int *time, bool fade_in);
void aw882xx_dev_set_fade_time(unsigned int time, bool fade_in);

/*monitor*/
void aw882xx_dev_monitor_hal_get_time(struct aw_device *aw_dev, uint32_t *time);
void aw882xx_dev_monitor_hal_work(struct aw_device *aw_dev, uint32_t *vmax);

/*device init*/
void aw882xx_dev_deinit(struct aw_device *aw_dev);
int aw882xx_dev_init(struct aw_device *aw_dev, struct aw_container *aw_cfg);

/*device probe*/
int aw882xx_dev_probe(struct aw_device *aw_dev);
int aw882xx_dev_remove(struct aw_device *aw_dev);

/*ef lock*/
int aw882xx_dev_check_ef_lock(struct aw_device *aw_dev);

#endif

