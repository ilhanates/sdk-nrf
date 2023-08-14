/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <soc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

#include "nrfx_ppi.h"
#include "hal/nrf_radio.h"
#include "hal/nrf_timer.h"
#include <helpers/nrfx_gppi.h>

#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)
#define APP_TIMER  NRF_TIMER3

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};


// PPI
static nrf_ppi_channel_t allocate_gppi_channel(void)
{
	nrf_ppi_channel_t channel;

	if (nrfx_ppi_channel_alloc(&channel) != NRFX_SUCCESS) {
		__ASSERT(false, "(D)PPI channel allocation error");
	}
	return channel;
}


void m_ppi_setup(void)
{
	nrf_ppi_channel_t ppi_t0_compare = allocate_gppi_channel();
	nrfx_gppi_channel_endpoints_setup(ppi_t0_compare, nrf_timer_event_address_get(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1), nrf_timer_task_address_get(APP_TIMER, NRF_TIMER_TASK_START));

	nrf_ppi_channel_t ppi_radio_ready = allocate_gppi_channel();
	nrfx_gppi_channel_endpoints_setup(ppi_radio_ready, (uint32_t)&NRF_RADIO->EVENTS_READY, nrf_timer_task_address_get(APP_TIMER, NRF_TIMER_TASK_CAPTURE0));

	nrf_ppi_channel_t ppi_radio_address = allocate_gppi_channel();
	nrfx_gppi_channel_endpoints_setup(ppi_radio_address, (uint32_t)&NRF_RADIO->EVENTS_ADDRESS, nrf_timer_task_address_get(APP_TIMER, NRF_TIMER_TASK_CAPTURE1));

	nrf_ppi_channel_t ppi_radio_end = allocate_gppi_channel();
	nrfx_gppi_channel_endpoints_setup(ppi_radio_end, (uint32_t)&NRF_RADIO->EVENTS_END, nrf_timer_task_address_get(APP_TIMER, NRF_TIMER_TASK_CAPTURE2));

	uint32_t ch_msk = (BIT(ppi_t0_compare) | BIT(ppi_radio_ready) | BIT(ppi_radio_address) | BIT(ppi_radio_end));
	nrfx_gppi_channels_enable(ch_msk);
}

// TIMER
uint32_t m_timer_current_value(void)
{
	nrf_timer_task_trigger(APP_TIMER, nrf_timer_capture_task_get(3));
	return nrf_timer_cc_get(APP_TIMER, 3);
}

void m_reset_captures(void)
{
	nrf_timer_cc_set(APP_TIMER, 0, 0);
	nrf_timer_cc_set(APP_TIMER, 1, 0);
	nrf_timer_cc_set(APP_TIMER, 2, 0);
	nrf_timer_cc_set(APP_TIMER, 3, 0);
}

void print_timings(uint8_t idx)
{
	uint32_t event_ready_time = nrf_timer_cc_get(APP_TIMER, 0);
	uint32_t event_address_time = nrf_timer_cc_get(APP_TIMER, 1);
	uint32_t event_end_time = nrf_timer_cc_get(APP_TIMER, 2);

	printk("idx:%d event_ready_time:%d, event_address_time:%d event_end_time:%d curr_time:%d\n", idx, event_ready_time, event_address_time, event_end_time, m_timer_current_value());
}

ISR_DIRECT_DECLARE(timer_isr)
{
	bool compare_4 = nrf_timer_event_check(APP_TIMER, NRF_TIMER_EVENT_COMPARE4);
	bool compare_5 = nrf_timer_event_check(APP_TIMER, NRF_TIMER_EVENT_COMPARE5);

	if (compare_4) {
		nrf_timer_event_clear(APP_TIMER, NRF_TIMER_EVENT_COMPARE4);
		print_timings(1);
	}

	if (compare_5) {
		nrf_timer_event_clear(APP_TIMER, NRF_TIMER_EVENT_COMPARE5);
		print_timings(2);

		nrf_timer_task_trigger(APP_TIMER, NRF_TIMER_TASK_STOP);
		nrf_timer_task_trigger(APP_TIMER, NRF_TIMER_TASK_CLEAR);

		m_reset_captures();
	}

	return 0;
}

void m_timer_setup(void)
{
	nrf_timer_bit_width_set(APP_TIMER, NRF_TIMER_BIT_WIDTH_32);
	nrf_timer_prescaler_set(APP_TIMER, NRF_TIMER_PRESCALER_CALCULATE(NRF_TIMER_BASE_FREQUENCY_GET(APP_TIMER), 16000000));
	nrf_timer_task_trigger(APP_TIMER, NRF_TIMER_TASK_CLEAR);

	nrf_timer_cc_set(APP_TIMER, 4, 1000);
	nrf_timer_cc_set(APP_TIMER, 5, 2000);

	m_reset_captures();

	IRQ_DIRECT_CONNECT(TIMER3_IRQn, 1, timer_isr, 0);
	irq_enable(TIMER3_IRQn);

	nrf_timer_int_enable(APP_TIMER, NRF_TIMER_INT_COMPARE4_MASK | NRF_TIMER_INT_COMPARE5_MASK);
}

void m_setup(void)
{
	m_timer_setup();
	m_ppi_setup();
}

int main(void)
{
	int err;
	m_setup();

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printk("Advertising successfully started\n");

	for (;;) {
		k_sleep(K_MSEC(50));
	}
}
