#include "esphome.h"
using namespace esphome;
#include "hub75_display.h"
#include <Fonts/TomThumb.h>
#include <algorithm>

namespace esphome
{
  namespace hub75_base
  {

    static const char *const TAG = "hub75_base";

    HUB75Display::~HUB75Display() {
      if (this->dma_display_ != nullptr) {
        delete this->dma_display_;
        this->dma_display_ = nullptr;
      }
    }

    void HUB75Display::setup() {
      ESP_LOGCONFIG(TAG, "Setting up HUB75Display...");

      // Validate configuration
      if (this->width_ < 16 || this->width_ > 256) {
        ESP_LOGE(TAG, "Invalid width: %d (must be 16-256)", this->width_);
        this->mark_failed();
        return;
      }
      
      if (this->height_ < 16 || this->height_ > 256) {
        ESP_LOGE(TAG, "Invalid height: %d (must be 16-256)", this->height_);
        this->mark_failed();
        return;
      }
      
      // Check memory availability
      size_t required_memory = this->width_ * this->height_ * 3; // RGB
      if (ESP.getFreeHeap() < required_memory + 10000) { // 10KB buffer
        ESP_LOGE(TAG, "Insufficient memory: %d bytes free, %d required", 
                 ESP.getFreeHeap(), required_memory);
        this->mark_failed();
        return;
      }

      ESP_LOGCONFIG(TAG, "Memory check passed: %d bytes free, %d required", 
                   ESP.getFreeHeap(), required_memory);

      // Module configuration
      HUB75_I2S_CFG mxconfig(
          this->width_,
          this->height_,
          this->chain_length_,
          this->pins_);

      if (this->user_defined_driver_)
        mxconfig.driver = this->driver_;

      if (this->user_defined_i2sspeed_)
        mxconfig.i2sspeed = this->i2sspeed_;

      if (this->latch_blanking_ >= 0)
        mxconfig.latch_blanking = this->latch_blanking_;

      if (this->user_defined_clock_phase_)
        mxconfig.clkphase = this->clock_phase_;

      // The min refresh rate correlates with the update frequency of the component
      mxconfig.min_refresh_rate = 1000 / this->update_interval_;

      // Enable double buffering for flicker reduction
      mxconfig.double_buff = this->double_buffer_enabled_;
      
      // Additional flicker reduction settings
      mxconfig.clkphase = true;  // Always enable clock phase
      mxconfig.latch_blanking = 12;  // Higher blanking for flicker reduction

      // Display Setup
      this->dma_display_ = new MatrixPanel_I2S_DMA(mxconfig);
      this->dma_display_->begin();
      uint8_t brightness = this->brightness_;
      this->set_brightness(0, false);
      this->set_brightness(brightness, true);
      this->dma_display_->clearScreen();

      // Now write some content to the display
      this->start_screen_();

      if (mxconfig.double_buff) {
        // Write same stuff to other buffer
        this->dma_display_->flipDMABuffer();
        this->start_screen_();
      }
    }

    void HUB75Display::on_set_brightness(int brightness) {
      this->set_brightness(brightness, true);
    }

    void HUB75Display::update() {
      unsigned long timeMillis = millis();
      
      // Frame rate limiting to prevent excessive updates
      uint32_t effective_interval = this->min_update_interval_;
        
      if (timeMillis - this->last_update_time_ < effective_interval) {
        return; // Skip update if too soon
      }
      this->last_update_time_ = timeMillis;
      
      this->update_brightness_(timeMillis);
      this->update_fps_monitoring_(timeMillis);
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
      ESP_LOGCONFIG(TAG, "  Pins: LAT:%i, OE:%i, CLK:%i", pins_.lat, pins_.oe, pins_.clk);

      LOG_UPDATE_INTERVAL(this);

      // Log driver settings
      switch (dma_display_->getCfg().driver) {
      case HUB75_I2S_CFG::shift_driver::SHIFTREG:
        ESP_LOGCONFIG(TAG, "  Driver: SHIFTREG");
        break;
      case HUB75_I2S_CFG::shift_driver::FM6124:
        ESP_LOGCONFIG(TAG, "  Driver: FM6124");
        break;
      case HUB75_I2S_CFG::shift_driver::FM6126A:
        ESP_LOGCONFIG(TAG, "  Driver: FM6126A");
        break;
      case HUB75_I2S_CFG::shift_driver::ICN2038S:
        ESP_LOGCONFIG(TAG, "  Driver: ICN2038S");
        break;
      case HUB75_I2S_CFG::shift_driver::MBI5124:
        ESP_LOGCONFIG(TAG, "  Driver: MBI5124");
        break;
      case HUB75_I2S_CFG::shift_driver::SM5266P:
        ESP_LOGCONFIG(TAG, "  Driver: SM5266P");
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
      // Reject invalid pixels
      if (x >= this->get_width_internal() || x < 0 || y >= this->get_height_internal() || y < 0)
        return;

      // Apply gamma correction only if enabled
      uint8_t r = color.r, g = color.g, b = color.b;
      if (this->enable_gamma_correction_) {
        this->apply_gamma_correction(r, g, b);
      }

      // Update pixel value in buffer
      this->dma_display_->drawPixelRGB888(x, y, r, g, b);
    }

    void HUB75Display::fill(Color color) {
      // Wrap fill screen method
      dma_display_->fillScreenRGB888(color.r, color.g, color.b);
    }

    void HUB75Display::filled_rectangle(int x1, int y1, int width, int height, Color color) {
      // Wrap fill rectangle method
      dma_display_->fillRect(x1, y1, width, height, color.r, color.g, color.b);
    }

    void HUB75Display::update_() { 
      this->dma_display_->fillRect(0, 8, 64, 7, display::ColorUtil::color_to_565(backgroundColor));
      this->dma_display_->setCursor(0, 8);
      this->dma_display_->setTextColor(display::ColorUtil::color_to_565(COLOR_RED));

      ESPTime now = this->time_->now();
      if (now.is_valid()) {
        this->dma_display_->printf("%02d:%02d:%02d", now.hour, now.minute, now.second);
      }
      else {
        this->dma_display_->print("--:--:-- ?");
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
      if (timeInMillis - _lastTime >= brightness_fade_speed_) {
        // Optimized brightness transition logic
        if (brightness_ != brightness_destination_) {
          int8_t step = (brightness_destination_ > brightness_) ? brightness_step_size_ : -brightness_step_size_;
          int16_t new_brightness = brightness_ + step;
          
          // Clamp to destination or bounds
          if ((step > 0 && new_brightness >= brightness_destination_) ||
              (step < 0 && new_brightness <= brightness_destination_)) {
            brightness_ = brightness_destination_;
          } else {
            brightness_ = (uint8_t)std::max(0, std::min(255, (int)new_brightness));
          }

          uint8_t brightness_destination_saved = brightness_destination_;
          this->set_brightness(brightness_);
          brightness_destination_ = brightness_destination_saved;
        }

        _lastTime = timeInMillis;
      }
    }

    void HUB75Display::update_fps_monitoring_(unsigned long timeInMillis) {
      if (!this->enable_fps_monitoring_) {
        return;
      }

      this->frame_count_++;
      
      if ((timeInMillis - this->last_fps_time_) >= 1000) {
        this->current_fps_ = (float)this->frame_count_ * 1000.0f / (timeInMillis - this->last_fps_time_);
        
        if (this->frame_count_ % 10 == 0) { // Log every 10 seconds
          ESP_LOGD(TAG, "FPS: %.1f, Frame count: %d, Free heap: %d bytes", 
                   this->current_fps_, this->frame_count_, ESP.getFreeHeap());
        }

        this->last_fps_time_ = timeInMillis;
        this->frame_count_ = 0;
      }
    }

    // Move gamma table to class scope for better performance
    const uint8_t HUB75Display::GAMMA_TABLE[256] = {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
      2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
      5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
     10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
     17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
     25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
     37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
     51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
     69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
     90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
    115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
    144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
    177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
    215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255
    };

    void HUB75Display::apply_gamma_correction(uint8_t& r, uint8_t& g, uint8_t& b) {
      r = GAMMA_TABLE[r];
      g = GAMMA_TABLE[g];
      b = GAMMA_TABLE[b];
    }

  }  // namespace hub75_base
}  // namespace esphome
