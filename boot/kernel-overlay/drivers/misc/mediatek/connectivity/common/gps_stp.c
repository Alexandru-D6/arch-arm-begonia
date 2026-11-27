// SPDX-License-Identifier: GPL-2.0
/* Begonia GPS binary transport. The positioning engine, not this device,
 * produces NMEA. ABI: MediaTek gps_stp/stp_chrdev_gps.c (2020).
 */
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm_wakeup.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include "stp_exp.h"
#include "wmt_exp.h"

#define GPS_BUFFER_SIZE 2048

static DEFINE_MUTEX(gps_open_lock);
static DEFINE_MUTEX(gps_read_lock);
static DEFINE_MUTEX(gps_write_lock);
static DEFINE_MUTEX(gps_control_lock);
static DECLARE_WAIT_QUEUE_HEAD(gps_wait);
static DECLARE_WAIT_QUEUE_HEAD(gps_reset_wait);
static struct wakeup_source *gps_wake;
static bool gps_opened;
static int gps_reset;
static u8 gps_rx[GPS_BUFFER_SIZE], gps_tx[GPS_BUFFER_SIZE];

static void gps_rx_ready(void)
{
	wake_up_interruptible(&gps_wait);
}

static void gps_reset_cb(ENUM_WMTDRV_TYPE_T src, ENUM_WMTDRV_TYPE_T dst,
			 ENUM_WMTMSG_TYPE_T type, void *data, u32 len)
{
	ENUM_WMTRSTMSG_TYPE_T event;

	if (type != WMTMSG_TYPE_RESET || !data || len != sizeof(event))
		return;
	memcpy(&event, data, sizeof(event));
	if (event == WMTRSTMSG_RESET_START)
		WRITE_ONCE(gps_reset, 1);
	else if (event == WMTRSTMSG_RESET_END || event == WMTRSTMSG_RESET_END_FAIL)
		WRITE_ONCE(gps_reset, 2);
	wake_up_interruptible(&gps_wait);
	wake_up_interruptible(&gps_reset_wait);
}

static int gps_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	if (mutex_lock_interruptible(&gps_open_lock))
		return -ERESTARTSYS;
	if (gps_opened) {
		ret = -EBUSY;
		goto out;
	}
	if (!mtk_wcn_wmt_func_on(WMTDRV_TYPE_GPS)) {
		ret = -EIO;
		goto out;
	}
	if (!mtk_wcn_stp_is_ready()) {
		mtk_wcn_wmt_func_off(WMTDRV_TYPE_GPS);
		ret = -EIO;
		goto out;
	}
	WRITE_ONCE(gps_reset, 0);
	mtk_wcn_stp_register_event_cb(GPS_TASK_INDX, gps_rx_ready);
	mtk_wcn_wmt_msgcb_reg(WMTDRV_TYPE_GPS, gps_reset_cb);
	WRITE_ONCE(gps_opened, true);
	__pm_stay_awake(gps_wake);
out:
	mutex_unlock(&gps_open_lock);
	return ret;
}

static int gps_release(struct inode *inode, struct file *file)
{
	mutex_lock(&gps_open_lock);
	WRITE_ONCE(gps_opened, false);
	/* The WMT/STP port synchronizes callbacks on unregister. */
	mtk_wcn_stp_register_event_cb(GPS_TASK_INDX, NULL);
	mtk_wcn_wmt_msgcb_unreg(WMTDRV_TYPE_GPS);
	wake_up_interruptible(&gps_wait);
	wake_up_interruptible(&gps_reset_wait);
	mtk_wcn_wmt_func_off(WMTDRV_TYPE_GPS);
	__pm_relax(gps_wake);
	mutex_unlock(&gps_open_lock);
	return 0;
}

static bool gps_readable(void)
{
	return READ_ONCE(gps_reset) ||
		!mtk_wcn_stp_is_rxqueue_empty(GPS_TASK_INDX);
}

static ssize_t gps_read(struct file *file, char __user *buf, size_t count,
			loff_t *pos)
{
	int ret;

	if (!count)
		return 0;
	if (mutex_lock_interruptible(&gps_read_lock))
		return -ERESTARTSYS;
	for (;;) {
		if (READ_ONCE(gps_reset)) {
			ret = -EIO;
			break;
		}
		ret = mtk_wcn_stp_receive_data(gps_rx,
				min_t(size_t, count, sizeof(gps_rx)), GPS_TASK_INDX);
		if (ret)
			break;
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		ret = wait_event_interruptible(gps_wait, gps_readable());
		if (ret)
			break;
	}
	if (ret > 0 && copy_to_user(buf, gps_rx, ret))
		ret = -EFAULT;
	mutex_unlock(&gps_read_lock);
	return ret;
}

static ssize_t gps_write(struct file *file, const char __user *buf, size_t count,
			 loff_t *pos)
{
	int ret;

	if (!count)
		return 0;
	if (READ_ONCE(gps_reset))
		return -EIO;
	count = min_t(size_t, count, sizeof(gps_tx));
	if (mutex_lock_interruptible(&gps_write_lock))
		return -ERESTARTSYS;
	if (copy_from_user(gps_tx, buf, count)) {
		ret = -EFAULT;
	} else {
		ret = mtk_wcn_stp_send_data(gps_tx, count, GPS_TASK_INDX);
		if (!ret)
			ret = -ENOSPC;
	}
	mutex_unlock(&gps_write_lock);
	return ret;
}

/* Fixed-width pointers are part of the vendor's shared 32/64-bit ABI. */
struct gps_fwctl {
	u32 size, tx_len, rx_max, pad;
	u64 tx_buf, rx_buf, rx_len, reserved;
};

static long gps_fw_control(void __user *arg)
{
	struct gps_fwctl ctl;
	u8 tx[128], rx[128];
	u32 len = 0;
	int ret;

	if (copy_from_user(&ctl, arg, sizeof(ctl)))
		return -EFAULT;
	if (ctl.size < sizeof(ctl) || !ctl.tx_len || ctl.tx_len > sizeof(tx))
		return -EINVAL;
	if (copy_from_user(tx, u64_to_user_ptr(ctl.tx_buf), ctl.tx_len))
		return -EFAULT;
	if (mutex_lock_interruptible(&gps_control_lock))
		return -ERESTARTSYS;
	ret = READ_ONCE(gps_reset) ? -EIO :
		mtk_wmt_gps_mcu_ctrl(tx, ctl.tx_len, rx, sizeof(rx), &len);
	mutex_unlock(&gps_control_lock);
	if (ret || len > sizeof(rx))
		return -EIO;
	if (copy_to_user(u64_to_user_ptr(ctl.rx_buf), rx, min(ctl.rx_max, len)) ||
	    copy_to_user(u64_to_user_ptr(ctl.rx_len), &len, sizeof(len)))
		return -EFAULT;
	return 0;
}

static long gps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	u32 value;
	int state;

	switch (cmd) {
	case 7: /* COMBO_IOC_GPS_IC_HW_VERSION */
		value = mtk_wcn_wmt_ic_info_get(WMTCHIN_HWVER);
		break;
	case 8: /* COMBO_IOC_GPS_IC_FW_VERSION */
		value = mtk_wcn_wmt_ic_info_get(WMTCHIN_FWVER);
		break;
	case 10: /* RTC low-power flag; vendor implementation also returns 0. */
		return 0;
	case 11:
		return mtk_wcn_wmt_co_clock_flag_get();
	case 13:
		state = READ_ONCE(gps_reset);
		return state == 1 ? -888 : state == 2 ? -889 : 0;
	case 14:
		__pm_stay_awake(gps_wake);
		return 0;
	case 15:
		__pm_relax(gps_wake);
		return 0;
	case 16:
		value = mtk_wmt_get_gps_lna_pin_num();
		break;
	case 17:
		return gps_fw_control((void __user *)arg);
	case 20:
		return wait_event_interruptible(gps_reset_wait,
			READ_ONCE(gps_reset) || !READ_ONCE(gps_opened));
	default:
		/* Do not pretend unsupported suspend/debug operations succeeded. */
		return -ENOTTY;
	}
	return put_user(value, (u32 __user *)arg);
}

static __poll_t gps_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &gps_wait, wait);
	if (READ_ONCE(gps_reset))
		return EPOLLERR;
	return gps_readable() ? EPOLLIN | EPOLLRDNORM : 0;
}

static const struct file_operations gps_fops = {
	.owner = THIS_MODULE,
	.open = gps_open,
	.release = gps_release,
	.read = gps_read,
	.write = gps_write,
	.poll = gps_poll,
	.unlocked_ioctl = gps_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
};

static struct miscdevice gps_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "stpgps",
	.fops = &gps_fops,
	.mode = 0600,
};

static int __init gps_init(void)
{
	struct device_node *node;
	int ret;

	node = of_find_compatible_node(NULL, NULL, "mediatek,mt6785-consys");
	if (!node)
		return -ENODEV;
	of_node_put(node);
	gps_wake = wakeup_source_register(NULL, "begonia-gps");
	if (!gps_wake)
		return -ENOMEM;
	ret = misc_register(&gps_device);
	if (ret)
		wakeup_source_unregister(gps_wake);
	return ret;
}

static void __exit gps_exit(void)
{
	misc_deregister(&gps_device);
	wakeup_source_unregister(gps_wake);
}

module_init(gps_init);
module_exit(gps_exit);
MODULE_DESCRIPTION("Begonia binary GPS transport over WMT/STP");
MODULE_LICENSE("GPL");
