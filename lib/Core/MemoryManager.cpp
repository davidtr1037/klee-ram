//===-- MemoryManager.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CoreStats.h"
#include "Memory.h"
#include "MemoryManager.h"

#include "klee/Expr.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"

#include <inttypes.h>
#include <sys/mman.h>

using namespace klee;

namespace {

llvm::cl::OptionCategory MemoryCat("Memory management options",
                                   "These options control memory management.");

llvm::cl::opt<bool> DeterministicAllocation(
    "allocate-determ",
    llvm::cl::desc("Allocate memory deterministically (default=false)"),
    llvm::cl::init(false), llvm::cl::cat(MemoryCat));

llvm::cl::opt<unsigned> DeterministicAllocationSize(
    "allocate-determ-size",
    llvm::cl::desc(
        "Preallocated memory for deterministic allocation in MB (default=100)"),
    llvm::cl::init(100), llvm::cl::cat(MemoryCat));

llvm::cl::opt<bool> NullOnZeroMalloc(
    "return-null-on-zero-malloc",
    llvm::cl::desc("Returns NULL if malloc(0) is called (default=false)"),
    llvm::cl::init(false), llvm::cl::cat(MemoryCat));

llvm::cl::opt<unsigned> RedzoneSize(
    "redzone-size",
    llvm::cl::desc("Set the size of the redzones to be added after each "
                   "allocation (in bytes). This is important to detect "
                   "out-of-bounds accesses (default=10)"),
    llvm::cl::init(10), llvm::cl::cat(MemoryCat));

llvm::cl::opt<unsigned long long> DeterministicStartAddress(
    "allocate-determ-start-address",
    llvm::cl::desc("Start address for deterministic allocation. Has to be page "
                   "aligned (default=0x7ff30000000)"),
    llvm::cl::init(0x7ff30000000), llvm::cl::cat(MemoryCat));

llvm::cl::opt<bool> LocalAddressSpace("local-address-space",
                                      llvm::cl::desc(""),
                                      llvm::cl::init(false));

} // namespace

/***/
MemoryManager::MemoryManager(ArrayCache *_arrayCache)
    : arrayCache(_arrayCache), deterministicSpace(0), nextFreeSlot(0),
      spaceSize(DeterministicAllocationSize.getValue() * 1024 * 1024) {
  if (DeterministicAllocation) {
    // Page boundary
    void *expectedAddress = (void *)DeterministicStartAddress.getValue();

    char *newSpace =
        (char *)mmap(expectedAddress, spaceSize, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (newSpace == MAP_FAILED) {
      klee_error("Couldn't mmap() memory for deterministic allocations");
    }
    if (expectedAddress != newSpace && expectedAddress != 0) {
      klee_error("Could not allocate memory deterministically");
    }

    klee_message("Deterministic memory allocation starting from %p", newSpace);
    deterministicSpace = newSpace;
    nextFreeSlot = newSpace;
  }
}

MemoryManager::~MemoryManager() {
  while (!objects.empty()) {
    MemoryObject *mo = *objects.begin();
    if (!mo->isFixed && !DeterministicAllocation)
      free((void *)mo->address);
    objects.erase(mo);
    delete mo;
  }

  if (DeterministicAllocation)
    munmap(deterministicSpace, spaceSize);
}

MemoryObject *MemoryManager::allocate(uint64_t size, bool isLocal,
                                      bool isGlobal,
                                      const llvm::Value *allocSite,
                                      size_t alignment,
                                      char **local_next_slot) {
  if (size > 10 * 1024 * 1024)
    klee_warning_once(0, "Large alloc: %" PRIu64
                         " bytes.  KLEE may run out of memory.",
                      size);

  // Return NULL if size is zero, this is equal to error during allocation
  if (NullOnZeroMalloc && size == 0)
    return 0;

  if (!llvm::isPowerOf2_64(alignment)) {
    klee_warning("Only alignment of power of two is supported");
    return 0;
  }

  uint64_t address = 0;
  char *next = LocalAddressSpace ? *local_next_slot : nextFreeSlot;
  if (!next) {
    next = deterministicSpace;
  }

  if (DeterministicAllocation) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
    address = llvm::alignTo((uint64_t)(next) + alignment - 1, alignment);
#else
    address = llvm::RoundUpToAlignment((uint64_t)(next) + alignment - 1, alignment);
#endif

    // Handle the case of 0-sized allocations as 1-byte allocations.
    // This way, we make sure we have this allocation between its own red zones
    size_t alloc_size = std::max(size, (uint64_t)1);
    if ((char *)(address) + alloc_size < deterministicSpace + spaceSize) {
      next = (char *)(address) + alloc_size + RedzoneSize;
    } else {
      klee_warning_once(0, "Couldn't allocate %" PRIu64
                           " bytes. Not enough deterministic space left.",
                        size);
      address = 0;
    }
  } else {
    // Use malloc for the standard case
    if (alignment <= 8)
      address = (uint64_t)malloc(size);
    else {
      int res = posix_memalign((void **)&address, alignment, size);
      if (res < 0) {
        klee_warning("Allocating aligned memory failed.");
        address = 0;
      }
    }
  }

  if (LocalAddressSpace) {
    *local_next_slot = next;
  } else {
    nextFreeSlot = next;
  }

  if (!address)
    return 0;

  ++stats::allocations;
  MemoryObject *res = new MemoryObject(address, size, isLocal, isGlobal, false, true,
                                       allocSite, this);
  objects.insert(res);
  return res;
}

MemoryObject *MemoryManager::allocateFixed(uint64_t address, uint64_t size,
                                           const llvm::Value *allocSite) {
#ifndef NDEBUG
  for (objects_ty::iterator it = objects.begin(), ie = objects.end(); it != ie;
       ++it) {
    MemoryObject *mo = *it;
    if (mo->address == address && mo->size == size) {
      continue;
    }
    if (address + size > mo->address && address < mo->address + mo->size) {
      klee_error("Trying to allocate an overlapping object");
    }
  }
#endif

  ++stats::allocations;
  MemoryObject *res =
      new MemoryObject(address, size, false, true, true, true, allocSite, this);
  objects.insert(res);
  return res;
}

bool MemoryManager::allocateWithPartition(std::vector<uint64_t> partition,
                                          bool isLocal,
                                          bool isGlobal,
                                          const llvm::Value *allocSite,
                                          size_t alignment,
                                          char **local_next_slot,
                                          std::vector<const MemoryObject *> &result) {
  uint64_t total_size = 0;
  for (uint64_t mo_size : partition) {
    total_size += mo_size;
  }

  if (total_size > 10 * 1024 * 1024) {
    klee_warning_once(0, "Large alloc: %" PRIu64 " bytes.  KLEE may run out of memory.", total_size);
  }

  if (NullOnZeroMalloc && total_size == 0) {
    assert(0);
  }

  if (!llvm::isPowerOf2_64(alignment)) {
    klee_warning("Only alignment of power of two is supported");
    return false;
  }

  uint64_t address = 0;
  char *next = LocalAddressSpace ? *local_next_slot : nextFreeSlot;

  if (DeterministicAllocation) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
    address = llvm::alignTo((uint64_t)(next) + alignment - 1, alignment);
#else
    address = llvm::RoundUpToAlignment((uint64_t)(next) + alignment - 1,
                                       alignment);
#endif

    size_t alloc_size = std::max(total_size, (uint64_t)(1));
    if ((char *)(address) + alloc_size < deterministicSpace + spaceSize) {
      next = (char *)(address) + alloc_size + RedzoneSize;
    } else {
      klee_warning_once(0, "Couldn't allocate %" PRIu64 " bytes. Not enough deterministic space left.", total_size);
      address = 0;
    }
  } else {
    if (alignment <= 8) {
      address = (uint64_t)(malloc(total_size));
    } else {
      int res = posix_memalign((void **)(&address), alignment, total_size);
      if (res < 0) {
        klee_warning("Allocating aligned memory failed.");
        address = 0;
      }
    }
  }

  if (LocalAddressSpace) {
    *local_next_slot = next;
  } else {
    nextFreeSlot = next;
  }

  if (!address) {
    return false;
  }

  uint64_t offset = 0;
  for (size_t mo_size : partition) {
    ++stats::allocations;
    /* TODO: first object can be free'd */
    MemoryObject *mo = new MemoryObject(address + offset,
                                        mo_size,
                                        isLocal,
                                        isGlobal,
                                        false,
                                        false,
                                        allocSite,
                                        this);
    objects.insert(mo);
    result.push_back(mo);
    offset += mo_size;
  }

  return true;
}

void MemoryManager::deallocate(const MemoryObject *mo) { assert(0); }

void MemoryManager::markFreed(MemoryObject *mo) {
  if (objects.find(mo) != objects.end()) {
    if (!mo->isFixed && mo->canFree && !DeterministicAllocation)
      free((void *)mo->address);
    objects.erase(mo);
  }
}

size_t MemoryManager::getUsedDeterministicSize() {
  return nextFreeSlot - deterministicSpace;
}
