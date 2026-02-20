#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3)

#include "decider_usb.h"

#include <cstring>

#include "esp_system.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "tusb.h"

namespace esphome::decider_usb {

static const char *const TAG = "decider_usb";

static constexpr uint32_t SECTOR_SIZE = 512;
static constexpr uint32_t DISK_SIZE_BYTES = 64u * 1024u * 1024u;
static constexpr uint32_t TOTAL_SECTORS = DISK_SIZE_BYTES / SECTOR_SIZE;
static constexpr uint32_t PARTITION_START_LBA = 2048;  // 1 MiB alignment
static constexpr uint32_t PARTITION_SECTORS = TOTAL_SECTORS - PARTITION_START_LBA;
static constexpr uint8_t PARTITION_TYPE_HIDDEN_FAT16 = 0x16;

static constexpr uint16_t RESERVED_SECTORS = 1;
static constexpr uint8_t NUM_FATS = 2;
static constexpr uint16_t ROOT_ENTRY_COUNT = 512;
static constexpr uint8_t SECTORS_PER_CLUSTER = 4;
static constexpr uint16_t SECTORS_PER_FAT = 128;

static constexpr uint16_t ROOT_DIR_SECTORS = (ROOT_ENTRY_COUNT * 32u) / SECTOR_SIZE;
static constexpr uint32_t PARTITION_BOOT_SECTOR = PARTITION_START_LBA;
static constexpr uint32_t FAT_1_START = PARTITION_BOOT_SECTOR + RESERVED_SECTORS;
static constexpr uint32_t FAT_2_START = FAT_1_START + SECTORS_PER_FAT;
static constexpr uint32_t ROOT_DIR_START = FAT_2_START + SECTORS_PER_FAT;
static constexpr uint32_t DATA_START = ROOT_DIR_START + ROOT_DIR_SECTORS;

static constexpr uint16_t FILE_FIRST_CLUSTER = 2;
static constexpr uint32_t FILE_FIRST_SECTOR = DATA_START + (FILE_FIRST_CLUSTER - 2u) * SECTORS_PER_CLUSTER;
static constexpr uint32_t FILE_MAX_SIZE = 256;
static constexpr uint8_t FLASH_MODE_VENDOR_REQUEST = 0xA5;
static constexpr uint16_t FLASH_MODE_VENDOR_VALUE = 0xDCD1;
static constexpr uint16_t FLASH_MODE_VENDOR_INDEX = 0x0001;
static constexpr uint8_t SCSI_ASC_MEDIUM_CHANGED = 0x28;
static constexpr uint8_t SCSI_ASCQ_MEDIUM_CHANGED = 0x00;

static portMUX_TYPE s_file_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_file_content[FILE_MAX_SIZE] = {0};
static uint32_t s_file_size = 0;
static bool s_media_changed_pending = false;
static bool s_flash_mode_vendor_request_pending = false;

static inline void set_le16(uint8_t *buffer, size_t offset, uint16_t value) {
  buffer[offset + 0] = value & 0xFF;
  buffer[offset + 1] = value >> 8;
}

static inline void set_le32(uint8_t *buffer, size_t offset, uint32_t value) {
  buffer[offset + 0] = value & 0xFF;
  buffer[offset + 1] = (value >> 8) & 0xFF;
  buffer[offset + 2] = (value >> 16) & 0xFF;
  buffer[offset + 3] = (value >> 24) & 0xFF;
}

static void set_file_content(const uint8_t *content, size_t size) {
  const uint32_t safe_size = size > FILE_MAX_SIZE ? FILE_MAX_SIZE : static_cast<uint32_t>(size);
  portENTER_CRITICAL(&s_file_lock);
  std::memset(s_file_content, 0, FILE_MAX_SIZE);
  if (safe_size > 0) {
    std::memcpy(s_file_content, content, safe_size);
  }
  s_file_size = safe_size;
  portEXIT_CRITICAL(&s_file_lock);
}

static uint32_t get_file_size() {
  uint32_t size;
  portENTER_CRITICAL(&s_file_lock);
  size = s_file_size;
  portEXIT_CRITICAL(&s_file_lock);
  return size;
}

static uint32_t copy_file_bytes(uint8_t *dest, uint32_t offset, uint32_t max_len) {
  uint32_t copied = 0;
  portENTER_CRITICAL(&s_file_lock);
  if (offset < s_file_size) {
    const uint32_t available = s_file_size - offset;
    copied = max_len < available ? max_len : available;
    std::memcpy(dest, s_file_content + offset, copied);
  }
  portEXIT_CRITICAL(&s_file_lock);
  return copied;
}

static void signal_media_changed() {
  portENTER_CRITICAL(&s_file_lock);
  s_media_changed_pending = true;
  portEXIT_CRITICAL(&s_file_lock);
}

static bool consume_media_changed() {
  bool changed;
  portENTER_CRITICAL(&s_file_lock);
  changed = s_media_changed_pending;
  s_media_changed_pending = false;
  portEXIT_CRITICAL(&s_file_lock);
  return changed;
}

static bool is_flash_mode_vendor_request(const tusb_control_request_t *request) {
  if (request == nullptr) {
    return false;
  }
  return request->bmRequestType_bit.direction == TUSB_DIR_OUT &&
         request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
         request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_DEVICE &&
         request->bRequest == FLASH_MODE_VENDOR_REQUEST && request->wValue == FLASH_MODE_VENDOR_VALUE &&
         request->wIndex == FLASH_MODE_VENDOR_INDEX && request->wLength == 0;
}

static void reboot_into_download_mode(const char *trigger_source) {
  ESP_LOGW(TAG, "%s accepted, rebooting into ROM download mode.", trigger_source);
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
}

static bool arm_flash_mode_vendor_request(uint8_t rhport, tusb_control_request_t const *request) {
  s_flash_mode_vendor_request_pending = false;
  if (!tud_control_status(rhport, request)) {
    return false;
  }
  s_flash_mode_vendor_request_pending = true;
  return true;
}

static void handle_flash_mode_vendor_request_ack() {
  if (!s_flash_mode_vendor_request_pending) {
    return;
  }
  s_flash_mode_vendor_request_pending = false;
  reboot_into_download_mode("Vendor control flash request");
}

static void build_mbr_sector(uint8_t *sector) {
  std::memset(sector, 0, SECTOR_SIZE);

  constexpr size_t partition_entry_offset = 446;
  sector[partition_entry_offset + 0] = 0x00;  // non-bootable
  sector[partition_entry_offset + 1] = 0xFF;  // CHS start (unused; use LBA)
  sector[partition_entry_offset + 2] = 0xFF;
  sector[partition_entry_offset + 3] = 0xFF;
  sector[partition_entry_offset + 4] = PARTITION_TYPE_HIDDEN_FAT16;
  sector[partition_entry_offset + 5] = 0xFF;  // CHS end (unused; use LBA)
  sector[partition_entry_offset + 6] = 0xFF;
  sector[partition_entry_offset + 7] = 0xFF;
  set_le32(sector, partition_entry_offset + 8, PARTITION_START_LBA);
  set_le32(sector, partition_entry_offset + 12, PARTITION_SECTORS);

  sector[510] = 0x55;
  sector[511] = 0xAA;
}

static void build_boot_sector(uint8_t *sector) {
  std::memset(sector, 0, SECTOR_SIZE);

  sector[0] = 0xEB;
  sector[1] = 0x3C;
  sector[2] = 0x90;
  std::memcpy(&sector[3], "MSDOS5.0", 8);

  set_le16(sector, 11, SECTOR_SIZE);
  sector[13] = SECTORS_PER_CLUSTER;
  set_le16(sector, 14, RESERVED_SECTORS);
  sector[16] = NUM_FATS;
  set_le16(sector, 17, ROOT_ENTRY_COUNT);
  set_le16(sector, 19, 0);  // total sectors in 16-bit field not used for 64 MiB.
  sector[21] = 0xF8;
  set_le16(sector, 22, SECTORS_PER_FAT);
  set_le16(sector, 24, 63);
  set_le16(sector, 26, 255);
  set_le32(sector, 28, PARTITION_START_LBA);
  set_le32(sector, 32, PARTITION_SECTORS);

  sector[36] = 0x80;
  sector[37] = 0x00;
  sector[38] = 0x29;
  set_le32(sector, 39, 0x20260216);
  std::memcpy(&sector[43], "DECIDER    ", 11);
  std::memcpy(&sector[54], "FAT16   ", 8);

  sector[510] = 0x55;
  sector[511] = 0xAA;
}

static void build_fat_sector(uint8_t *sector, uint32_t sector_index_in_fat) {
  std::memset(sector, 0, SECTOR_SIZE);
  if (sector_index_in_fat != 0) {
    return;
  }

  set_le16(sector, 0, 0xFFF8);  // media descriptor + reserved bits
  set_le16(sector, 2, 0xFFFF);  // reserved cluster
  set_le16(sector, 4, 0xFFFF);  // cluster #2 -> EOF (DECIDER.CHO)
}

static void build_root_dir_sector(uint8_t *sector, uint32_t sector_index_in_root) {
  std::memset(sector, 0, SECTOR_SIZE);
  if (sector_index_in_root != 0) {
    return;
  }

  std::memcpy(&sector[0], "DECIDER CHO", 11);
  sector[11] = 0x20;  // Archive attribute
  set_le16(sector, 26, FILE_FIRST_CLUSTER);
  set_le32(sector, 28, get_file_size());
}

static bool build_data_sector(uint8_t *sector, uint32_t lba) {
  std::memset(sector, 0, SECTOR_SIZE);

  if (lba == FILE_FIRST_SECTOR) {
    copy_file_bytes(sector, 0, SECTOR_SIZE);
  }
  return true;
}

static bool build_sector(uint8_t *sector, uint32_t lba) {
  if (lba >= TOTAL_SECTORS) {
    return false;
  }

  if (lba == 0) {
    build_mbr_sector(sector);
    return true;
  }

  if (lba < PARTITION_START_LBA) {
    std::memset(sector, 0, SECTOR_SIZE);
    return true;
  }

  if (lba == PARTITION_BOOT_SECTOR) {
    build_boot_sector(sector);
    return true;
  }

  if (lba >= FAT_1_START && lba < FAT_1_START + SECTORS_PER_FAT) {
    build_fat_sector(sector, lba - FAT_1_START);
    return true;
  }
  if (lba >= FAT_2_START && lba < FAT_2_START + SECTORS_PER_FAT) {
    build_fat_sector(sector, lba - FAT_2_START);
    return true;
  }
  if (lba >= ROOT_DIR_START && lba < ROOT_DIR_START + ROOT_DIR_SECTORS) {
    build_root_dir_sector(sector, lba - ROOT_DIR_START);
    return true;
  }

  return build_data_sector(sector, lba);
}

int32_t read_from_virtual_disk(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (offset >= SECTOR_SIZE) {
    return TUD_MSC_RET_ERROR;
  }

  auto *out = static_cast<uint8_t *>(buffer);
  uint32_t remaining = bufsize;
  uint32_t current_lba = lba;
  uint32_t current_offset = offset;
  uint8_t sector[SECTOR_SIZE];

  while (remaining > 0) {
    if (!build_sector(sector, current_lba)) {
      return TUD_MSC_RET_ERROR;
    }

    const uint32_t available = SECTOR_SIZE - current_offset;
    const uint32_t chunk = remaining < available ? remaining : available;
    std::memcpy(out, &sector[current_offset], chunk);

    out += chunk;
    remaining -= chunk;
    current_lba++;
    current_offset = 0;
  }

  return static_cast<int32_t>(bufsize);
}

void DeciderUsb::setup() {
  // File storage is statically zero-initialized; default file is empty.
}

void DeciderUsb::dump_config() {
  ESP_LOGCONFIG(TAG, "Decider USB:");
}

void DeciderUsb::set_boot_option(const std::string &choice_type, const std::optional<std::string> &entry_id) {
  std::string content;
  if (choice_type == "entry_id") {
    if (!entry_id.has_value()) {
      ESP_LOGE(TAG, "set_boot_option failed: choice_type is 'entry_id' but entry_id was not provided.");
      return;
    }
    content.reserve(32 + choice_type.size() + (entry_id.has_value() ? entry_id->size() : 0));
    content += "choice_type=";
    content += choice_type;
    content += "\nentry_id=";
    if (entry_id.has_value()) {
      content += *entry_id;
    }
    content += '\n';
  } else {
    content.reserve(16 + choice_type.size());
    content += "choice_type=";
    content += choice_type;
    content += '\n';
  }

  if (content.size() > FILE_MAX_SIZE) {
    ESP_LOGE(TAG, "set_boot_option failed: content too long (%u > %u).", static_cast<unsigned>(content.size()),
             FILE_MAX_SIZE);
    return;
  }

  set_file_content(reinterpret_cast<const uint8_t *>(content.data()), content.size());
  signal_media_changed();
  ESP_LOGI(TAG, "Updated DECIDER.CHO (%u bytes): choice_type='%s'%s", static_cast<unsigned>(content.size()),
           choice_type.c_str(), entry_id.has_value() ? "" : ", no entry_id");
}

}  // namespace esphome::decider_usb

extern "C" {

uint8_t tud_msc_get_maxlun_cb(void) { return 0; }

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
  (void) lun;

  std::memset(vendor_id, ' ', 8);
  std::memset(product_id, ' ', 16);
  std::memset(product_rev, ' ', 4);

  std::memcpy(vendor_id, "ROTFRONT", 8);
  std::memcpy(product_id, "DECIDER", 7);
  std::memcpy(product_rev, "1.0", 3);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  if (esphome::decider_usb::consume_media_changed()) {
    tud_msc_set_sense(lun, SCSI_SENSE_UNIT_ATTENTION, esphome::decider_usb::SCSI_ASC_MEDIUM_CHANGED,
                      esphome::decider_usb::SCSI_ASCQ_MEDIUM_CHANGED);
    return false;
  }
  return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
  (void) lun;
  *block_count = esphome::decider_usb::TOTAL_SECTORS;
  *block_size = esphome::decider_usb::SECTOR_SIZE;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  (void) lun;
  int32_t result = esphome::decider_usb::read_from_virtual_disk(lba, offset, buffer, bufsize);
  if (result < 0) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);  // logical block address out of range
  }
  return result;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  (void) lba;
  (void) offset;
  (void) buffer;
  (void) bufsize;
  tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);  // write protected
  return TUD_MSC_RET_ERROR;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
  (void) lun;
  return false;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
  (void) buffer;
  (void) bufsize;

  switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
      return 0;
    default:
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);  // invalid command operation code
      return TUD_MSC_RET_ERROR;
  }
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  (void) lun;
  (void) power_condition;
  (void) start;
  (void) load_eject;
  return true;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  if (!esphome::decider_usb::is_flash_mode_vendor_request(request)) {
    return false;
  }

  if (stage == CONTROL_STAGE_SETUP) {
    return esphome::decider_usb::arm_flash_mode_vendor_request(rhport, request);
  }

  if (stage == CONTROL_STAGE_ACK) {
    esphome::decider_usb::handle_flash_mode_vendor_request_ack();
  }

  return true;
}

}  // extern "C"

#endif  // USE_ESP32_VARIANT_ESP32P4 || USE_ESP32_VARIANT_ESP32S2 || USE_ESP32_VARIANT_ESP32S3
