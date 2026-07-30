/* Platform-specific initialization */

#ifndef MMW_PLATFORM_H
#define MMW_PLATFORM_H

#include <ti/demo/xwr18xx/mmw/include/mmw_config.h>
#include <ti/control/mmwave/mmwave.h>
#include <ti/utils/hsiheader/hsiheader.h>

#ifdef __cplusplus
extern "C" {
#endif

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
extern rlRfLdoBypassCfg_t gRFLdoBypassCfg;

/**
 *  @b Description
 *  @n
 *      Platform-specific hardware initialization (pinmux, GPIO, clocks)
 *
 *  @param[in]  config     Platform initialization configuration
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_platformInit(MmwDemo_platformCfg *config);

/**
 *  @b Description
 *  @n
 *      Function to Setup the HSI Clock. Required for LVDS streaming.
 *
 *  @retval
 *      0  - Success.
 *      <0 - Failed with errors
 */
int32_t MmwDemo_mssSetHsiClk(void);

#ifdef __cplusplus
}
#endif

#endif /* MMW_PLATFORM_H */
