#include "rc522.h"
#include "esphome/core/log.h"

// Based on:
// - https://github.com/miguelbalboa/rfid

namespace esphome {
namespace rc522 {

static const uint8_t WAIT_I_RQ = 0x30;  // RxIRq and IdleIRq

static const char *TAG = "rc522";

static const uint8_t RESET_COUNT = 5;

void format_uid(char *buf, const uint8_t *uid, uint8_t uid_length) {
  int offset = 0;
  for (uint8_t i = 0; i < uid_length; i++) {
    const char *format = "%02X";
    if (i + 1 < uid_length)
      format = "%02X-";
    offset += sprintf(buf + offset, format, uid[i]);
  }
}

void RC522::setup() {
  state_ = STATE_SETUP;
  // Pull device out of power down / reset state.

  // First set the resetPowerDownPin as digital input, to check the MFRC522 power down mode.
  if (reset_pin_ != nullptr) {
    reset_pin_->pin_mode(INPUT);

    if (reset_pin_->digital_read() == LOW) {  // The MFRC522 chip is in power down mode.
      ESP_LOGV(TAG, "Power down mode detected. Hard resetting...");
      reset_pin_->pin_mode(OUTPUT);     // Now set the resetPowerDownPin as digital output.
      reset_pin_->digital_write(LOW);   // Make sure we have a clean LOW state.
      delayMicroseconds(2);             // 8.8.1 Reset timing requirements says about 100ns. Let us be generous: 2μsl
      reset_pin_->digital_write(HIGH);  // Exit power down mode. This triggers a hard reset.
      // Section 8.8.2 in the datasheet says the oscillator start-up time is the start up time of the crystal + 37,74μs.
      // Let us be generous: 50ms.
      reset_timeout_ = millis();
      return;
    }
  }

  // Setup a soft reset
  reset_count_ = RESET_COUNT;
  reset_timeout_ = millis();
}

void RC522::initialize_() {
  // Per originall code, wait 50 ms
  if (millis() - reset_timeout_ < 50)
    return;

  // Reset baud rates
  ESP_LOGV(TAG, "Initialize");

  pcd_write_register(TX_MODE_REG, 0x00);
  pcd_write_register(RX_MODE_REG, 0x00);
  // Reset ModWidthReg
  pcd_write_register(MOD_WIDTH_REG, 0x26);

  // When communicating with a PICC we need a timeout if something goes wrong.
  // f_timer = 13.56 MHz / (2*TPreScaler+1) where TPreScaler = [TPrescaler_Hi:TPrescaler_Lo].
  // TPrescaler_Hi are the four low bits in TModeReg. TPrescaler_Lo is TPrescalerReg.
  pcd_write_register(T_MODE_REG, 0x80);  // TAuto=1; timer starts automatically at the end of the transmission in all
                                         // communication modes at all speeds

  // TPreScaler = TModeReg[3..0]:TPrescalerReg, ie 0x0A9 = 169 => f_timer=40kHz, ie a timer period of 25μs.
  pcd_write_register(T_PRESCALER_REG, 0xA9);
  pcd_write_register(T_RELOAD_REG_H, 0x03);  // Reload timer with 0x3E8 = 1000, ie 25ms before timeout.
  pcd_write_register(T_RELOAD_REG_L, 0xE8);

  // Default 0x00. Force a 100 % ASK modulation independent of the ModGsPReg register setting
  pcd_write_register(TX_ASK_REG, 0x40);
  pcd_write_register(MODE_REG, 0x3D);  // Default 0x3F. Set the preset value for the CRC coprocessor for the CalcCRC
                                       // command to 0x6363 (ISO 14443-3 part 6.2.4)
  pcd_antenna_on_();                   // Enable the antenna driver pins TX1 and TX2 (they were disabled by the reset)

  state_ = STATE_INIT;
}

void RC522::dump_config() {
  ESP_LOGCONFIG(TAG, "RC522:");
  switch (this->error_code_) {
    case NONE:
      break;
    case RESET_FAILED:
      ESP_LOGE(TAG, "Reset command failed!");
      break;
  }

  LOG_PIN("  RESET Pin: ", this->reset_pin_);

  LOG_UPDATE_INTERVAL(this);

  for (auto *child : this->binary_sensors_) {
    LOG_BINARY_SENSOR("  ", "Tag", child);
  }
}

void RC522::update() {
  for (auto *obj : this->binary_sensors_)
    obj->on_scan_end();

  if (state_ == STATE_INIT) {
    uint8_t buffer_atqa[2];
    uint8_t buffer_size = sizeof(buffer_atqa);

    uint8_t valid_bits = 7;  // For REQA and WUPA we need the short frame format - transmit only 7 bits of the last (and
                             // only) uint8_t. TxLastBits = BitFramingReg[2..0]

    pcd_clear_register_bit_mask_(COLL_REG, 0x80);  // ValuesAfterColl=1 => Bits received after collision are cleared.

    uint8_t command = PICC_CMD_REQA;
    pcd_transceive_data_(&command, 1, buffer_atqa, &buffer_size, &valid_bits);

    state_ = STATE_PICC_REQUEST_A;
  } else {
    ESP_LOGD(TAG, "State: %d awaiting comm: %d", state_, awaiting_comm_);
  }
}

void RC522::loop() {
  // First check reset is needed
  if (reset_count_ > 0) {
    pcd_reset_();
    return;
  }
  if (state_ == STATE_SETUP) {
    initialize_();
    return;
  }

  StatusCode status = STATUS_ERROR;  // For lint passing. TODO: refactor this
  if (awaiting_comm_) {
    if (await_communication_(&status)) {
      awaiting_comm_ = false;
    } else
      return;
  }

  switch (state_) {
    case STATE_PICC_REQUEST_A:
      if (status == STATUS_TIMEOUT) {
        // This is normal
        ESP_LOGV(TAG, "CMD_REQA -> status: %d", status);
        state_ = STATE_INIT;
        return;
      } else if (status != STATUS_OK) {
        ESP_LOGW(TAG, "CMD_REQA -> status: %d", status);
        state_ = STATE_INIT;
        return;
      } else if (back_length_ != 2 || *valid_bits_ != 0) {  // ATQA must be exactly 16 bits.
        ESP_LOGW(TAG, "CMD_REQA -> back_length_ %d, valid bits: %d", back_length_, *valid_bits_);
        state_ = STATE_INIT;
        return;
      }

      state_ = STATE_READ_SERIAL;
      break;

    case STATE_READ_SERIAL: {
      uint8_t tx_buffer[2] = {PICC_CMD_SEL_CL1, 32};
      pcd_transceive_data_(tx_buffer, 2, this->back_data_, &this->back_length_);
      state_ = STATE_READ_SERIAL_DONE;
      break;
    }

    case STATE_READ_SERIAL_DONE: {
      if (status != STATUS_OK || back_length_ != 5) {
        ESP_LOGW(TAG, "Unexpected response. Read status is %d. Read bytes: %d", status, back_length_);
        state_ = STATE_INIT;
        return;
      }
      state_ = STATE_INIT;

      bool report = true;
      // 1. Go through all triggers
      for (auto *trigger : this->triggers_)
        trigger->process(this->back_data_, 4);

      // 2. Find a binary sensor
      for (auto *tag : this->binary_sensors_) {
        if (tag->process(this->back_data_, 4)) {
          // 2.1 if found, do not dump
          report = false;
        }
      }

      if (report) {
        char buf[32];
        format_uid(buf, this->back_data_, 4);
        ESP_LOGD(TAG, "Found new tag '%s'", buf);
      }
      break;
    }
    default:
      break;
  }
}

/**
 * Performs a soft reset on the MFRC522 chip and waits for it to be ready again.
 */
void RC522::pcd_reset_() {
  // The datasheet does not mention how long the SoftRest command takes to complete.
  // But the MFRC522 might have been in soft power-down mode (triggered by bit 4 of CommandReg)
  // Section 8.8.2 in the datasheet says the oscillator start-up time is the start up time of the crystal + 37,74μs.
  // Let us be generous: 50ms.

  if (millis() - reset_timeout_ < 50)
    return;

  if (reset_count_ == RESET_COUNT) {
    ESP_LOGV(TAG, "Soft reset...");
    // Issue the SoftReset command.
    pcd_write_register(COMMAND_REG, PCD_SOFT_RESET);
  }

  // Expect the PowerDown bit in CommandReg to be cleared (max 3x50ms)
  if ((pcd_read_register(COMMAND_REG) & (1 << 4)) == 0) {
    reset_count_ = 0;
    ESP_LOGI(TAG, "Device online.");
    // Wait for initialize
    reset_timeout_ = millis();
    return;
  }

  if (--reset_count_ == 0) {
    ESP_LOGE(TAG, "Unable to reset RC522.");
    mark_failed();
  }
}

/**
 * Turns the antenna on by enabling pins TX1 and TX2.
 * After a reset these pins are disabled.
 */
void RC522::pcd_antenna_on_() {
  uint8_t value = pcd_read_register(TX_CONTROL_REG);
  if ((value & 0x03) != 0x03) {
    pcd_write_register(TX_CONTROL_REG, value | 0x03);
  }
}

/**
 * Sets the bits given in mask in register reg.
 */
void RC522::pcd_set_register_bit_mask_(PcdRegister reg,  ///< The register to update. One of the PCD_Register enums.
                                       uint8_t mask      ///< The bits to set.
) {
  uint8_t tmp = pcd_read_register(reg);
  pcd_write_register(reg, tmp | mask);  // set bit mask
}

/**
 * Clears the bits given in mask from register reg.
 */
void RC522::pcd_clear_register_bit_mask_(PcdRegister reg,  ///< The register to update. One of the PCD_Register enums.
                                         uint8_t mask      ///< The bits to clear.
) {
  uint8_t tmp = pcd_read_register(reg);
  pcd_write_register(reg, tmp & (~mask));  // clear bit mask
}

/**
 * Executes the Transceive command.
 * CRC validation can only be done if backData and backLen are specified.
 *
 * @return STATUS_OK on success, STATUS_??? otherwise.
 */
void RC522::pcd_transceive_data_(
    uint8_t *send_data,  ///< Pointer to the data to transfer to the FIFO.
    uint8_t send_len,    ///< Number of uint8_ts to transfer to the FIFO.
    uint8_t *back_data,  ///< nullptr or pointer to buffer if data should be read back after executing the command.
    uint8_t *back_len,   ///< In: Max number of uint8_ts to write to *backData. Out: The number of uint8_ts returned.
    uint8_t
        *valid_bits,   ///< In/Out: The number of valid bits in the last uint8_t. 0 for 8 valid bits. Default nullptr.
    uint8_t rx_align,  ///< In: Defines the bit position in backData[0] for the first bit received. Default 0.
    bool check_crc     ///< In: True => The last two uint8_ts of the response is assumed to be a CRC_A that must be
                       ///< validated.
) {
  pcd_communicate_with_picc_(PCD_TRANSCEIVE, WAIT_I_RQ, send_data, send_len, back_data, back_len, valid_bits, rx_align,
                             check_crc);
}

/**
 * Transfers data to the MFRC522 FIFO, executes a command, waits for completion and transfers data back from the FIFO.
 * CRC validation can only be done if backData and backLen are specified.
 *
 * @return STATUS_OK on success, STATUS_??? otherwise.
 */
void RC522::pcd_communicate_with_picc_(
    uint8_t command,      ///< The command to execute. One of the PCD_Command enums.
    uint8_t wait_i_rq,    ///< The bits in the ComIrqReg register that signals successful completion of the command.
    uint8_t *send_data,   ///< Pointer to the data to transfer to the FIFO.
    uint8_t send_len,     ///< Number of uint8_ts to transfer to the FIFO.
    uint8_t *back_data,   ///< nullptr or pointer to buffer if data should be read back after executing the command.
    uint8_t *back_len,    ///< In: Max number of uint8_ts to write to *backData. Out: The number of uint8_ts returned.
    uint8_t *valid_bits,  ///< In/Out: The number of valid bits in the last uint8_t. 0 for 8 valid bits.
    uint8_t rx_align,     ///< In: Defines the bit position in backData[0] for the first bit received. Default 0.
    bool check_crc        ///< In: True => The last two uint8_ts of the response is assumed to be a CRC_A that must be
                          ///< validated.
) {
  ESP_LOGV(TAG, "pcd_communicate_with_picc_(%d, %d,... %d) %d", command, wait_i_rq, check_crc, state_);

  // Prepare values for BitFramingReg
  uint8_t tx_last_bits = valid_bits ? *valid_bits : 0;
  uint8_t bit_framing =
      (rx_align << 4) + tx_last_bits;  // RxAlign = BitFramingReg[6..4]. TxLastBits = BitFramingReg[2..0]

  pcd_write_register(COMMAND_REG, PCD_IDLE);               // Stop any active command.
  pcd_write_register(COM_IRQ_REG, 0x7F);                   // Clear all seven interrupt request bits
  pcd_write_register(FIFO_LEVEL_REG, 0x80);                // FlushBuffer = 1, FIFO initialization
  pcd_write_register(FIFO_DATA_REG, send_len, send_data);  // Write sendData to the FIFO
  pcd_write_register(BIT_FRAMING_REG, bit_framing);        // Bit adjustments
  pcd_write_register(COMMAND_REG, command);                // Execute the command
  if (command == PCD_TRANSCEIVE) {
    pcd_set_register_bit_mask_(BIT_FRAMING_REG, 0x80);  // StartSend=1, transmission of data starts
  }

  rx_align_ = rx_align;
  valid_bits_ = valid_bits;
  check_crc_ = check_crc;

  awaiting_comm_ = true;
  awaiting_comm_time_ = millis();
}

bool RC522::await_communication_(RC522::StatusCode *return_code) {
  *return_code = STATUS_TIMEOUT;
  uint8_t n = pcd_read_register(
      COM_IRQ_REG);  // ComIrqReg[7..0] bits are: Set1 TxIRq RxIRq IdleIRq HiAlertIRq LoAlertIRq ErrIRq TimerIRq
  if (n & 0x01) {    // Timer interrupt - nothing received in 25ms
    return true;
  }
  if (!(n & WAIT_I_RQ)) {  // None of the interrupts that signal success has been set.
                           // Wait for the command to complete.

    if (millis() - awaiting_comm_time_ < 40)
      return false;

    ESP_LOGW(TAG, "Await comm timeout!");
    return true;
  }

  // Stop now if any errors except collisions were detected.
  uint8_t error_reg_value = pcd_read_register(
      ERROR_REG);  // ErrorReg[7..0] bits are: WrErr TempErr reserved BufferOvfl CollErr CRCErr ParityErr ProtocolErr
  if (error_reg_value & 0x13) {  // BufferOvfl ParityErr ProtocolErr
    *return_code = STATUS_ERROR;
    return true;
  }

  uint8_t valid_bits_local = 0;
  back_length_ = sizeof(back_data_);

  n = pcd_read_register(FIFO_LEVEL_REG);  // Number of uint8_ts in the FIFO
  if (n > back_length_) {
    *return_code = STATUS_NO_ROOM;
    return true;
  }
  back_length_ = n;                                            // Number of uint8_ts returned
  pcd_read_register(FIFO_DATA_REG, n, back_data_, rx_align_);  // Get received data from FIFO
  valid_bits_local =
      pcd_read_register(CONTROL_REG) & 0x07;  // RxLastBits[2:0] indicates the number of valid bits in the last
                                              // received uint8_t. If this value is 000b, the whole uint8_t is valid.
  if (valid_bits_) {
    *valid_bits_ = valid_bits_local;
  }

  // Tell about collisions
  if (error_reg_value & 0x08) {  // CollErr
    *return_code = STATUS_COLLISION;
    return true;
  }

  *return_code = STATUS_OK;
  return true;
}

bool RC522BinarySensor::process(const uint8_t *data, uint8_t len) {
  if (len != this->uid_.size())
    return false;

  for (uint8_t i = 0; i < len; i++) {
    if (data[i] != this->uid_[i])
      return false;
  }

  this->publish_state(true);
  this->found_ = true;
  return true;
}
void RC522Trigger::process(const uint8_t *uid, uint8_t uid_length) {
  char buf[32];
  format_uid(buf, uid, uid_length);
  this->trigger(std::string(buf));
}

}  // namespace rc522
}  // namespace esphome