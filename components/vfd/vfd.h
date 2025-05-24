#pragma once

#include "esphome/core/component.h"
#include "esphome/core/time.h"
#include "esphome/components/spi/spi.h"
#include <vector>

namespace esphome {
namespace vfd {

class VFDComponent;

using vfd_writer_t = std::function<void(VFDComponent &)>;

class VFDComponent : public PollingComponent,
                     public spi::SPIDevice<spi::BIT_ORDER_LSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                           spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_200KHZ> {

 public:
  float get_setup_priority() const override;
  void set_writer(vfd_writer_t &&writer);

  struct VFD_cmd_t
    {
      byte cmd;
      byte data;
    };

  void setup() override;
  void dump_config() override;

  void en_pin_toggle(bool b);
  void reset_pin_toggle();

  void update() override;
  void loop() override;

  void clearscreen();
  void clear(char bit);

  void show(char bit, uint16_t chr);
  void show(char bit, const std::vector<uint16_t> &str);

  void intensity(byte intensity);
  void fade_in(byte pertime);
  void fade_out(byte pertime);

  void display_status(bool status);
  void standby_mode(bool mode);
  void standby(bool mode) { standby_mode(mode); } // Alias

  void show_cust_data(char bit, char flag);
  void write_cust_data(char flag, const byte *data);
  void setCmd(byte cmd, byte data);

  uint8_t print(uint8_t pos, const char *str);
  uint8_t print(const char *str);
  uint8_t print(uint8_t pos, const std::vector<uint16_t> &processed);
  uint8_t print(const std::vector<uint16_t> &processed);

  uint8_t printf(uint8_t pos, const char *format, ...) __attribute__((format(printf, 3, 4)));
  uint8_t printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

  uint8_t strftime(uint8_t pos, const char *format, ESPTime time) __attribute__((format(strftime, 3, 0)));
  uint8_t strftime(const char *format, ESPTime time) __attribute__((format(strftime, 2, 0)));

  /// @brief Set how many digits the display has (6 or 8 or 16)
  /// @param digits 6 or 8 or 16
  void set_digits(uint8_t digits) { this->digits_ = digits; }

  /// @brief Set the pin attached to the Reset pin of the display
  /// @param reset_pin Pin number (not GPIO)
  void set_reset_pin(int8_t reset_pin) { this->reset_pin_ = reset_pin; }

  /// @brief Set the EN Pin which supplies power to the display
  /// @param en_pin Pin number (not GPIO)
  void set_en_pin(int8_t en_pin) { this->en_pin_ = en_pin; }

  void set_scroll_delays(uint16_t initial_delay, uint16_t subsequent_delay);

  /// @brief Set to true to remove leading or trailing spaces from the printed string
  /// @param remove_spaces false (default) or true
  void set_remove_spaces(bool remove_spaces) { this->remove_spaces_ = remove_spaces; }

  /// @brief Set the string obtained from replace: "" in the YAML
  /// @param replace_string The string to be parsed
  void set_replace_string(const std::string &replace_string) { this->replace_string_ = replace_string; }
  void parse_replace_string(const std::string &replace_string);
  void add_replacement_pair(const std::string &pair);

  /// @brief Set the string obtained from custom: "" in the YAML
  /// @param custom_string The string to be parsed
  void set_custom_string(const std::string &custom_string) { this->custom_string_ = custom_string; }
  void parse_custom_string(const std::string &custom_string);
  void add_custom_pair(const std::string &entry);

  int16_t convert_to_byte(const std::string &value_str);
  std::vector<uint16_t> process_string(const char *str);

protected:
  std::vector<uint16_t> scroll_text_;  // stores scroll text
  uint8_t scroll_index_{0}; // scroll index
  bool is_scrolling_{false}; // scrolling flag
  unsigned long last_update_time_{0};
  unsigned long initial_delay_{2000}; // Default initial delay of 2 seconds for the first update
  unsigned long subsequent_delay_{500}; // Default subsequent delay of 0.5 seconds for scrolling

  uint8_t digits_{8};
  uint8_t intensity_{200}; // Intensity of the display from 0 to 255
  bool intensity_changed_{}; // True if we need to re-send the intensity

  int8_t reset_pin_{-1};
  int8_t en_pin_{-1};

  std::vector<uint16_t> print_str_;
  bool remove_spaces_{false}; // remove leading and trailing spaces

  std::string replace_string_{};
  std::vector<std::string> replace_from_{};
  std::vector<byte> replace_to_{};

  std::string custom_string_{};
  std::vector<std::string> custom_from_{};
  std::vector<std::vector<uint8_t>> custom_to_{};
 
  optional<vfd_writer_t> writer_{};
};

}  // namespace vfd
}  // namespace esphome
