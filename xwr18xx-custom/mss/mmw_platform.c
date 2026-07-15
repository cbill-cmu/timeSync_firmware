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
     * - Ball K3 (SDK id PINK13_PADAZ): CHIRP_START instead of GPIO_2.
     *   Raw FUNC_SEL=11 per the AWR1843AOP datasheet Table 5-1; this SDK's
     *   pinmux_xwr18xx.h has no named macro for it since it predates the
     *   AOP-specific alternate functions. True hardware pulse straight off
     *   RadarSS -- no MSS software/mailbox involvement, no jitter, and
     *   chirp-level (not just frame-level). Costs the DS3 status LED,
     *   which was driven off this same pad as GPIO_2/SensorStatusGPIO.
     *
     *   Output override is forced EN here rather than RETAIN_HW_CTRL:
     *   CHIRP_START is an RF/BSS-domain debug tap, not an MSS peripheral
     *   like GPIO_2/UART that manages its own pad output-enable, so
     *   "retain hardware control" of OE likely just leaves the buffer
     *   permanently disabled (this is the same override pattern that
     *   left SYNC_OUT on ball M3 permanently flat too).
     *
     *   FUNC_SEL=11 (CHIRP_START) confirmed correctly landing in the pad
     *   register (readback 0xDB) but never actually pulsed on scope while
     *   actively chirping -- suspect RadarSS just doesn't drive this
     *   class of debug signal in this firmware (TI has said as much for
     *   the related AWR2243). Testing FRAME_START (13) next per same
     *   K3/PINK13_PADAZ table: GPIO_26=0, GPIO_2=1, OSC_CLKOUT=2,
     *   MSS_UARTB_TX=7, BSS_UART_TX=8, SYNC_OUT=9, PMIC_CLKOUT=10,
     *   CHIRP_START=11, CHIRP_END=12, FRAME_START=13. Swap the literal
     *   below to try each in turn.
     **********************************************************************/
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINK13_PADAZ, PINMUX_OUTPUT_EN, PINMUX_INPUT_DIS);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINK13_PADAZ, 13U /* FRAME_START -- was 11U CHIRP_START */);

    /* Ball M3 (pad PING13_PADBC): muxed to GPIO_29 instead of SYNC_OUT.
     * SYNC_OUT never toggled on scope across repeated attempts; RadarSS
     * appears not to drive it. GPIO_29 is host-driven from the frame
     * trigger event instead -- see MmwDemo_eventCallbackFxn in mss_main.c. */
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PING13_PADBC, PINMUX_OUTPUT_EN, PINMUX_INPUT_DIS);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PING13_PADBC, SOC_XWR18XX_PING13_PADBC_GPIO_29);
    GPIO_setConfig(SOC_XWR18XX_GPIO_29, GPIO_CFG_OUTPUT);
    // GPIO_write(SOC_XWR18XX_GPIO_29, 0U);

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
     * - GPIO Output: pin K13 is no longer muxed as GPIO_2 (now CHIRP_START,
     *   see above), so this GPIO_2 config/write is now a no-op on the
     *   physical pin -- the DS3 status LED stays dark. Left in place since
     *   it's harmless and other code still reads SensorStatusGPIO.
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
