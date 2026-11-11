#include <assert.h>
#include <config/board.h>
#include <ion/external_apps.h>
#include <ion/reset.h>
#include <shared/drivers/flash_write_with_interruptions.h>

#include "board.h"

#ifndef EXTERNAL_APPS_API_LEVEL
#error This file expects EXTERNAL_APPS_API_LEVEL to be defined
#endif

extern "C" {
extern uint8_t _external_apps_flash_start;
extern uint8_t _external_apps_flash_end;
}

namespace Ion {
namespace ExternalApps {

constexpr static uint32_t k_magic = 0xDEC0BEBA;

bool appAtAddress(uint8_t* address) {
  return *reinterpret_cast<uint32_t*>(address) == k_magic;
}

/* Custom OS: when this userland runs in one slot and has no apps of its own,
 * it uses the apps installed (by the NumWorks website) in the other slot. */
static uint8_t* s_storageStart = nullptr;
static uint8_t* s_storageEnd = nullptr;
static bool s_usesOtherSlot = false;

static bool looksLikeApp(uint8_t* address, uint8_t* end) {
  if (!appAtAddress(address)) {
    return false;
  }
  uint32_t* info = reinterpret_cast<uint32_t*>(address);
  uint32_t size = info[static_cast<uint8_t>(App::AppInfo::AppSize)];
  return info[static_cast<uint8_t>(App::AppInfo::MagicEnd)] == k_magic &&
         size >= App::k_minAppSize &&
         size <= static_cast<uint32_t>(end - address);
}

static void initStorage() {
  if (s_storageStart != nullptr) {
    return;
  }
  s_storageStart = &_external_apps_flash_start;
  s_storageEnd = &_external_apps_flash_end;
  if (appAtAddress(s_storageStart)) {
    return;
  }
  constexpr uint32_t k_slotSize =
      Ion::Device::Board::Config::ExternalFlashLength / 2;
  constexpr uint32_t k_sector =
      Ion::Device::Board::Config::ExternalAppsSectorUnit;
  bool runsInSlotB = reinterpret_cast<uint32_t>(s_storageStart) >=
                     Ion::Device::Board::Config::SlotBOrigin;
  uint8_t* otherEnd =
      runsInSlotB ? s_storageEnd - k_slotSize : s_storageEnd + k_slotSize;
  uint8_t* otherSlotStart =
      reinterpret_cast<uint8_t*>(runsInSlotB
                                     ? Ion::Device::Board::Config::SlotAOrigin
                                     : Ion::Device::Board::Config::SlotBOrigin);
  for (uint8_t* address = otherSlotStart + k_sector;
       address + App::k_minAppSize <= otherEnd; address += k_sector) {
    if (looksLikeApp(address, otherEnd)) {
      s_storageStart = address;
      s_storageEnd = otherEnd;
      s_usesOtherSlot = true;
      return;
    }
  }
}

static uint8_t* storageStart() {
  initStorage();
  return s_storageStart;
}

static uint8_t* storageEnd() {
  initStorage();
  return s_storageEnd;
}

App::App(uint8_t* a) : m_startAddress(a) {
  assert(appAtAddress(m_startAddress));
}

bool addressWithinExternalAppsSection(const uint8_t* address) {
  return address >= storageStart() && address < storageEnd();
}

uint32_t App::appInfo(AppInfo info) const {
  return reinterpret_cast<uint32_t*>(
      m_startAddress)[static_cast<uint8_t>(info)];
}

uint8_t* App::appInfoToAddress(AppInfo info) const {
  assert(info == AppInfo::NameAddress || info == AppInfo::IconAddress ||
         info == AppInfo::EntryPointAddress);
  uint8_t* address = m_startAddress + appInfo(info);
  // Check that address is in authorized memory
  return addressWithinExternalAppsSection(address) ? address : nullptr;
}

const uint32_t App::APILevel() const { return appInfo(AppInfo::APILevel); }

const char* App::name() const {
  const char* n =
      reinterpret_cast<const char*>(appInfoToAddress(AppInfo::NameAddress));
  if (n == nullptr) {
    return nullptr;
  }
  // Check that all name is within authorized memory
  const char* c = n;
  while (*c != 0) {
    if (!addressWithinExternalAppsSection(
            reinterpret_cast<const uint8_t*>(c++))) {
      return nullptr;
    }
  }
  return n;
}

uint32_t App::iconSize() const {
  uint32_t size = appInfo(AppInfo::IconSize);
  const uint8_t* data = iconData();
  if (data == nullptr || !addressWithinExternalAppsSection(data + size - 1)) {
    return 0;
  }
  return size;
}

const uint8_t* App::iconData() const {
  // TODO: Add check on decompression?
  return reinterpret_cast<const uint8_t*>(
      appInfoToAddress(AppInfo::IconAddress));
}

void* App::entryPoint() const {
  if (APILevel() != EXTERNAL_APPS_API_LEVEL) {
    return nullptr;
  }
  /* As stated in ARM Cortex guide generic user guide: Bit[0] of any address
   * you write to the PC with a BX, BLX, LDM, LDR, or POP instruction must be
   * 1 for correct execution, because this bit indicates the required
   * instruction set, and the Cortex-M7 processor only supports Thumb
   * instructions.
   */
  return reinterpret_cast<void*>(
      reinterpret_cast<uint32_t>(appInfoToAddress(AppInfo::EntryPointAddress)) |
      0x1);
}

void App::eraseMagicCode() {
  assert(appAtAddress(m_startAddress));
  uint8_t value = 0x00;
  Ion::Device::Flash::WriteMemoryWithInterruptions(
      m_startAddress, reinterpret_cast<uint8_t*>(&value), sizeof(value), true);
}

uint8_t* nextSectorAlignedAddress(uint8_t* address) {
  // Trick to return address if it was already aligned
  address -= 1;
  // Find previous aligned address
  address = reinterpret_cast<uint8_t*>(
      reinterpret_cast<uint32_t>(address) &
      ~(Ion::Device::Board::Config::ExternalAppsSectorUnit - 1));
  address += Ion::Device::Board::Config::ExternalAppsSectorUnit;
  return address;
}

AppIterator& AppIterator::operator++() {
  uint32_t sizeOfCurrentApp =
      *reinterpret_cast<uint32_t*>(m_currentAddress + 6 * sizeof(uint32_t));
  m_currentAddress += sizeOfCurrentApp;
  // Find the next address aligned on external apps sector size
  m_currentAddress = nextSectorAlignedAddress(m_currentAddress);
  if (m_currentAddress < storageStart() ||
      m_currentAddress + App::k_minAppSize > storageEnd() ||
      !appAtAddress(m_currentAddress)) {
    m_currentAddress = nullptr;
  }
  return *this;
}

bool hideExternalApps(bool isExamModeActive) { return isExamModeActive; }

AppIterator Apps::begin() const {
  uint8_t* start = storageStart();
  assert(nextSectorAlignedAddress(start) == start);
  if (m_isExamModeActive || !appAtAddress(start)) {
    return end();
  }
  return AppIterator(start);
}

int numberOfApps(bool isExamModeActive) {
  int counter = 0;
  for (App a : Apps(isExamModeActive)) {
    assert(!hideExternalApps(isExamModeActive));
    (void)a;
    counter++;
  }
  return counter;
}

void deleteApps(bool isExamModeActive, bool keepAfterCrash) {
  initStorage();
  if (s_usesOtherSlot) {
    // Never erase the apps of the official software in the other slot
    return;
  }
  if (keepAfterCrash &&
      Ion::Reset::lastResetType() == Ion::Reset::ResetType::Software) {
    /* Keep external apps after a software crash.
     * A voluntary software reset (from the settings menu) calls with
     * keepAfterCrash = false. */
    return;
  }
  for (App a : Apps(isExamModeActive)) {
    assert(!hideExternalApps(isExamModeActive));
    a.eraseMagicCode();
  }
}

bool allowThirdParty() { return true; }

void updateClearanceLevel(bool isExamModeActive) {
  if (numberOfApps(isExamModeActive) > 0) {
    Device::Board::updateClearanceLevelForExternalApps();  // Display pop-up
  }
}

}  // namespace ExternalApps
}  // namespace Ion
