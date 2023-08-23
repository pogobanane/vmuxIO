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

#include <stdlib.h>
#include <string.h>

#include <cassert>
#include <iostream>

#include "sims/nic/i40e_bm/i40e_base_wrapper.h"
#include "sims/nic/i40e_bm/i40e_bm.h"

namespace i40e {

host_mem_cache::host_mem_cache(i40e_bm &dev_) : dev(dev_) {
  reset();
}

void host_mem_cache::reset() {
  for (size_t i = 0; i < MAX_SEGMENTS; i++) {
    segs[i].addr = 0;
    segs[i].pgcount = 0;
    segs[i].valid = false;
    segs[i].direct = false;
  }
}

void host_mem_cache::reg_updated(uint64_t addr) {
  if (addr == I40E_PFHMC_SDCMD) {
    // read/write command for descriptor
    uint32_t cmd = dev.regs.pfhmc_sdcmd;
    uint16_t idx =
        (cmd & I40E_PFHMC_SDCMD_PMSDIDX_MASK) >> I40E_PFHMC_SDCMD_PMSDIDX_SHIFT;

    uint32_t lo = dev.regs.pfhmc_sddatalow;
    uint32_t hi = dev.regs.pfhmc_sddatahigh;
    if ((cmd & I40E_PFHMC_SDCMD_PMSDWR_MASK)) {
      // write
#ifdef DEBUG_HMC
      std::cerr << "hmc: writing descriptor " << idx << std::endl;
#endif

      segs[idx].addr = ((lo & I40E_PFHMC_SDDATALOW_PMSDDATALOW_MASK) >>
                        I40E_PFHMC_SDDATALOW_PMSDDATALOW_SHIFT)
                       << 12;
      segs[idx].addr |= ((uint64_t)hi) << 32;
      segs[idx].pgcount = (lo & I40E_PFHMC_SDDATALOW_PMSDBPCOUNT_MASK) >>
                          I40E_PFHMC_SDDATALOW_PMSDBPCOUNT_SHIFT;
      segs[idx].valid = !!(lo & I40E_PFHMC_SDDATALOW_PMSDVALID_MASK);
      segs[idx].direct = !!(lo & I40E_PFHMC_SDDATALOW_PMSDTYPE_MASK);

#ifdef DEBUG_HMC
      std::cerr << "    addr=" << segs[idx].addr
                << " pgcount=" << segs[idx].pgcount
                << " valid=" << segs[idx].valid
                << " direct=" << segs[idx].direct << std::endl;
#endif
    } else {
      // read
#ifdef DEBUG_HMC
      std::cerr << "hmc: reading descriptor " << idx << std::endl;
#endif

      dev.regs.pfhmc_sddatalow =
          ((segs[idx].addr >> 12) << I40E_PFHMC_SDDATALOW_PMSDDATALOW_SHIFT) &
          I40E_PFHMC_SDDATALOW_PMSDDATALOW_MASK;
      dev.regs.pfhmc_sddatalow |=
          (segs[idx].pgcount << I40E_PFHMC_SDDATALOW_PMSDBPCOUNT_SHIFT) &
          I40E_PFHMC_SDDATALOW_PMSDBPCOUNT_MASK;
      if (segs[idx].valid)
        dev.regs.pfhmc_sddatalow |= I40E_PFHMC_SDDATALOW_PMSDVALID_MASK;
      if (segs[idx].direct)
        dev.regs.pfhmc_sddatalow |= I40E_PFHMC_SDDATALOW_PMSDTYPE_MASK;
      dev.regs.pfhmc_sddatahigh = segs[idx].addr >> 32;
    }
  }
}

void host_mem_cache::issue_mem_op(mem_op &op) {
  uint64_t addr = op.dma_addr_;
  uint16_t seg_idx = addr >> 21;
  uint16_t seg_idx_last = (addr + op.len_ - 1) >> 21;
  uint32_t dir_off = addr & ((1 << 21) - 1);
  struct segment *seg = &segs[seg_idx];

  if (seg_idx >= MAX_SEGMENTS) {
    std::cerr << "hmc issue_mem_op: seg index too high " << seg_idx
              << std::endl;
    abort();
  }

  if (!seg->valid) {
    // TODO(antoinek): errorinfo and data registers
    std::cerr << "hmc issue_mem_op: segment invalid addr=" << addr << std::endl;
    op.failed = true;
    return;
  }

  if (seg_idx != seg_idx_last) {
    std::cerr << "hmc issue_mem_op: operation crosses segs addr=" << addr
              << " len=" << op.len_ << std::endl;
    abort();
  }

  if (!seg->direct) {
    std::cerr << "hmc issue_mem_op: TODO paged ops addr=" << addr << std::endl;
    abort();
  }

  op.failed = false;
  op.dma_addr_ = seg->addr + dir_off;

#ifdef DEBUG_HMC
  std::cerr << "hmc issue_mem_op: hmc_addr=" << addr
            << " dma_addr=" << op.dma_addr_ << " len=" << op.len_ << std::endl;
#endif
  dev.runner_->IssueDma(op);
}
}  // namespace i40e
