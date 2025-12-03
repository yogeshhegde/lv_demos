/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _EVSE_H_
#define _EVSE_H_

/* EV Charging Use-case UI
 *
 * Copyright (c) 2025 Texas Instruments
 */

/**
* Toggles the state of the EV charging.
*
* @param state The new state of the EV charging,
*              0: Pause Charging, 1: Resume Charging
*
* @return void
*/
void toggle_ev_charging(int state);

#endif /* _EVSE_H_ */
