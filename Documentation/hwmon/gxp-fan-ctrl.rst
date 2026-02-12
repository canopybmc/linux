.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver gxp-fan-ctrl
==========================

Supported chips:

  * HPE GXP SOC

Author: Nick Hawkins <nick.hawkins@hpe.com>


Description
-----------

gxp-fan-ctrl is a driver which provides fan control for the hpe gxp soc.
The driver allows the gathering of fan status and the use of fan
PWM control.


Sysfs attributes
----------------

======================= ===========================================================
fan[1-8]_input		Fan 1 to 8 respective PWM duty cycle (0-255).
			The GXP has no tachometer; this reports PWM drive level.
fan[1-8]_fault		Fan 1 to 8 respective fault status: 1 fail, 0 ok
pwm[0-7]		Fan 0 to 7 respective PWM value (0-255)
pwm[0-7]_enable         Fan 0 to 7 respective enabled status: 1 enabled, 0 disabled
======================= ===========================================================
