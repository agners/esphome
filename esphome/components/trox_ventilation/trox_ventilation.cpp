#include "trox_ventilation.h"
#include "esphome/core/log.h"

namespace esphome {
namespace trox_ventilation {

static const char *const TAG = "trox_ventilation";

void TroxVentilation::update() {}

void TroxVentilation::on_modbus_data(const std::vector<uint8_t> &data) {
  ESP_LOGV(TAG, "Recieved data %s for addr %02X", format_hex_pretty(data).c_str(), this->address_);
  uint8_t function_code = data[1];

  // Read input registers
  if (function_code == 0x04 && this->request_received && data.size() == 39)
  {
    ESP_LOGV(TAG, "Read input register response of interest detected, parsing...");
    if (this->drosselstellung_ != nullptr) {
      float drosselstellung = encode_uint16(data[3], data[4]);
      this->drosselstellung_->publish_state(drosselstellung);
    }

    if (this->volumenstrom_prozent_ != nullptr) {
      float volumenstrom_prozent = encode_uint16(data[5], data[6]);
      this->volumenstrom_prozent_->publish_state(volumenstrom_prozent);
    }

    if (this->druck_drosselorgan_ != nullptr) {
      float druck_drosselorgan = encode_uint16(data[7], data[8]);
      this->druck_drosselorgan_->publish_state(druck_drosselorgan);
    }

    if (this->volumenstrom_ != nullptr) {
      float volumenstrom = encode_uint16(data[9], data[10]);
      this->volumenstrom_->publish_state(volumenstrom);
    }

    if (this->temperatur_ != nullptr) {
      float temperatur = encode_uint16(data[11], data[12]);
      this->temperatur_->publish_state(temperatur / 10.0f);
    }

    if (this->voc_ != nullptr) {
      float voc = encode_uint16(data[13], data[14]);
      this->voc_->publish_state(voc);
    }

  }
  this->request_received = !this->request_received;
};

void TroxVentilation::dump_config() {
  ESP_LOGCONFIG(TAG, "Trox Ventilation:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
}

bool TroxVentilation::is_response_expected() {
  return this->request_received;
}

}  // namespace trox_ventilation
}  // namespace esphome
