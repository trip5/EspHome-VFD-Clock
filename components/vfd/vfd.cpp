#include "vfd.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include <string>
#include <algorithm>
#include <vector>

// Datasheet:
// - https://en.sekorm.com/doc/3669013.html

// Based on https://github.com/sfxfs/ESP32-VFD-8DM-Arduino/
// with elements from ESPHome's Max7219 Driver https://esphome.io/api/max7219_8cpp_source
// and also the Max7219Digit Driver: https://esphome.io/api/max7219digit_8cpp_source
// and a lot of my own additions, especially with replacement characters and custom characters
// Please note some of the sections of this driver are untested...

namespace esphome {
namespace vfd {

static const char *const TAG = "vfd";
static const char *VFD_TAG = "VFD";

static const uint8_t DCRAM_DATA_WRITE = 0x20;
static const uint8_t DGRAM_DATA_CLEAR = 0x10;
static const uint8_t CGRAM_DATA_WRITE = 0x40;
static const uint8_t SET_DISPLAY_TIMING = 0xE0;
static const uint8_t SET_DIMMING_DATA = 0xE4;
static const uint8_t SET_DISPLAY_LIGHT_ON = 0xE8;
static const uint8_t SET_DISPLAY_LIGHT_OFF = 0xEA;
static const uint8_t SET_STAND_BY_MODE = 0xEC;
static const uint8_t EMPTY_DATA = 0x00;

float VFDComponent::get_setup_priority() const { return setup_priority::PROCESSOR; }

/// @brief Sets up the SPI and Display
void VFDComponent::setup() // from void VFD_Display::init() const
{
  ESP_LOGCONFIG(TAG, "Setting up VFD...");
  this->spi_setup();
  this->en_pin_toggle(true);
  this->reset_pin_toggle();
  this->clearscreen();
  this->enable();
  VFD_cmd_t VFD_initcmd[] = {{SET_DISPLAY_TIMING, byte(this->digits_ - 1)},
                             {SET_DIMMING_DATA, this->intensity_},
                             {SET_DISPLAY_LIGHT_ON, EMPTY_DATA}};
  for (size_t i = 0; i < 3; i++)
  {
    setCmd(VFD_initcmd[i].cmd, VFD_initcmd[i].data);
  }
  parse_replace_string(this->replace_string_);
  parse_custom_string(this->custom_string_);
}

/// @brief Dump the configured settings to the log
void VFDComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "VFD:");
  ESP_LOGCONFIG(TAG, "  Intensity: %u", this->intensity_);
  //LOG_PIN("  CLK Pin: ", this->clk_pin_);
  //LOG_PIN("  SDI Pin: ", this->sdi_pin_);
  //LOG_PIN("  SDO Pin: ", this->sdo_pin_);
  LOG_PIN("  CS Pin: ", this->cs_);
  if (this->en_pin_ >= 0)
  {
    ESP_LOGCONFIG(TAG, "  EN Pin (optional): %u", this->en_pin_);
  }
  else
  {
    ESP_LOGCONFIG(TAG, "  EN Pin: unused");
  }
  if (this->reset_pin_ >= 0)
  {
    ESP_LOGCONFIG(TAG, "  RESET Pin (optional): %u", this->reset_pin_);
  }
  else
  {
    ESP_LOGCONFIG(TAG, "  RESET Pin: unused");
  }
  ESP_LOGCONFIG(TAG, "  Other options:");
  ESP_LOGCONFIG(TAG, "    Initial Delay: %u", this->initial_delay_);
  ESP_LOGCONFIG(TAG, "    Subsequent Delay: %u", this->subsequent_delay_);
  ESP_LOGCONFIG(TAG, "    Remove Spaces: %s", this->remove_spaces_ ? "true" : "false");
  ESP_LOGCONFIG(TAG, "    Replace: %s", this->replace_string_.c_str());
  // Takes a bit to turn the parsed pairs back to strings but very useful for diagnosis...
  std::string replace_from_all;
  std::string replace_to_all;
  for (size_t i = 0; i < this->replace_from_.size(); ++i)
  {
    replace_from_all += this->replace_from_[i];
    if (i < this->replace_from_.size() - 1)
    {
      replace_from_all += ",";
    }
  }
  for (size_t i = 0; i < this->replace_to_.size(); ++i)
  {
    replace_to_all += std::to_string(static_cast<uint8_t>(this->replace_to_[i]));
    if (i < this->replace_to_.size() - 1)
    {
      replace_to_all += ",";  // Add a comma between entries
    }
  }
  ESP_LOGCONFIG(TAG, "    Replace From: %s", replace_from_all.c_str());
  ESP_LOGCONFIG(TAG, "    Replace To: %s", replace_to_all.c_str());
  ESP_LOGCONFIG(TAG, "    Custom: %s", this->custom_string_.c_str());
  // Do it again for custom pairs...
  // ESP_LOGCONFIG(TAG, "    Custom From: %s", this->custom_from_.empty() ? "none" : "available");
  std::string custom_from_all;
  std::string custom_to_all;
  for (size_t i = 0; i < this->custom_from_.size(); ++i)
  {
    custom_from_all += this->custom_from_[i];
    if (i < this->custom_from_.size() - 1)
    {
      custom_from_all += ",";  // Add a comma between entries
    }
  }

  for (size_t i = 0; i < this->custom_to_.size(); ++i)
  {
    for (const auto& value : this->custom_to_[i])
    {
      custom_to_all += std::to_string(static_cast<uint8_t>(value));
      custom_to_all += ",";
    }
    if (!custom_to_all.empty())
    {
      custom_to_all.pop_back();
    }
    if (i < this->custom_to_.size() - 1)
    {
      custom_to_all += ";";
    }
  }
  ESP_LOGCONFIG(TAG, "    Custom From: %s", custom_from_all.c_str());
  ESP_LOGCONFIG(TAG, "    Custom To: %s", custom_to_all.c_str());
  LOG_UPDATE_INTERVAL(this);
}

/// @brief Function to toggle the en pin (if en_pin is set) to power the display
/// @param b = true to pull high and power the display, otherwise false
void VFDComponent::en_pin_toggle(bool b)
{
  if (this->en_pin_ >= 0)
  {
    pinMode(this->en_pin_, OUTPUT);
    if (b == true)
    {
      digitalWrite(this->en_pin_, HIGH);
    }
    else
    {
      digitalWrite(this->en_pin_, LOW);
    }
  }
}

/// @brief Function to hold the reset button and release it (if reset_pin is set)
void VFDComponent::reset_pin_toggle()
{
  if (this->reset_pin_ >= 0)
  {
    pinMode(this->reset_pin_, OUTPUT);
    digitalWrite(reset_pin_, HIGH);
    delay(1);
    digitalWrite(reset_pin_, LOW);
    delay(10);
    digitalWrite(reset_pin_, HIGH);
    delay(3);
  }
}

/// @brief Update the display
void VFDComponent::update()
{
    // Handle intensity changes
    if (this->intensity_changed_)
    {
        setCmd(SET_DIMMING_DATA, this->intensity_);
        this->intensity_changed_ = false;
    }
    // Handle writer operations if they exist
    if (this->writer_.has_value())
    {
        (*this->writer_)(*this);
    }
    // Refresh display
    this->display_status(true);
}

/// @brief Main loop: scrolls longer text that was sent to print
void VFDComponent::loop()
{
  // Check if we are in scrolling mode
  if (this->is_scrolling_)
  {
    // Check if it's time to update the display
    // due to the way this works, first text is actually displayed for time of initial delay + subsequent delay...
    if (millis() - this->last_update_time_ >= (this->scroll_index_ == 0 ? this->initial_delay_ : this->subsequent_delay_))
    {
      std::vector<uint16_t> display_text;
      int total_length = this->scroll_text_.size();  // use size(), not length()

      // Extract the current scroll text
      if (this->scroll_index_ + this->digits_ <= total_length)
      {
        display_text = std::vector<uint16_t>(
          this->scroll_text_.begin() + this->scroll_index_,
          this->scroll_text_.begin() + this->scroll_index_ + this->digits_);
      }
      else
      {
        // Wrap around case: take from scroll_index_ to end, then from start to needed amount
        display_text = std::vector<uint16_t>(
          this->scroll_text_.begin() + this->scroll_index_,
          this->scroll_text_.end());

        display_text.insert(
          display_text.end(),
          this->scroll_text_.begin(),
          this->scroll_text_.begin() + (this->scroll_index_ + this->digits_ - total_length));
      }
      show(0, display_text); // Display the extracted string
      this->last_update_time_ = millis(); // Update last update time
      // Move to the next position
      this->scroll_index_++;
      // Reset the index if we reach the end
      if (this->scroll_index_ >= total_length)
      {
        this->scroll_index_ = 0; // Restart scrolling from the beginning
      }
    }
  }
}


/// @brief Function to clear the entire display
void VFDComponent::clearscreen() // void VFD_Display::clear() const
{
  for (size_t i = 0; i < digits_; i++)
  {
    setCmd(DCRAM_DATA_WRITE | i, DGRAM_DATA_CLEAR);
  }
}

/// @brief Function to clear individual bits from the display
/// @param bit The position where the character will be cleared
void VFDComponent::clear(char bit)
{
  if (bit > -1 && bit < digits_)
  {
    setCmd(DCRAM_DATA_WRITE | bit, DGRAM_DATA_CLEAR);
  }
  else
  {
    ESP_LOGE(VFD_TAG, "Out of bits!");
  }    
}

/// @brief Function to print individual bits to the display
/// @param bit The position where the character will be printed
/// @param chr The character to display
void VFDComponent::show(char bit, uint16_t chr)
{
  if (chr >= 256 && chr <= 263)
  {
    chr -= 256;  // Custom CGRAM char
  }
  setCmd(DCRAM_DATA_WRITE | bit, static_cast<uint8_t>(chr));
}

/// @brief Function to display a string on the VFD
/// @param bit The position where the first character will be displayed
/// @param str The string to be displayed
void VFDComponent::show(char bit, const std::vector<uint16_t> &str)
{
  for (size_t i = 0; i < str.size(); i++)
  {
    show(bit, str[i]); // Overload show() to accept uint16_t
    if (bit < this->digits_ - 1)
      bit += 1;
    else
      break;
  }
}

/// @brief Sets the brightness of the VFD (will change with next update)
/// @param intensity The brightness level of the VFD (minimum is 0, maximum is 255)
void VFDComponent::intensity(byte intensity)
{
  if (intensity != this->intensity_) {
    this->intensity_changed_ = true;
    this->intensity_ = intensity;
  }
}

/// @brief Fade-in effect for the VFD
/// @param pertime Controls the interval for adjusting brightness (adjusts the speed of the fade-in)
void VFDComponent::fade_in(byte pertime)
{
  for (int i = 0; i < this->intensity_; i++)
  {
    setCmd(SET_DIMMING_DATA, i); // Character fade-in effect
    this->intensity_ = i;
    delay(pertime);
  }
}

/// @brief Fade-out effect for the VFD
/// @param pertime Controls the interval for adjusting brightness (adjusts the speed of the fade-out)
void VFDComponent::fade_out(byte pertime)
{
  for (int i = this->intensity_; i >= 0; i--)
  {
    setCmd(SET_DIMMING_DATA, i); // Character fade-out effect
    this->intensity_ = i;
    delay(pertime);
  }
}

/// @brief Set the on/off state of the VFD
/// @param status The on/off state of the VFD
void VFDComponent::display_status(bool status)
{
  if (status == true)
  {
    setCmd(SET_DISPLAY_LIGHT_ON, EMPTY_DATA);
  }
  else
  {
    setCmd(SET_DISPLAY_LIGHT_OFF, EMPTY_DATA);
  }
}

/// @brief Standby mode for the VFD
/// @param mode Enables or disables standby mode
void VFDComponent::standby_mode(bool mode)
{
    setCmd(SET_STAND_BY_MODE | mode, EMPTY_DATA);
}

/// @brief Display custom characters on the VFD
/// @param bit The position where the custom character will be displayed
/// @param flag The identifier for the stored custom character
void VFDComponent::show_cust_data(char bit, char cgram_flag)
{
  if (cgram_flag >= 0 && cgram_flag <= 7)
  {
    setCmd(DCRAM_DATA_WRITE | bit, cgram_flag);
  }
  else
  {
    ESP_LOGE(VFD_TAG, "Out of DCRAM storage space!");
  }
}

/// @brief Write custom character data to the VFD register
/// @param flag The identifier for the stored custom character (used when called)
/// @param data The custom character data to be written
void VFDComponent::write_cust_data(char cgram_flag, const byte *data) // The data consists of 5 bytes, and the CGRAM can store up to 8 custom characters.
{
  if (cgram_flag >= 0 && cgram_flag <= 7)
  {
    this->enable();
    this->write_byte(CGRAM_DATA_WRITE | cgram_flag); // Specify the storage location
    for (size_t i = 0; i < 5; i++)
    {
      this->write_byte(data[i]); // Write graphic data
    }
    this->disable();
  }
  else
  {
    ESP_LOGE(VFD_TAG, "DCRAM storage space limit reached!");
  }
}

/// @brief Send command or data to the VFD
/// @param cmd The command to be sent
/// @param data The data to be sent
void VFDComponent::setCmd(byte cmd, byte data)
{
  this->enable();
  this->write_byte(cmd);
  this->write_byte(data);
  this->disable();
}

/// @brief Set a custom writer function for the display
/// @param &&writer A lambda function or callable that takes a reference to the VFDComponent and writes data to the display
void VFDComponent::set_writer(vfd_writer_t &&writer) { this->writer_ = writer; }

/// @brief Print to the display starting at a certain position (Processed but will not center text or put longer text in the scroll buffer)
/// @param start_pos Starting position (0 to max digits -1)
/// @param str The string to print
/// @return How many characters successfully printed
uint8_t VFDComponent::print(uint8_t start_pos, const char* str)
{
  std::vector<uint16_t> processed = process_string(str);  // Process the C-string into vector<uint16_t>
  return this->print(start_pos, processed);   // Send to the new vector overload that handles truncation and printing
}

/// @brief Print to the display (Processed: will center short text, put longer text in the scroll buffer)
/// @param str The string to print
/// @return How many characters successfully printed
uint8_t VFDComponent::print(const char *str)
{
  return this->print(process_string(str));
}

/// @brief Print to the display starting at a certain position (Processed but will not center text or put longer text in the scroll buffer)
/// @param start_pos Starting position (0 to max digits -1)
/// @param processed A vector of character codes (UTF-8, replacements, custom characters) to print
/// @return How many characters successfully printed
uint8_t VFDComponent::print(uint8_t start_pos, const std::vector<uint16_t>& processed)
{
  size_t available = (start_pos < this->digits_) ? (this->digits_ - start_pos) : 0;   // Calculate available space from start_pos to end of display digits
  if (available == 0) // If no space, nothing to print
    return 0;
  std::vector<uint16_t> truncated(processed.begin(), processed.begin() + std::min(processed.size(), available)); // Truncate if longer than available space
  show(start_pos, truncated); // Show the truncated string starting at start_pos
  this->is_scrolling_ = false; // Reset scrolling, not scrolling when position is specified
  this->print_str_ = truncated; // Store what we printed so the component can track state if needed
  return truncated.size();
}

/// @brief Print to the display (Processed: will center short text, put longer text in the scroll buffer)
/// @param processed A vector of character codes (UTF-8, replacements, custom characters) to print
/// @return How many characters successfully printed
uint8_t VFDComponent::print(const std::vector<uint16_t> &processed)
{
  if (this->print_str_ != processed) // Check if the new string is different
  {
    this->is_scrolling_ = false; // Reset scrolling flag
    this->print_str_ = processed;
    if (processed.size() <= this->digits_) // If the string is shorter or equal to max digits, center it
    {
      size_t pad_front = (this->digits_ - processed.size()) / 2; // Calculate spaces added to back
      size_t pad_back = this->digits_ - processed.size() - pad_front; // Front gets the extra space if odd
      std::vector<uint16_t> padded;
      padded.insert(padded.end(), pad_front, ' '); // Insert spaces at the front
      padded.insert(padded.end(), processed.begin(), processed.end());
      padded.insert(padded.end(), pad_back, ' '); // Append spaces at the back
      show(0, padded); // Display centered string
    }
    else // Handle scrolling for strings longer than max digits
    {
      std::vector<uint16_t> scroll_text = processed;
      scroll_text.push_back(' '); // Add a space at the end for scrolling
      this->scroll_text_ = scroll_text; // Store the scroll text for scrolling
      this->scroll_index_ = 0; // Reset scroll index
      this->is_scrolling_ = true; // Set scrolling flag
      this->last_update_time_ = millis(); // Initialize last update time
      std::vector<uint16_t> slice(scroll_text.begin(), scroll_text.begin() + this->digits_);
      show(0, slice); // Display first part of text
    }
  }
  return 0;
}

/// @brief Print a formatted string to the display starting at a certain position (Processed but will not center text or put longer text in the scroll buffer)
/// @param pos Starting position (0 to max digits -1)
/// @param format The string to print, which includes standard format specifiers (%d, %s, %c, etc.)
/// @param ... Variadic arguments corresponding to the format string
/// @return The number of characters successfully printed to the display
uint8_t VFDComponent::printf(uint8_t pos, const char *format, ...)
{
  va_list arg;
  va_start(arg, format);
  char buffer[64];
  int ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);
  if (ret > 0)
  {
    std::vector<uint16_t> processed = process_string(buffer); // Process the string
    return this->print(pos, processed);  // Delegate to proper print()
  }
  return 0;
}

/// @brief Print a formatted string to the display (Processed: will center short text, put longer text in the scroll buffer)
/// @param format The string to print, which includes standard format specifiers (%d, %s, %c, etc.)
/// @param ... Variadic arguments corresponding to the format string
/// @return The number of characters successfully printed to the display
uint8_t VFDComponent::printf(const char *format, ...)
{
  va_list arg;
  va_start(arg, format);
  char buffer[64];
  int ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);
  if (ret > 0)
  {
    std::vector<uint16_t> processed = process_string(buffer);
    return this->print(processed);  // Delegate to print() for centering/scrolling logic
  }
  return 0;
}

/// @brief Print a formatted time to the display starting at a specific position (Processed but will not center text or put longer text in the scroll buffer)
/// @param pos Starting position (0 to max digits -1)
/// @param format The format string that follows the `strftime` format conventions (%Y for year, %m for month, etc.)
/// @param time The time value of type `ESPTime` to be formatted and printed
/// @return The number of characters successfully printed to the display.
uint8_t VFDComponent::strftime(uint8_t pos, const char *format, ESPTime time)
{
  char buffer[64];
  size_t ret = time.strftime(buffer, sizeof(buffer), format);
  if (ret > 0)
  {
    std::vector<uint16_t> processed = process_string(buffer); // Process the string
    return this->print(pos, processed); // Print the processed string
  }
  return 0;
}

/// @brief Print a formatted time to the display (Processed: will center short text, put longer text in the scroll buffer)
/// @param format The format string that follows the `strftime` format conventions (%Y for year, %m for month, etc.)
/// @param time The time value of type `ESPTime` to be formatted and printed
/// @return The number of characters successfully printed to the display.
uint8_t VFDComponent::strftime(const char *format, ESPTime time)
{
  char buffer[64];
  size_t ret = time.strftime(buffer, sizeof(buffer), format);
  if (ret > 0)
  {
    std::vector<uint16_t> processed = process_string(buffer); // Process the string
    return this->print(processed);  // Let the print logic decide to scroll or center
  }
  return 0;
}

/// @brief Sets the delay times for scroll speed
/// @param initial_delay, subsequent_delay in milliseconds
void VFDComponent::set_scroll_delays(uint16_t initial_delay, uint16_t subsequent_delay)
{
  this->initial_delay_ = initial_delay;
  this->subsequent_delay_ = subsequent_delay;
}

/// @brief Parses the string obtained from replace: "" in the YAML
/// @param replace_string A string that contains a character followed by a colon and then the Fubata equivalent byte
// ... ie. replace: "Ä:142;ü:0x81;п:0b11100010" (binary is MSB-LSB order in the specification)
void VFDComponent::parse_replace_string(const std::string &replace_string) {
  size_t start = 0;
  size_t end = 0;
  while ((end = replace_string.find(';', start)) != std::string::npos) {
    std::string pair = replace_string.substr(start, end - start);
    add_replacement_pair(pair);
    start = end + 1;
  }
  add_replacement_pair(replace_string.substr(start));  // Add last pair
}

/// @brief Adds the replacement pairs to 2 separate vector arrays
/// @param pair Accepts a character and a number in decimal, binary, or hex
void VFDComponent::add_replacement_pair(const std::string &pair) {
  size_t colon_index = pair.find(':');
  if (colon_index != std::string::npos) {
    std::string char_from = pair.substr(0, colon_index);
    std::string value_str = pair.substr(colon_index + 1);
    int16_t char_to = -1;
    // Check if value_str is a single printable ASCII character (33 to 126)
    if (value_str.length() == 1 && value_str[0] >= 33 && value_str[0] <= 126) {
      char_to = static_cast<uint16_t>(value_str[0]);
    } else {
      // Else, try to convert as binary, hex, or decimal
      char_to = convert_to_byte(value_str);
    }
    if (char_to != -1)  // Only add if conversion was successful
    {
      this->replace_from_.push_back(char_from);
      this->replace_to_.push_back(static_cast<uint16_t>(char_to));
    }
  }
}

/// @brief Parses the string obtained from custom: "" in the YAML
/// @param custom_string Accepts a character and 5 numbers in decimal, binary, or hex
/// ... painting with binary! ie. custom: ₿:20,0x0B,0b0101010,0x0B,62
void VFDComponent::parse_custom_string(const std::string &custom_string)
{
  size_t start = 0;
  size_t end = 0;
  while ((end = custom_string.find(';', start)) != std::string::npos)
  {
    std::string entry = custom_string.substr(start, end - start);
    add_custom_pair(entry);
    start = end + 1;
  }
  // Handle last entry if there’s no trailing semicolon
  if (start < custom_string.length())
  {
    add_custom_pair(custom_string.substr(start));
  }
}

/// @brief Adds the custom pairs to 2 separate vector arrays
/// @param entry Accepts a character and 5 numbers in decimal, binary, or hex
void VFDComponent::add_custom_pair(const std::string &entry) {
  size_t colon_index = entry.find(':');
  if (colon_index != std::string::npos)
  {
    std::string char_from = entry.substr(0, colon_index);
    if (char_from == "") { char_from = ":"; } // use colon if blank
    std::string bytes = entry.substr(colon_index + 1);
    // Store the values in a fixed-size array
    uint8_t custom_values[5] = {0}; // To hold up to 5 bytes
    size_t index = 0; // To track how many valid bytes we've added
    // Split bytes by comma
    size_t byte_start = 0;
    size_t byte_end = 0;
    while ((byte_end = bytes.find(',', byte_start)) != std::string::npos && index < 5) {
      std::string byte_str = bytes.substr(byte_start, byte_end - byte_start);
      uint8_t value = convert_to_byte(byte_str);
      // Push to the array only if the conversion was successful
      if (value < 256) { // Ensure it’s a valid byte
        custom_values[index] = value; // Store the valid byte
        ++index;
      }
      byte_start = byte_end + 1;
    }
    // Handle the last byte entry if it exists
    if (byte_start < bytes.length() && index < 5) {
      std::string byte_str = bytes.substr(byte_start);
      uint8_t value = convert_to_byte(byte_str);
      if (value < 256) { // Ensure it’s a valid byte
        custom_values[index] = value; // Store the valid byte
        ++index;
      }
    }
    // Add the character to the custom_from_ array
    this->custom_from_.push_back(char_from);
    // Add the corresponding bytes to the custom_to_ array
    custom_to_.push_back(std::vector<byte>(custom_values, custom_values + index)); // Store the valid bytes
  }
}

/// @brief Converts a string representing a number in binary, hex, or decimal format to a byte (uint16_t)
/// @param value_str The string representation of the number
/// @return The converted byte, or -1 if the conversion is invalid
int16_t VFDComponent::convert_to_byte(const std::string &value_str)
{
  uint16_t char_to = 0;
  if (value_str.substr(0, 2) == "0b") // Binary
  {
    std::string binary_value = value_str.substr(2);
    for (char bit : binary_value)
    {
      if (bit == '0' || bit == '1')
      {
        char_to = (char_to << 1) | (bit - '0');
      }
      else
      {
        return -1; // Invalid binary
      }
    }
  }
  else if (value_str.substr(0, 2) == "0x") // Hexadecimal
  {
    std::string hex_value = value_str.substr(2);
    for (char hex_char : hex_value)
    {
      if (std::isdigit(hex_char))
      {
        char_to = (char_to << 4) | (hex_char - '0');
      }
      else if (hex_char >= 'A' && hex_char <= 'F')
      {
        char_to = (char_to << 4) | (hex_char - 'A' + 10);
      }
      else if (hex_char >= 'a' && hex_char <= 'f')
      {
        char_to = (char_to << 4) | (hex_char - 'a' + 10);
      }
      else
      {
        return -1; // Invalid hex
      }
    }
  }
  else // Decimal
  {
    bool valid = true;
    for (char digit : value_str)
    {
      if (!std::isdigit(digit))
      {
        valid = false; // Invalid decimal
        break;
      }
    }
    if (valid)
    {
      char_to = static_cast<uint16_t>(std::stoi(value_str));
    }
    else
    {
      return -1; // Invalid decimal
    }
  }
  // Return the char_to if it's within valid byte range; otherwise, return -1
  return (char_to <= 255) ? static_cast<int16_t>(char_to) : -1;
}

/// @brief This processes a string (heavily) for the print functions, according to settings in the YAML
/// ... spaces can be removed, replacements can be made, custom can be characters used
/// @param str a const char string to be processed
/// @return A processed std::vector<uint16_t> that may include removed spaces, applied replacements, custom character substitutions
std::vector<uint16_t> VFDComponent::process_string(const char *str)
{
  std::string temp(str); // Convert to std::string for easier manipulation
  std::vector<uint16_t> result; // To hold the processed result
  char cgram_flag = 0; // Initialize cgram_flag to 0
  std::map<std::string, uint8_t> assigned_custom_chars; // so we can re-use characters already assigned
  if (this->remove_spaces_) // Remove spaces if needed
  {
    temp.erase(temp.begin(), std::find_if(temp.begin(), temp.end(), [](unsigned char ch) {
      return !std::isspace(ch);
    }));
    temp.erase(std::find_if(temp.rbegin(), temp.rend(), [](unsigned char ch) {
      return !std::isspace(ch);
    }).base(), temp.end());
  }

  for (size_t i = 0; i < temp.length();) // Process each character for replacements (handle UTF-8 multi-byte characters)
  {
    char32_t codepoint = 0;
    unsigned char byte = temp[i];
    size_t bytes_consumed = 1; // Track how many bytes a character consumes
    // Determine if the current character is multi-byte and decode accordingly
    if ((byte & 0x80) == 0) // 1-byte character (ASCII)
    {
      codepoint = byte;
    } 
    else if ((byte & 0xE0) == 0xC0) // 2-byte character
    {
      if (i + 1 < temp.length()) // Check bounds
      {
        codepoint = (byte & 0x1F) << 6;
        codepoint |= (temp[i + 1] & 0x3F);
        bytes_consumed = 2;
      }
      else
      {
        break; // Handle incomplete UTF-8 sequence
      }
    } 
    else if ((byte & 0xF0) == 0xE0) // 3-byte character
    {
      if (i + 2 < temp.length()) // Check bounds
      {
        codepoint = (byte & 0x0F) << 12;
        codepoint |= (temp[i + 1] & 0x3F) << 6;
        codepoint |= (temp[i + 2] & 0x3F);
        bytes_consumed = 3;
      }
      else
      {
        break; // Handle incomplete UTF-8 sequence
      }
    } 
    else if ((byte & 0xF8) == 0xF0) // 4-byte character
    {
      if (i + 3 < temp.length()) // Check bounds
      {
        codepoint = (byte & 0x07) << 18;
        codepoint |= (temp[i + 1] & 0x3F) << 12;
        codepoint |= (temp[i + 2] & 0x3F) << 6;
        codepoint |= (temp[i + 3] & 0x3F);
        bytes_consumed = 4;
      }
      else
      {
        break; // Handle incomplete UTF-8 sequence
      }
    } 
    else
    {
      ++i; // Invalid UTF-8 sequence, skip, move to next byte
      continue;
    }
    // Convert codepoint to UTF-8 string for comparison
    std::string utf8_char;
    if (codepoint < 0x80)
    {
      utf8_char = static_cast<char>(codepoint);
    }
    else if (codepoint < 0x800)
    {
      utf8_char = static_cast<char>((codepoint >> 6) | 0xC0);
      utf8_char += static_cast<char>((codepoint & 0x3F) | 0x80);
    }
    else if (codepoint < 0x10000)
    {
      utf8_char = static_cast<char>((codepoint >> 12) | 0xE0);
      utf8_char += static_cast<char>(((codepoint >> 6) & 0x3F) | 0x80);
      utf8_char += static_cast<char>((codepoint & 0x3F) | 0x80);
    }
    else
    {
      utf8_char = static_cast<char>((codepoint >> 18) | 0xF0);
      utf8_char += static_cast<char>(((codepoint >> 12) & 0x3F) | 0x80);
      utf8_char += static_cast<char>(((codepoint >> 6) & 0x3F) | 0x80);
      utf8_char += static_cast<char>((codepoint & 0x3F) | 0x80);
    }

    // Check for replacements
    bool replaced = false;
    for (size_t j = 0; j < this->replace_from_.size(); ++j)
    {
      if (this->replace_from_[j] == utf8_char)
      {
        result.push_back(static_cast<uint16_t>(this->replace_to_[j])); // Add the replacement byte to the result
        replaced = true;
        break; // Stop after the first replacement
      }
    }

    // Check for custom character to add to DRAM
    if (!replaced && cgram_flag <= 7)
    {
      for (size_t j = 0; j < this->custom_from_.size(); ++j)
      {
        if (this->custom_from_[j] == utf8_char)
        {
          // Check if already assigned
          auto it = assigned_custom_chars.find(utf8_char);
          if (it != assigned_custom_chars.end())
          {
            // Reuse previously assigned CGRAM slot + 256 (will be handled by show)
            result.push_back(static_cast<uint16_t>(it->second + 256));
            replaced = true;
            break;
          }
          // ...or assign a new CGRAM slot
          if (j < this->custom_to_.size()) // Ensure index is valid
          {
            const std::vector<uint8_t>& custom_values = this->custom_to_[j];
            write_cust_data(cgram_flag, custom_values.data()); // Send the custom values
            assigned_custom_chars[utf8_char] = cgram_flag; // Record assignment
            result.push_back(static_cast<uint16_t>(cgram_flag + 256)); // Replace with the cgram_flag (0 to 7) + 256 (will be handled by show)
            ESP_LOGD(VFD_TAG, "DCRAM storage slot %d assigned for '%s'", cgram_flag, utf8_char.c_str());
            cgram_flag++;
            replaced = true;
            break; // Stop after the first custom replacement
          }
        }
      }
    }
    // If still not replaced, handle characters (unreplaced characters above 255 bytes are dropped)
    if (!replaced)
    {
      if (codepoint < 256)
      {
        result.push_back(static_cast<uint16_t>(codepoint));
      }
    }

    i += bytes_consumed; // Move to the next character
  }
  return result; // Return the processed string
}

}  // namespace vfd
}  // namespace esphome
