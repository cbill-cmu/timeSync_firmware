/* Standard Include Files. */
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* BIOS/XDC Include Files. */
#include <xdc/std.h>
#include <xdc/cfg/global.h>
#include <xdc/runtime/IHeap.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Memory.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/heaps/HeapBuf.h>
#include <ti/sysbios/heaps/HeapMem.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/utils/Load.h>
#include <ti/sysbios/family/c64p/Hwi.h>

/* mmWave SDK Include Files: */
#include <ti/common/sys_common.h>
#include <ti/common/mmwave_sdk_version.h>
#include <ti/control/dpm/dpm.h>
#include <ti/datapath/dpc/objectdetection/objdethwa/objectdetection.h>
#include <ti/drivers/soc/soc.h>
#include <ti/drivers/esm/esm.h>
#include <ti/drivers/osal/DebugP.h>
#include <ti/utils/mathutils/mathutils.h>

/* Demo Include Files */
#include <ti/demo/utils/mmwdemo_rfparser.h>
#include <ti/demo/xwr18xx/mmw/include/mmw_output.h>
#include <ti/demo/xwr18xx/mmw/include/mmw_config.h>
#include <ti/demo/xwr18xx/mmw/dss/mmw_dss.h>
#include <ti/utils/cycleprofiler/cycle_profiler.h>

/**
 * @brief Task Priority settings:
 */
#define MMWDEMO_DPC_OBJDET_DPM_TASK_PRIORITY      5

/*! L3 RAM buffer for object detection DPC */
uint8_t gMmwL3[SOC_L3RAM_SIZE];
#ifndef SOC_XWR68XX
/* EDMA 4K silicon bug related : Align heap to 4K address boundary so that
 * non heap related changes (such as program code) does not alter the 4K related
 * behavior */
#pragma DATA_ALIGN(gMmwL3, 4096U);
#endif
#pragma DATA_SECTION(gMmwL3, ".l3ram");

 /*! L2 RAM buffer for object detection DPC */
#define MMWDEMO_OBJDET_L2RAM_SIZE (49U * 1024U)
uint8_t gDPC_ObjDetL2Heap[MMWDEMO_OBJDET_L2RAM_SIZE];
#ifndef SOC_XWR68XX
/* EDMA 4K silicon bug related : Align heap to 4K address boundary so that
 * non heap related changes (such as program code) does not alter the 4K related
 * behavior */
#pragma DATA_ALIGN(gDPC_ObjDetL2Heap, 4096U);
#endif
#pragma DATA_SECTION(gDPC_ObjDetL2Heap, ".dpc_l2Heap");

 /*! HSRAM for processing results */
#pragma DATA_SECTION(gHSRAM, ".demoSharedMem");
#pragma DATA_ALIGN(gHSRAM, 4);

/**************************************************************************
 *************************** Global Definitions ***************************
 **************************************************************************/

/**
 * @brief
 *  Global Variable for tracking information required by the mmw Demo
 */
MmwDemo_DSS_MCB    gMmwDssMCB;

/**
 * @brief
 *  Global Variable for DPM result buffer
 */
DPM_Buffer  resultBuffer;

/**
 * @brief
 *  Global Variable for HSRAM buffer used to share results to remote
 */
MmwDemo_HSRAM gHSRAM;

/**************************************************************************
 ******************* Millimeter Wave Demo Functions Prototype *******************
 **************************************************************************/
static void MmwDemo_dssInitTask(UArg arg0, UArg arg1);
static void MmwDemo_DPC_ObjectDetection_reportFxn
(
    DPM_Report  reportType,
    uint32_t    instanceId,
    int32_t     errCode,
    uint32_t    arg0,
    uint32_t    arg1
);
static void MmwDemo_DPC_ObjectDetection_dpmTask(UArg arg0, UArg arg1);

/**************************************************************************
 ************************* Millimeter Wave Demo Functions **********************
 **************************************************************************/

/* DPM requires a report callback to be registered; this build doesn't need
 * to react to any DPC report type, so it's a no-op. */
static void MmwDemo_DPC_ObjectDetection_reportFxn
(
    DPM_Report  reportType,
    uint32_t    instanceId,
    int32_t     errCode,
    uint32_t    arg0,
    uint32_t    arg1
) { return; }


/**
 *  @b Description
 *  @n
 *      DPM Execution Task. DPM execute results are processed here:
 *      a) Update states based on timestamp from DPC.
 *      b) Copy results to shared memory to be shared with MSS.
 *      c) Send Results to MSS by calling DPM_sendResult()
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_DPC_ObjectDetection_dpmTask(UArg arg0, UArg arg1)
{
    int32_t     retVal;

    while (1)
    {
        /* Execute the DPM module: */
        retVal = DPM_execute (gMmwDssMCB.dataPathObj.objDetDpmHandle, &resultBuffer);
        if (retVal < 0) {
            System_printf ("Error: DPM execution failed [Error code %d]\n", retVal);
            MmwDemo_debugAssert (0);
        }
        else
        {
            if ((resultBuffer.size[0] == sizeof(DPC_ObjectDetection_ExecuteResult)))
            {
                /* DPM_sendResult must be called even though this build doesn't
                 * use the result content -- something downstream in the
                 * DPM/mmwave control flow blocks waiting on it. */
                DPM_sendResult(gMmwDssMCB.dataPathObj.objDetDpmHandle, true, &resultBuffer);
            }
        }
    }
}

/**
 *  @b Description
 *  @n
 *      System Initialization Task which initializes the various
 *      components in the system.
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_dssInitTask(UArg arg0, UArg arg1)
{
    int32_t             errCode;
    Task_Params         taskParams;
    DPM_InitCfg         dpmInitCfg;
    DPC_ObjectDetection_InitParams      objDetInitParams;
    uint32_t            edmaCCIdx;

    /*****************************************************************************
     * Driver Init:
     *****************************************************************************/

    /* Initialize the Mailbox */
    Mailbox_init(MAILBOX_TYPE_DSS);

    /*****************************************************************************
     * Driver Open/Configuraiton:
     *****************************************************************************/

    /* Initialize the Data Path: */
    MmwDemo_dataPathInit(&gMmwDssMCB.dataPathObj);
    MmwDemo_dataPathOpen(&gMmwDssMCB.dataPathObj);

    /*****************************************************************************
     * Initialization of the DPM Module:
     *****************************************************************************/
    memset ((void *)&objDetInitParams, 0, sizeof(DPC_ObjectDetection_InitParams));

    /* Note this must be after MmwDemo_dataPathOpen() above which opens the hwa */
    objDetInitParams.hwaHandle = gMmwDssMCB.dataPathObj.hwaHandle;
    objDetInitParams.L3ramCfg.addr = (void *)&gMmwL3[0];
    objDetInitParams.L3ramCfg.size = sizeof(gMmwL3);
    objDetInitParams.CoreLocalRamCfg.addr = &gDPC_ObjDetL2Heap[0];
    objDetInitParams.CoreLocalRamCfg.size = sizeof(gDPC_ObjDetL2Heap);
    for (edmaCCIdx = 0; edmaCCIdx < EDMA_NUM_CC; edmaCCIdx++)
    {
        objDetInitParams.edmaHandle[edmaCCIdx] = gMmwDssMCB.dataPathObj.edmaHandle[edmaCCIdx];
    }

    /* DPC Call-back config */
    objDetInitParams.processCallBackCfg.processFrameBeginCallBackFxn = NULL;
    objDetInitParams.processCallBackCfg.processInterFrameBeginCallBackFxn = NULL;

    memset ((void *)&dpmInitCfg, 0, sizeof(DPM_InitCfg));

    /* Setup the configuration: */
    dpmInitCfg.socHandle        = gMmwDssMCB.socHandle;
    dpmInitCfg.ptrProcChainCfg  = &gDPC_ObjectDetectionCfg;
    dpmInitCfg.instanceId       = 0xFEEDFEED;
    dpmInitCfg.domain           = DPM_Domain_REMOTE;
    dpmInitCfg.reportFxn        = MmwDemo_DPC_ObjectDetection_reportFxn;
    dpmInitCfg.arg              = &objDetInitParams;
    dpmInitCfg.argSize          = sizeof(DPC_ObjectDetection_InitParams);

    /* Initialize the DPM Module: */
    gMmwDssMCB.dataPathObj.objDetDpmHandle = DPM_init (&dpmInitCfg, &errCode);
    if (gMmwDssMCB.dataPathObj.objDetDpmHandle == NULL)
    {
        System_printf ("Error: Unable to initialize the DPM Module [Error: %d]\n", errCode);
        MmwDemo_debugAssert (0);
        return;
    }

    /* Synchronization: This will synchronize the execution of the control module
     * between the domains. This is a prerequiste and always needs to be invoked. */
    while (1)
    {
        int32_t syncStatus;

        /* Get the synchronization status: */
        syncStatus = DPM_synch (gMmwDssMCB.dataPathObj.objDetDpmHandle, &errCode);
        if (syncStatus < 0)
        {
            /* Error: Unable to synchronize the framework */
            System_printf ("Error: DPM Synchronization failed [Error code %d]\n", errCode);
            MmwDemo_debugAssert (0);
            return;
        }
        if (syncStatus == 1)
        {
            /* Synchronization acheived: */
            break;
        }
        /* Sleep and poll again: */
        Task_sleep(1);
    }
    System_printf ("Debug: DPM Module Sync is done\n");

    /* Launch the DPM Task */
    Task_Params_init(&taskParams);
    taskParams.priority  = MMWDEMO_DPC_OBJDET_DPM_TASK_PRIORITY;
    taskParams.stackSize = 4*1024;
    gMmwDssMCB.objDetDpmTaskHandle = Task_create(MmwDemo_DPC_ObjectDetection_dpmTask, &taskParams, NULL);

    return;
}

/**
 *  @b Description
 *  @n
 *     Function to sleep the DSP using IDLE instruction.
 *     When DSP has no work left to do,
 *     the BIOS will be in Idle thread and will call this function.
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_sleep(void)
{
    /* issue WFI (Wait For Interrupt) instruction */
    asm(" IDLE ");
}

/**
 *  @b Description
 *  @n
 *      Entry point into the Millimeter Wave Demo
 *
 *  @retval
 *      Not Applicable.
 */
int main (void)
{
    Task_Params     taskParams;
    int32_t         errCode;
    SOC_Handle      socHandle;
    SOC_Cfg         socCfg;

    /* Initialize and populate the demo MCB */
    memset ((void*)&gMmwDssMCB, 0, sizeof(MmwDemo_DSS_MCB));

    /* Initialize the SOC confiugration: */
    memset ((void *)&socCfg, 0, sizeof(SOC_Cfg));

    /* Populate the SOC configuration: */
    socCfg.clockCfg = SOC_SysClock_BYPASS_INIT;

    /* Initialize the SOC Module: This is done as soon as the application is started
     * to ensure that the MPU is correctly configured. */
    socHandle = SOC_init (&socCfg, &errCode);
    if (socHandle == NULL)
    {
        System_printf ("Error: SOC Module Initialization failed [Error code %d]\n", errCode);
        MmwDemo_debugAssert (0);
        return -1;
    }

    gMmwDssMCB.socHandle = socHandle;

    /* Initialize the Task Parameters. */
    Task_Params_init(&taskParams);
    taskParams.stackSize = 4*1024;
    gMmwDssMCB.initTaskHandle = Task_create(MmwDemo_dssInitTask, &taskParams, NULL);

    /* Start BIOS */
    BIOS_start();
    return 0;
}
