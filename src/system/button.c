/*
	SlimeVR Code is placed under the MIT license
	Copyright (c) 2025 SlimeVR Contributors

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/
#include "globals.h"
#include "connection/esb.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_power.h>

LOG_MODULE_REGISTER(button, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_ALIAS(sw0))

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

// Debounce interval, short/long press thresholds and double-press window
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_SHORT_PRESS_MAX_MS 1000
#define BUTTON_LONG_PRESS_MS 5000
#define BUTTON_DOUBLE_PRESS_WINDOW_MS 500

static struct gpio_callback button_cb_data;
static bool button_pressed;
static int64_t press_start_time;
// Sentinel: no short press recorded yet (or a double press was just consumed)
#define BUTTON_NO_PRESS (-(BUTTON_DOUBLE_PRESS_WINDOW_MS + 1))
static int64_t last_short_press_time = BUTTON_NO_PRESS;

static void button_debounce_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(button_debounce_work, button_debounce_handler);
static void button_long_press_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(button_long_press_work, button_long_press_handler);

// Single long press (5 s): enter the UF2 bootloader DFU mode
static void button_long_press_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!button_pressed)
		return;
#if defined(CONFIG_BUILD_OUTPUT_UF2)
	LOG_INF("Long press detected, entering DFU");
	// Adafruit UF2 bootloader DFU magic (same as the console "dfu" command)
	NRF_POWER->GPREGRET = 0x57;
	sys_reboot(SYS_REBOOT_COLD);
#else
	LOG_WRN("Long press detected, but no UF2 bootloader support");
#endif
}

static void button_debounce_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	bool pressed = gpio_pin_get_dt(&button) == 1;
	if (pressed == button_pressed)
		return; // bounced, debounced state unchanged
	button_pressed = pressed;
	if (pressed)
	{
		press_start_time = k_uptime_get();
		k_work_reschedule(&button_long_press_work, K_MSEC(BUTTON_LONG_PRESS_MS));
		return;
	}
	k_work_cancel_delayable(&button_long_press_work);
	int64_t now = k_uptime_get();
	if (now - press_start_time > BUTTON_SHORT_PRESS_MAX_MS)
		return; // held too long for a click, too short for DFU
	// Double short press: enter pairing mode
	if (now - last_short_press_time <= BUTTON_DOUBLE_PRESS_WINDOW_MS)
	{
		last_short_press_time = BUTTON_NO_PRESS; // require a fresh pair of presses
		LOG_INF("Double press detected, entering pairing mode");
		// sleeps internally, must not be called from ISR context
		esb_reset_pair();
	}
	else
	{
		last_short_press_time = now;
	}
}

static void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	// re-scheduled on every edge, so the handler runs once after bouncing stops
	k_work_reschedule(&button_debounce_work, K_MSEC(BUTTON_DEBOUNCE_MS));
}

static int button_init(void)
{
	LOG_DBG("button_init");
	if (!device_is_ready(button.port))
	{
		LOG_ERR("Button GPIO device not ready");
		return -ENODEV;
	}
	gpio_pin_configure_dt(&button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	return 0;
}

SYS_INIT(button_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif
