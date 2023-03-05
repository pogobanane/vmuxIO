#pragma once
#include <string>
extern "C" {
  #include "libvfio-user.h"
}

class Capabilities {
  public:
    vfu_ctx_t *vfu_ctx_stub;

    Capabilities(const vfio_region_info *config_info, void *config_ptr, std::string device);

    void *capa(const char name[], int id, size_t size, bool extended);
    void *dsn();
    void *pm();
    void *msi();
    void *msix();
    void *exp();
    void *vpd();

  private:
    void *header_mmap;
    size_t header_size;

    void map_header(std::string device);
};
