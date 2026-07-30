/* EDMA interface */
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* BIOS/XDC Include Files */
#include <xdc/runtime/System.h>

/* mmWave SDK Include Files */
#include <ti/drivers/edma/edma.h>
#include <ti/drivers/soc/soc.h>

#include "mmw_mss.h"
#include "mmw_edma.h"
#include "mmw_res.h"

/**************************************************************************
 *************************** External Definitions *************************
 **************************************************************************/

/* Access to global MCB for EDMA handles and error info */
extern MmwDemo_MSS_MCB gMmwMssMCB;

/**************************************************************************
 *************************** Local Functions ******************************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      Call back function for EDMA CC (Channel controller) error as per EDMA API.
 *      Declare fatal error if happens, the output errorInfo can be examined if code
 *      gets trapped here.
 */
static void MmwDemo_EDMA_errorCallbackFxn(EDMA_Handle handle, EDMA_errorInfo_t *errorInfo)
{
    gMmwMssMCB.EDMA_errorInfo = *errorInfo;
    MmwDemo_debugAssert(0);
}

/**
 *  @b Description
 *  @n
 *      Call back function for EDMA transfer controller error as per EDMA API.
 *      Declare fatal error if happens, the output errorInfo can be examined if code
 *      gets trapped here.
 */
static void MmwDemo_EDMA_transferControllerErrorCallbackFxn(EDMA_Handle handle,
                EDMA_transferControllerErrorInfo_t *errorInfo)
{
    gMmwMssMCB.EDMA_transferControllerErrorInfo = *errorInfo;
    MmwDemo_debugAssert(0);
}

/**************************************************************************
 *************************** Public Functions *****************************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      Init EDMA.
 */
void MmwDemo_edmaInit(void)
{
    int32_t errorCode;

    errorCode = EDMA_init(MMW_LVDS_STREAM_EDMA_INSTANCE);
    if (errorCode != EDMA_NO_ERROR)
    {
        System_printf ("Debug: EDMA instance %d initialization returned error %d\n", errorCode);
        MmwDemo_debugAssert (0);
        return;
    }

    memset(&gMmwMssMCB.EDMA_errorInfo, 0, sizeof(gMmwMssMCB.EDMA_errorInfo));
    memset(&gMmwMssMCB.EDMA_transferControllerErrorInfo, 0, sizeof(gMmwMssMCB.EDMA_transferControllerErrorInfo));
}

/**
 *  @b Description
 *  @n
 *      Open EDMA.
 */
void MmwDemo_edmaOpen(void)
{
    int32_t             errCode;
    EDMA_instanceInfo_t edmaInstanceInfo;
    EDMA_errorConfig_t  errorConfig;
    uint8_t             tc;

    gMmwMssMCB.edmaHandle = EDMA_open(MMW_LVDS_STREAM_EDMA_INSTANCE, &errCode, &edmaInstanceInfo);
    gMmwMssMCB.numEdmaEventQueues = edmaInstanceInfo.numEventQueues;

    if (gMmwMssMCB.edmaHandle == NULL)
    {
        System_printf("Error: Unable to open the EDMA Instance err:%d\n",errCode);
        MmwDemo_debugAssert (0);
        return;
    }

    errorConfig.isConfigAllEventQueues = true;
    errorConfig.isConfigAllTransferControllers = true;
    errorConfig.isEventQueueThresholdingEnabled = true;
    errorConfig.eventQueueThreshold = EDMA_EVENT_QUEUE_THRESHOLD_MAX;
    errorConfig.isEnableAllTransferControllerErrors = true;

    gMmwMssMCB.isPollEdmaError = false;
    if (edmaInstanceInfo.isErrorInterruptConnected == true)
    {
        errorConfig.callbackFxn = MmwDemo_EDMA_errorCallbackFxn;
    }
    else
    {
        errorConfig.callbackFxn = NULL;
        gMmwMssMCB.isPollEdmaError = true;
    }

    errorConfig.transferControllerCallbackFxn = MmwDemo_EDMA_transferControllerErrorCallbackFxn;
    gMmwMssMCB.isPollEdmaTransferControllerError = false;

    for(tc = 0; tc < edmaInstanceInfo.numEventQueues; tc++)
    {
        if (edmaInstanceInfo.isTransferControllerErrorInterruptConnected[tc] == false)
        {
            errorConfig.transferControllerCallbackFxn = NULL;
            gMmwMssMCB.isPollEdmaTransferControllerError = true;
            break;
        }
    }

    if ((errCode = EDMA_configErrorMonitoring(gMmwMssMCB.edmaHandle, &errorConfig)) != EDMA_NO_ERROR)
    {
        //System_printf("Error: EDMA_configErrorMonitoring() failed with errorCode = %d\n", errCode);
        MmwDemo_debugAssert (0);
        return;
    }
}

/**
 *  @b Description
 *  @n
 *      Checks for EDMA errors, used on devices where error interrupts are not connected
 *      to the CPU. Current use case is for LVDS, note it is not very useful to
 *      check for edma errors within the CBUFF session completion interrupts
 *      because they will not happen if edma had errors. So this API should be
 *      called at opportune places in the application code, typically at some time
 *      later than the triggering of the session when it is roughly expected that
 *      the session would have completed by that time.
 */
void MmwDemo_checkEdmaErrors(void)
{
    int32_t     errCode;
    bool        isAnyError;

    if (gMmwMssMCB.isPollEdmaError == true)
    {
        errCode = EDMA_getErrorStatus(gMmwMssMCB.edmaHandle, &isAnyError, &gMmwMssMCB.EDMA_errorInfo);
        if (errCode != EDMA_NO_ERROR)
        {
            System_printf("Error: EDMA_getErrorStatus() failed with error code = %d\n", errCode);
            MmwDemo_debugAssert(0);
        }

        if (isAnyError == true)
        {
            System_printf("EDMA channel controller has errors, see gMmwMssMCB.EDMA_errorInfo\n");
            MmwDemo_debugAssert(0);
        }
    }

    if (gMmwMssMCB.isPollEdmaTransferControllerError == true)
    {
        uint8_t tc;

        for(tc = 0; tc < gMmwMssMCB.numEdmaEventQueues; tc++)
        {
            errCode = EDMA_getTransferControllerErrorStatus(gMmwMssMCB.edmaHandle, tc,
                           &isAnyError, &gMmwMssMCB.EDMA_transferControllerErrorInfo);
            if (errCode != EDMA_NO_ERROR)
            {
                System_printf("Error: EDMA_getTransferControllerErrorStatus() failed with error code = %d\n", errCode);
                MmwDemo_debugAssert(0);
            }

            if (isAnyError == true)
            {
                System_printf("EDMA Transfer Controller instance %d has errors, see gMmwMssMCB.EDMA_transferControllerErrorInfo\n", tc);
                MmwDemo_debugAssert(0);
            }
        }
    }
}
