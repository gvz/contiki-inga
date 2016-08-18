/**
 * \file
 * \brief  A uDTN time synchronization
 * \author
 *         Georg von Zengen <vonzengen@ibr.cs.tu-bs.de>
 */
#ifndef __DT_TIMESYNC_H__
#define __DT_TIMESYNC_H__


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
*   \brief Initializes the time synchronization
*
*/
void timesync_init(void);
/**
*   \brief activates pairing mode
*
*/
void timesync_activate_pairing(void);
/**
*   \brief deactivates pairing mode
*
*/
void timesync_deactivate_pairing(void);
/**
*   \brief get the interval of synchornization beacons
*
*   \returns interval in seconds
*/
uint16_t timesync_get_interval(void);
/**
*   \brief sets the interval of synchornization beacons
*
*   \param interval in seconds
*/
void timesync_set_interval(uint16_t sec);

#endif //__DT_TIMESYNC_H__


