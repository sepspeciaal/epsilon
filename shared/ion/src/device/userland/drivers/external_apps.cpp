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

/* Custom OS: apps can live in two places.
 * - Region 0: this slot's own app space (filled by our build, extra_apps/).
 * - Region 1: the app space of the other slot, where the NumWorks website
 *   installs apps while the official software runs.
 * Both are shown in the home screen. */
struct AppRegion {
  uint8_t* start;
  uint8_t* end;
};
static AppRegion s_regions[2];
static int s_numberOfRegions = -1;

static bool looksLikeApp(uint8_t* address, uint8_t* end) {
  if (address + App::k_minAppSize > end || !appAtAddress(address)) {
    return false;
  }
  uint32_t* info = reinterpret_cast<uint32_t*>(address);
  uint32_t size = info[static_cast<uint8_t>(App::AppInfo::AppSize)];
  return info[static_cast<uint8_t>(App::AppInfo::MagicEnd)] == k_magic &&
         size >= App::k_minAppSize &&
         size <= static_cast<uint32_t>(end - address);
}

static void initStorage() {
  if (s_numberOfRegions >= 0) {
    return;
  }
  s_numberOfRegions = 0;
  uint8_t* ownStart = &_external_apps_flash_start;
  uint8_t* ownEnd = &_external_apps_flash_end;
  if (looksLikeApp(ownStart, ownEnd)) {
    s_regions[s_numberOfRegions++] = {ownStart, ownEnd};
  }
  constexpr uint32_t k_slotSize =
      Ion::Device::Board::Config::ExternalFlashLength / 2;
  constexpr uint32_t k_sector =
      Ion::Device::Board::Config::ExternalAppsSectorUnit;
  bool runsInSlotB = reinterpret_cast<uint32_t>(ownStart) >=
                     Ion::Device::Board::Config::SlotBOrigin;
  uint8_t* otherEnd = runsInSlotB ? ownEnd - k_slotSize : ownEnd + k_slotSize;
  uint8_t* otherSlotStart =
      reinterpret_cast<uint8_t*>(runsInSlotB
                                     ? Ion::Device::Board::Config::SlotAOrigin
                                     : Ion::Device::Board::Config::SlotBOrigin);
  for (uint8_t* address = otherSlotStart + k_sector;
       address + App::k_minAppSize <= otherEnd; address += k_sector) {
    if (looksLikeApp(address, otherEnd)) {
      s_regions[s_numberOfRegions++] = {address, otherEnd};
      break;
    }
  }
}

static int regionOf(const uint8_t* address) {
  initStorage();
  for (int i = 0; i < s_numberOfRegions; i++) {
    if (address >= s_regions[i].start && address < s_regions[i].end) {
      return i;
    }
  }
  return -1;
}

App::App(uint8_t* a) : m_startAddress(a) {
  assert(appAtAddress(m_startAddress));
}

bool addressWithinExternalAppsSection(const uint8_t* address) {
  return regionOf(address) >= 0;
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
  int region = regionOf(m_currentAddress);
  uint32_t sizeOfCurrentApp =
      *reinterpret_cast<uint32_t*>(m_currentAddress + 6 * sizeof(uint32_t));
  uint8_t* next = nextSectorAlignedAddress(m_currentAddress + sizeOfCurrentApp);
  if (region >= 0 && looksLikeApp(next, s_regions[region].end)) {
    m_currentAddress = next;
    return *this;
  }
  // Continue with the next region, if any
  m_currentAddress = nullptr;
  if (region >= 0 && region + 1 < s_numberOfRegions) {
    m_currentAddress = s_regions[region + 1].start;
  }
  return *this;
}

bool hideExternalApps(bool isExamModeActive) { return isExamModeActive; }

AppIterator Apps::begin() const {
  initStorage();
  if (m_isExamModeActive || s_numberOfRegions == 0) {
    return end();
  }
  return AppIterator(s_regions[0].start);
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
  /* Custom OS: never erase apps. The extra apps are part of our build, and the
   * apps of the other slot belong to the official software. */
  (void)isExamModeActive;
  (void)keepAfterCrash;
}

bool allowThirdParty() { return true; }

void updateClearanceLevel(bool isExamModeActive) {
  if (numberOfApps(isExamModeActive) > 0) {
    Device::Board::updateClearanceLevelForExternalApps();  // Display pop-up
  }
}

}  // namespace ExternalApps
}  // namespace Ion
