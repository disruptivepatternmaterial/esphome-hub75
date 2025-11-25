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

      if (this->enabled_) {
        // Proper double buffering pattern:
        // The library maintains two buffers - one displayed, one for drawing
        // When double buffering is enabled, we draw to the back buffer (non-visible),
        // then flip to show it. This eliminates flicker and provides smooth updates.
        
        if (this->double_buffer_enabled_) {
          // With double buffering: 
          // 1. Clear the back buffer (current drawing target) if auto_clear is enabled
          //    This happens on the non-visible buffer, so no flicker
          if (this->auto_clear_enabled_) {
            this->dma_display_->fillScreenRGB888(0, 0, 0);
          }
          
          // 2. Draw content to the back buffer
          if (this->page_ != nullptr) {
            this->page_->get_writer()(*this);
          } else if (this->writer_.has_value()) {
            (*this->writer_)(*this);
          } else {
            update_();
          }
          
          // 3. Flip to swap buffers: back buffer becomes front (displayed), 
          //    front buffer becomes back (next drawing target)
          this->dma_display_->flipDMABuffer();
        } else {
          // Single buffer mode: clear visible buffer first (will cause visible lag)
          if (this->auto_clear_enabled_) {
            this->clear();
          }
          
          // Draw directly to the visible buffer
          if (this->page_ != nullptr) {
            this->page_->get_writer()(*this);
          } else if (this->writer_.has_value()) {
            (*this->writer_)(*this);
          } else {
            update_();
          }
        }
      }
      else {
        // Display is disabled - clear both buffers if double buffering
        if (this->double_buffer_enabled_) {
          this->dma_display_->fillScreenRGB888(0, 0, 0);
          this->dma_display_->flipDMABuffer();
          this->dma_display_->fillScreenRGB888(0, 0, 0);
          this->dma_display_->flipDMABuffer();
        } else {
          this->dma_display_->clearScreen();
        }
      }

      // Remove all not ended clipping regions
      while (is_clipping()) {
        end_clipping();
      }
    }

    void HUB75DefaultDisplay::update_() { 
      HUB75Display::update_();
    }

  }  // namespace hub75_default
}  // namespace esphome
