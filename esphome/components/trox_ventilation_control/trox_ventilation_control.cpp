#include "trox_ventilation_control.h"
#include "esphome/core/log.h"

namespace esphome {
namespace trox_ventilation_control {

static const char *const TAG = "trox_ventilation_control";

void TroxVentilationControl::setup() {
  ESP_LOGD(TAG, "Setting up Trox Ventilation Control");

  std::string value;

  // Default to 0: Minimum
  size_t index = 0;
  this->pref_ = global_preferences->make_preference<size_t>(this->get_object_id_hash());
  if (!this->pref_.load(&index)) {
    value = this->traits.get_options().at(index);
    ESP_LOGD(TAG, "State from initial (could not load): %s", value.c_str());
  } else {
    value = this->traits.get_options().at(index);
    ESP_LOGD(TAG, "Restored option '%s'", value.c_str());
  }

  optional<uint16_t> mapval = this->mapping_[index];
  if (mapval.has_value()) {
    this->new_mode_ = this->mode_ = *mapval;
    ESP_LOGD(TAG, "Restored value %hu for option '%s'", *mapval, value.c_str());
  }

  this->publish_state(value);
}

void TroxVentilationControl::update() {}

void TroxVentilationControl::on_modbus_data(const std::vector<uint8_t> &data) {
  //ESP_LOGV(TAG, "Recieved data %s for control addr %02X", format_hex_pretty(data).c_str(), this->address_);

  uint8_t function_code = data[1];
  if (function_code == 0x03 && !this->request_received)
  {
    // Request reading holding register, assume its the register 0..2
    ESP_LOGI(TAG, "Read holding register request received, sending mode %hu", this->mode_);
    std::vector<uint8_t> response;
    response.push_back(this->address_);
    response.push_back(function_code);
    response.push_back(6); // byte count
    auto mode = decode_value(this->mode_);
    auto val1 = decode_value(uint16_t(60));
    auto val2 = decode_value(uint16_t(100));
    response.push_back(mode[0]);
    response.push_back(mode[1]);
    response.push_back(val1[0]);
    response.push_back(val1[1]);
    response.push_back(val2[0]);
    response.push_back(val2[1]);
    this->send_raw(response);
    this->mode_ = this->new_mode_;

    std::string value;
    size_t index = std::find(this->mapping_.begin(), this->mapping_.end(), this->mode_) - this->mapping_.begin();
    value = this->traits.get_options().at(index);
    this->ventilation_mode_->publish_state(value);
  }
  this->request_received = !this->request_received;
};

void TroxVentilationControl::dump_config() {
  ESP_LOGCONFIG(TAG, "Trox Ventilation:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
}

bool TroxVentilationControl::is_response_expected() {
  return this->request_received;
}

void TroxVentilationControl::control(const std::string &value) {
  auto options = this->traits.get_options();
  size_t index = std::find(options.begin(), options.end(), value) - options.begin();
  optional<uint16_t> mapval = this->mapping_[index];
  if (!mapval.has_value()) {
    ESP_LOGW(TAG, "Invalid index %d for option '%s'", index, value.c_str());
    return;
  }

  ESP_LOGD(TAG, "Found value %hu for option '%s'", *mapval, value.c_str());
  this->new_mode_ = *mapval;
  this->pref_.save(&index);

  this->publish_state(value);
}

}  // namespace trox_ventilation_control
}  // namespace esphome
