/* EDMA interface */
#ifndef MMW_EDMA_H
#define MMW_EDMA_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  @b Description
 *  @n
 *      Initialize EDMA instance
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_edmaInit(void);

/**
 *  @b Description
 *  @n
 *      Open EDMA driver with error monitoring configuration
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_edmaOpen(void);

/**
 *  @b Description
 *  @n
 *      Check for EDMA errors (polling-based for devices without error interrupts)
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_checkEdmaErrors(void);

#ifdef __cplusplus
}
#endif

#endif /* MMW_EDMA_H */
