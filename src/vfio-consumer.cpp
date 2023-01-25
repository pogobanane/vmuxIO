#include "src/vfio-consumer.hpp"
#include <linux/vfio.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <err.h>
#include <stdlib.h>
#include <vector>

#include "src/util.hpp"

int VfioConsumer::init() {
  int ret, container, group, device;
  uint32_t i;
  struct vfio_group_status group_status =
                                  { .argsz = sizeof(group_status) };
  struct vfio_iommu_type1_info iommu_info = { .argsz = sizeof(iommu_info) };
  struct vfio_iommu_type1_dma_map dma_map = { .argsz = sizeof(dma_map) };
  struct vfio_device_info device_info = { .argsz = sizeof(device_info) };

  /* Create a new container */
  container = open("/dev/vfio/vfio", O_RDWR);
  if (container < 0) {
    die("Cannot open /dev/vfio/vfio");
  }

  if (ioctl(container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
    /* Unknown API version */
    die("VFIO version mismatch");
  }

  if (!ioctl(container, VFIO_CHECK_EXTENSION, VFIO_TYPE1v2_IOMMU)) {
    /* Doesn't support the IOMMU driver we want. */
    die("VFIO extension unsupported");
  }

  /* Open the group */
  group = open("/dev/vfio/29", O_RDWR);
  if (group < 0) {
    die("Cannot open /dev/vfio/29");
  }

  /* Test the group is viable and available */
  ret = ioctl(group, VFIO_GROUP_GET_STATUS, &group_status);
  if (ret < 0) {
    die("Cannot get vfio satus");
  }

  if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
    /* Group is not viable (ie, not all devices bound for vfio) */
    die("Some flag missing");
  }

  /* Add the group to the container */
  ret = ioctl(group, VFIO_GROUP_SET_CONTAINER, &container);
  if (ret < 0) {
    die("Cannot set vfio container");
  }

  /* Enable the IOMMU model we want */
  ret = ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
  if (ret < 0) {
    die("Cannot set iommu type");
  }

  /* Get addition IOMMU info */
  ret = ioctl(container, VFIO_IOMMU_GET_INFO, &iommu_info);
  if (ret < 0) {
    die("Cannot get iommu info");
  }

  /* Allocate some space and setup a DMA mapping */
  dma_map.vaddr = (uint64_t)(mmap(0, 1024 * 1024, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, 0, 0));
  if (dma_map.vaddr == (uint64_t) MAP_FAILED) {
    die("Cannot allocate memory for DMA map");
  }
  dma_map.size = 1024 * 1024;
  dma_map.iova = 0; /* 1MB starting at 0x0 from device view */
  dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

  ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
  if (ret < 0) {
    die("Cannot set dma map");
  }

  /* Get a file descriptor for the device */
  device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, "0000:18:00.0");
  if (device < 0) {
    die("Cannot device id for group %d", group);
  }
  this->device = device;

  /* Test and setup the device */
  ret = ioctl(device, VFIO_DEVICE_GET_INFO, &device_info);
  if (ret < 0) {
    die("Cannot get device info for device %d", device);
  }

  printf("\nDevice regions: %d\n\n", device_info.num_regions);

  for (i = 0; i < device_info.num_regions; i++) {
          struct vfio_region_info reg = { .argsz = sizeof(reg) };

          reg.index = i;

          ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &reg);
          __builtin_dump_struct(&reg, &printf);
          this->regions.push_back(reg);

          /* Setup mappings... read/write offsets, mmaps
           * For PCI devices, config space is a region */
  }

  printf("\nDevice irsq: %d\n\n", device_info.num_irqs);

  for (i = 0; i < device_info.num_irqs; i++) {
          struct vfio_irq_info irq = { .argsz = sizeof(irq) };

          irq.index = i;

          ioctl(device, VFIO_DEVICE_GET_IRQ_INFO, &irq);
          __builtin_dump_struct(&irq, &printf);
          this->interrupts.push_back(irq);

          /* Setup IRQs... eventfds, VFIO_DEVICE_SET_IRQS */
  }

  /* Gratuitous device reset and go... */
  ioctl(device, VFIO_DEVICE_RESET);

  return 0;
}

int VfioConsumer::init_mmio() {
  // Only iterate bars 0-5. Bar 6 seems not mappable. 
  for (int i = 0; i <= 5; i++) { 
    auto region = this->regions[i];
    if (region.size == 0) {
      printf("Mapping region BAR %d skipped\n", region.index);
      continue;
    }
    void* mem = mmap(NULL, region.size, PROT_READ | PROT_WRITE, MAP_SHARED, this->device, region.offset);
    if (mem == MAP_FAILED) {
      die("failed to map mmio region BAR %d via vfio", region.index);
    }
    this->mmio[region.index] = mem;
    printf("Mapping region BAR %d offset 0x%llx size 0x%llx\n", region.index, region.offset, region.size);
  }
  return 0;
}
