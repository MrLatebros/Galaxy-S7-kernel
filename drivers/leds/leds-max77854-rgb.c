/*
 * RGB-led driver for Maxim MAX77854
 *
 * Copyright (C) 2013 Maxim Integrated Product
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/mfd/max77854.h>
#include <linux/mfd/max77854-private.h>
#include <linux/leds-max77854-rgb.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/sec_sysfs.h>
/* added LEDs Fade */
#include <linux/time.h>
#include <linux/syscalls.h>
#include <linux/sysfs_helpers.h>

#define SEC_LED_SPECIFIC

/* Registers */
/*defined max77854-private.h*//*
 max77854_led_reg {
	MAX77854_RGBLED_REG_LEDEN           = 0x30,
	MAX77854_RGBLED_REG_LED0BRT         = 0x31,
	MAX77854_RGBLED_REG_LED1BRT         = 0x32,
	MAX77854_RGBLED_REG_LED2BRT         = 0x33,
	MAX77854_RGBLED_REG_LED3BRT         = 0x34,
	MAX77854_RGBLED_REG_LEDRMP          = 0x36,
	MAX77854_RGBLED_REG_LEDBLNK         = 0x38,
	MAX77854_LED_REG_END,
};*/

/* MAX77854_REG_LED0BRT */
#define MAX77854_LED0BRT	0xFF

/* MAX77854_REG_LED1BRT */
#define MAX77854_LED1BRT	0xFF

/* MAX77854_REG_LED2BRT */
#define MAX77854_LED2BRT	0xFF

/* MAX77854_REG_LED3BRT */
#define MAX77854_LED3BRT	0xFF

/* MAX77854_REG_LEDBLNK */
#define MAX77854_LEDBLINKD	0xF0
#define MAX77854_LEDBLINKP	0x0F

/* MAX77854_REG_LEDRMP */
#define MAX77854_RAMPUP		0xF0
#define MAX77854_RAMPDN		0x0F

#define LED_R_MASK		0x00FF0000
#define LED_G_MASK		0x0000FF00
#define LED_B_MASK		0x000000FF
#define LED_MAX_CURRENT		0xFF

/* MAX77854_STATE*/
#define LED_DISABLE			0
#define LED_ALWAYS_ON			1
#define LED_BLINK			2

#define LEDBLNK_ON(time)	((time < 100) ? 0 :			\
				(time < 500) ? time/100-1 :		\
				(time < 3250) ? (time-500)/250+4 : 15)

#define LEDBLNK_OFF(time)	((time < 1) ? 0x00 :			\
				(time < 500) ? 0x01 :			\
				(time < 5000) ? time/500 :		\
				(time < 8000) ? (time-5000)/1000+10 :	 \
				(time < 12000) ? (time-8000)/2000+13 : 15)

extern unsigned int lcdtype;

static u8 led_dynamic_current = 0x14;
static u8 normal_powermode_current = 0x14;
static u8 low_powermode_current = 0x05;

/*
	device_type
*/
static unsigned int device_type = 0;

static unsigned int brightness_ratio_r = 100;
static unsigned int brightness_ratio_g = 100;
static unsigned int brightness_ratio_b = 100;
static unsigned int brightness_ratio_r_low = 20;
static unsigned int brightness_ratio_g_low = 20;
static unsigned int brightness_ratio_b_low = 20;
static u8 led_lowpower_mode = 0x0;

static unsigned int octa_color = 0x0;

/* added LED fade */
unsigned int led_enable_fade = 0;
unsigned int led_fade_time_up = 800;
unsigned int led_fade_time_down = 800;
unsigned int led_always_disable = 0;
unsigned int led_debug_enable = 0;
int led_block_leds_time_start = -1;
int led_block_leds_time_stop = -1;
struct device *GBLdev = NULL;

static struct delayed_work check_led_time;
static bool is_work_active = false;
/* end LED fade */

/* added LEDs control */
//unsigned int led_enable_fade = 0;
//unsigned int led_fade_time_up = 800;
//unsigned int led_fade_time_down = 800;
/* end */

enum max77854_led_color {
	WHITE,
	RED,
	GREEN,
	BLUE,
};
enum max77854_led_pattern {
	PATTERN_OFF,
	CHARGING,
	CHARGING_ERR,
	MISSED_NOTI,
	LOW_BATTERY,
	FULLY_CHARGED,
	POWERING,
};

static struct device *led_dev;

struct max77854_rgb {
	struct led_classdev led[4];
	struct i2c_client *i2c;
	unsigned int delay_on_times_ms;
	unsigned int delay_off_times_ms;
};

/*added LED FADE */
#ifdef SEC_LED_SPECIFIC
static struct leds_control {
	u8 	current_low;
	u8 	current_high;
	u16 	noti_ramp_control;
	u16 	noti_ramp_up;
	u16 	noti_ramp_down;
	u16 	noti_delay_on;
	u16 	noti_delay_off;
} leds_control = {
	.current_low = 5,
	.current_high = 40,
	.noti_ramp_control = 0,
	.noti_ramp_up = 500,
	.noti_ramp_down = 650,
	.noti_delay_on = 500,
	.noti_delay_off = 5000,
};
#endif
/* end of LED FADE */

#if 0
#if defined(CONFIG_LEDS_USE_ED28) && defined(CONFIG_SEC_FACTORY)
extern bool jig_status;
#endif
#endif

static int max77854_rgb_number(struct led_classdev *led_cdev,
				struct max77854_rgb **p)
{
	const struct device *parent = led_cdev->dev->parent;
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(parent);
	int i;

	*p = max77854_rgb;

	for (i = 0; i < 4; i++) {
		if (led_cdev == &max77854_rgb->led[i]) {
			pr_info("leds-max77854-rgb: %s, %d\n", __func__, i);
			return i;
		}
	}

	return -ENODEV;
}

static void max77854_rgb_set(struct led_classdev *led_cdev,
				unsigned int brightness)
{
	const struct device *parent = led_cdev->dev->parent;
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(parent);
	struct device *dev;
	int n;
	int ret;

	ret = max77854_rgb_number(led_cdev, &max77854_rgb);
	if (IS_ERR_VALUE(ret)) {
		dev_err(led_cdev->dev,
			"max77854_rgb_number() returns %d.\n", ret);
		return;
	}

	dev = led_cdev->dev;
	n = ret;

	if (brightness == LED_OFF) {
		/* Flash OFF */
		ret = max77854_update_reg(max77854_rgb->i2c,
					MAX77854_RGBLED_REG_LEDEN, 0 , 3 << (2*n));
		if (IS_ERR_VALUE(ret)) {
			dev_err(dev, "can't write LEDEN : %d\n", ret);
			return;
		}
	} else {
		/* Set current */
		ret = max77854_write_reg(max77854_rgb->i2c,
				MAX77854_RGBLED_REG_LED0BRT + n, brightness);
		if (IS_ERR_VALUE(ret)) {
			dev_err(dev, "can't write LEDxBRT : %d\n", ret);
			return;
		}
	}
}

static void max77854_rgb_set_state(struct led_classdev *led_cdev,
				unsigned int brightness, unsigned int led_state)
{
	const struct device *parent = led_cdev->dev->parent;
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(parent);
	struct device *dev;
	int n;
	int ret;

	pr_info("leds-max77854-rgb: %s\n", __func__);

	ret = max77854_rgb_number(led_cdev, &max77854_rgb);

	if (IS_ERR_VALUE(ret)) {
		dev_err(led_cdev->dev,
			"max77854_rgb_number() returns %d.\n", ret);
		return;
	}

	dev = led_cdev->dev;
	n = ret;

	if(brightness != 0) {
		/* apply brightness ratio for optimize each led brightness*/
		switch(n) {
		case RED:
			if ((device_type == 2 || device_type == 3) && led_lowpower_mode == 1)
				brightness = brightness * brightness_ratio_r_low / 100;
			else
				brightness = brightness * brightness_ratio_r / 100;
			break;
		case GREEN:
			if ((device_type == 2 || device_type == 3) && led_lowpower_mode == 1)
				brightness = brightness * brightness_ratio_g_low / 100;
			else
				brightness = brightness * brightness_ratio_g / 100;
			break;
		case BLUE:
			if ((device_type == 2 || device_type == 3) && led_lowpower_mode == 1)
				brightness = brightness * brightness_ratio_b_low / 100;
			else
				brightness = brightness * brightness_ratio_b / 100;
			break;
		}

		/*
			There is possibility that low_powermode_current is 0.
			ex) low_powermode_current is 1 & brightness_ratio_r is 90
			brightness = 1 * 90 / 100 = 0.9
			brightness is inteager, so brightness is 0.
			In this case, it is need to assign 1 of value.
		*/
		if(brightness == 0)
			brightness = 1;
	}
	max77854_rgb_set(led_cdev, brightness);

	pr_info("leds-max77854-rgb: %s, led_num = %d, brightness = %d\n", __func__, ret, brightness);

	ret = max77854_update_reg(max77854_rgb->i2c,
			MAX77854_RGBLED_REG_LEDEN, led_state << (2*n), 0x3 << 2*n);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "can't write FLASH_EN : %d\n", ret);
		return;
	}
}

static unsigned int max77854_rgb_get(struct led_classdev *led_cdev)
{
	const struct device *parent = led_cdev->dev->parent;
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(parent);
	struct device *dev;
	int n;
	int ret;
	u8 value;

	pr_info("leds-max77854-rgb: %s\n", __func__);

	ret = max77854_rgb_number(led_cdev, &max77854_rgb);
	if (IS_ERR_VALUE(ret)) {
		dev_err(led_cdev->dev,
			"max77854_rgb_number() returns %d.\n", ret);
		return 0;
	}
	n = ret;

	dev = led_cdev->dev;

	/* Get status */
	ret = max77854_read_reg(max77854_rgb->i2c,
				MAX77854_RGBLED_REG_LEDEN, &value);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "can't read LEDEN : %d\n", ret);
		return 0;
	}
	if (!(value & (1 << n)))
		return LED_OFF;

	/* Get current */
	ret = max77854_read_reg(max77854_rgb->i2c,
				MAX77854_RGBLED_REG_LED0BRT + n, &value);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "can't read LED0BRT : %d\n", ret);
		return 0;
	}

	return value;
}

static int max77854_rgb_ramp(struct device *dev, int ramp_up, int ramp_down)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	int value;
	int ret;

	pr_info("leds-max77854-rgb: %s\n", __func__);

	if (ramp_up <= led_fade_time_up) {
		ramp_up /= 100;
	} else {
		ramp_up = (ramp_up - led_fade_time_up) * 2 + led_fade_time_up;
		ramp_up /= 100;
	}

	if (ramp_down <= led_fade_time_down) {
		ramp_down /= 100;
	} else {
		ramp_down = (ramp_down - led_fade_time_down) * 2 + led_fade_time_down;
		ramp_down /= 100;
	}

	value = (ramp_down) | (ramp_up << 4);
	ret = max77854_write_reg(max77854_rgb->i2c,
					MAX77854_RGBLED_REG_LEDRMP, value);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "can't write REG_LEDRMP : %d\n", ret);
		return -ENODEV;
	}

	return 0;
}

static int max77854_rgb_blink(struct device *dev,
				unsigned int delay_on, unsigned int delay_off)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	int value;
	int ret = 0;

	pr_info("leds-max77854-rgb: %s\n", __func__);

	value = (LEDBLNK_ON(delay_on) << 4) | LEDBLNK_OFF(delay_off);
	ret = max77854_write_reg(max77854_rgb->i2c,
				MAX77854_RGBLED_REG_LEDBLNK, value);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "can't write REG_LEDBLNK : %d\n", ret);
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_OF
static struct max77854_rgb_platform_data
			*max77854_rgb_parse_dt(struct device *dev)
{
	struct max77854_rgb_platform_data *pdata;
	struct device_node *nproot = dev->parent->of_node;
	struct device_node *np;
	int ret;
	int i;
	int temp;
	char octa[4] = {0, };
	char br_ratio_r[23] = "br_ratio_r";
	char br_ratio_g[23] = "br_ratio_g";
	char br_ratio_b[23] = "br_ratio_b";
	char br_ratio_r_low[23] = "br_ratio_r_low";
	char br_ratio_g_low[23] = "br_ratio_g_low";
	char br_ratio_b_low[23] = "br_ratio_b_low";
	char normal_po_cur[29] = "normal_powermode_current";
	char low_po_cur[26] = "low_powermode_current";

	pr_info("leds-max77854-rgb: %s\n", __func__);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (unlikely(pdata == NULL))
		return ERR_PTR(-ENOMEM);

	np = of_find_node_by_name(nproot, "rgb");
	if (unlikely(np == NULL)) {
		dev_err(dev, "rgb node not found\n");
		devm_kfree(dev, pdata);
		return ERR_PTR(-EINVAL);
	}

	for (i = 0; i < 4; i++)	{
		ret = of_property_read_string_index(np, "rgb-name", i,
						(const char **)&pdata->name[i]);

		pr_info("leds-max77854-rgb: %s, %s\n", __func__,pdata->name[i]);

		if (IS_ERR_VALUE(ret)) {
			devm_kfree(dev, pdata);
			return ERR_PTR(ret);
		}
	}

	/* get device_type value in dt */
	ret = of_property_read_u32(np, "led_device_type", &temp);
	if (IS_ERR_VALUE(ret)) {
		pr_info("leds-max77854-rgb: %s, can't parsing device_type in dt\n", __func__);
	}
	else {
		device_type = (u8)temp;
	}
	pr_info("leds-max77854-rgb: %s, device_type = %x\n", __func__, device_type);

	/* HERO */
	if(device_type == 0) {
		switch(octa_color) {
		case 0:
			strcpy(octa, "_bk");
			break;
		case 1:
			strcpy(octa, "_wh");
			break;
		case 2:
			strcpy(octa, "_sv");
			break;
		case 3:
			strcpy(octa, "_gd");
			break;
		case 4:
			strcpy(octa, "_pg");
			break;
		case 5:
			strcpy(octa, "_bp");
			break;
		default:
			break;
		}
	}
	/* HERO2 */
	else if(device_type == 1) {
		switch(octa_color) {
		case 0:
			strcpy(octa, "_bk");
			break;
		case 1:
			strcpy(octa, "_wh");
			break;
		case 2:
			strcpy(octa, "_gd");
			break;
		case 3:
			strcpy(octa, "_sv");
			break;
		case 4:
			strcpy(octa, "_pg");
			break;
		default:
			break;
		}
	}
	/* GRACE */
	else if(device_type == 2) {
		switch(octa_color) {
		case 0:
			strcpy(octa, "_u");
			break;
		case 1:
			strcpy(octa, "_bk");
			break;
		case 3:
			strcpy(octa, "_gd");
			break;
		case 4:
			strcpy(octa, "_sv");
			break;
		case 6:
			strcpy(octa, "_bl");
			break;
		case 7:
			strcpy(octa, "_pg");
			break;
		default:
			break;
		}
	}
	strcat(normal_po_cur, octa);
	strcat(low_po_cur, octa);
	strcat(br_ratio_r, octa);
	strcat(br_ratio_g, octa);
	strcat(br_ratio_b, octa);
	strcat(br_ratio_r_low, octa);
	strcat(br_ratio_g_low, octa);
	strcat(br_ratio_b_low, octa);

	/* get normal_powermode_current value in dt */
	ret = of_property_read_u32(np, normal_po_cur, &temp);
	if (IS_ERR_VALUE(ret)) {
		pr_info("leds-max77854-rgb: %s, can't parsing normal_powermode_current in dt\n", __func__);
	}
	else {
		normal_powermode_current = (u8)temp;
	}
	pr_info("leds-max77854-rgb: %s, normal_powermode_current = %x\n", __func__, normal_powermode_current);

	/* get low_powermode_current value in dt */
	ret = of_property_read_u32(np, low_po_cur, &temp);
	if (IS_ERR_VALUE(ret)) {
		pr_info("leds-max77854-rgb: %s, can't parsing low_powermode_current in dt\n", __func__);
	}
	else
		low_powermode_current = (u8)temp;
	pr_info("leds-max77854-rgb: %s, low_powermode_current = %x\n", __func__, low_powermode_current);

	/* get led red brightness ratio */
	ret = of_property_read_u32(np, br_ratio_r, &temp);
	if (IS_ERR_VALUE(ret)) {
		pr_info("leds-max77854-rgb: %s, can't parsing brightness_ratio_r in dt\n", __func__);
	}
	else {
		brightness_ratio_r = (int)temp;
	}
	pr_info("leds-max77854-rgb: %s, brightness_ratio_r = %x\n", __func__, brightness_ratio_r);

	/* get led green brightness ratio */
	ret = of_property_read_u32(np, br_ratio_g, &temp);
	if (IS_ERR_VALUE(ret)) {
		pr_info("leds-max77854-rgb: %s, can't parsing brightness_ratio_g in dt\n", __func__);
	}
	else {
		brightness_ratio_g = (int)temp;
	}
	pr_info("leds-max77854-rgb: %s, brightness_ratio_g = %x\n", __func__, brightness_ratio_g);

	/* get led blue brightness ratio */
	ret = of_property_read_u32(np, br_ratio_b, &temp);
	if (IS_ERR_VALUE(ret)) {
		pr_info("leds-max77854-rgb: %s, can't parsing brightness_ratio_b in dt\n", __func__);
	}
	else {
		brightness_ratio_b = (int)temp;
	}
	pr_info("leds-max77854-rgb: %s, brightness_ratio_b = %x\n", __func__, brightness_ratio_b);

	if (device_type == 2 || device_type == 3) {
		/* get led red brightness ratio lowpower */
		ret = of_property_read_u32(np, br_ratio_r_low, &temp);
		if (IS_ERR_VALUE(ret)) {
			pr_info("leds-max77854-rgb: %s, can't parsing "\
					"brightness_ratio_r_low in dt\n", __func__);
		}
		else {
			brightness_ratio_r_low = (int)temp;
		}
		pr_info("leds-max77854-rgb: %s, brightness_ratio_r_low = %x\n",
				__func__, brightness_ratio_r_low);

		/* get led green brightness ratio lowpower*/
		ret = of_property_read_u32(np, br_ratio_g_low, &temp);
		if (IS_ERR_VALUE(ret)) {
			pr_info("leds-max77854-rgb: %s, can't parsing "\
					"brightness_ratio_g_low in dt\n", __func__);
		}
		else {
			brightness_ratio_g_low = (int)temp;
		}
		pr_info("leds-max77854-rgb: %s, brightness_ratio_g_low = %x\n",
				__func__, brightness_ratio_g_low);

		/* get led blue brightness ratio lowpower */
		ret = of_property_read_u32(np, br_ratio_b_low, &temp);
		if (IS_ERR_VALUE(ret)) {
			pr_info("leds-max77854-rgb: %s, can't parsing "\
					"brightness_ratio_b_low in dt\n", __func__);
		}
		else {
			brightness_ratio_b_low = (int)temp;
		}
		pr_info("leds-max77854-rgb: %s, brightness_ratio_b_low = %x\n",
				__func__, brightness_ratio_b_low);
	}
	return pdata;
}
#endif

static void max77854_rgb_reset(struct device *dev)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	max77854_rgb_set_state(&max77854_rgb->led[RED], LED_OFF, LED_DISABLE);
	max77854_rgb_set_state(&max77854_rgb->led[GREEN], LED_OFF, LED_DISABLE);
	max77854_rgb_set_state(&max77854_rgb->led[BLUE], LED_OFF, LED_DISABLE);
	max77854_rgb_ramp(dev, 0, 0);
}

static ssize_t store_max77854_rgb_lowpower(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	int ret;
	u8 led_lowpower;

/* added LED FADE */
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
/* end */

	ret = kstrtou8(buf, 0, &led_lowpower);
	if (ret != 0) {
		dev_err(dev, "fail to get led_lowpower.\n");
		return count;
	}

	led_lowpower_mode = led_lowpower;

/* LED FADE */
	led_dynamic_current = (led_lowpower_mode) ? leds_control.current_low : leds_control.current_high;

	max77854_rgb_set_state(&max77854_rgb->led[RED], led_dynamic_current, LED_BLINK);
	max77854_rgb_set_state(&max77854_rgb->led[GREEN], led_dynamic_current, LED_BLINK);
	max77854_rgb_set_state(&max77854_rgb->led[BLUE], led_dynamic_current, LED_BLINK);
/* end */

	pr_info("leds-max77854-rgb: led_lowpower mode set to %i\n", led_lowpower);

	return count;
}
static ssize_t store_max77854_rgb_brightness(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	int ret;
/* LED FADE */
	u8 max_brightness;
/* end */
	u8 brightness;
	pr_info("leds-max77854-rgb: %s\n", __func__);

	ret = kstrtou8(buf, 0, &brightness);
	if (ret != 0) {
		dev_err(dev, "fail to get led_brightness.\n");
		return count;
	}

	led_lowpower_mode = 0;

//	if (brightness > LED_MAX_CURRENT)
	brightness = LED_MAX_CURRENT;
/* LED FADE */
	max_brightness = (led_lowpower_mode) ? leds_control.current_low : leds_control.current_high;
	brightness = (brightness * max_brightness) / LED_MAX_CURRENT;
/* end */

	led_dynamic_current = brightness;

	dev_dbg(dev, "led brightness set to %i\n", brightness);

	return count;
}

/* LED FADE */
static bool check_restrictions(void)
{
	struct timeval curtime;
	struct tm tmv;
	int curhour;
	bool ret = true;

	if (led_always_disable)
	{
		ret = false;
		max77854_rgb_reset(GBLdev);
		goto skipitall;
	}
	if (led_block_leds_time_start != -1 && led_block_leds_time_stop != -1)
	{
		do_gettimeofday(&curtime);
		time_to_tm(curtime.tv_sec, 0, &tmv);

		curhour = tmv.tm_hour + ((sys_tz.tz_minuteswest / 60) * -1);
		if (curhour < 0)
			curhour = 24 + curhour;
		if (curhour > 23)
			curhour = curhour - 24;

		if (led_debug_enable) pr_alert("CHECK LED TIME RESTRICTION: %d:%d:%d:%ld -- %d -- %d -- %d\n", tmv.tm_hour, tmv.tm_min,
				         tmv.tm_sec, curtime.tv_usec, sys_tz.tz_minuteswest, sys_tz.tz_dsttime, curhour);
		if (led_block_leds_time_start > led_block_leds_time_stop)
		{
			if (curhour >= led_block_leds_time_start || curhour < led_block_leds_time_stop)
				ret = false;
		}
		else
		{
			if (curhour >= led_block_leds_time_start && curhour < led_block_leds_time_stop)
				ret = false;
		}
		/* Set all LEDs Off */
		if (!ret && GBLdev != NULL)
			max77854_rgb_reset(GBLdev);
	}
skipitall:
	return ret;
}

/* end */

static ssize_t store_max77854_rgb_pattern(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	unsigned int mode = 0;
	int ret;

	ret = sscanf(buf, "%1d", &mode);
	if (ret == 0) {
		dev_err(dev, "fail to get led_pattern mode.\n");
		return count;
	}
	pr_info("leds-max77854-rgb: %s pattern=%d lowpower=%i\n", __func__, mode, led_lowpower_mode);

/* LED FADE */
	GBLdev = dev;
/* end */

	/* Set all LEDs Off */
	max77854_rgb_reset(dev);
	if (mode == PATTERN_OFF)
		return count;
/* LED FADE */
	if (!check_restrictions())
		return count;
/* end */

	/* Set to low power consumption mode */
	if (led_lowpower_mode == 1)
		led_dynamic_current = low_powermode_current;
	else
		led_dynamic_current = normal_powermode_current;

	switch (mode) {

	case CHARGING:
// LED FADE		max77854_rgb_set_state(&max77854_rgb->led[RED], led_dynamic_current, LED_ALWAYS_ON);
		if (leds_control.noti_ramp_control == 1) {
			max77854_rgb_ramp(dev, leds_control.noti_ramp_up, leds_control.noti_ramp_down);
			max77854_rgb_blink(dev, 500, 500);
			max77854_rgb_set_state(&max77854_rgb->led[RED], led_dynamic_current, LED_BLINK);
		} else {
			max77854_rgb_set_state(&max77854_rgb->led[RED], led_dynamic_current, LED_ALWAYS_ON);
		}
/* end */
		break;
	case CHARGING_ERR:
		max77854_rgb_blink(dev, 500, 500);
		max77854_rgb_set_state(&max77854_rgb->led[RED], led_dynamic_current, LED_BLINK);
		break;
	case MISSED_NOTI:
//		max77854_rgb_blink(dev, 500, 5000);
/* LED CONTROL		if (led_enable_fade) {
			max77854_rgb_ramp(dev, led_fade_time_up, led_fade_time_down);
			max77854_rgb_blink(dev, led_fade_time_up, 5000);
		} else {
			max77854_rgb_blink(dev, 500, 5000);
		} /* added LEDs */
/* LED FADE */
		if (leds_control.noti_ramp_control == 1) {
			max77854_rgb_ramp(dev, leds_control.noti_ramp_up, leds_control.noti_ramp_down);
			if (led_enable_fade) {
				max77854_rgb_ramp(dev, led_fade_time_up, led_fade_time_down);
				max77854_rgb_blink(dev, led_fade_time_up, 5000);
			}
		} else {
			max77854_rgb_blink(dev, 500, 5000);
		}
/* END LED FADE */

		max77854_rgb_set_state(&max77854_rgb->led[BLUE], led_dynamic_current, LED_BLINK);
		break;
	case LOW_BATTERY:
//		max77854_rgb_blink(dev, 500, 5000);
/* LED CONTROL		if (led_enable_fade) {
			max77854_rgb_ramp(dev, led_fade_time_up, led_fade_time_down);
			max77854_rgb_blink(dev, led_fade_time_up, 5000);
		} else {
			max77854_rgb_blink(dev, 500, 5000);
		} /* added LEDs */
/* LED FADE */
		if (leds_control.noti_ramp_control == 1) {
			max77854_rgb_ramp(dev, leds_control.noti_ramp_up, leds_control.noti_ramp_down);
		}
		max77854_rgb_blink(dev, leds_control.noti_delay_on, leds_control.noti_delay_off);
/* END LED FADE */

		max77854_rgb_set_state(&max77854_rgb->led[RED], led_dynamic_current, LED_BLINK);
		break;
	case FULLY_CHARGED:
		max77854_rgb_set_state(&max77854_rgb->led[GREEN], led_dynamic_current, LED_ALWAYS_ON);
		break;
	case POWERING:
//		max77854_rgb_ramp(dev, 800, 800);
//		max77854_rgb_blink(dev, 200, 200);
/* LED CONTROL		if (led_enable_fade) {
			max77854_rgb_ramp(dev, led_fade_time_up, led_fade_time_down);
			max77854_rgb_blink(dev, led_fade_time_up, 5000);
		} else {
			max77854_rgb_ramp(dev, 800, 800);
			max77854_rgb_blink(dev, 200, 200);
		} /* added LEDs */
/* LED FADE */
		if (leds_control.noti_ramp_control == 1) {
			max77854_rgb_ramp(dev, leds_control.noti_ramp_up, leds_control.noti_ramp_down);
		}
		max77854_rgb_blink(dev, leds_control.noti_delay_on, leds_control.noti_delay_off);
/* END LED FADE */

		max77854_rgb_set_state(&max77854_rgb->led[BLUE], led_dynamic_current, LED_ALWAYS_ON);
		max77854_rgb_set_state(&max77854_rgb->led[GREEN], led_dynamic_current, LED_BLINK);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t store_max77854_rgb_blink(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	int led_brightness = 0;
	int delay_on_time = 0;
	int delay_off_time = 0;
	u8 led_r_brightness = 0;
	u8 led_g_brightness = 0;
	u8 led_b_brightness = 0;
	unsigned int led_total_br = 0;
	unsigned int led_max_br = 0;
	int ret;

	ret = sscanf(buf, "0x%8x %5d %5d", &led_brightness,
					&delay_on_time, &delay_off_time);
	if (ret == 0) {
		dev_err(dev, "fail to get led_blink value.\n");
		return count;
	}

	/* Set to low power consumption mode */
	if (led_lowpower_mode == 1)
		led_dynamic_current = low_powermode_current;
	else
		led_dynamic_current = normal_powermode_current;
	/*Reset led*/
	max77854_rgb_reset(dev);

	led_r_brightness = (led_brightness & LED_R_MASK) >> 16;
	led_g_brightness = (led_brightness & LED_G_MASK) >> 8;
	led_b_brightness = led_brightness & LED_B_MASK;

	/* In user case, LED current is restricted to less than tuning value */
	if (led_r_brightness != 0) {
		led_r_brightness = (led_r_brightness * led_dynamic_current) / LED_MAX_CURRENT;
		if (led_r_brightness == 0)
			led_r_brightness = 1;
	}
	if (led_g_brightness != 0) {
		led_g_brightness = (led_g_brightness * led_dynamic_current) / LED_MAX_CURRENT;
		if (led_g_brightness == 0)
			led_g_brightness = 1;
	}
	if (led_b_brightness != 0) {
		led_b_brightness = (led_b_brightness * led_dynamic_current) / LED_MAX_CURRENT;
		if (led_b_brightness == 0)
			led_b_brightness = 1;
	}

	led_total_br += led_r_brightness * brightness_ratio_r / 100;
	led_total_br += led_g_brightness * brightness_ratio_g / 100;
	led_total_br += led_b_brightness * brightness_ratio_b / 100;

	if (brightness_ratio_r >= brightness_ratio_g &&
		brightness_ratio_r >= brightness_ratio_b) {
		led_max_br = normal_powermode_current * brightness_ratio_r / 100;
	} else if (brightness_ratio_g >= brightness_ratio_r &&
		brightness_ratio_g >= brightness_ratio_b) {
		led_max_br = normal_powermode_current * brightness_ratio_g / 100;
	} else if (brightness_ratio_b >= brightness_ratio_r &&
		brightness_ratio_b >= brightness_ratio_g) {
		led_max_br = normal_powermode_current * brightness_ratio_b / 100;
	}

	/* Each color decreases according to the limit at the same rate. */
	if (led_total_br > led_max_br) {
		if (led_r_brightness != 0) {
			led_r_brightness = led_r_brightness * led_max_br / led_total_br;
			if (led_r_brightness == 0)
				led_r_brightness = 1;
		}
		if (led_g_brightness != 0) {
			led_g_brightness = led_g_brightness * led_max_br / led_total_br;
			if (led_g_brightness == 0)
				led_g_brightness = 1;
		}
		if (led_b_brightness != 0) {
			led_b_brightness = led_b_brightness * led_max_br / led_total_br;
			if (led_b_brightness == 0)
				led_b_brightness = 1;
		}
	}

	if (led_r_brightness) {
		max77854_rgb_set_state(&max77854_rgb->led[RED], led_r_brightness, LED_BLINK);
	}
	if (led_g_brightness) {
		max77854_rgb_set_state(&max77854_rgb->led[GREEN], led_g_brightness, LED_BLINK);
	}
	if (led_b_brightness) {
		max77854_rgb_set_state(&max77854_rgb->led[BLUE], led_b_brightness, LED_BLINK);
	}

	/*Set LED blink mode*/
/* added LEDs */
//	if (led_enable_fade && delay_on_time > 0)
//		max77854_rgb_ramp(dev, led_fade_time_up, led_fade_time_down); /* end */
/* LED FADE */
	if (leds_control.noti_ramp_control == 1)
		max77854_rgb_ramp(dev, leds_control.noti_ramp_up, leds_control.noti_ramp_down);
/* END LED FADE */

	max77854_rgb_blink(dev, delay_on_time, delay_off_time);

	pr_info("leds-max77854-rgb: %s, delay_on_time: %d, delay_off_time: %d, color: 0x%x, lowpower: %i\n", 
			__func__, delay_on_time, delay_off_time, led_brightness, led_lowpower_mode);

	return count;
}

static ssize_t store_led_r(struct device *dev,
			struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	unsigned int brightness;
	int ret;

	ret = kstrtouint(buf, 0, &brightness);
	if (ret != 0) {
		dev_err(dev, "fail to get brightness.\n");
		goto out;
	}
	if (brightness != 0) {
		max77854_rgb_set_state(&max77854_rgb->led[RED], brightness, LED_ALWAYS_ON);
	} else {
		max77854_rgb_set_state(&max77854_rgb->led[RED], LED_OFF, LED_DISABLE);
	}
out:
	pr_info("leds-max77854-rgb: %s\n", __func__);
	return count;
}
static ssize_t store_led_g(struct device *dev,
			struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	unsigned int brightness;
	int ret;

	ret = kstrtouint(buf, 0, &brightness);
	if (ret != 0) {
		dev_err(dev, "fail to get brightness.\n");
		goto out;
	}
	if (brightness != 0) {
		max77854_rgb_set_state(&max77854_rgb->led[GREEN], brightness, LED_ALWAYS_ON);
	} else {
		max77854_rgb_set_state(&max77854_rgb->led[GREEN], LED_OFF, LED_DISABLE);
	}
out:
	pr_info("leds-max77854-rgb: %s\n", __func__);
	return count;
}
static ssize_t store_led_b(struct device *dev,
		struct device_attribute *devattr,
		const char *buf, size_t count)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	unsigned int brightness;
	int ret;

	ret = kstrtouint(buf, 0, &brightness);
	if (ret != 0) {
		dev_err(dev, "fail to get brightness.\n");
		goto out;
	}
	if (brightness != 0) {
		max77854_rgb_set_state(&max77854_rgb->led[BLUE], brightness, LED_ALWAYS_ON);
	} else	{
		max77854_rgb_set_state(&max77854_rgb->led[BLUE], LED_OFF, LED_DISABLE);
	}
out:
	pr_info("leds-max77854-rgb: %s\n", __func__);
	return count;
}

/* Added for led common class */
static ssize_t led_delay_on_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", max77854_rgb->delay_on_times_ms);
}

static ssize_t led_delay_on_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	unsigned int time;

	if (kstrtouint(buf, 0, &time)) {
		dev_err(dev, "can not write led_delay_on\n");
		return count;
	}

	max77854_rgb->delay_on_times_ms = time;

	return count;
}

static ssize_t led_delay_off_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", max77854_rgb->delay_off_times_ms);
}

static ssize_t led_delay_off_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	unsigned int time;

	if (kstrtouint(buf, 0, &time)) {
		dev_err(dev, "can not write led_delay_off\n");
		return count;
	}

	max77854_rgb->delay_off_times_ms = time;

	return count;
}

static ssize_t led_blink_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	const struct device *parent = dev->parent;
	struct max77854_rgb *max77854_rgb_num = dev_get_drvdata(parent);
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	unsigned int blink_set;
	int n = 0;
	int i;

	if (!sscanf(buf, "%1d", &blink_set)) {
		dev_err(dev, "can not write led_blink\n");
		return count;
	}

	if (!blink_set) {
		max77854_rgb->delay_on_times_ms = LED_OFF;
		max77854_rgb->delay_off_times_ms = LED_OFF;
	}

	for (i = 0; i < 4; i++) {
		if (dev == max77854_rgb_num->led[i].dev)
			n = i;
	}

	max77854_rgb_blink(max77854_rgb_num->led[n].dev->parent,
		max77854_rgb->delay_on_times_ms,
		max77854_rgb->delay_off_times_ms);
	max77854_rgb_set_state(&max77854_rgb_num->led[n], led_dynamic_current, LED_BLINK);

	pr_info("leds-max77854-rgb: %s\n", __func__);
	return count;
}

/* added LEDs */
/* static ssize_t led_fade_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_enable_fade);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_enable_fade);

	return ret;
}

static ssize_t led_fade_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int enabled = 0;
	retval = sscanf(buf, "%d", &enabled);
	if (retval != 0 && (enabled == 0 || enabled == 1))
		led_enable_fade = enabled;

	return count;
}

static ssize_t led_fade_time_up_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_fade_time_up);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_fade_time_up);

	return ret;
}

static ssize_t led_fade_time_up_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int val = 0;
	retval = sscanf(buf, "%d", &val);
	if (retval != 0 && val >= 100  &&  val <= 4000)
		led_fade_time_up = val;

	return count;
}

static ssize_t led_fade_time_down_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_fade_time_down);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_fade_time_down);

	return ret;
}

static ssize_t led_fade_time_down_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int val = 0;
	retval = sscanf(buf, "%d", &val);
	if (retval != 0 && val >= 100  &&  val <= 4000)
		led_fade_time_down = val;

	return count;
} */
/* end */

/* added LED FADE */
static ssize_t led_fade_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_enable_fade);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_enable_fade);

	return ret;
}

static ssize_t led_fade_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int enabled = 0;
	retval = sscanf(buf, "%d", &enabled);
	if (retval != 0 && (enabled == 0 || enabled == 1))
		led_enable_fade = enabled;

	printk(KERN_DEBUG "led_fade is called\n");

	return count;
}

static ssize_t led_debug_enable_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_debug_enable);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_debug_enable);

	return ret;
}

static ssize_t led_debug_enable_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int enabled = 0;
	retval = sscanf(buf, "%d", &enabled);
	if (retval != 0 && (enabled == 0 || enabled == 1))
		led_debug_enable = enabled;

	printk(KERN_DEBUG "led_debug_enable is called\n");

	return count;
}

static ssize_t led_fade_time_up_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_fade_time_up);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_fade_time_up);

	return ret;
}

static ssize_t led_fade_time_up_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int val = 0;
	retval = sscanf(buf, "%d", &val);
	if (retval != 0 && val >= 100  &&  val <= 4000)
		led_fade_time_up = val;
	printk(KERN_DEBUG "led_time_on is called\n");

	return count;
}

static ssize_t led_fade_time_down_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_fade_time_down);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_fade_time_down);

	return ret;
}

static ssize_t led_fade_time_down_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int val = 0;
	retval = sscanf(buf, "%d", &val);
	if (retval != 0 && val >= 100  &&  val <= 4000)
		led_fade_time_down = val;
	printk(KERN_DEBUG "led_time_off is called\n");

	return count;
}

static ssize_t led_always_disable_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_always_disable);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_always_disable);

	return ret;
}

static ssize_t led_always_disable_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int val = 0;
	retval = sscanf(buf, "%d", &val);
	if (retval != 0 && (val == 0 || val == 1))
		led_always_disable = val;
	printk(KERN_DEBUG "led_time_off is called\n");

	return count;
}

static ssize_t led_block_leds_time_start_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_block_leds_time_start);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_block_leds_time_start);

	return ret;
}

static ssize_t led_block_leds_time_start_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int val = 0;
	retval = sscanf(buf, "%d", &val);
	if (retval != 0 && (val == -1 || (val >= 0 && val <= 23))) {
		led_block_leds_time_start = val;
	}
	if (!is_work_active && led_block_leds_time_start != -1 && led_block_leds_time_stop != -1)
	{
		is_work_active = true;
		schedule_delayed_work_on(0, &check_led_time, msecs_to_jiffies(30000));
	}
	else if (led_block_leds_time_start == -1 || led_block_leds_time_stop == -1)
		is_work_active = false;

	return count;
}

static ssize_t led_block_leds_time_stop_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", led_block_leds_time_stop);
	pr_info("[LED] %s: led_fade=%d\n", __func__, led_block_leds_time_stop);

	return ret;
}

static ssize_t led_block_leds_time_stop_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int retval;
	int val = 0;
	retval = sscanf(buf, "%d", &val);
	if (retval != 0 && (val == -1 || (val >= 0 && val <= 23))) {
		led_block_leds_time_stop = val;
	}
	if (!is_work_active && led_block_leds_time_start != -1 && led_block_leds_time_stop != -1)
	{
		is_work_active = true;
		schedule_delayed_work_on(0, &check_led_time, msecs_to_jiffies(30000));
	}
	else if (led_block_leds_time_start == -1 || led_block_leds_time_stop == -1)
		is_work_active = false;

	return count;
}
/* END LED FADE */

/* permission for sysfs node */
static DEVICE_ATTR(delay_on, 0640, led_delay_on_show, led_delay_on_store);
static DEVICE_ATTR(delay_off, 0640, led_delay_off_show, led_delay_off_store);
static DEVICE_ATTR(blink, 0640, NULL, led_blink_store);
/* added LEDs */
//static DEVICE_ATTR(led_fade, 0664, led_fade_show, led_fade_store);
//static DEVICE_ATTR(led_fade_time_up, 0664, led_fade_time_up_show, led_fade_time_up_store);
//static DEVICE_ATTR(led_fade_time_down, 0664, led_fade_time_down_show, led_fade_time_down_store);
/* end */

/* LED FADE */

static DEVICE_ATTR(led_fade, 0664, led_fade_show, led_fade_store);
static DEVICE_ATTR(led_fade_time_up, 0664, led_fade_time_up_show, led_fade_time_up_store);
static DEVICE_ATTR(led_fade_time_down, 0664, led_fade_time_down_show, led_fade_time_down_store);
static DEVICE_ATTR(led_always_disable, 0664, led_always_disable_show, led_always_disable_store);
static DEVICE_ATTR(led_debug_enable, 0664, led_debug_enable_show, led_debug_enable_store);
static DEVICE_ATTR(led_block_leds_time_start, 0664, led_block_leds_time_start_show, led_block_leds_time_start_store);
static DEVICE_ATTR(led_block_leds_time_stop, 0664, led_block_leds_time_stop_show, led_block_leds_time_stop_store);
/* end */

#ifdef SEC_LED_SPECIFIC
/* LED FADE */
static ssize_t show_leds_property(struct device *dev,
				  struct device_attribute *attr, char *buf);

static ssize_t store_leds_property(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len);

#define LEDS_ATTR(_name)				\
{							\
	.attr = {					\
	.name = #_name,					\
	.mode = S_IRUGO | S_IWUSR | S_IWGRP,		\
},							\
	.show = show_leds_property,			\
	.store = store_leds_property,			\
}

static struct device_attribute leds_control_attrs[] = {
	LEDS_ATTR(led_lowpower_current),
	LEDS_ATTR(led_highpower_current),
	LEDS_ATTR(led_notification_ramp_control),
	LEDS_ATTR(led_notification_ramp_up),
	LEDS_ATTR(led_notification_ramp_down),
	LEDS_ATTR(led_notification_delay_on),
	LEDS_ATTR(led_notification_delay_off),
};

enum {
	LOWPOWER_CURRENT = 0,
	HIGHPOWER_CURRENT,
	NOTIFICATION_RAMP_CONTROL,
	NOTIFICATION_RAMP_UP,
	NOTIFICATION_RAMP_DOWN,
	NOTIFICATION_DELAY_ON,
	NOTIFICATION_DELAY_OFF,
};

static ssize_t show_leds_property(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	const ptrdiff_t offset = attr - leds_control_attrs;

	switch (offset) {
	case LOWPOWER_CURRENT:
		return sprintf(buf, "%d", leds_control.current_low);
	case HIGHPOWER_CURRENT:
		return sprintf(buf, "%d", leds_control.current_high);
	case NOTIFICATION_RAMP_CONTROL:
		return sprintf(buf, "%d", leds_control.noti_ramp_control);
	case NOTIFICATION_RAMP_UP:
		return sprintf(buf, "%d", leds_control.noti_ramp_up);
	case NOTIFICATION_RAMP_DOWN:
		return sprintf(buf, "%d", leds_control.noti_ramp_down);
	case NOTIFICATION_DELAY_ON:
		return sprintf(buf, "%d", leds_control.noti_delay_on);
	case NOTIFICATION_DELAY_OFF:
		return sprintf(buf, "%d", leds_control.noti_delay_off);
	}

	return -EINVAL;
}

static ssize_t store_leds_property(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	int val;
	const ptrdiff_t offset = attr - leds_control_attrs;

	if(sscanf(buf, "%d", &val) != 1)
	return -EINVAL;

	switch (offset) {
	case LOWPOWER_CURRENT:
		sanitize_min_max(val, 0, LED_MAX_CURRENT);
		leds_control.current_low = val;
		break;
	case HIGHPOWER_CURRENT:
		sanitize_min_max(val, 0, LED_MAX_CURRENT);
		leds_control.current_high = val;
		break;
	case NOTIFICATION_RAMP_CONTROL:
		sanitize_min_max(val, 0, 1);
		leds_control.noti_ramp_control = val;
		break;
	case NOTIFICATION_RAMP_UP:
		sanitize_min_max(val, 0, 4000);
		leds_control.noti_ramp_up = val;
		break;
	case NOTIFICATION_RAMP_DOWN:
		sanitize_min_max(val, 0, 4000);
		leds_control.noti_ramp_down = val;
		break;
	case NOTIFICATION_DELAY_ON:
		sanitize_min_max(val, 0, 10000);
		leds_control.noti_delay_on = val;
		break;
	case NOTIFICATION_DELAY_OFF:
		sanitize_min_max(val, 0, 10000);
		leds_control.noti_delay_off = val;
		break;
	}

	return len;
}
/* END LED FADE */

/* below nodes is SAMSUNG specific nodes */
static DEVICE_ATTR(led_r, 0660, NULL, store_led_r);
static DEVICE_ATTR(led_g, 0660, NULL, store_led_g);
static DEVICE_ATTR(led_b, 0660, NULL, store_led_b);
/* led_pattern node permission is 222 */
/* To access sysfs node from other groups */
static DEVICE_ATTR(led_pattern, 0660, NULL, store_max77854_rgb_pattern);
static DEVICE_ATTR(led_blink, 0660, NULL,  store_max77854_rgb_blink);
static DEVICE_ATTR(led_brightness, 0660, NULL, store_max77854_rgb_brightness);
static DEVICE_ATTR(led_lowpower, 0660, NULL,  store_max77854_rgb_lowpower);
#endif

static struct attribute *led_class_attrs[] = {
	&dev_attr_delay_on.attr,
	&dev_attr_delay_off.attr,
	&dev_attr_blink.attr,
	NULL,
};

static struct attribute_group common_led_attr_group = {
	.attrs = led_class_attrs,
};

#ifdef SEC_LED_SPECIFIC
static struct attribute *sec_led_attributes[] = {
	&dev_attr_led_r.attr,
	&dev_attr_led_g.attr,
	&dev_attr_led_b.attr,
	&dev_attr_led_pattern.attr,
	&dev_attr_led_blink.attr,
	&dev_attr_led_brightness.attr,
	&dev_attr_led_lowpower.attr,
/* added LEDs */
//	&dev_attr_led_fade.attr,
//	&dev_attr_led_fade_time_up.attr,
//	&dev_attr_led_fade_time_down.attr,
/* end */
/* LED FADE */
	&dev_attr_led_fade.attr,
	&dev_attr_led_fade_time_up.attr,
	&dev_attr_led_fade_time_down.attr,
	&dev_attr_led_always_disable.attr,
	&dev_attr_led_debug_enable.attr,
	&dev_attr_led_block_leds_time_start.attr,
	&dev_attr_led_block_leds_time_stop.attr,
/* END LED FADE */

	NULL,
};

static struct attribute_group sec_led_attr_group = {
	.attrs = sec_led_attributes,
};
#endif
static int max77854_rgb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max77854_rgb_platform_data *pdata;
	struct max77854_rgb *max77854_rgb;
	struct max77854_dev *max77854_dev = dev_get_drvdata(dev->parent);
	char temp_name[4][40] = {{0,},}, name[40] = {0,}, *p;
	int i, ret;

	pr_info("leds-max77854-rgb: %s\n", __func__);

	octa_color = (lcdtype >> 16) & 0x0000000f;
#ifdef CONFIG_OF
	pdata = max77854_rgb_parse_dt(dev);
	if (unlikely(IS_ERR(pdata)))
		return PTR_ERR(pdata);

	led_dynamic_current = normal_powermode_current;
#else
	pdata = dev_get_platdata(dev);
#endif

	pr_info("leds-max77854-rgb: %s : lcdtype=%d, octa_color=%x device_type=%x \n",
		__func__, lcdtype, octa_color, device_type);
	max77854_rgb = devm_kzalloc(dev, sizeof(struct max77854_rgb), GFP_KERNEL);
	if (unlikely(!max77854_rgb))
		return -ENOMEM;
	pr_info("leds-max77854-rgb: %s 1 \n", __func__);

	max77854_rgb->i2c = max77854_dev->i2c;

	for (i = 0; i < 4; i++) {
		ret = snprintf(name, 30, "%s", pdata->name[i])+1;
		if (1 > ret)
			goto alloc_err_flash;

		p = devm_kzalloc(dev, ret, GFP_KERNEL);
		if (unlikely(!p))
			goto alloc_err_flash;

		strcpy(p, name);
		strcpy(temp_name[i], name);
		max77854_rgb->led[i].name = p;
		max77854_rgb->led[i].brightness_set = max77854_rgb_set;
		max77854_rgb->led[i].brightness_get = max77854_rgb_get;
		max77854_rgb->led[i].max_brightness = LED_MAX_CURRENT;

		ret = led_classdev_register(dev, &max77854_rgb->led[i]);
		if (IS_ERR_VALUE(ret)) {
			dev_err(dev, "unable to register RGB : %d\n", ret);
			goto alloc_err_flash_plus;
		}
		ret = sysfs_create_group(&max77854_rgb->led[i].dev->kobj,
						&common_led_attr_group);
		if (ret) {
			dev_err(dev, "can not register sysfs attribute\n");
			goto register_err_flash;
		}
	}

	led_dev = sec_device_create(max77854_rgb, "led");
	if (IS_ERR(led_dev)) {
		dev_err(dev, "Failed to create device for samsung specific led\n");
		goto create_err_flash;
	}


	ret = sysfs_create_group(&led_dev->kobj, &sec_led_attr_group);
	if (ret < 0) {
		dev_err(dev, "Failed to create sysfs group for samsung specific led\n");
		goto device_create_err;
	}

/* added LED FADE */
	for(i = 0; i < ARRAY_SIZE(leds_control_attrs); i++) {
		ret = sysfs_create_file(&led_dev->kobj, &leds_control_attrs[i].attr);
	}
/* end */

	platform_set_drvdata(pdev, max77854_rgb);
#if 0
#if defined(CONFIG_LEDS_USE_ED28) && defined(CONFIG_SEC_FACTORY)
	if( lcdtype == 0 && jig_status == false) {
		max77854_rgb_set_state(&max77854_rgb->led[RED], led_dynamic_current, LED_ALWAYS_ON);
	}
#endif
#endif
	pr_info("leds-max77854-rgb: %s done\n", __func__);

	return 0;

device_create_err:
	sec_device_destroy(led_dev->devt);
create_err_flash:
	sysfs_remove_group(&led_dev->kobj, &common_led_attr_group);
register_err_flash:
	led_classdev_unregister(&max77854_rgb->led[i]);
alloc_err_flash_plus:
	devm_kfree(dev, temp_name[i]);
alloc_err_flash:
	while (i--) {
		led_classdev_unregister(&max77854_rgb->led[i]);
		devm_kfree(dev, temp_name[i]);
	}
	devm_kfree(dev, max77854_rgb);
	return -ENOMEM;
}

static int max77854_rgb_remove(struct platform_device *pdev)
{
	struct max77854_rgb *max77854_rgb = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < 4; i++)
		led_classdev_unregister(&max77854_rgb->led[i]);

	return 0;
}

static void max77854_rgb_shutdown(struct device *dev)
{
	struct max77854_rgb *max77854_rgb = dev_get_drvdata(dev);
	int i;

	if (!max77854_rgb->i2c)
		return;

	max77854_rgb_reset(dev);

	sysfs_remove_group(&led_dev->kobj, &sec_led_attr_group);

	for (i = 0; i < 4; i++){
		sysfs_remove_group(&max77854_rgb->led[i].dev->kobj,
						&common_led_attr_group);
		led_classdev_unregister(&max77854_rgb->led[i]);
	}
	devm_kfree(dev, max77854_rgb);
}
static struct platform_driver max77854_fled_driver = {
	.driver		= {
		.name	= "leds-max77854-rgb",
		.owner	= THIS_MODULE,
		.shutdown = max77854_rgb_shutdown,
	},
	.probe		= max77854_rgb_probe,
	.remove		= max77854_rgb_remove,
};

/* LED FADE */
static void check_led_timer(struct work_struct *work)
{
	check_restrictions();
	if (is_work_active && led_block_leds_time_start != -1 && led_block_leds_time_stop != -1)
		schedule_delayed_work_on(0, &check_led_time, msecs_to_jiffies(30000));
}
/* end */

static int __init max77854_rgb_init(void)
{
	pr_info("leds-max77854-rgb: %s\n", __func__);
// LED FADE
	INIT_DELAYED_WORK(&check_led_time, check_led_timer); /* end */
	return platform_driver_register(&max77854_fled_driver);
}
module_init(max77854_rgb_init);

static void __exit max77854_rgb_exit(void)
{
	platform_driver_unregister(&max77854_fled_driver);
}
module_exit(max77854_rgb_exit);

MODULE_ALIAS("platform:max77854-rgb");
MODULE_AUTHOR("Jeongwoong Lee<jell.lee@samsung.com>");
MODULE_DESCRIPTION("MAX77854 RGB driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
