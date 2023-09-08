/*
 * Copyright 2021 Max Planck Institute for Software Systems, and
 * National University of Singapore
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <simbricks/network/if.h>

// #define DEBUG_PKTMETA

static struct SimbricksNetIf nsif;
static int tap_fd;

static int tap_open(const char *name) {
  struct ifreq ifr;
  int fd;

  if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
    perror("tap_open: open failed");
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

  /* fix gcc warning here: this is okay, kernel will nul-terminate ifr name,
     if neeeded, no need for us to worry */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
  strncpy(ifr.ifr_name, name, IFNAMSIZ);
#pragma GCC diagnostic pop

  if (ioctl(fd, TUNSETIFF, &ifr) != 0) {
    perror("tap_open: ioctl failed");
    close(fd);
    return -1;
  }

  return fd;
}

static void d2n_send(volatile struct SimbricksProtoNetMsgPacket *s) {
#ifdef DEBUG_PKTMETA
  printf("sent packet: len=%u\n", s->len);
#endif

  if (write(tap_fd, (void *)s->data, s->len) != (ssize_t)s->len) {
    perror("d2n_send: send failed");
  }
}

static void poll_d2n(void) {
  volatile union SimbricksProtoNetMsg *msg = SimbricksNetIfInPoll(&nsif, 0);
  uint8_t type;

  /* message not ready */
  if (msg == NULL)
    return;

  type = SimbricksNetIfInType(&nsif, msg);
  switch (type) {
    case SIMBRICKS_PROTO_NET_MSG_PACKET:
      d2n_send(&msg->packet);
      break;

    default:
      fprintf(stderr, "poll_d2n: unsupported type=%u\n", type);
  }

  SimbricksNetIfInDone(&nsif, msg);
}

static void *rx_handler(void *arg) {
  volatile union SimbricksProtoNetMsg *msg;
  volatile struct SimbricksProtoNetMsgPacket *rx;
  ssize_t len;

  while (1) {
    msg = SimbricksNetIfOutAlloc(&nsif, 0);
    if (msg == NULL) {
      fprintf(stderr, "coudl not allocate message for rx\n");
      abort();
    }
    rx = &msg->packet;

    len = read(tap_fd, (void *)rx->data,
               SimbricksBaseIfOutMsgLen(&nsif.base) - sizeof(*msg));
    if (len <= 0) {
      perror("rx handler: read failed");
    }
    rx->len = len;
    rx->port = 0;
#ifdef DEBUG_PKTMETA
    printf("received packet: len=%u\n", rx->len);
#endif

    SimbricksNetIfOutSend(&nsif, msg, SIMBRICKS_PROTO_NET_MSG_PACKET);
  }
}

int main(int argc, char *argv[]) {
  int sync;

  if (argc != 3) {
    fprintf(stderr, "Usage: net_tap TAP_DEVICE_NAME SOCKET\n");
    return EXIT_FAILURE;
  }

  if ((tap_fd = tap_open(argv[1])) < 0) {
    return -1;
  }

  struct SimbricksBaseIfParams params;
  SimbricksNetIfDefaultParams(&params);

  sync = 0;
  if (SimbricksNetIfInit(&nsif, &params, argv[2], &sync) != 0) {
    close(tap_fd);
    return -1;
  }

  pthread_t worker;
  if (pthread_create(&worker, NULL, rx_handler, NULL) != 0) {
    return EXIT_FAILURE;
  }

  printf("start polling\n");
  while (1) {
    poll_d2n();
  }
  return 0;
}
