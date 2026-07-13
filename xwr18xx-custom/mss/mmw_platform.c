/* Platform-specific initialization */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* BIOS/XDC Include Files */
#include <xdc/runtime/System.h>
#include <ti/sysbios/knl/Task.h>

/* mmWave SDK Include Files */
#include <ti/drivers/pinmux/pinmux.h>
#include <ti/drivers/gpio/gpio.h>
#include <ti/drivers/soc/soc.h>
#include <ti/control/mmwave/mmwave.h>

#include "mmw_platform.h"

/**************************************************************************
 *************************** Global Definitions ***************************
 **************************************************************************/

/**
 * @brief
 *  Global Variable for LDO BYPASS config
 **********************************************************
 * AS PER RECOMMENDATION FROM THE xWR1843BOOST EVM User guide
 **********************************************************
 *  set LDO bypass since the board has 1.0V RF supply 1
 *  and 1.0V RF supply 2. Please update this API if this
 *  assumption is not valid else it may DAMAGE your board!
 **********************************************************
 **********************************************************
 */
rlRfLdoBypassCfg_t gRFLdoBypassCfg =
{
    .ldoBypassEnable   = 3, /* 1.0V RF supply 1 and 1.0V RF supply 2 */
    .supplyMonIrDrop   = 1, /* IR drop of 3% */
    .ioSupplyIndicator = 0, /* 3.3 V IO supply */
};

/**************************************************************************
 *************************** Platform Functions ***************************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      Platform specific hardware initialization.
 *
 *  @param[in]  config     Platform initialization configuraiton
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_platformInit(MmwDemo_platformCfg *config)
{
    /* Setup the PINMUX to bring out the UART-1 */
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINN5_PADBE, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINN5_PADBE, SOC_XWR18XX_PINN5_PADBE_MSS_UARTA_TX);
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINN4_PADBD, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINN4_PADBD, SOC_XWR18XX_PINN4_PADBD_MSS_UARTA_RX);

    /* Setup the PINMUX to bring out the UART-3 */
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINF14_PADAJ, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINF14_PADAJ, SOC_XWR18XX_PINF14_PADAJ_MSS_UARTB_TX);

    /**********************************************************************
     * Setup the PINMUX:
     * - GPIO Output: Configure pin K13 as GPIO_2 output
     **********************************************************************/
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINK13_PADAZ, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINK13_PADAZ, SOC_XWR18XX_PINK13_PADAZ_GPIO_2);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PING13_PADBC, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PING13_PADBC, SOC_XWR18XX_PING13_PADBC_SYNC_OUT);

    /**********************************************************************
     * Setup the PINMUX:
     * - Chirp-level debug output: Configure pin P9 (ball V10 / PMIC_CLKOUT /
     *   SOP[2] on the AOP package) as CHIRP_START output.
     *
     *   SOC_XWR18XX_PINP9_PADBA is documented as "Equivalent to PINV10_PADBA
     *   on XWR18XXAOP Device" in pinmux_xwr18xx.h. FUNC_SEL=6 (CHIRP_START)
     *   is not exposed as a named macro in this SDK release, but is
     *   documented for ball V10 in the AWR1843AOP datasheet (Table 5-1) and
     *   Pinmux_Set_FuncSel() performs an unchecked raw write into the 4-bit
     *   FUNC_SEL field, so the literal mode value is used directly instead.
     *   (FUNC_SEL values for this pad: 0=GPIO_27, 1=PMIC_CLKOUT, 6=CHIRP_START,
     *   7=CHIRP_END, 8=FRAME_START, 11=EPWM1B, 12=EPWM2A.)
     *
     *   This is purely additive: SOP[2] is only latched at power-up, well
     *   before MmwDemo_platformInit() runs, so repurposing this ball here is
     *   safe and does not affect boot-mode sensing.
     **********************************************************************/
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINP9_PADBA, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINP9_PADBA, 6U /* CHIRP_START */);

    /**********************************************************************
     * Setup the PINMUX:
     * - for QSPI Flash
     **********************************************************************/
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINR12_PADAP, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINR12_PADAP, SOC_XWR18XX_PINR12_PADAP_QSPI_CLK);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINP11_PADAQ, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINP11_PADAQ, SOC_XWR18XX_PINP11_PADAQ_QSPI_CSN);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINR13_PADAL, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINR13_PADAL, SOC_XWR18XX_PINR13_PADAL_QSPI_D0);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINN12_PADAM, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINN12_PADAM, SOC_XWR18XX_PINN12_PADAM_QSPI_D1);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINR14_PADAN, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINR14_PADAN, SOC_XWR18XX_PINR14_PADAN_QSPI_D2);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINP12_PADAO, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINP12_PADAO, SOC_XWR18XX_PINP12_PADAO_QSPI_D3);

    /**********************************************************************
     * Setup the GPIO:
     * - GPIO Output: Configure pin K13 as GPIO_2 output
     **********************************************************************/
    config->SensorStatusGPIO    = SOC_XWR18XX_GPIO_2;

    /* Initialize the DEMO configuration: */
    config->sysClockFrequency   = MSS_SYS_VCLK;
    config->loggingBaudRate     = 921600;
    config->commandBaudRate     = 115200;

    /**********************************************************************
     * Setup the DS3 LED on the EVM connected to GPIO_2
     **********************************************************************/
    GPIO_setConfig (config->SensorStatusGPIO, GPIO_CFG_OUTPUT);
}

/**
 *  @b Description
 *  @n
 *      Function to Setup the HSI Clock. Required for LVDS streaming.
 *
 *  @retval
 *      0  - Success.
 *      <0 - Failed with errors
 */
int32_t MmwDemo_mssSetHsiClk(void)
{
    rlDevHsiClk_t                           hsiClkgs;
    int32_t                                 retVal;

    /*************************************************************************************
     * Setup the HSI Clock through the mmWave Link:
     *************************************************************************************/
    memset ((void*)&hsiClkgs, 0, sizeof(rlDevHsiClk_t));

    /* Setup the HSI Clock as per the Radar Interface Document:
     * - This is set to 600Mhz DDR Mode */
    hsiClkgs.hsiClk = 0x9;

    /* Setup the HSI in the radar link: */
    retVal = rlDeviceSetHsiClk(RL_DEVICE_MAP_CASCADED_1, &hsiClkgs);
    if (retVal != RL_RET_CODE_OK)
    {
        /* Error: Unable to set the HSI clock */
        System_printf ("Error: Setting up the HSI Clock Failed [Error %d]\n", retVal);
        return -1;
    }

    /*The delay below is needed only if the DCA1000EVM is being used to capture the data traces.
      This is needed because the DCA1000EVM FPGA needs the delay to lock to the
      bit clock before they can start capturing the data correctly. */
    Task_sleep(HSI_DCA_MIN_DELAY_MSEC);

    return 0;
}
