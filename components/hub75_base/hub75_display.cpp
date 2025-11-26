#include "esphome.h"
using namespace esphome;
#include "hub75_display.h"
#include <Fonts/TomThumb.h>
#include <string>
#include <cstdio>

namespace esphome
{
  namespace hub75_base
  {

    static const char *const TAG = "hub75_base";

    int HUB75Display::calculate_optimal_latch_blanking() {
      // If user explicitly set latch_blanking, use it
      if (this->latch_blanking_ >= 0) {
        ESP_LOGCONFIG(TAG, "  Using user-defined latch blanking: %d", this->latch_blanking_);
        return this->latch_blanking_;
      }
      
      // Automatic calculation based on chipset and display characteristics
      // Start more conservative - too much blanking can cause issues too
      int blanking = 1; // Default minimum
      int base_blanking = 1;
      int size_adjustment = 0;
      int speed_adjustment = 0;
      
      // Chipset-specific defaults (more conservative values)
      if (this->user_defined_driver_) {
        switch (this->driver_) {
          case HUB75_I2S_CFG::shift_driver::ICN2038S:
            // ICN2037BP maps to ICN2038S, so this covers both
            // More conservative: ICN203x can work with less blanking
            base_blanking = 2; // Reduced from 4
            break;
          case HUB75_I2S_CFG::shift_driver::FM6124:
            base_blanking = 1;
            break;
          case HUB75_I2S_CFG::shift_driver::MBI5124:
            base_blanking = 2;
            break;
          default:
            base_blanking = 1;
            break;
        }
      }
      blanking = base_blanking;
      
      // Adjust for display size (more conservative adjustments)
      int total_pixels = this->width_ * this->height_ * this->chain_length_;
      if (total_pixels > 4096) { // Large displays (128x64, etc.)
        size_adjustment = 1; // Reduced from 2
      } else if (total_pixels > 2048) { // Medium displays
        size_adjustment = 0; // Reduced from 1
      }
      blanking += size_adjustment;
      
      // Adjust for I2S speed (more conservative)
      if (this->user_defined_i2sspeed_) {
        switch (this->i2sspeed_) {
          case HUB75_I2S_CFG::clk_speed::HZ_20M:
            speed_adjustment = 1; // Reduced from 2
            break;
          case HUB75_I2S_CFG::clk_speed::HZ_16M:  // HZ_15M and HZ_16M are the same enum value
            speed_adjustment = 0; // Reduced from 1
            break;
          default:
            speed_adjustment = 0;
            break;
        }
      }
      blanking += speed_adjustment;
      
      // Cap at reasonable maximum (too high can cause other issues)
      if (blanking > 8)  // Reduced cap from 16 to 8
        blanking = 8;
      
      // Log the calculation breakdown for debugging
      ESP_LOGCONFIG(TAG, "  Auto-calculated latch blanking: %d (base: %d, size: +%d, speed: +%d)", 
                    blanking, base_blanking, size_adjustment, speed_adjustment);
      
      return blanking;
    }

    void HUB75Display::setup() {
      ESP_LOGCONFIG(TAG, "Setting up HUB75Display...");

      // Module configuration
      HUB75_I2S_CFG mxconfig(
          this->width_,
          this->height_,
          this->chain_length_,
          this->pins_);

      if (this->user_defined_driver_)
        mxconfig.driver = this->driver_;

      if (this->user_defined_line_driver_)
        mxconfig.line_decoder = this->line_driver_;

      if (this->user_defined_i2sspeed_)
        mxconfig.i2sspeed = this->i2sspeed_;

      // Use automatic blanking calculation if not explicitly set
      // (logging is done inside calculate_optimal_latch_blanking)
      mxconfig.latch_blanking = this->calculate_optimal_latch_blanking();

      if (this->user_defined_clock_phase_)
        mxconfig.clkphase = this->clock_phase_;

      // The min refresh rate correlates with the update frequency of the component
      // Cap at reasonable maximum (most displays can't handle > 200-300 Hz effectively)
      // Higher rates can cause buffer synchronization issues
      uint16_t calculated_rate = 1000 / this->update_interval_;
      mxconfig.min_refresh_rate = (calculated_rate > 300) ? 300 : calculated_rate;
      
      if (calculated_rate > 300) {
        ESP_LOGW(TAG, "Update interval %dms results in %d Hz - capping at 300 Hz for stability", 
                 this->update_interval_, calculated_rate);
      }

      // Configure double buffering - when enabled, library maintains two buffers
      // for smooth, flicker-free updates
      mxconfig.double_buff = this->double_buffer_enabled_;

      // Display Setup
      this->dma_display_ = new MatrixPanel_I2S_DMA(mxconfig);
      
      if (!this->dma_display_) {
        ESP_LOGE(TAG, "Failed to allocate MatrixPanel_I2S_DMA object!");
        return;
      }
      
      if (!this->dma_display_->begin()) {
        ESP_LOGE(TAG, "Failed to initialize HUB75 display!");
        return;
      }
      
      ESP_LOGCONFIG(TAG, "HUB75 display initialized successfully");
      ESP_LOGCONFIG(TAG, "  Double buffering: %s", this->double_buffer_enabled_ ? "enabled" : "disabled");
      
      // Set brightness with fade
      uint8_t brightness = this->brightness_;
      this->set_brightness(0, false);
      this->set_brightness(brightness, true);
      
      // Clear both buffers if double buffering is enabled
      // Use clearScreen() which is optimized by the library
      if (mxconfig.double_buff) {
        // Clear the current back buffer (drawing target)
        this->dma_display_->clearScreen();
        // Flip to clear the other buffer
        this->dma_display_->flipDMABuffer();
        this->dma_display_->clearScreen();
        // Flip back to have a clean back buffer ready for drawing
        this->dma_display_->flipDMABuffer();
      } else {
        // Single buffer mode - just clear once
        this->dma_display_->clearScreen();
      }
      
      ESP_LOGCONFIG(TAG, "Display buffers initialized and cleared");
    }

    void HUB75Display::on_set_brightness(int brightness) {
      this->set_brightness(brightness, true);
    }

    void HUB75Display::update() {
      unsigned long timeMillis = millis();
      this->update_brightness_(timeMillis);
    }

    void HUB75Display::dump_config() {
      ESP_LOGCONFIG(TAG, "HUB75Display:");
      ESP_LOGCONFIG(TAG, "  Display Width:    %u", this->width_);
      ESP_LOGCONFIG(TAG, "  Display Height:   %u", this->height_);
      ESP_LOGCONFIG(TAG, "  Brightness:       %u", this->brightness_);
      ESP_LOGCONFIG(TAG, "  Brightness (min): %d", this->min_brightness_);
      ESP_LOGCONFIG(TAG, "  Brightness (max): %d", this->max_brightness_);

      // Log pin settings
      ESP_LOGCONFIG(TAG, "  Pins: R1:%i, G1:%i, B1:%i, R2:%i, G2:%i, B2:%i", pins_.r1, pins_.g1, pins_.b1, pins_.r2, pins_.g2, pins_.b2);
      ESP_LOGCONFIG(TAG, "  Pins: A:%i, B:%i, C:%i, D:%i, E:%i", pins_.a, pins_.b, pins_.c, pins_.d, pins_.e);
      ESP_LOGCONFIG(TAG, "  Pins: LAT:%i, OE:%i, CLK:%i", pins_.lat, pins_.oe, pins_.b1, pins_.clk);

      LOG_UPDATE_INTERVAL(this);

      // Log driver settings
      switch (dma_display_->getCfg().driver) {
      case HUB75_I2S_CFG::shift_driver::SHIFTREG:
        ESP_LOGCONFIG(TAG, "  Driver: SHIFTREG");
        break;
      case HUB75_I2S_CFG::shift_driver::FM6124:
        ESP_LOGCONFIG(TAG, "  Driver: FM6124/FM6047 (compatible chipsets)");
        break;
      case HUB75_I2S_CFG::shift_driver::FM6126A:
        ESP_LOGCONFIG(TAG, "  Driver: FM6126A");
        break;
      case HUB75_I2S_CFG::shift_driver::ICN2038S:
        ESP_LOGCONFIG(TAG, "  Driver: ICN2038S/ICN2037BP (compatible chipsets)");
        break;
      case HUB75_I2S_CFG::shift_driver::MBI5124:
        ESP_LOGCONFIG(TAG, "  Driver: MBI5124");
        break;
      case HUB75_I2S_CFG::shift_driver::DP3246:
        ESP_LOGCONFIG(TAG, "  Driver: DP3246");
        break;
      default:
        ESP_LOGCONFIG(TAG, "  Driver: Unknown");
        break;
      }

      switch (dma_display_->getCfg().i2sspeed)
      {
      case HUB75_I2S_CFG::clk_speed::HZ_8M:
        ESP_LOGCONFIG(TAG, "  I2SSpeed: HZ_8M (is the same like HZ_10M)");
        break;
      case HUB75_I2S_CFG::clk_speed::HZ_16M:
        ESP_LOGCONFIG(TAG, "  I2SSpeed: HZ_16M (is the same like HZ_15M)");
        break;
      case HUB75_I2S_CFG::clk_speed::HZ_20M:
        ESP_LOGCONFIG(TAG, "  I2SSpeed: HZ_20M");
        break;
      }

      ESP_LOGCONFIG(TAG, "  Latch blanking: %i", dma_display_->getCfg().latch_blanking);

      ESP_LOGCONFIG(TAG, "  Clock Phase: %s", dma_display_->getCfg().clkphase ? "true" : "false");

      ESP_LOGCONFIG(TAG, "  Min refresh rate: %i", dma_display_->getCfg().min_refresh_rate);
    }

    std::string HUB75Display::get_chipset_name() {
      if (!this->dma_display_) return "Unknown";
      switch (this->dma_display_->getCfg().driver) {
        case HUB75_I2S_CFG::shift_driver::SHIFTREG: return "SHIFTREG";
        case HUB75_I2S_CFG::shift_driver::FM6124: return "FM6124/FM6047";  // FM6047 uses FM6124 initialization
        case HUB75_I2S_CFG::shift_driver::FM6126A: return "FM6126A";
        case HUB75_I2S_CFG::shift_driver::ICN2038S: return "ICN2038S/ICN2037BP";  // ICN2037BP uses ICN2038S initialization
        case HUB75_I2S_CFG::shift_driver::MBI5124: return "MBI5124";
        case HUB75_I2S_CFG::shift_driver::DP3246: return "DP3246";
        default: return "Unknown";
      }
    }

    std::string HUB75Display::get_i2sspeed_name() {
      if (!this->dma_display_) return "Unknown";
      switch (this->dma_display_->getCfg().i2sspeed) {
        case HUB75_I2S_CFG::clk_speed::HZ_8M: return "HZ_8M/HZ_10M";  // HZ_8M and HZ_10M are the same enum value
        case HUB75_I2S_CFG::clk_speed::HZ_16M: return "HZ_15M/HZ_16M";  // HZ_15M and HZ_16M are the same enum value
        case HUB75_I2S_CFG::clk_speed::HZ_20M: return "HZ_20M";
        default: return "Unknown";
      }
    }

    std::string HUB75Display::get_line_driver_name() {
      if (!this->dma_display_) return "Unknown";
      switch (this->dma_display_->getCfg().line_decoder) {
        case HUB75_I2S_CFG::line_driver::TYPE138: return "TYPE138";
        case HUB75_I2S_CFG::line_driver::TYPE595: return "TYPE595/SM5368";  // TYPE595 and SM5368 are the same enum value
        case HUB75_I2S_CFG::line_driver::TYPE_DIRECT: return "TYPE_DIRECT";
        case HUB75_I2S_CFG::line_driver::SM5266P: return "SM5266P";
        default: return "Unknown";
      }
    }

    std::string HUB75Display::get_config_summary() {
      if (!this->dma_display_) return "Display not initialized";
      char buffer[256];
      snprintf(buffer, sizeof(buffer), 
        "Chipset:%s I2SSpeed:%s LineDriver:%s LatchBlanking:%d ClockPhase:%s Width:%dx%d",
        this->get_chipset_name().c_str(),
        this->get_i2sspeed_name().c_str(),
        this->get_line_driver_name().c_str(),
        this->dma_display_->getCfg().latch_blanking,
        this->dma_display_->getCfg().clkphase ? "true" : "false",
        this->width_,
        this->height_);
      return std::string(buffer);
    }

    void HUB75Display::run_test_pattern() {
      if (!this->dma_display_) return;
      ESP_LOGI(TAG, "Running diagnostic test pattern...");
      
      // Clear screen
      this->dma_display_->clearScreen();
      delay(100);
      
      // Test 1: Fill with red (top half)
      this->dma_display_->fillRect(0, 0, this->width_, this->height_ / 2, 255, 0, 0);
      delay(500);
      
      // Test 2: Fill with green (bottom half)
      this->dma_display_->fillRect(0, this->height_ / 2, this->width_, this->height_ / 2, 0, 255, 0);
      delay(500);
      
      // Test 3: Fill with blue (full screen)
      this->dma_display_->fillScreenRGB888(0, 0, 255);
      delay(500);
      
      // Test 4: Draw corner markers
      this->dma_display_->fillRect(0, 0, 4, 4, 255, 255, 255); // Top-left
      this->dma_display_->fillRect(this->width_ - 4, 0, 4, 4, 255, 255, 255); // Top-right
      this->dma_display_->fillRect(0, this->height_ - 4, 4, 4, 255, 255, 255); // Bottom-left
      this->dma_display_->fillRect(this->width_ - 4, this->height_ - 4, 4, 4, 255, 255, 255); // Bottom-right
      delay(1000);
      
      // Test 5: Draw grid pattern
      this->dma_display_->clearScreen();
      for (int x = 0; x < this->width_; x += 8) {
        this->dma_display_->drawFastVLine(x, 0, this->height_, 255, 255, 255);
      }
      for (int y = 0; y < this->height_; y += 8) {
        this->dma_display_->drawFastHLine(0, y, this->width_, 255, 255, 255);
      }
      delay(2000);
      
      // Test 6: Draw pixel-by-pixel test (check pixel mapping)
      this->dma_display_->clearScreen();
      for (int y = 0; y < this->height_; y++) {
        for (int x = 0; x < this->width_; x++) {
          uint8_t r = (x * 255) / this->width_;
          uint8_t g = (y * 255) / this->height_;
          uint8_t b = 128;
          this->dma_display_->drawPixelRGB888(x, y, r, g, b);
        }
      }
      delay(2000);
      
      ESP_LOGI(TAG, "Test pattern complete");
    }

    void HUB75Display::dump_config_to_log() {
      if (!this->dma_display_) {
        ESP_LOGW(TAG, "Display not initialized - cannot dump config");
        return;
      }
      ESP_LOGI(TAG, "=== HUB75 Display Configuration ===");
      ESP_LOGI(TAG, "Chipset: %s", this->get_chipset_name().c_str());
      ESP_LOGI(TAG, "I2S Speed: %s", this->get_i2sspeed_name().c_str());
      ESP_LOGI(TAG, "Line Driver: %s", this->get_line_driver_name().c_str());
      ESP_LOGI(TAG, "Latch Blanking: %d", this->dma_display_->getCfg().latch_blanking);
      ESP_LOGI(TAG, "Clock Phase: %s", this->dma_display_->getCfg().clkphase ? "true" : "false");
      ESP_LOGI(TAG, "Width: %d, Height: %d", this->width_, this->height_);
      ESP_LOGI(TAG, "Chain Length: %d", this->chain_length_);
      ESP_LOGI(TAG, "Brightness: %d (min: %d, max: %d)", this->brightness_, this->min_brightness_, this->max_brightness_);
      ESP_LOGI(TAG, "Double Buffer: %s", this->double_buffer_enabled_ ? "enabled" : "disabled");
      ESP_LOGI(TAG, "===================================");
    }

    void HUB75Display::set_brightness(uint8_t brightness) {
      this->set_brightness(brightness, false);
    }

    //uint8_t HUB75Display::get_brightness() {
    //  return this->brightness_;
    //}

    void HUB75Display::set_brightness(uint8_t brightness, bool with_fade) {
      if (brightness <= this->min_brightness_) {
        brightness = this->min_brightness_;
      }

      if (brightness >= this->max_brightness_) {
        brightness = this->max_brightness_;
      }

      this->brightness_destination_ = brightness;
      if (!with_fade) {
        this->brightness_ = brightness;

        if (this->dma_display_)
          this->dma_display_->setBrightness8(this->brightness_);      
      } 
    }

    void HOT HUB75Display::draw_pixel_at(int x, int y, Color color) {
      // Safety check - ensure display is initialized
      if (!this->dma_display_) {
        return;
      }
      
      // Optimized bounds checking - use unsigned comparison for better performance
      if ((unsigned int)x >= (unsigned int)this->get_width_internal() || 
          (unsigned int)y >= (unsigned int)this->get_height_internal())
        return;

      // Direct pixel drawing using library's native RGB888 method
      this->dma_display_->drawPixelRGB888(x, y, color.r, color.g, color.b);
    }

    void HUB75Display::fill(Color color) {
      // Safety check - ensure display is initialized
      if (!this->dma_display_) {
        return;
      }
      
      // Optimize: use clearScreen() for black fills (faster)
      if (color.r == 0 && color.g == 0 && color.b == 0) {
        this->dma_display_->clearScreen();
      } else {
        // Use library's native fillScreenRGB888 method for other colors
        this->dma_display_->fillScreenRGB888(color.r, color.g, color.b);
      }
    }

    void HUB75Display::filled_rectangle(int x1, int y1, int width, int height, Color color) {
      // Safety check - ensure display is initialized
      if (!this->dma_display_) {
        return;
      }
      
      // Use library's native fillRect method for optimal performance
      this->dma_display_->fillRect(x1, y1, width, height, color.r, color.g, color.b);
    }

    void HUB75Display::update_() { 
      // Optimized update - only redraw when necessary
      static ESPTime last_time = ESPTime();
      static bool last_valid = false;
      
      ESPTime now = this->time_->now();
      bool is_valid = now.is_valid();
      
      // Only update if time changed or validity changed
      if (now != last_time || is_valid != last_valid) {
        this->dma_display_->fillRect(0, 8, 64, 7, display::ColorUtil::color_to_565(backgroundColor));
        this->dma_display_->setCursor(0, 8);
        this->dma_display_->setTextColor(display::ColorUtil::color_to_565(COLOR_RED));

        if (is_valid) {
          this->dma_display_->printf("%02d:%02d:%02d", now.hour, now.minute, now.second);
        } else {
          this->dma_display_->print("--:--:-- ?");
        }
        
        last_time = now;
        last_valid = is_valid;
      }
    }

    void HUB75Display::start_screen_() {
      this->dma_display_->setFont(&TomThumb);
      this->dma_display_->setCursor(0, 0+5);
      this->dma_display_->setTextColor(display::ColorUtil::color_to_565(hub75_base::COLOR_GREEN_LIGHT));
      this->dma_display_->printf("-> %s Display", this->display_name_.c_str());

      this->dma_display_->setCursor(0, 16+8);
      this->dma_display_->setTextColor(display::ColorUtil::color_to_565(hub75_base::COLOR_BLUE_LIGHTER));
      this->dma_display_->print("(c) 2024 by");
      this->dma_display_->setFont();

      this->dma_display_->setCursor(0, 24);
      this->dma_display_->setTextColor(display::ColorUtil::color_to_565(hub75_base::COLOR_BLUE_LIGHT));
      this->dma_display_->print("sekureco42");
    }

    void HUB75Display::update_brightness_(unsigned long timeInMillis) {
      // Optimized brightness update - only process when needed
      static unsigned long last_brightness_update = 0;
      static bool brightness_changing = false;
      
      // Only update brightness if it's changing and enough time has passed
      if (brightness_ != brightness_destination_) {
        if (!brightness_changing) {
          brightness_changing = true;
          last_brightness_update = timeInMillis;
        }
        
        if (timeInMillis - last_brightness_update >= brightness_fade_speed_) {
          // Optimized brightness calculation
          int8_t brightness_diff = brightness_destination_ - brightness_;
          int8_t step = (brightness_diff > 0) ? BRIGHTNESS_STEP : -BRIGHTNESS_STEP;
          
          brightness_ += step;
          
          // Clamp to destination
          if ((step > 0 && brightness_ >= brightness_destination_) ||
              (step < 0 && brightness_ <= brightness_destination_)) {
            brightness_ = brightness_destination_;
            brightness_changing = false;
          }
          
          this->set_brightness(brightness_);
          last_brightness_update = timeInMillis;
        }
      } else {
        brightness_changing = false;
      }
      
      // Optimized FPS logging - only log every 5 seconds
      frameCounter_++;
      if ((timeInMillis - frameTime_) >= FPS_LOG_INTERVAL) { // Log every 5 seconds
        ESP_LOGD(TAG, "%d frames per 5 seconds (%.1f FPS)", frameCounter_, frameCounter_ / 5.0);
        frameTime_ = timeInMillis;
        frameCounter_ = 0;
      }
      
      _lastTime = timeInMillis;
    }

  }  // namespace hub75_base
}  // namespace esphome
