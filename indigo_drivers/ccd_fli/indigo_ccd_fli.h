// Copyright (c) 2016 Rumen G. Bogdanovski
// All rights reserved.
//
// You can use this software under the terms of 'INDIGO Astronomy
// open-source license' (see LICENSE.md).
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS 'AS IS' AND ANY EXPRESS
// OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// version history
// 2.0 Build 0 - PoC by Rumen Bogdanovski <rumen@skyarchive.org>

/** INDIGO CCD FLI driver
 \file indigo_ccd_fli.h
 */

#ifndef __ccd_fli_h
#define __ccd_fli_h

#include "indigo_driver.h"
#include "indigo_ccd_driver.h"
#include "indigo_guider_driver.h"
#include "indigo_wheel_driver.h"
#include "indigo_focuser_driver.h"

#define CCD_SIMULATOR_IMAGER_CAMERA_NAME		"CCD Imager Simulator"
#define CCD_SIMULATOR_WHEEL_NAME				"CCD Imager Simulator (wheel)"
#define CCD_SIMULATOR_FOCUSER_NAME				"CCD Imager Simulator (focuser)"

#define CCD_SIMULATOR_GUIDER_CAMERA_NAME		"CCD Guider Simulator"
#define CCD_SIMULATOR_GUIDER_NAME				"CCD Guider Simulator (guider)"


/** Create CCD Simulator device instance
 */

extern indigo_result indigo_ccd_fli(indigo_driver_action action, indigo_driver_info *info);

#endif /* ccd_fli_h */
