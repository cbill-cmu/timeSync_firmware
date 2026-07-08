#ifndef MMW_DSS_H
#define MMW_DSS_H

#include <ti/sysbios/knl/Task.h>

#include <ti/common/mmwave_error.h>
#include <ti/drivers/soc/soc.h>
#include <ti/drivers/soc/soc.h>
#include <ti/drivers/mailbox/mailbox.h>
#include <ti/drivers/hwa/hwa.h>
#include <ti/drivers/edma/edma.h>
#include <ti/drivers/osal/DebugP.h>

#include <ti/demo/xwr18xx/mmw/include/mmw_output.h>
#include <ti/datapath/dpc/objectdetection/objdethwa/objectdetection.h>

/* This is used to resolve RL_MAX_SUBFRAMES, TODO: wired */
#include <ti/control/mmwavelink/mmwavelink.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MmwDemo_DataPathObj_t
{
    /*! @brief Handle to hardware accelerator driver. */
    HWA_Handle          hwaHandle;

    /*! @brief dpm Handle */
    DPM_Handle          objDetDpmHandle;

    /*! @brief   Handle of the EDMA driver. */
    EDMA_Handle         edmaHandle[EDMA_NUM_CC];

    /*! @brief   EDMA error Information when there are errors like missing events */
    EDMA_errorInfo_t    EDMA_errorInfo;

    /*! @brief EDMA transfer controller error information. */
    EDMA_transferControllerErrorInfo_t EDMA_transferControllerErrorInfo;

    /*! @brief          Processing Stats */
    MmwDemo_output_message_stats   subFrameStats[RL_MAX_SUBFRAMES];
} MmwDemo_DataPathObj;

/**
 * @brief
 *  Millimeter Wave Demo MCB
 *
 * @details
 *  The structure is used to hold all the relevant information for the
 *  Millimeter Wave demo
 */
typedef struct MmwDemo_DSS_MCB_t
{
    /*! * @brief   Handle to the SOC Module */
    SOC_Handle                  socHandle;

    /*! @brief     DPM Handle */
    Task_Handle                 objDetDpmTaskHandle;

    /*! @brief     init Task Handle */
    Task_Handle                 initTaskHandle;

    /*! @brief     Data Path object */
    MmwDemo_DataPathObj         dataPathObj;
} MmwDemo_DSS_MCB;


/**************************************************************************
 *************************** Extern Definitions ***************************
 **************************************************************************/
extern void MmwDemo_dataPathInit(MmwDemo_DataPathObj *obj);
extern void MmwDemo_dataPathOpen(MmwDemo_DataPathObj *obj);
extern void MmwDemo_dataPathClose(MmwDemo_DataPathObj *obj);

/* Sensor Management Module Exported API */
extern void _MmwDemo_debugAssert(int32_t expression, const char *file, int32_t line);
#define MmwDemo_debugAssert(expression) {                                      \
                                         DebugP_assert(expression);             \
                                        }
                                        
#ifdef __cplusplus
}
#endif

#endif /* MMW_DSS_H */

