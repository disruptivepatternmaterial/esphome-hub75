#include "esphome.h"
using namespace esphome;
#include "esphome/components/hub75_default/hub75_default_display.h"
#include <cstring>

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
          // Smart auto-clear: only clear when necessary to reduce flicker
          if (this->auto_clear_enabled_) {
            // Smart auto-clear: only clear if we're switching pages or if it's been a while
            static uint32_t last_clear_time = 0;
            static uint32_t scroll_update_count = 0;
          
          // Use page pointer comparison instead of name lookup
          static const void* last_page_ptr = nullptr;
          const void* current_page_ptr = (this->page_ != nullptr) ? this->page_ : nullptr;
          uint32_t current_time = millis();
          
          // Check if page changed
          bool page_changed = (last_page_ptr != current_page_ptr);
          
          if (page_changed) {
            // Page changed - always clear
            this->clear_efficient();
            last_clear_time = current_time;
            last_page_ptr = current_page_ptr;
            scroll_update_count = 0;
          } else if (current_page_ptr != nullptr) {
            // Same page - use smart clearing logic
            scroll_update_count++;
            // During scrolling, only clear every 30 updates (3.6 seconds) to prevent buildup
            if (scroll_update_count >= 30) {
              this->clear_efficient();
              last_clear_time = current_time;
              scroll_update_count = 0;
            }
          } else {
            // No page - clear if it's been more than 100ms since last clear
            if ((current_time - last_clear_time) > 100) {
              this->clear_efficient();
              last_clear_time = current_time;
              scroll_update_count = 0;
            }
          }
        }

        if (this->page_ != nullptr) {
          this->page_->get_writer()(*this);
        } else if (this->writer_.has_value()) {
          (*this->writer_)(*this);
        }
        else {
          update_();
        }
        this->dma_display_->flipDMABuffer();
      }
      else {
        this->dma_display_->clearScreen();
      }

      // remove all not ended clipping regions
      while (is_clipping()) {
        end_clipping();
      }

    }

    void HUB75DefaultDisplay::update_() { 
      HUB75Display::update_();
    }

  }  // namespace hub75_default
}  // namespace esphome
