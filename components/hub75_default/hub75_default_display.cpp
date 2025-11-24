#include "esphome.h"
using namespace esphome;
#include "esphome/components/hub75_default/hub75_default_display.h"

namespace esphome {
  namespace hub75_default {

    static const char *TAG = "hub75_default";

    void HUB75DefaultDisplay::setup() {
      ESP_LOGCONFIG(TAG, "Starting setup...");
      this->display_name_ = "HUB75d";
      HUB75Display::setup();

      ESP_LOGI(TAG, "Finished Setup");
    }

    void HUB75DefaultDisplay::dump_config() {
      ESP_LOGCONFIG(TAG, "Dumping Config...");
      HUB75Display::dump_config();
      ESP_LOGI(TAG, "Finished Dumping");
    }
    

    void HUB75DefaultDisplay::update() {
      HUB75Display::update();

      if (!this->enabled_) {
        this->dma_display_->clearScreen();
        return;
      }

      if (this->double_buffer_enabled_) {
        // Double buffering: flip to get back buffer, clear it, draw, then flip to show
        // Flip first to get the back buffer (the one we'll draw to)
        this->dma_display_->flipDMABuffer();
        
        // Clear the back buffer if auto_clear is enabled (not visible, so no lag)
        if (this->auto_clear_enabled_) {
          this->dma_display_->fillScreenRGB888(0, 0, 0);
        }

        // Draw to the back buffer
        if (this->page_ != nullptr) {
          this->page_->get_writer()(*this);
        } else if (this->writer_.has_value()) {
          (*this->writer_)(*this);
        } else {
          update_();
        }
        
        // Flip to show the newly drawn frame
        this->dma_display_->flipDMABuffer();
      } else {
        // Single buffer mode: clear the visible buffer if needed
        if (this->auto_clear_enabled_) {
          this->clear();
        }

        // Draw to the single buffer
        if (this->page_ != nullptr) {
          this->page_->get_writer()(*this);
        } else if (this->writer_.has_value()) {
          (*this->writer_)(*this);
        } else {
          update_();
        }
      }

      // Clean up clipping regions
      while (is_clipping()) {
        end_clipping();
      }
    }

    void HUB75DefaultDisplay::update_() { 
      HUB75Display::update_();
    }

  }  // namespace hub75_default
}  // namespace esphome
