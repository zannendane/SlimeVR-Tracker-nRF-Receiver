/*
 * Copyright (c) 2025 SlimeVR Contributors.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board initialization for ZannenDongle with RFX2401C FEM.
 *
 * The nRF52840's internal REG0 (LDO/DCDC stage for 1.8V radio supply)
 * must be configured correctly for the RFX2401C FEM to operate.
 * We set REG0 to 3.0V (VOUT_3V0) on first boot to ensure proper
 * FEM supply voltage. This is written to UICR so it persists.
 */

#include <zephyr/init.h>
#include <hal/nrf_power.h>

static int board_zannendongle_uf2_init(void)
{

	/*
	 * The RFX2401C FEM is typically powered from the nRF52840's
	 * VDD_nRF (REG0 output) or an external LDO at 3.0V-3.3V.
	 *
	 * If the main regulator is in HIGH mode and REGOUT0 is still
	 * at its POR default (1.8V), we reprogram it to 3.0V and
	 * trigger a system reset so the change takes effect.
	 *
	 * This is a one-time initialization; after the reset,
	 * REGOUT0 will already be at 3.0V and this code becomes a no-op.
	 */
	if ((nrf_power_mainregstatus_get(NRF_POWER) ==
	     NRF_POWER_MAINREGSTATUS_HIGH) &&
	    ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk) ==
	     (UICR_REGOUT0_VOUT_DEFAULT << UICR_REGOUT0_VOUT_Pos))) {

		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
			;
		}

		NRF_UICR->REGOUT0 =
		    (NRF_UICR->REGOUT0 & ~((uint32_t)UICR_REGOUT0_VOUT_Msk)) |
		    (UICR_REGOUT0_VOUT_3V0 << UICR_REGOUT0_VOUT_Pos);

		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
			;
		}

		/* A reset is required for the REGOUT0 change to take effect */
		NVIC_SystemReset();
	}

	return 0;
}

SYS_INIT(board_zannendongle_uf2_init, PRE_KERNEL_1,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
