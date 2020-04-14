#include <application.h>
#include <bc_ds18b20.h>

/*

 SENSOR MODULE CONNECTION
==========================

Sensor Module R1.0 - 4 pin connector
VCC, GND, - , DATA

Sensor Module R1.1 - 5 pin connector
- , GND , VCC , - , DATA


 DS18B20 sensor pinout
=======================
VCC - red
GND - black
DATA- yellow (white)

*/

// Time after the sending is less frequent to save battery
#define SERVICE_INTERVAL_INTERVAL (10 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL   (30 * 60 * 1000)

#define UPDATE_SERVICE_INTERVAL            (5 * 1000)
#define UPDATE_NORMAL_INTERVAL             (1 * 60 * 1000)

#define TEMPERATURE_DS18B20_PUB_NO_CHANGE_INTEVAL (5  * 60* 1000)
#define TEMPERATURE_DS18B20_PUB_VALUE_CHANGE 0.5f

#define TEMPERATURE_TAG_PUB_VALUE_CHANGE 0.2f
#define TEMPERATURE_TAG_PUB_NO_CHANGE_INTEVAL (15 * 60 * 1000)
#define APPLICATION_TASK_ID 0


static bc_led_t led;

float temperature_on_display = NAN;

static bc_ds18b20_t ds18b20;

int device_index;

// Thermometer instance
bc_tmp112_t tmp112;
event_param_t temperature_event_param = { .next_pub = 0, .value = NAN };
event_param_t temperature_set_point;


struct {
    event_param_t temperature_112;
    event_param_t temperature_ds18b20;

} params;

void handler_ds18b20(bc_ds18b20_t *s, uint64_t device_id, bc_ds18b20_event_t e, void *p);

void climate_module_event_handler(bc_module_climate_event_t event, void *event_param);

void switch_to_normal_mode_task(void *param);

void battery_event_handler(bc_module_battery_event_t e, void *p)
{
    (void) e;
    (void) p;

    float voltage;

    if (bc_module_battery_get_voltage(&voltage))
    {
        bc_radio_pub_battery(&voltage);
    }
}


void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param)
{
    float value;
    // event_param_t *param = (event_param_t *)event_param;

    if (event != BC_TMP112_EVENT_UPDATE)
    {
        return;
    }

    if (bc_tmp112_get_temperature_celsius(self, &value))
    {
        if ((fabsf(value - params.temperature_112.value) >= TEMPERATURE_TAG_PUB_VALUE_CHANGE) || (params.temperature_112.next_pub < bc_scheduler_get_spin_tick()))
        {
            // bc_radio_pub_temperature(BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_ALTERNATE, &value);
            params.temperature_112.value = value;
            params.temperature_112.next_pub = bc_scheduler_get_spin_tick() + TEMPERATURE_TAG_PUB_NO_CHANGE_INTEVAL;
        }
    }
    else
    {
        params.temperature_112.value = NAN;
    }

    // if (temperature_set_point.next_pub < bc_scheduler_get_spin_tick())
    // {
    //     radio_pub_set_temperature();
    // }

    bc_scheduler_plan_from_now(APPLICATION_TASK_ID, 300);

}

void ds18b20_event_handler(bc_ds18b20_t *self, uint64_t device_address, bc_ds18b20_event_t e, void *p)
{
    (void) p;

    float value = NAN;

    if (e == bc_ds18b20_EVENT_UPDATE)
    {
        bc_ds18b20_get_temperature_celsius(self, device_address, &value);

        //bc_log_debug("UPDATE %" PRIx64 "(%d) = %f", device_address, device_index, value);

        if ((fabs(value - params.temperature_ds18b20.value) >= TEMPERATURE_DS18B20_PUB_VALUE_CHANGE) || (params.temperature_ds18b20.next_pub < bc_scheduler_get_spin_tick()))
        {
            static char topic[64];
            snprintf(topic, sizeof(topic), "thermometer/%" PRIx64 "/temperature", device_address);
            bc_radio_pub_float(topic, &value);
            params.temperature_ds18b20.value = value;
            params.temperature_ds18b20.next_pub = bc_scheduler_get_spin_tick() + TEMPERATURE_DS18B20_PUB_NO_CHANGE_INTEVAL;
            bc_scheduler_plan_from_now(0, 300);

        }
    }
}

// This task is fired once after the SERVICE_INTERVAL_INTERVAL milliseconds and changes the period
// of measurement. After module power-up you get faster updates so you can test the module and see
// instant changes. After SERVICE_INTERVAL_INTERVAL the update period is longer to save batteries.
void switch_to_normal_mode_task(void *param)
{
    bc_ds18b20_set_update_interval(&ds18b20, UPDATE_NORMAL_INTERVAL);

    bc_scheduler_unregister(bc_scheduler_get_current_task_id());
}

void application_init(void)
{
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);

    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize thermometer sensor on core module
    bc_tmp112_init(&tmp112, BC_I2C_I2C0, 0x49);
    bc_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    bc_tmp112_set_update_interval(&tmp112, UPDATE_SERVICE_INTERVAL);

    // For single sensor you can call bc_ds18b20_init()
    bc_ds18b20_init(&ds18b20, BC_DS18B20_RESOLUTION_BITS_12);

    bc_ds18b20_set_event_handler(&ds18b20, ds18b20_event_handler, NULL);
    bc_ds18b20_set_update_interval(&ds18b20, UPDATE_SERVICE_INTERVAL);

    bc_scheduler_register(switch_to_normal_mode_task, NULL, SERVICE_INTERVAL_INTERVAL);

    bc_radio_pairing_request("printer-temperature-monitor", VERSION);

    bc_led_pulse(&led, 2000);

    bc_module_lcd_init();

    //bc_log_init(BC_LOG_LEVEL_DEBUG, BC_LOG_TIMESTAMP_ABS);
    bc_scheduler_plan_from_now(0, 1000);
}

void application_task(void)
{
    static char str_temperature_inside[10];
    static char str_temperature_outside[10];

    if (!bc_module_lcd_is_ready())
    {
    	return;
    }

    bc_system_pll_enable();

    bc_module_lcd_clear();

    // Print core (inside) temperature sensor
    bc_module_lcd_set_font(&bc_font_ubuntu_13);
    bc_module_lcd_draw_string(20, 05, "INSIDE   ", true);
    
    bc_module_lcd_set_font(&bc_font_ubuntu_33);
    snprintf(str_temperature_inside, sizeof(str_temperature_inside), "%.1f   ", params.temperature_112.value);
    int x2 = bc_module_lcd_draw_string(20, 20, str_temperature_inside, true);
    bc_module_lcd_set_font(&bc_font_ubuntu_24);
    bc_module_lcd_draw_string(x2 - 20, 25, "\xb0" "C   ", true);


    // Print ds18b20 (outside) temperature sensor
    bc_module_lcd_set_font(&bc_font_ubuntu_13);
    bc_module_lcd_draw_string(20, 75, "OUTSIDE   ", true);

    bc_module_lcd_set_font(&bc_font_ubuntu_33);
    snprintf(str_temperature_outside, sizeof(str_temperature_outside), "%.1f   ", params.temperature_ds18b20.value);
    int x1 = bc_module_lcd_draw_string(20, 90, str_temperature_outside, true);
    // temperature_on_display = params.temperature_ds18b20.value;

    bc_module_lcd_set_font(&bc_font_ubuntu_24);
    bc_module_lcd_draw_string(x1 - 20, 95, "\xb0" "C   ", true);



    bc_module_lcd_update();

    bc_system_pll_disable();

}