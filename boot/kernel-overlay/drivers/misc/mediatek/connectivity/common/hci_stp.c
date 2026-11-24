// SPDX-License-Identifier: GPL-2.0
/* Linux HCI bridge for the begonia vendor WMT/STP transport. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/workqueue.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "wmt_exp.h"
#include "stp_exp.h"
#include "../../../../bluetooth/h4_recv.h"

struct stp_hci {
	struct hci_dev *hdev;
	struct platform_device *parent;
	struct workqueue_struct *wq;
	struct work_struct rx_work;
	struct work_struct error_work;
	struct delayed_work tx_work;
	struct sk_buff_head txq;
	struct sk_buff *rx_skb;
	bool running;
	bool fault;
	unsigned int retries;
};

/* The WMT API has one callback slot for the single onboard BT function. */
static struct stp_hci *stp_bt;
static char *bdaddr;
module_param(bdaddr, charp, 0400);
MODULE_PARM_DESC(bdaddr, "Persistent per-device Bluetooth address");

static const struct h4_recv_pkt stp_recv_pkts[] = {
	{ H4_RECV_ACL, .recv = hci_recv_frame },
	{ H4_RECV_SCO, .recv = hci_recv_frame },
	{ H4_RECV_EVENT, .recv = hci_recv_frame },
};

static void stp_hci_error(struct work_struct *work)
{
	struct stp_hci *bt = container_of(work, struct stp_hci, error_work);
	struct sk_buff *skb;
	const u8 event[] = { HCI_EV_HARDWARE_ERROR, 1, 0x01 };

	if (!READ_ONCE(bt->running))
		return;
	skb = bt_skb_alloc(sizeof(event), GFP_KERNEL);
	if (!skb)
		return;
	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
	skb_put_data(skb, event, sizeof(event));
	hci_recv_frame(bt->hdev, skb);
}

static void stp_hci_rx(struct work_struct *work)
{
	struct stp_hci *bt = container_of(work, struct stp_hci, rx_work);
	u8 buf[1024];
	int len;

	while (READ_ONCE(bt->running) && !READ_ONCE(bt->fault)) {
		len = mtk_wcn_stp_receive_data(buf, sizeof(buf), BT_TASK_INDX);
		if (len <= 0)
			break;
		bt->hdev->stat.byte_rx += len;
		bt->rx_skb = h4_recv_buf(bt->hdev, bt->rx_skb, buf, len,
					stp_recv_pkts, ARRAY_SIZE(stp_recv_pkts));
		if (IS_ERR(bt->rx_skb)) {
			bt_dev_err(bt->hdev, "Malformed STP H4 stream: %ld",
				   PTR_ERR(bt->rx_skb));
			bt->rx_skb = NULL;
			bt->hdev->stat.err_rx++;
			WRITE_ONCE(bt->fault, true);
			queue_work(bt->wq, &bt->error_work);
			break;
		}
	}
}

static void stp_hci_tx(struct work_struct *work)
{
	struct stp_hci *bt = container_of(to_delayed_work(work),
					struct stp_hci, tx_work);
	struct sk_buff *skb;
	int ret;

	while (READ_ONCE(bt->running) && !READ_ONCE(bt->fault)) {
		skb = skb_dequeue(&bt->txq);
		if (!skb)
			return;
		ret = mtk_wcn_stp_send_data(skb->data, skb->len, BT_TASK_INDX);
		if (ret == skb->len) {
			bt->hdev->stat.byte_tx += ret;
			bt->retries = 0;
			kfree_skb(skb);
			continue;
		}
		/* A zero return means the STP window/buffer is full. Keep order. */
		if (!ret && ++bt->retries < 100) {
			skb_queue_head(&bt->txq, skb);
			queue_delayed_work(bt->wq, &bt->tx_work, msecs_to_jiffies(10));
			return;
		}
		bt_dev_err(bt->hdev, "STP transmit failed: %d/%u", ret, skb->len);
		bt->hdev->stat.err_tx++;
		kfree_skb(skb);
		WRITE_ONCE(bt->fault, true);
		queue_work(bt->wq, &bt->error_work);
	}
}

static void stp_hci_rx_ready(void)
{
	struct stp_hci *bt = stp_bt;

	if (READ_ONCE(bt->running))
		queue_work(bt->wq, &bt->rx_work);
}

static void stp_hci_tx_ready(void)
{
	struct stp_hci *bt = stp_bt;

	if (READ_ONCE(bt->running))
		mod_delayed_work(bt->wq, &bt->tx_work, 0);
}

static void stp_hci_reset(ENUM_WMTDRV_TYPE_T src, ENUM_WMTDRV_TYPE_T dst,
			ENUM_WMTMSG_TYPE_T type, void *data, u32 len)
{
	struct stp_hci *bt = stp_bt;
	ENUM_WMTRSTMSG_TYPE_T msg;

	if (type != WMTMSG_TYPE_RESET || len != sizeof(msg))
		return;
	memcpy(&msg, data, sizeof(msg));
	WRITE_ONCE(bt->fault, true);
	/* Let WMT finish its shared reset before HCI recovery closes BT. */
	if (msg == WMTRSTMSG_RESET_END || msg == WMTRSTMSG_RESET_END_FAIL)
		queue_work(bt->wq, &bt->error_work);
}

static int stp_hci_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *addr)
{
	struct sk_buff *skb;
	int ret;

	/* MediaTek Set BD_ADDR, also used by the mainline btmtk driver. */
	skb = __hci_cmd_sync(hdev, 0xfc1a, sizeof(*addr), addr, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);
	ret = skb->len && !skb->data[0] ? 0 : -EIO;
	kfree_skb(skb);
	return ret;
}

static int stp_hci_setup(struct hci_dev *hdev)
{
	bdaddr_t addr;

	/* WMT firmware otherwise uses the common 00:00:46:76:63:01 address. */
	if (!bdaddr || strlen(bdaddr) != 17 ||
	    sscanf(bdaddr, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
		   &addr.b[5], &addr.b[4], &addr.b[3],
		   &addr.b[2], &addr.b[1], &addr.b[0]) != 6 ||
	    !bacmp(&addr, BDADDR_ANY) || !bacmp(&addr, BDADDR_NONE))
		return -EINVAL;
	return stp_hci_set_bdaddr(hdev, &addr);
}

static int stp_hci_open(struct hci_dev *hdev)
{
	struct stp_hci *bt = hci_get_drvdata(hdev);

	if (!mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT))
		return -EIO;
	if (!mtk_wcn_stp_is_ready()) {
		mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT);
		return -EIO;
	}
	bt->retries = 0;
	WRITE_ONCE(bt->fault, false);
	/* Queue mode retains packet type bytes and supports fragmented H4. */
	mtk_wcn_stp_set_bluez(0);
	mtk_wcn_stp_register_event_cb(BT_TASK_INDX, stp_hci_rx_ready);
	mtk_wcn_stp_register_tx_event_cb(BT_TASK_INDX, stp_hci_tx_ready);
	mtk_wcn_wmt_msgcb_reg(WMTDRV_TYPE_BT, stp_hci_reset);
	WRITE_ONCE(bt->running, true);
	queue_work(bt->wq, &bt->rx_work);
	return 0;
}

static int stp_hci_close(struct hci_dev *hdev)
{
	struct stp_hci *bt = hci_get_drvdata(hdev);

	WRITE_ONCE(bt->running, false);
	/* Patched WMT/STP unregister waits for callbacks already in flight. */
	mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);
	mtk_wcn_stp_register_tx_event_cb(BT_TASK_INDX, NULL);
	mtk_wcn_wmt_msgcb_unreg(WMTDRV_TYPE_BT);
	cancel_work_sync(&bt->rx_work);
	cancel_delayed_work_sync(&bt->tx_work);
	cancel_work_sync(&bt->error_work);
	skb_queue_purge(&bt->txq);
	kfree_skb(bt->rx_skb);
	bt->rx_skb = NULL;
	return mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT) ? 0 : -EIO;
}

static int stp_hci_flush(struct hci_dev *hdev)
{
	struct stp_hci *bt = hci_get_drvdata(hdev);

	cancel_delayed_work_sync(&bt->tx_work);
	skb_queue_purge(&bt->txq);
	return 0;
}

static int stp_hci_send(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct stp_hci *bt = hci_get_drvdata(hdev);
	u8 type = hci_skb_pkt_type(skb);

	if (!READ_ONCE(bt->running) || READ_ONCE(bt->fault))
		return -ENETDOWN;
	switch (type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	default:
		return -EILSEQ;
	}
	if (skb_queue_len(&bt->txq) >= 256)
		return -ENOBUFS;
	if (skb_cow_head(skb, 1))
		return -ENOMEM;
	*(u8 *)skb_push(skb, 1) = type;
	skb_queue_tail(&bt->txq, skb);
	queue_delayed_work(bt->wq, &bt->tx_work, 0);
	return 0;
}

static int __init stp_hci_init(void)
{
	struct device_node *np;
	struct stp_hci *bt;
	struct hci_dev *hdev;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mt6785-consys");
	if (!np)
		return -ENODEV;
	bt = kzalloc(sizeof(*bt), GFP_KERNEL);
	if (!bt) {
		of_node_put(np);
		return -ENOMEM;
	}
	bt->parent = of_find_device_by_node(np);
	of_node_put(np);
	if (!bt->parent) {
		ret = -ENODEV;
		goto free_bt;
	}
	bt->wq = alloc_ordered_workqueue("begonia-bt", WQ_MEM_RECLAIM);
	if (!bt->wq) {
		ret = -ENOMEM;
		goto put_parent;
	}
	hdev = hci_alloc_dev();
	if (!hdev) {
		ret = -ENOMEM;
		goto free_wq;
	}
	bt->hdev = hdev;
	INIT_WORK(&bt->rx_work, stp_hci_rx);
	INIT_WORK(&bt->error_work, stp_hci_error);
	INIT_DELAYED_WORK(&bt->tx_work, stp_hci_tx);
	skb_queue_head_init(&bt->txq);
	hdev->bus = HCI_UART;
	/* Observed Unknown HCI Command for 0x0c77 on begonia firmware. */
	hci_set_quirk(hdev, HCI_QUIRK_BROKEN_SYNC_TRAIN_PARAMS);
	hdev->setup = stp_hci_setup;
	hdev->set_bdaddr = stp_hci_set_bdaddr;
	hci_set_quirk(hdev, HCI_QUIRK_NON_PERSISTENT_SETUP);
	hdev->open = stp_hci_open;
	hdev->close = stp_hci_close;
	hdev->flush = stp_hci_flush;
	hdev->send = stp_hci_send;
	hci_set_drvdata(hdev, bt);
	SET_HCIDEV_DEV(hdev, &bt->parent->dev);
	stp_bt = bt;
	ret = hci_register_dev(hdev);
	if (!ret)
		return 0;
	stp_bt = NULL;
	hci_free_dev(hdev);
free_wq:
	destroy_workqueue(bt->wq);
put_parent:
	put_device(&bt->parent->dev);
free_bt:
	kfree(bt);
	return ret;
}

static void __exit stp_hci_exit(void)
{
	struct stp_hci *bt = stp_bt;

	hci_unregister_dev(bt->hdev);
	hci_free_dev(bt->hdev);
	destroy_workqueue(bt->wq);
	put_device(&bt->parent->dev);
	stp_bt = NULL;
	kfree(bt);
}

module_init(stp_hci_init);
module_exit(stp_hci_exit);
MODULE_DESCRIPTION("Begonia Bluetooth HCI over vendor WMT/STP/BTIF");
MODULE_LICENSE("GPL");
