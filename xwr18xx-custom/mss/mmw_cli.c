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

MMWave_CtrlCfg ctrlCfg;
MMWave_OpenCfg openCfg;
static int32_t tmpChirpCfg(int idx) {
    int32_t errCode;

    double mmwave_freq_scale_factor = SOC_getDeviceRFFreqScaleFactor(
        gMmwMssMCB.socHandle, &errCode);
    if (errCode != 0) { return errCode; }

    rlChirpCfg_t chirpCfg;
    memset ((void *)&chirpCfg, 0, sizeof(rlChirpCfg_t));

    chirpCfg.chirpStartIdx   = idx;
    chirpCfg.chirpEndIdx     = idx;
    chirpCfg.profileId       = 0;  // use chirp ID 0 for now

    /* Translate from Hz to number of [1 LSB = (gCLI_mmwave_freq_scale_factor * 1e9) / 2^26 Hz]
     * units of mmwavelink format */
    chirpCfg.startFreqVar = (uint32_t) (0.0 * (1U << 26) / (mmwave_freq_scale_factor * 1e9));

    /* Translate from KHz/us to number of [1 LSB = (gCLI_mmwave_freq_scale_factor * 1e6) * 900 /2^26 KHz/us]
     * units of mmwavelink format */
    chirpCfg.freqSlopeVar    = (uint16_t) (0.0 * (1U << 26) / (mmwave_freq_scale_factor * 1e6 * 900.0));

    /* Translate from us to [1 LSB = 10ns] units of mmwavelink format */
    chirpCfg.idleTimeVar     = (uint32_t)(0.0 * 1000.0 / 10.0);

    /* Translate from us to [1 LSB = 10ns] units of mmwavelink format */
    chirpCfg.adcStartTimeVar = (uint32_t)(0.0 * 1000.0 / 10.0);

    chirpCfg.txEnable        = 1 << idx;

    MMWave_ProfileHandle profileHandle2;
    if (MMWave_getProfileHandle (gMmwMssMCB.ctrlHandle, chirpCfg.profileId,
                                 &profileHandle2, &errCode) < 0)
    {
        return errCode;
    }
    if (MMWave_addChirp (profileHandle2, &chirpCfg, &errCode) == NULL)
    {
        return errCode;
    }

    return 0;
}


static int32_t tmpInitCfg() {
    int32_t errCode;

    ctrlCfg.dfeDataOutputMode = MMWave_DFEDataOutputMode_FRAME;  // =1

    memset((void *)&openCfg.adcOutCfg, 0, sizeof(rlAdcOutCfg_t));
    openCfg.adcOutCfg.fmt.b2AdcBits = 2;
    openCfg.adcOutCfg.fmt.b2AdcOutFmt = 1;
 
    memset((void *)&openCfg.lowPowerMode, 0, sizeof(rlLowPowerModeCfg_t));
    openCfg.lowPowerMode.lpAdcMode = 0; // regular mode

    memset((void *)&openCfg.chCfg, 0, sizeof(rlChanCfg_t));
    openCfg.chCfg.rxChannelEn = 0b1111;
    openCfg.chCfg.txChannelEn = 0b111;
    openCfg.chCfg.cascading = 0;  // single chip

    double mmwave_freq_scale_factor = SOC_getDeviceRFFreqScaleFactor(
        gMmwMssMCB.socHandle, &errCode);
    if (errCode != 0) { return errCode; }

    /// frame cfg
    memset ((void *)&ctrlCfg.u.frameCfg.frameCfg, 0, sizeof(rlFrameCfg_t));
    ctrlCfg.u.frameCfg.frameCfg.chirpStartIdx      = 0;
    ctrlCfg.u.frameCfg.frameCfg.chirpEndIdx        = 2;
    ctrlCfg.u.frameCfg.frameCfg.numLoops           = 128;
    ctrlCfg.u.frameCfg.frameCfg.numFrames          = 0;
    ctrlCfg.u.frameCfg.frameCfg.framePeriodicity   = (uint32_t)(125.0 * 1000000 / 5);
    ctrlCfg.u.frameCfg.frameCfg.triggerSelect      = 1;  // software trigger
    ctrlCfg.u.frameCfg.frameCfg.frameTriggerDelay  = (uint32_t)(0.0 * 1000000 / 5);

    rlProfileCfg_t profileCfg;
    memset ((void *)&profileCfg, 0, sizeof(rlProfileCfg_t));

    profileCfg.profileId = 0;  // profile ID 0

    // Translate from GHz to [1 LSB = gCLI_mmwave_freq_scale_factor * 1e9 / 2^26 Hz] units
    // of mmwavelink format
    profileCfg.startFreqConst = (uint32_t) (77.0 * (1U << 26) / mmwave_freq_scale_factor);

    /* Translate below times from us to [1 LSB = 10 ns] units of mmwavelink format */
    profileCfg.idleTimeConst         = (uint32_t)(200.0 * 1000 / 10);
    profileCfg.adcStartTimeConst     = (uint32_t)(5.7 * 1000 / 10);
    profileCfg.rampEndTime           = (uint32_t)(59.0 * 1000 / 10);

    profileCfg.txOutPowerBackoffCode = 0; // no backoff
    profileCfg.txPhaseShifter        = 0; // no phase shifter

    /* Translate from MHz/us to [1 LSB = (gCLI_mmwave_freq_scale_factor * 1e6 * 900) / 2^26 kHz/uS]
     * units of mmwavelink format */
    profileCfg.freqSlopeConst = (int16_t)(
        67.012 * (1U << 26) / ((mmwave_freq_scale_factor * 1e3) * 900.0));

    /* Translate from us to [1 LSB = 10 ns] units of mmwavelink format */
    profileCfg.txStartTime           = (int32_t)(1.0 * 1000 / 10);

    profileCfg.numAdcSamples         = 128;   // 128 samples
    profileCfg.digOutSampleRate      = 2500;  // 2500Ksps
    profileCfg.hpfCornerFreq1        = 0;     // 175KHz
    profileCfg.hpfCornerFreq2        = 0;     // 350KHz
    profileCfg.rxGain                = 30;    // 30dB

    MMWave_ProfileHandle profileHandle = MMWave_addProfile (gMmwMssMCB.ctrlHandle, &profileCfg, &errCode);
    if (profileHandle == NULL) { return errCode; }

    ctrlCfg.u.frameCfg.profileHandle[profileCfg.profileId] = profileHandle;
    // ptrBaseCfgProfileHandle = ctrlCfg->u.advancedFrameCfg.profileHandle[0U];

    int i;
    for(i = 0; i <= 2; i++) {
        tmpChirpCfg(i);
    }

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


static int32_t MmwDemo_CLISensorStart (int32_t argc, char* argv[])
{
    int32_t     retVal = 0;

    if ((gMmwMssMCB.sensorState != MmwDemo_SensorState_STOPPED) &&
        (gMmwMssMCB.sensorState != MmwDemo_SensorState_INIT) &&
        (gMmwMssMCB.sensorState != MmwDemo_SensorState_OPENED))
    {
        MmwDemo_stopSensor();
        gMmwMssMCB.sensorState = MmwDemo_SensorState_STOPPED;
    }

    // ADC Buffers
    MmwDemo_ADCBufCfg   adcBufCfg;
    memset ((void *)&adcBufCfg, 0, sizeof(adcBufCfg));
    adcBufCfg.adcFmt          = 0;  // COMPLEX_1X
    adcBufCfg.iqSwapSel       = 1;  // MSB_LSB_IQ
    adcBufCfg.chInterleave    = 1;  // non-interleaved
    adcBufCfg.chirpThreshold  = 1;  // ???
    MmwDemo_CfgUpdate((void *)&adcBufCfg,
                      MMWDEMO_ADCBUFCFG_OFFSET,
                      sizeof(MmwDemo_ADCBufCfg), -1); // all subframes

    // LVDS Stream
    MmwDemo_LvdsStreamCfg   cfg;
    memset ((void *)&cfg, 0, sizeof(MmwDemo_LvdsStreamCfg));
    cfg.isHeaderEnabled = false;
    cfg.dataFmt         = 1;  // MMW_DEMO_LVDS_STREAM_CFG_DATAFMT_ADC
    cfg.isSwEnabled     = false;
    MmwDemo_CfgUpdate((void *)&cfg,
                      MMWDEMO_LVDSSTREAMCFG_OFFSET,
                      sizeof(MmwDemo_LvdsStreamCfg), -1); // -1= all subframes

    retVal = tmpInitCfg();
    if (retVal != 0) { return retVal; }

    /***********************************************************************************
     * Do sensor state management to influence the sensor actions
     ***********************************************************************************/

    /* Error checking initial state: no partial config is allowed 
       until the first sucessful sensor start state */
    if ((gMmwMssMCB.sensorState == MmwDemo_SensorState_INIT) || 
         (gMmwMssMCB.sensorState == MmwDemo_SensorState_OPENED))
    {
        /* need to get number of sub-frames so that next function to check
         * pending state can work */
        gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.numSubFrames =
            MmwDemo_RFParser_getNumSubFrames(&ctrlCfg);
    }

    /* need to get number of sub-frames so that next function to check
        * pending state can work */
    gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.numSubFrames =
        MmwDemo_RFParser_getNumSubFrames(&ctrlCfg);

    /***********************************************************************************
     * Retreive and check mmwave Open related config before calling openSensor
     ***********************************************************************************/
    /* Get the open configuration: */
    memcpy(&gMmwMssMCB.cfg.openCfg, &openCfg, sizeof(MMWave_OpenCfg));
    /* call sensor open */
    retVal = MmwDemo_openSensor(true);
    if(retVal != 0) { return -1; }
    gMmwMssMCB.sensorState = MmwDemo_SensorState_OPENED;

    /***********************************************************************************
     * Retrieve mmwave Control related config before calling startSensor
     ***********************************************************************************/
    /* if MmwDemo_openSensor has non-first time related processing, call here again*/
    /* call sensor config */
    memcpy(&gMmwMssMCB.cfg.ctrlCfg, &ctrlCfg, sizeof(MMWave_CtrlCfg));
    retVal = MmwDemo_configSensor();
    if(retVal != 0) { return -1; }

    retVal = MmwDemo_startSensor();
    if(retVal != 0) { return -1; }

    /***********************************************************************************
     * Set the state
     ***********************************************************************************/
    gMmwMssMCB.sensorState = MmwDemo_SensorState_STARTED;
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Execution Task
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_CLIInit (uint8_t taskPriority)
{
    memset ((void *)&ctrlCfg, 0, sizeof(MMWave_CtrlCfg));
    memset ((void *)&openCfg, 0, sizeof(MMWave_OpenCfg));

    /* Sanity check UART handle */
    if (gMmwMssMCB.commandUartHandle == NULL)
    {
        /* Ensure UART is open (it should be from mss_main.c) */
        return;
    }

    /* Manually start the sensor since we are not using CLI */
    MmwDemo_CLISensorStart(0, NULL);
    return;
}
