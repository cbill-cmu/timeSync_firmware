#ifndef MSS_LVDS_STREAM_H
#define MSS_LVDS_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ti/drivers/cbuff/cbuff.h>
#include <ti/utils/hsiheader/hsiheader.h>

/**
 * @brief   This is the maximum number of EDMA Channels which is used by
 * the HW Session
 */
#define MMWDEMO_LVDS_STREAM_HW_SESSION_MAX_EDMA_CHANNEL             11U


/**
 * @brief
 *  LVDS streaming MCB
 *
 * @details
 *  The LVDS streaming MCB.
 */
typedef struct MmwDemo_LVDSStream_MCB
{
    /**
    * @brief   Handle to the CBUFF Driver
    */
    CBUFF_Handle             cbuffHandle;

    /**
     * @brief   EDMA Channel Allocator Index for the HW Session
     */
    uint8_t                  hwSessionEDMAChannelAllocatorIndex;

    /**
     * @brief   EDMA Channel Resource Table: This is used for creating the CBUFF Session.
     */
    CBUFF_EDMAChannelCfg     hwSessionEDMAChannelTable[MMWDEMO_LVDS_STREAM_HW_SESSION_MAX_EDMA_CHANNEL];

    /**
     * @brief   HW session HSI header.
     */
    HSIHeader                hwSessionHSIHeader;

    /**
     * @brief   True if hw session HSI header is allocated, false otherwise
     */
    bool                     isHwSessionHSIHeaderAllocated;
    
    /**
     * @brief   Handle to the HW CBUFF Session Handle.
     */
    CBUFF_SessionHandle      hwSessionHandle;
    
    /**
     * @brief   Number of HW frame done interrupt received.
     */
    uint16_t                 hwFrameDoneCount;
    
    /**
     * @brief   Semaphore handle to signal hw session done.
     */
    Semaphore_Handle         hwFrameDoneSemHandle;
} MmwDemo_LVDSStream_MCB_t;

int32_t MmwDemo_LVDSStreamInit (void);
int32_t MmwDemo_LVDSStreamHwConfig (uint8_t subFrameIndx);
void MmwDemo_configLVDSHwData(uint8_t subFrameIndx);
void MmwDemo_LVDSStreamDeleteHwSession (void);

#ifdef __cplusplus
}
#endif

#endif
