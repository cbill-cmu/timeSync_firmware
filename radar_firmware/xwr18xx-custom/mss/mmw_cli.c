/**************************************************************************
 *************************** Include Files ********************************
 **************************************************************************/

/* Standard Include Files. */
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* BIOS/XDC Include Files. */
#include <xdc/runtime/System.h>

/* mmWave SDK Include Files: */
#include <ti/common/sys_common.h>
#include <ti/common/mmwave_sdk_version.h>
#include <ti/drivers/uart/UART.h>
#include <ti/control/mmwavelink/mmwavelink.h>
#include <ti/utils/cli/cli.h>
#include <ti/utils/mathutils/mathutils.h>

/* Demo Include Files */
#include "../include/mmw_config.h"
#include "mmw_mss.h"
#include <ti/demo/utils/mmwdemo_adcconfig.h>
#include <ti/demo/utils/mmwdemo_rfparser.h>

/**************************************************************************
 *************************** Extern Definitions *******************************
 **************************************************************************/

extern MmwDemo_MSS_MCB    gMmwMssMCB;

/**
 *  @b Description
 *  @n
 *      Utility function to apply configuration to specified sub-frame
 *
 *  @param[in] srcPtr Pointer to configuration
 *  @param[in] offset Offset of configuration within the parent structure
 *  @param[in] size   Size of configuration
 *  @param[in] subFrameNum Sub-frame Number (0 based) to apply to, broadcast to
 *                         all sub-frames if special code MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG
 */
static void MmwDemo_CfgUpdate(void *srcPtr, uint32_t offset, uint32_t size, int8_t subFrameNum)
{
    /* if subFrameNum undefined, broadcast to all sub-frames */
    if(subFrameNum == MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG)
    {
        uint8_t  indx;
        for(indx = 0; indx < RL_MAX_SUBFRAMES; indx++)
        {
            memcpy((void *)((uint32_t) &gMmwMssMCB.subFrameCfg[indx] + offset), srcPtr, size);
        }
    }
    else
    {
        /* Apply configuration to specific subframe (or to position zero for the legacy case
           where there is no advanced frame config) */
        memcpy((void *)((uint32_t) &gMmwMssMCB.subFrameCfg[subFrameNum] + offset), srcPtr, size);
    }
}

/**
 *  @b Description
 *  @n
 *      Utility function used by many CLI command handlers to parse and
 *      validate the leading <subFrameIdx> argument.
 *
 *  @param[in] argc Number of arguments
 *  @param[in] argv Arguments
 *  @param[in] expectedArgc Expected number of arguments for the calling command
 *  @param[out] subFrameNum Parsed sub-frame index
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIGetSubframe(int32_t argc, char* argv[], int32_t expectedArgc,
                                       int8_t* subFrameNum)
{
    int8_t subframe;

    if (argc != expectedArgc)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Subframe info is always in position 1 */
    subframe = (int8_t) atoi(argv[1]);

    if (subframe >= (int8_t)RL_MAX_SUBFRAMES)
    {
        CLI_write ("Error: Subframe number is invalid\n");
        return -1;
    }

    *subFrameNum = subframe;
    return 0;
}

/* Minimal CLI Write implementation */
void MmwDemo_CLI_write (const char* format, ...)
{
    va_list     arg;
    char        logMessage[256];

    /* Zero the buffer first: guarantees a real null terminator even if
     * vsnprintf doesn't reliably terminate on this runtime, so strlen()
     * below can't walk into stale stack memory past the real message. */
    memset(logMessage, 0, sizeof(logMessage));

    /* Format the message: */
    va_start (arg, format);
    vsnprintf (&logMessage[0], sizeof(logMessage), format, arg);
    va_end (arg);

    /* Log the message on the UART CLI console: */
    if (gMmwMssMCB.commandUartHandle != NULL)
    {
        /* Use strlen() rather than vsnprintf's return value: on this runtime
         * the return value is not reliably the formatted length, which was
         * sending a few bytes of stale stack memory past the real string. */
        UART_writePolling (gMmwMssMCB.commandUartHandle, (uint8_t*)&logMessage[0],
                            strlen(logMessage));
    }
}


/**
 *  @b Description
 *  @n
 *      CLI Handler for "adcbufCfg". Direct port of the equivalent handler
 *      in the stock mmwave_sdk demo (packages/ti/demo/xwr18xx/mmw/mss/mmw_cli.c).
 */
static int32_t MmwDemo_CLIADCBufCfg (int32_t argc, char* argv[])
{
    MmwDemo_ADCBufCfg   adcBufCfg;
    int8_t              subFrameNum;

    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: This command is not allowed after sensor has started\n");
        return 0;
    }

    if (MmwDemo_CLIGetSubframe(argc, argv, 6, &subFrameNum) < 0)
    {
        return -1;
    }

    memset ((void *)&adcBufCfg, 0, sizeof(adcBufCfg));
    adcBufCfg.adcFmt          = (uint8_t) atoi (argv[2]);
    adcBufCfg.iqSwapSel       = (uint8_t) atoi (argv[3]);
    adcBufCfg.chInterleave    = (uint8_t) atoi (argv[4]);
    adcBufCfg.chirpThreshold  = (uint8_t) atoi (argv[5]);

    /* This demo is using HWA for 1D processing which does not allow multi-chirp
     * processing */
    if (adcBufCfg.chirpThreshold != 1)
    {
        CLI_write("Error: chirpThreshold must be 1, multi-chirp is not allowed\n");
        return -1;
    }

    MmwDemo_CfgUpdate((void *)&adcBufCfg,
                      MMWDEMO_ADCBUFCFG_OFFSET,
                      sizeof(MmwDemo_ADCBufCfg), subFrameNum);
    return 0;
}

/**
 *  @b Description
 *  @n
 *      CLI Handler for "lvdsStreamCfg". Direct port of the equivalent handler
 *      in the stock mmwave_sdk demo.
 */
static int32_t MmwDemo_CLILvdsStreamCfg (int32_t argc, char* argv[])
{
    MmwDemo_LvdsStreamCfg   cfg;
    int8_t                  subFrameNum;

    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: This command is not allowed after sensor has started\n");
        return 0;
    }

    if (MmwDemo_CLIGetSubframe(argc, argv, 5, &subFrameNum) < 0)
    {
        return -1;
    }

    memset ((void *)&cfg, 0, sizeof(MmwDemo_LvdsStreamCfg));
    cfg.isHeaderEnabled = (bool)    atoi(argv[2]);
    cfg.dataFmt         = (uint8_t) atoi(argv[3]);
    cfg.isSwEnabled     = (bool)    atoi(argv[4]);

    if ((cfg.isSwEnabled == true) && (cfg.dataFmt != MMW_DEMO_LVDS_STREAM_CFG_DATAFMT_DISABLED))
    {
        if (cfg.isHeaderEnabled == false)
        {
            CLI_write("Error: header must be enabled when both h/w and s/w streaming are enabled\n");
            return -1;
        }
    }

    MmwDemo_CfgUpdate((void *)&cfg,
                      MMWDEMO_LVDSSTREAMCFG_OFFSET,
                      sizeof(MmwDemo_LvdsStreamCfg), subFrameNum);
    return 0;
}

/**
 *  @b Description
 *  @n
 *      Shared handler for CLI commands that are purely cosmetic in this
 *      build (guiMonitor, cfarCfg, etc. -- detection-chain tuning knobs
 *      that don't matter since the DPC/datapath is disabled here, see
 *      MmwDemo_startSensor). We still need to accept and acknowledge them:
 *      xwr.radar's setup() sends them unconditionally and treats anything
 *      other than "Done" as a protocol error.
 */
static int32_t MmwDemo_CLINoOp (int32_t argc, char* argv[])
{
    return 0;
}

/**
 *  @b Description
 *  @n
 *      CLI Handler for "sensorStart". Simplified relative to the stock
 *      demo: this build has no pending-config state machine
 *      (MmwDemo_isAllCfgInPendingState() and friends), so we always
 *      reconfigure from whatever the CLI mmWave extension currently holds.
 *      That matches how xwr.radar drives the board -- setup() always sends
 *      full configuration before start().
 */
static int32_t MmwDemo_CLISensorStart (int32_t argc, char* argv[])
{
    int32_t     retVal;

    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: Sensor is already started\n");
        return 0;
    }

    if ((gMmwMssMCB.sensorState != MmwDemo_SensorState_STOPPED) &&
        (gMmwMssMCB.sensorState != MmwDemo_SensorState_INIT) &&
        (gMmwMssMCB.sensorState != MmwDemo_SensorState_OPENED))
    {
        MmwDemo_stopSensor();
        gMmwMssMCB.sensorState = MmwDemo_SensorState_STOPPED;
    }

    /* Pull in whatever profileCfg/chirpCfg/frameCfg/channelCfg/adcCfg/
     * dfeDataOutputMode/lowPower commands have configured so far -- these
     * are parsed and stored internally by the CLI's mmWave extension. */
    CLI_getMMWaveExtensionConfig (&gMmwMssMCB.cfg.ctrlCfg);
    gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.numSubFrames =
        MmwDemo_RFParser_getNumSubFrames(&gMmwMssMCB.cfg.ctrlCfg);

    /* DEBUG: ground-truth the raw frame config right before it's applied to
     * RF hardware, to rule in/out whether the CLI is actually receiving/
     * storing the frame_period the host sent (vs. a downstream RF-level
     * fallback/rejection we can't see from the CLI layer alone). Raw units
     * are 1 LSB = 5ns, so ms = raw * 5 / 1e6. Remove once resolved. */
    if (gMmwMssMCB.loggingUartHandle != NULL)
    {
        char     dbg[96];
        uint32_t rawPeriod = gMmwMssMCB.cfg.ctrlCfg.u.frameCfg.frameCfg.framePeriodicity;
        snprintf(dbg, sizeof(dbg),
                 "sensorStart: framePeriodicity raw=%lu (%lu.%03lu ms) numLoops=%lu chirpEnd=%d\r\n",
                 (unsigned long)rawPeriod,
                 (unsigned long)((rawPeriod * 5UL) / 1000000UL),
                 (unsigned long)(((rawPeriod * 5UL) % 1000000UL) / 1000UL),
                 (unsigned long)gMmwMssMCB.cfg.ctrlCfg.u.frameCfg.frameCfg.numLoops,
                 gMmwMssMCB.cfg.ctrlCfg.u.frameCfg.frameCfg.chirpEndIdx);
        UART_writePolling(gMmwMssMCB.loggingUartHandle, (uint8_t*)dbg, strlen(dbg));
    }

    if ((gMmwMssMCB.sensorState == MmwDemo_SensorState_INIT) ||
        (gMmwMssMCB.sensorState == MmwDemo_SensorState_OPENED))
    {
        CLI_getMMWaveExtensionOpenConfig (&gMmwMssMCB.cfg.openCfg);
        retVal = MmwDemo_openSensor(true);
        if (retVal != 0) { return -1; }
        gMmwMssMCB.sensorState = MmwDemo_SensorState_OPENED;
    }

    retVal = MmwDemo_configSensor();
    if (retVal != 0) { return -1; }

    retVal = MmwDemo_startSensor();
    if (retVal != 0) { return -1; }

    gMmwMssMCB.sensorState = MmwDemo_SensorState_STARTED;
    return 0;
}

/**
 *  @b Description
 *  @n
 *      CLI Handler for "sensorStop".
 */
static int32_t MmwDemo_CLISensorStop (int32_t argc, char* argv[])
{
    if ((gMmwMssMCB.sensorState == MmwDemo_SensorState_STOPPED) ||
        (gMmwMssMCB.sensorState == MmwDemo_SensorState_INIT) ||
        (gMmwMssMCB.sensorState == MmwDemo_SensorState_OPENED))
    {
        CLI_write ("Ignored: Sensor is already stopped\n");
        return 0;
    }

    MmwDemo_stopSensor();
    gMmwMssMCB.sensorState = MmwDemo_SensorState_STOPPED;
    return 0;
}

/**
 *  @b Description
 *  @n
 *      Sets up and launches the CLI task. Uses the CLI library's mmWave
 *      extension (ti/utils/cli/cli.h) for the standard mmwavelink commands
 *      (dfeDataOutputMode, channelCfg, adcCfg, profileCfg, chirpCfg,
 *      frameCfg, advFrameCfg, lowPower, contModeCfg); everything else
 *      needed by xwr.radar's setup()/start()/stop() sequence is registered
 *      below.
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_CLIInit (uint8_t taskPriority)
{
    CLI_Cfg     cliCfg;
    uint32_t    cnt;
    uint32_t    i;
    /* Cosmetic commands xwr.radar's setup() boilerplate sends but which
     * have no effect in this build -- see MmwDemo_CLINoOp. */
    const char* noOpCmds[] = {
        "guiMonitor", "cfarCfg", "multiObjBeamForming", "calibDcRangeSig",
        "clutterRemoval", "aoaFovCfg", "cfarFovCfg",
        "measureRangeBiasAndRxChanPhase", "extendedMaxVelocity",
        "CQRxSatMonitor", "CQSigImgMonitor", "analogMonitor", "calibData",
        "compRangeBiasAndRxChanPhase"
    };

    /* Sanity check UART handle */
    if (gMmwMssMCB.commandUartHandle == NULL)
    {
        /* Ensure UART is open (it should be from mss_main.c) */
        return;
    }

    memset ((void *)&cliCfg, 0, sizeof(CLI_Cfg));

    cliCfg.cliPrompt              = "mmwDemo:/>";
    cliCfg.cliBanner              = "timeSync custom firmware -- CLI ready\n";
    cliCfg.cliUartHandle          = gMmwMssMCB.commandUartHandle;
    cliCfg.taskPriority           = taskPriority;
    cliCfg.socHandle              = gMmwMssMCB.socHandle;
    cliCfg.mmWaveHandle           = gMmwMssMCB.ctrlHandle;
    cliCfg.enableMMWaveExtension  = 1U;
    cliCfg.usePolledMode          = true;
    cliCfg.overridePlatform       = true;
    cliCfg.overridePlatformString = "xWR18xx_AOP";

    cnt = 0;
    cliCfg.tableEntry[cnt].cmd            = "sensorStart";
    cliCfg.tableEntry[cnt].helpString     = "[doReconfig(unused, always reconfigures)]";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLISensorStart;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "sensorStop";
    cliCfg.tableEntry[cnt].helpString     = "No arguments";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLISensorStop;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "adcbufCfg";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <adcOutputFmt> <SampleSwap> <ChanInterleave> <ChirpThreshold>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIADCBufCfg;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "lvdsStreamCfg";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <enableHeader> <dataFmt> <enableSW>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLILvdsStreamCfg;
    cnt++;

    for (i = 0; i < sizeof(noOpCmds) / sizeof(noOpCmds[0]); i++)
    {
        cliCfg.tableEntry[cnt].cmd            = (char*) noOpCmds[i];
        cliCfg.tableEntry[cnt].helpString     = "No-op in this build";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLINoOp;
        cnt++;
    }

    if (CLI_open (&cliCfg) < 0)
    {
        System_printf ("Error: Unable to open the CLI\n");
        return;
    }
    return;
}
