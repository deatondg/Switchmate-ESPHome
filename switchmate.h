#include "esphome.h"
using namespace ble_client;

static const char *const TAG = "switchmate";

// 180F
static espbt::ESPBTUUID BATTERY_SERVICE_UUID = espbt::ESPBTUUID::from_uint16(0x180FULL);
// 2A19
static espbt::ESPBTUUID BATTERY_CHAR_UUID = espbt::ESPBTUUID::from_uint16(0x2A19ULL);

// A22BD383-EBDD-49AC-B2E7-40EB55F5D0AB
static espbt::ESPBTUUID STATE_SERVICE_UUID = espbt::ESPBTUUID::from_raw(
    (const uint8_t[16]){0xAB,0xD0,0xF5,0x55,0xEB,0x40,0xE7,0xB2,0xAC,0x49,0xDD,0xEB,0x83,0xD3,0x2B,0xA2});
// A22B0090-EBDD-49AC-B2E7-40EB55F5D0AB
static espbt::ESPBTUUID STATE_CHAR_UUID = espbt::ESPBTUUID::from_raw(
    (const uint8_t[16]){0xAB,0xD0,0xF5,0x55,0xEB,0x40,0xE7,0xB2,0xAC,0x49,0xDD,0xEB,0x90,0x00,0x2B,0xA2});

static uint8_t STATE_VAL_OFF[1] = {0x00};
static uint8_t STATE_VAL_ON[1]  = {0x01};

class SwitchmateSwitchController {
  public:
    virtual void write_switch_state(bool state) = 0;
};

class SwitchmateSwitch : public Switch {
  public:
    explicit SwitchmateSwitch(SwitchmateSwitchController *controller) {
        this->controller = controller;
    }

    SwitchmateSwitchController *controller;

    void write_state(bool state) {
        controller->write_switch_state(state);
    }
};

class SwitchmateController : public PollingComponent, public BLEClientNode, public SwitchmateSwitchController {
  public:
    explicit SwitchmateController(
            const std::string &name,
            bool notify,
            uint32_t update_interval,
            BLEClient *client
    ) : PollingComponent(update_interval) {
        this->name = name;
        
        this->notify = notify;

        battery_sensor = new Sensor();
        state_switch = new SwitchmateSwitch(this);

        client->register_ble_node(this);
    }
    
    std::string name;

    bool notify;

    Sensor *battery_sensor;
    SwitchmateSwitch *state_switch;

    uint16_t battery_handle;
    uint16_t state_handle;

    bool battery_warning = false;
    bool state_warning = false;

    bool battery_established = false;
    bool state_established = false;

    void write_switch_state(bool state) {
        if (this->node_state != espbt::ClientState::ESTABLISHED) {
	    ESP_LOGW(TAG, "[%s] Cannot write state, not connected.", this->get_name().c_str());
            return;
        }
        if (state_handle == 0) {
	    ESP_LOGW(TAG, "[%s] Cannot write state, service or characteristic not found.", this->get_name().c_str());
            return;
        }

        auto status =
            esp_ble_gattc_write_char(this->parent()->gattc_if, this->parent()->conn_id, state_handle, 
                    1, state ? STATE_VAL_ON : STATE_VAL_OFF, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
        if (status) {
            //state_warning = true;
            //state_switch->publish_state(false);

            ESP_LOGW(TAG, "[%s] Error writing state, status=%d.", this->get_name().c_str(), status);
        } else {
            ESP_LOGD(TAG, "[%s] Requested state write for value=%d.", this->get_name().c_str(), state);
        }
    }

    void update() override {
        if (this->node_state != espbt::ClientState::ESTABLISHED) {
            ESP_LOGW(TAG, "[%s] Cannot poll, not connected.", this->get_name().c_str());
            return;
        }

        if (battery_handle == 0) {
            ESP_LOGW(TAG, "[%s] Cannot poll battery level, service or characteristic not found.", this->get_name().c_str());
        } else {
            auto status =
                esp_ble_gattc_read_char(this->parent()->gattc_if, this->parent()->conn_id, battery_handle, ESP_GATT_AUTH_REQ_NONE);
            if (status) {

                battery_warning = true;
                battery_sensor->publish_state(NAN);

                ESP_LOGW(TAG, "[%s] Error sending read request for battery level, status=%d.", this->get_name().c_str(), status);
            }
        }

        if (state_handle == 0) {
            ESP_LOGW(TAG, "[%s] Cannot poll state, service or characteristic not found.", this->get_name().c_str());
        } else {
            auto status =
                esp_ble_gattc_read_char(this->parent()->gattc_if, this->parent()->conn_id, state_handle, ESP_GATT_AUTH_REQ_NONE);
            if (status) {

                state_warning = true;
                state_switch->publish_state(false);

                ESP_LOGW(TAG, "[%s] Error sending read request for state, status=%d.", this->get_name().c_str(), status);
            }
        }
    
        if (battery_warning || state_warning) {
            this->status_set_warning();
        }
    }

    void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                             esp_ble_gattc_cb_param_t *param) {
        switch (event) {
        case ESP_GATTC_OPEN_EVT: { // Connection event
            if (param->open.status == ESP_GATT_OK) {
                ESP_LOGI(TAG, "[%s] Connected successfully!", this->get_name().c_str());
            }
            break;
        }
        case ESP_GATTC_DISCONNECT_EVT: { // Disconnection event
            ESP_LOGW(TAG, "[%s] Disconnected!", this->get_name().c_str());
            
            battery_warning = true;
            battery_sensor->publish_state(NAN);

            state_warning = true;
            state_switch->publish_state(false);

            break;
        }
        case ESP_GATTC_SEARCH_CMPL_EVT: { // Discovery completed, I guess

            battery_handle = 0;
            auto battery_chr = this->parent()->get_characteristic(BATTERY_SERVICE_UUID, BATTERY_CHAR_UUID);
            if (battery_chr == nullptr) {
                battery_warning = true;
                battery_sensor->publish_state(NAN); 
                ESP_LOGW(TAG, "[%s] Battery level characteristic not found at service %s char %s.", this->get_name().c_str(),
                    BATTERY_SERVICE_UUID.to_string().c_str(), BATTERY_CHAR_UUID.to_string().c_str());
            } else {
                battery_handle = battery_chr->handle;

                if (this->notify) {
                    auto status =
                        esp_ble_gattc_register_for_notify(this->parent()->gattc_if, this->parent()->remote_bda, battery_chr->handle);
                    if (status) {
                        ESP_LOGW(TAG, "[%s] esp_ble_gattc_register_for_notify failed for battery level, status=%d.", this->get_name().c_str(), status);
                    }
                } else {
                    battery_established = true;
                }
            }

            state_handle = 0;
            auto state_chr = this->parent()->get_characteristic(STATE_SERVICE_UUID, STATE_CHAR_UUID);
            if (state_chr == nullptr) {
                state_warning = true;
                state_switch->publish_state(false);
                ESP_LOGW(TAG, "[%s] State characteristic not found at service %s char %s.", this->get_name().c_str(),
                    STATE_SERVICE_UUID.to_string().c_str(), STATE_SERVICE_UUID.to_string().c_str());
            } else {
                state_handle = state_chr->handle;

                if (this->notify) {
                    auto status =
                        esp_ble_gattc_register_for_notify(this->parent()->gattc_if, this->parent()->remote_bda, state_chr->handle);
                    if (status) {
                        ESP_LOGW(TAG, "[%s] esp_ble_gattc_register_for_notify failed for state, status=%d.", this->get_name().c_str(), status);
                    }
                } else {
                    state_established = true;
                }
            }

            if (battery_established && state_established) {
                this->node_state = espbt::ClientState::ESTABLISHED;
            }
            if (battery_warning || state_warning) {
                this->status_set_warning();
            }
            break;
        }
        case ESP_GATTC_READ_CHAR_EVT: { // Read characteristic
            if (param->read.conn_id != this->parent()->conn_id)
                break;
            if (param->read.status != ESP_GATT_OK) {
                ESP_LOGW(TAG, "[%s] Error reading char at handle %d, status=%d.", this->get_name().c_str(), param->read.handle, param->read.status);
                break;
            }

            if (param->read.handle == battery_handle) {
                battery_warning = false;
                battery_sensor->publish_state(param->read.value[0]);
            }
            if (param->read.handle == state_handle) {
                state_warning = false;
                state_switch->publish_state(param->read.value[0]);
            }

            if ( !(battery_warning || state_warning) ) {
                this->status_clear_warning();
            }
            break;
        }
        case ESP_GATTC_WRITE_CHAR_EVT: { // Wrote characteristic
            if (param->write.conn_id != this->parent()->conn_id || param->write.handle != state_handle)
                break;
            if (param->write.status != ESP_GATT_OK) {
                ESP_LOGW(TAG, "[%s] Error writing char at handle %d, status=%d.", this->get_name().c_str(), param->write.handle, param->write.status);
                break;
            } 

            ESP_LOGD(TAG, "[%s] Successfully wrote state.", this->get_name().c_str());
            state_warning = false;
            if (!battery_warning) {
                this->status_clear_warning();
            }
            
            this->update();
                
            break;
        }
        case ESP_GATTC_NOTIFY_EVT: { // Got notified
            if (param->notify.conn_id != this->parent()->conn_id)
                break;

            if (param->notify.handle == battery_handle) {
                ESP_LOGV(TAG, "[%s] Battery level notification: handle=0x%x, value=0x%x", this->get_name().c_str(),
                    param->notify.handle, param->notify.value[0]);
                battery_warning = false;
                battery_sensor->publish_state(param->notify.value[0]);
            }
            if (param->notify.handle == state_handle) {
                ESP_LOGV(TAG, "[%s] State notification: handle=0x%x, value=0x%x", this->get_name().c_str(),
                    param->notify.handle, param->notify.value[0]);
                state_warning = false;
                state_switch->publish_state(param->notify.value[0]);
            }
            break;
        }
        case ESP_GATTC_REG_FOR_NOTIFY_EVT: { // Successfully registered for notifications
            if (param->reg_for_notify.handle == battery_handle) {
                battery_established = true;
            }
            if (param->reg_for_notify.handle == state_handle) {
                state_established = true;
            }

            if (battery_established && state_established) {
                this->node_state = espbt::ClientState::ESTABLISHED;
            }
            break;
        }
        default:
            break;
        }

        if (state_warning || battery_warning) {
            this->status_set_warning();
        }
    }
    
    std::string get_name() {
        return name;
    }
    
    // I have no idea what this is
    uint32_t hash_base() { return 2345872354UL; }
};

