/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <fcntl.h>
#include <rte_mbuf_ptype.h>
#include <rte_memcpy.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include "src/util.hpp"
#include "src/drivers/driver.hpp"

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

// from dpdk/app/test/packet_burst_generator.c
static void
copy_buf_to_pkt_segs(void *buf, unsigned len, struct rte_mbuf *pkt,
		unsigned offset)
{
	struct rte_mbuf *seg;
	void *seg_buf;
	unsigned copy_len;

	seg = pkt;
	while (offset >= seg->data_len) {
		offset -= seg->data_len;
		seg = seg->next;
	}
	copy_len = seg->data_len - offset;
	seg_buf = rte_pktmbuf_mtod_offset(seg, char *, offset);
	while (len > copy_len) {
		rte_memcpy(seg_buf, buf, (size_t) copy_len);
		len -= copy_len;
		buf = ((char *) buf + copy_len);
		seg = seg->next;
		seg_buf = rte_pktmbuf_mtod(seg, void *);
	}
	rte_memcpy(seg_buf, buf, (size_t) len);
}

// from dpdk/app/test/packet_burst_generator.c
static inline void
copy_buf_to_pkt(void *buf, unsigned len, struct rte_mbuf *pkt, unsigned offset)
{
	if (offset + len <= pkt->data_len) {
		rte_memcpy(rte_pktmbuf_mtod_offset(pkt, char *, offset), buf,
			   (size_t) len);
		return;
	}
	copy_buf_to_pkt_segs(buf, len, pkt, offset);
}


/* basicfwd.c: Basic DPDK skeleton forwarding example. */

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */

/* Main functional part of port initialization. 8< */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	return 0;
}
/* >8 End of main functional part of port initialization. */

static void
lcore_poll_once(void) {
	uint16_t port;

	/*
	 * Receive packets on a port and forward them on the same
	 * port. 
	 */
	RTE_ETH_FOREACH_DEV(port) {

		/* Get burst of RX packets, from first port of pair. */
		struct rte_mbuf *bufs[BURST_SIZE];
		const uint16_t nb_rx = rte_eth_rx_burst(port, 0,
				bufs, BURST_SIZE);

		if (unlikely(nb_rx == 0))
			continue;

		if_log_level(LOG_DEBUG, 
			for (int i = 0; i < nb_rx; i++) {
				struct rte_mbuf* buf = bufs[i];
				if (buf->l2_type == RTE_PTYPE_L2_ETHER) {
					// struct rte_ether_hdr* header = (struct rte_ether_hdr*) buf.buf_addr;
					struct rte_ether_hdr* header = rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr *);
					char src_addr[RTE_ETHER_ADDR_FMT_SIZE];
					char dst_addr[RTE_ETHER_ADDR_FMT_SIZE];
					rte_ether_format_addr(src_addr, RTE_ETHER_ADDR_FMT_SIZE, &header->src_addr);
					rte_ether_format_addr(dst_addr, RTE_ETHER_ADDR_FMT_SIZE, &header->dst_addr);
					printf("ethernet (%d bytes) %s -> %s\n", buf->buf_len, src_addr, dst_addr);
				} else {
					printf("non ethernet packet: l2 type: %d\n", buf->l2_type);
				}
			}
		);

		/* Send burst of TX packets, back to same port. */
		const uint16_t nb_tx = rte_eth_tx_burst(port, 0,
				bufs, nb_rx);

		/* Free any unsent packets. */
		if (unlikely(nb_tx < nb_rx)) {
			uint16_t buf;
			for (buf = nb_tx; buf < nb_rx; buf++)
				rte_pktmbuf_free(bufs[buf]);
		}
	}
}

static void lcore_init_checks() {
	uint16_t port;

	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	RTE_ETH_FOREACH_DEV(port)
		if (rte_eth_dev_socket_id(port) >= 0 &&
				rte_eth_dev_socket_id(port) !=
						(int)rte_socket_id())
			printf("WARNING, port %u is on remote NUMA node to "
					"polling thread.\n\tPerformance will "
					"not be optimal.\n", port);
}

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */

 /* Basic forwarding application lcore. 8< */
static __rte_noreturn void
lcore_main(void)
{
	lcore_init_checks();

	printf("\nCore %u forwarding packets. [Ctrl+C to quit]\n",
			rte_lcore_id());

	/* Main work of application loop. 8< */
	for (;;) {
		lcore_poll_once();
	}
}
/* >8 End Basic forwarding application lcore. */


class Dpdk : public Driver {
private:
	struct rte_mempool *mbuf_pool;
public:
	Dpdk(int argc, char *argv[]) {
		/*
 	 	 * The main function, which does initialization and calls the per-lcore
 	 	 * functions.
 	 	 */
		struct rte_mempool *mbuf_pool;
		unsigned nb_ports;
		uint16_t portid;

		/* Initializion the Environment Abstraction Layer (EAL). 8< */
		int ret = rte_eal_init(argc, argv);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
		/* >8 End of initialization the Environment Abstraction Layer (EAL). */

		argc -= ret;
		argv += ret;

		/* Check that there is an even number of ports to send/receive on. */
		nb_ports = rte_eth_dev_count_avail();
		if (nb_ports != 1)
			rte_exit(EXIT_FAILURE, "Error: number of ports must be 1. Is %d.\n", nb_ports);

		/* Creates a new mempool in memory to hold the mbufs. */

		/* Allocates mempool to hold the mbufs. 8< */
		mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
			MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
		this->mbuf_pool = mbuf_pool;
		/* >8 End of allocating mempool to hold mbuf. */

		if (mbuf_pool == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

		/* Initializing all ports. 8< */
		RTE_ETH_FOREACH_DEV(portid)
			if (port_init(portid, mbuf_pool) != 0)
				rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
						portid);
		/* >8 End of initializing all ports. */

		if (rte_lcore_count() > 1)
			printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");
	}

	virtual ~Dpdk() {
		/* clean up the EAL */
		rte_eal_cleanup();
	}

	// blocks, busy waiting!
	void poll_once() {
		lcore_init_checks();
		lcore_poll_once();

		/* Call lcore_main on the main core only. Called on single lcore. 8< */
		// lcore_main();
		/* >8 End of called on single lcore. */
	}

	virtual void send(const char *buf, const size_t len) {
		lcore_init_checks();
		uint16_t port;
		RTE_ETH_FOREACH_DEV(port) {
			// prepare packet buffer
			struct rte_mbuf *pkt;
			pkt = rte_pktmbuf_alloc(this->mbuf_pool);
			if (pkt == NULL) {
				return; // drop packet
			}
			pkt->data_len = len;
			pkt->pkt_len = len;
			pkt->nb_segs = 1;
			copy_buf_to_pkt((void*)buf, len, pkt, 0);

			/* Send burst of TX packets. */
			const uint16_t nb_tx = rte_eth_tx_burst(port, 0,
					&pkt, 1);
			if (nb_tx != 1) {
				printf("\nWARNING: Sending packet failed. \n");
			}

			/* Free packets. */
			rte_pktmbuf_free(pkt);
		}
	}

  virtual void recv() {
		lcore_init_checks();
		uint16_t port;

		/*
	 	 * Receive packets on a port and forward them on the same
	 	 * port. 
	 	 */
		RTE_ETH_FOREACH_DEV(port) {

			/* Get burst of RX packets, from first port of pair. */
			struct rte_mbuf *bufs[BURST_SIZE];
			const uint16_t nb_rx = rte_eth_rx_burst(port, 0,
					bufs, BURST_SIZE);

			if (unlikely(nb_rx == 0))
				continue;

			// place packet in vmux buffer
			if (nb_rx - 1 > 0)
				printf("dropping %d packets\n", nb_rx - 1);
			struct rte_mbuf* buf = bufs[0]; // we checked before that there is at least one packet
			void* pkt = rte_pktmbuf_mtod(buf, void*);
			if (buf->nb_segs != 1)
				die("This rx buffer has multiple segments. Unimplemented.");
			if (buf->pkt_len >= this->MAX_BUF)
				die("Cant handle packets of size %d", buf->pkt_len);
			rte_memcpy(this->rxFrame, pkt, buf->pkt_len);
			this->rxFrame_used = buf->pkt_len;
			if_log_level(LOG_DEBUG, Util::dump_pkt(&this->rxFrame, this->rxFrame_used));

      // free pkt
			for (uint16_t buf = 0; buf < nb_rx; buf++)
				rte_pktmbuf_free(bufs[buf]);
		}
  }
};
