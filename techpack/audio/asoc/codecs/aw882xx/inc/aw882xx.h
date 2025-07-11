/* SPDX-License-Identifier: GPL-2.0
 * aw882xx.h
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

#ifndef _AW882XX_H_
#define _AW882XX_H_
#include <linux/version.h>
#include <linux/kernel.h>
#include "aw882xx_device.h"

/*
 * i2c transaction on Linux limited to 64k
 * (See Linux kernel documentation: Documentation/i2c/writing-clients)
 */
#define AW882XX_CHIP_ID_REG (0x00)
#define MAX_I2C_BUFFER_SIZE					65536
#define AW882XX_I2C_READ_MSG_NUM		2
#define AW882XX_DC_DELAY_TIME	(1000)
#define AW882XX_LOAD_FW_DELAY_TIME	(0)
#define AW_START_RETRIES	(5)

#define AW_PID_2055_VERSION_DIFF_REG	(0x23)

#define AW_I2C_RETRIES			5	/* 5 times */
#define AW_I2C_RETRY_DELAY		5	/* 5 ms */

#define ACF_BIN_NAME	"aw882xx_acf.bin"

#define AW882XX_RATES SNDRV_PCM_RATE_8000_48000
#define AW882XX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
						SNDRV_PCM_FMTBIT_S24_LE | \
						SNDRV_PCM_FMTBIT_S32_LE)
/*#define AW882XX_IRQ_START_FLAG*/
enum {
	AW882XX_STREAM_CLOSE = 0,
	AW882XX_STREAM_OPEN,
};

enum aw882xx_chipid {
	PID_1852_ID = 0x1852,
	PID_2013_ID = 0x2013,
	PID_2032_ID = 0x2032,
	PID_2055_ID = 0x2055,
	PID_2055A_ID = 0x2055A,
	PID_2071_ID = 0x2071,
	PID_2113_ID = 0x2113,
	PID_2116_ID = 0x2116,
	PID_2308_ID = 0x2308,
};

#define AW882XX_SOFT_RESET_REG		(0x00)
#define AW882XX_SOFT_RESET_VALUE	(0x55aa)

enum aw882xx_int_type {
	INT_TYPE_NONE = 0,
	INT_TYPE_UVLO = 0x1,
	INT_TYPE_BSTOC = 0x2,
	INT_TYPE_OCDI = 0x4,
	INT_TYPE_OTHI = 0x8,
};

#if KERNEL_VERSION(4, 19, 1) <= LINUX_VERSION_CODE
#define AW_KERNEL_VER_OVER_4_19_1
#endif


#if KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE
#define AW_KERNEL_VER_OVER_5_4_0
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
#define AW_KERNEL_VER_OVER_5_10_0
#endif
#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
#define AW_KERNEL_VER_OVER_6_1_0
#endif

#if KERNEL_VERSION(6, 6, 0) <= LINUX_VERSION_CODE
#define AW_KERNEL_VER_OVER_6_6_0
#endif

#ifdef AW_KERNEL_VER_OVER_4_19_1
typedef struct snd_soc_component aw_snd_soc_codec_t;
typedef const struct snd_soc_component_driver aw_snd_soc_codec_driver_t;
#else
typedef struct snd_soc_codec aw_snd_soc_codec_t;
typedef const struct snd_soc_codec_driver aw_snd_soc_codec_driver_t;
#endif

struct aw_componet_codec_ops {
	aw_snd_soc_codec_t *(*kcontrol_codec)(struct snd_kcontrol *kcontrol);
	void *(*codec_get_drvdata)(aw_snd_soc_codec_t *codec);
	int (*add_codec_controls)(aw_snd_soc_codec_t *codec,
		const struct snd_kcontrol_new *controls, unsigned int num_controls);
	void (*unregister_codec)(struct device *dev);
	int (*register_codec)(struct device *dev,
			const aw_snd_soc_codec_driver_t *codec_drv,
			struct snd_soc_dai_driver *dai_drv,
			int num_dai);
};

enum {
	AWRW_I2C_ST_NONE = 0,
	AWRW_I2C_ST_READ,
	AWRW_I2C_ST_WRITE,
};

#define AWRW_ADDR_BYTES (1)
#define AWRW_DATA_BYTES (2)
#define AWRW_HDR_LEN (24)


enum {
	KCTL_TYPE_PROFILE = 0,
	KCTL_TYPE_SWITCH,
	KCTL_TYPE_MONITOR,
	KCTL_TYPE_VOLUME,
	KCTL_TYPE_MON_HAL,
	KCTL_TYPE_VOLTAGE_OFFSET,
	KCTL_TYPE_IV_OUTPUT,
	AW_KCTL_NUM,

};

enum {
	AWRW_FLAG_WRITE = 0,
	AWRW_FLAG_READ,
};

enum {
	AW_BSTCFG_DISABLE = 0,
	AW_BSTCFG_ENABLE,
};

enum {
	AW_FRCSET_DISABLE = 0,
	AW_FRCSET_ENABLE,
};

enum {
	AW_BOP_DISABLE = 0,
	AW_BOP_ENABLE,
};

enum {
	AW_RENAME_DISABLE = 0,
	AW_RENAME_ENABLE,
};

enum {
	AWRW_HDR_WR_FLAG = 0,
	AWRW_HDR_ADDR_BYTES,
	AWRW_HDR_DATA_BYTES,
	AWRW_HDR_REG_NUM,
	AWRW_HDR_REG_ADDR,
	AWRW_HDR_MAX,
};

struct aw882xx_i2c_packet {
	char status;
	unsigned int reg_num;
	unsigned int reg_addr;
	char *reg_data;
};

/********************************************
 * struct aw882xx
 *******************************************/
struct aw882xx {
	int sysclk;
	int rate;
	int pstream;
	int cstream;

	unsigned char phase_sync;	/* phase sync */
	unsigned char dc_flag;
	unsigned char dbg_en_prof;	/* debug enable/disable profile function */
	unsigned char allow_pw;		/* allow power */
	uint32_t rename_flag;
	unsigned char sync_load;	/* sync load fw */

	int reset_gpio;
	int irq_gpio;

#ifndef CONFIG_TARGET_PRODUCT_ZIYI
	int spksw_gpio;
	int spksw_level;
#endif

	unsigned char fw_status;
	unsigned char fw_retry_cnt;
	unsigned char rw_reg_addr;	/* rw attr node store read addr */

	aw_snd_soc_codec_t *codec;
	struct aw_componet_codec_ops *codec_ops;

	struct i2c_client *i2c;
	struct device *dev;
	struct aw882xx_i2c_packet i2c_packet;
	struct aw_device *aw_pa;

	struct workqueue_struct *work_queue;
	struct delayed_work start_work;
	struct delayed_work interrupt_work;
	struct delayed_work dc_work;
	struct delayed_work fw_work;

	struct mutex lock;
	int dsm_state;
};

int aw882xx_init(struct aw_device *aw_pa);
int aw882xx_hw_reset(struct aw882xx *aw882xx);


#endif

