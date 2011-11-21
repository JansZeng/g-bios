#include <autoconf.h>
#include <net/net.h>
#include <net/mii.h>
#include <irq.h>
#include "lan9220.h"

#if 0
struct lan9220_chip
{
	int lan9220_32bits;
	__u32 (*readl)(lan9220_chip *, int);
	__u32 (*writel)(lan9220_chip *, int, __u32);
	// ...
};
#else
static int lan9220_32bits;
#endif

static inline __u32 lan9220_readl(__u8 reg)
{
	if (lan9220_32bits)
		return readl(VA(LAN9220_BASE + reg));

	return readw(VA(LAN9220_BASE + reg + 2)) << 16 | readw(VA(LAN9220_BASE + reg));
}

static inline void lan9220_writel(__u8 reg, __u32 val)
{
	if (lan9220_32bits) {
		writel(VA(LAN9220_BASE + reg), val);
	} else {
		writew(VA(LAN9220_BASE + reg), val & 0xffff);
		writew(VA(LAN9220_BASE + reg + 2), val >> 16 & 0xffff);
	}
}

static __u32 lan9220_mac_csr_read(__u32 csr_reg)
{
	lan9220_writel(MAC_CSR_CMD, 1 << 31 | 1 << 30 | csr_reg);
	while (lan9220_readl(MAC_CSR_CMD) & 1 << 31);
	return lan9220_readl(MAC_CSR_DATA);
}

static void lan9220_mac_csr_write(__u32 csr_reg, __u32 val)
{
	lan9220_writel(MAC_CSR_DATA, val);
	lan9220_writel(MAC_CSR_CMD, 1 << 31 | csr_reg);
	while (lan9220_readl(MAC_CSR_CMD) & 1 << 31);
}

static __u16 lan9220_mdio_read(struct net_device *ndev, __u8 mii_id, __u8 reg)
{
	lan9220_mac_csr_write(MII_ACC, mii_id << 11 | reg << 6 | 1);
	while (lan9220_mac_csr_read(MII_ACC) & 0x1);
	return lan9220_mac_csr_read(MII_DATA) & 0xffff;
}

static void lan9220_mdio_write(struct net_device *ndev, __u8 mii_id, __u8 reg, __u16 val)
{
	lan9220_mac_csr_write(MII_DATA, val & 0xffff);
	lan9220_mac_csr_write(MII_ACC, mii_id << 11 | reg << 6 | 1 << 1 | 1);
	while (lan9220_mac_csr_read(MII_ACC) & 0x1);
}

static int lan9220_hw_init(void)
{
	__u32 val;

	// reset phy
	val = lan9220_readl(HW_CFG);
	lan9220_writel(HW_CFG, val | 0x1);
	while (lan9220_readl(HW_CFG) & 0x1);

	// reset mac
	// while (lan9220_readl(PMT_CTRL) & 0x1);
	val = lan9220_readl(PMT_CTRL);
	lan9220_writel(PMT_CTRL, val | 0x1 << 10);
	while (lan9220_readl(PMT_CTRL) & 0x1 << 10);

	// fifo setting
	// irq setting
#ifdef CONFIG_IRQ_SUPPORT
	lan9220_writel(INT_EN, 0xffffffff);
	val = lan9220_readl(IRQ_CFG);
	lan9220_writel(IRQ_CFG, val | 0x1 << 8);
#endif

	// enable rx and tx
	val = lan9220_mac_csr_read(MAC_CR);
	lan9220_mac_csr_write(MAC_CR, val | 0x3 << 2);

	return 0;
}

static int lan9220_send_packet(struct net_device *ndev, struct sock_buff *skb)
{
	__u32 cmd_A, cmd_B, status;
	__u32 *data;
	int i;

	cmd_A = (1 << 13) | (1 << 12) | (skb->size & 0x3ff);
	cmd_B = skb->size & 0x3ff;
	data = (__u32 *)skb->data;

	lan9220_writel(TX_DATA_PORT, cmd_A);
	lan9220_writel(TX_DATA_PORT, cmd_B);

	for (i = 0; i < skb->size; i += 4, data++)
		lan9220_writel(TX_DATA_PORT, *data);

	status = lan9220_readl(TX_CFG);
	lan9220_writel(TX_CFG, status | 0x1 << 1);
	// wait for tx complete
	while (lan9220_readl(TX_CFG) & 0x1);

	status = lan9220_readl(TX_STATUS_PORT);

	ndev->stat.tx_packets++;

	return 0;
}

static int lan9220_recv_packet(struct net_device *ndev)
{
	__u32 info_status, packet_status;
	__u32 packet_count, packet_length, packet_length_pad;
	__u32 *data;
	struct sock_buff *skb;
	int i;

	info_status = lan9220_readl(RX_FIFO_INF);
	packet_count = info_status >> 16 & 0xff;
	if (0 == packet_count)
		return 0;

	while (packet_count--) {
		packet_status = lan9220_readl(RX_STATUS_PORT);
		// fixme, need to discard the error packet
		packet_length = packet_status >> 16 & 0x3fff;
		if (0 == packet_length)
			break;

		packet_length_pad = packet_length;
		ALIGN_UP(packet_length_pad, 4);

		skb = skb_alloc(0, packet_length_pad);
		if (NULL == skb)
			return -ENOMEM;

		skb->size = packet_length;
		data = (__u32 *)skb->data;

		for (i = 0; i < packet_length_pad; i += 4, data++)
			*data = lan9220_readl(RX_DATA_PORT);

		skb->size -= 4;
		netif_rx(skb);

		ndev->stat.rx_packets++;
	}

	return 0;
}

static int lan9220_isr(__u32 irq, void *dev)
{
	struct net_device *ndev = (struct net_device *)dev;
#ifdef CONFIG_IRQ_SUPPORT
	__u32 status;

	status = lan9220_readl(INT_STS);
	lan9220_writel(INT_STS, status);

	if (0 == status)
		return IRQ_NONE;
	// printf("%s(): status = 0x%08x\n", __func__, status);

	// fixme
	if (status & 1 << 20 | 1 << 3)
		lan9220_recv_packet(ndev);

	return IRQ_HANDLED;
#else

	return lan9220_recv_packet(ndev);
#endif
}

static int lan9220_set_mac(struct net_device *ndev, const __u8 *mac)
{
	lan9220_mac_csr_write(ADDRL, *(__u32 *)mac);
	lan9220_mac_csr_write(ADDRH, *(__u16 *)(mac + 4));
	return 0;
}

static int lan9220_poll(struct net_device *ndev)
{
	return lan9220_isr(LAN9220_IRQ_NUM, ndev);
}

static __INIT__ int lan9220_probe(void)
{
	int ret;
	__u32 mac_id;
	struct net_device *ndev;
	const char *chip_name = NULL;;

	mac_id = lan9220_readl(ID_REV);
	printf("ID = 0x%08x\n", mac_id);

	switch (mac_id >> 16) {
	case 0x0118:
		lan9220_32bits = 1;
		chip_name = "LAN9118 (32-bit)";
		break;

	case 0x9220:
		lan9220_32bits = 0;
		chip_name = "LAN9220 (16-bit)";
		break;

	default:
		chip_name = "Unknown";
		break;
	}

	ndev = ndev_new(0);
	if (NULL == ndev)
		return -ENOMEM;

	ndev->chip_name = chip_name;
	ndev->set_mac_addr = lan9220_set_mac;
	ndev->send_packet = lan9220_send_packet;
#ifndef CONFIG_IRQ_SUPPORT
	ndev->ndev_poll = lan9220_poll;
#endif
	ndev->mdio_read  = lan9220_mdio_read;
	ndev->mdio_write = lan9220_mdio_write;

	ret = ndev_register(ndev);
	if (ret < 0)
		goto error;

#ifdef CONFIG_IRQ_SUPPORT
	writel(GPIO1_BASE + LEVELDETECT1, 1 << 19); // fixme
	ret = irq_register_isr(LAN9220_IRQ_NUM, lan9220_isr, ndev);
	if (ret < 0)
		goto error;
#endif

	ret = lan9220_hw_init();
	return ret;

error:
	free(ndev);
	return ret;
}

DRIVER_INIT(lan9220_probe);
