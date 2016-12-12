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
// 2.0 Build 0 - PoC by Rumen G. Bogdanovski

/** INDIGO MOUNT Nexstar (celestron & skywatcher) driver
 \file indigo_mount_nexstar.c
 */

#define DRIVER_VERSION 0x0001

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <assert.h>

#include "indigo_driver_xml.h"

#include "nexstar.h"
#include "indigo_mount_nexstar.h"

#undef PRIVATE_DATA
#define PRIVATE_DATA        ((nexstar_private_data *)DEVICE_CONTEXT->private_data)

#define RA_MIN_DIFF         (1/24/60/10)
#define DEC_MIN_DIFF        (1/60/60)

typedef struct {
	int dev_id;
	bool parked;
	char tty_name[INDIGO_VALUE_SIZE];
	int count_open;
	//double currentRA, requestedRA;
	//double currentDec, requestedDec;
	pthread_mutex_t serial_mutex;
	indigo_timer *slew_timer, *guider_timer;
} nexstar_private_data;

// -------------------------------------------------------------------------------- INDIGO MOUNT device implementation

static bool mount_open(indigo_device *device) {
	pthread_mutex_lock(&PRIVATE_DATA->serial_mutex);
	if (PRIVATE_DATA->count_open++ == 0) {
		int dev_id = open_telescope(DEVICE_PORT_ITEM->text.value);
		if (dev_id == -1) {
			pthread_mutex_unlock(&PRIVATE_DATA->serial_mutex);
			INDIGO_LOG(indigo_log("indigo_mount_nexstar: open_telescope(%s) = %d", DEVICE_PORT_ITEM->text.value, dev_id));
			return false;
		} else {
			INDIGO_LOG(indigo_log("indigo_mount_nexstar: open_telescope(%s) = %d", DEVICE_PORT_ITEM->text.value, dev_id));
			PRIVATE_DATA->dev_id = dev_id;
		}
	}
	pthread_mutex_unlock(&PRIVATE_DATA->serial_mutex);
	return true;
}

static bool mount_handle_coordinates(indigo_device *device) {
	int res = RC_OK;
	pthread_mutex_lock(&PRIVATE_DATA->serial_mutex);
	// GOTO requested
	if(MOUNT_ON_COORDINATES_SET_TRACK_ITEM->sw.value) {
		res = tc_goto_rade_p(PRIVATE_DATA->dev_id, MOUNT_EQUATORIAL_COORDINATES_RA_ITEM->number.value, MOUNT_EQUATORIAL_COORDINATES_DEC_ITEM->number.value);
		if (res != RC_OK) {
			INDIGO_LOG(indigo_log("indigo_mount_nexstar: tc_goto_rade_p(%d) = %d", PRIVATE_DATA->dev_id, res));
		}
	}
	// SYNC requested
	else if (MOUNT_ON_COORDINATES_SET_SYNC_ITEM->sw.value) {
		res = tc_sync_rade_p(PRIVATE_DATA->dev_id, MOUNT_EQUATORIAL_COORDINATES_RA_ITEM->number.value, MOUNT_EQUATORIAL_COORDINATES_DEC_ITEM->number.value);
		if (res != RC_OK) {
			INDIGO_LOG(indigo_log("indigo_mount_nexstar: tc_sync_rade_p(%d) = %d", PRIVATE_DATA->dev_id, res));
		}
	}
	pthread_mutex_unlock(&PRIVATE_DATA->serial_mutex);
	if (res) return false;
	else return true;
}


static bool mount_cancel_slew(indigo_device *device) {
	pthread_mutex_lock(&PRIVATE_DATA->serial_mutex);

	int res = tc_goto_cancel(PRIVATE_DATA->dev_id);
	if (res != RC_OK) {
		INDIGO_LOG(indigo_log("indigo_mount_nexstar: tc_goto_cancel(%d) = %d", PRIVATE_DATA->dev_id, res));
	}

	pthread_mutex_unlock(&PRIVATE_DATA->serial_mutex);
	return true;
}


static void mount_close(indigo_device *device) {
	pthread_mutex_lock(&PRIVATE_DATA->serial_mutex);

	if (--PRIVATE_DATA->count_open == 0) {
		close_telescope(PRIVATE_DATA->dev_id);
		PRIVATE_DATA->dev_id = -1;
	}

	pthread_mutex_unlock(&PRIVATE_DATA->serial_mutex);
}


static void slew_timer_callback(indigo_device *device) {
	double ra, dec;
	int dev_id = PRIVATE_DATA->dev_id;

	pthread_mutex_lock(&PRIVATE_DATA->serial_mutex);
	if (tc_goto_in_progress(dev_id)) {
		MOUNT_EQUATORIAL_COORDINATES_PROPERTY->state = INDIGO_OK_STATE;
		PRIVATE_DATA->slew_timer = NULL;
	} else {
		MOUNT_EQUATORIAL_COORDINATES_PROPERTY->state = INDIGO_BUSY_STATE;
		PRIVATE_DATA->slew_timer = indigo_set_timer(device, 0.2, slew_timer_callback);
	}

	int res = tc_get_rade_p(dev_id, &ra, &dec);
	if (res != RC_OK) {
		INDIGO_LOG(indigo_log("indigo_mount_nexstar: tc_get_rade_p(%d) = %d", dev_id, res));
	}
	pthread_mutex_unlock(&PRIVATE_DATA->serial_mutex);
	MOUNT_EQUATORIAL_COORDINATES_RA_ITEM->number.value = ra;
	MOUNT_EQUATORIAL_COORDINATES_DEC_ITEM->number.value = dec;
	indigo_update_property(device, MOUNT_EQUATORIAL_COORDINATES_PROPERTY, NULL);
}

static indigo_result mount_attach(indigo_device *device) {
	assert(device != NULL);
	assert(device->device_context != NULL);
	nexstar_private_data *private_data = device->device_context;
	device->device_context = NULL;

	if (indigo_mount_attach(device, DRIVER_VERSION) == INDIGO_OK) {
		DEVICE_CONTEXT->private_data = private_data;
		pthread_mutex_init(&PRIVATE_DATA->serial_mutex, NULL);
		// -------------------------------------------------------------------------------- SIMULATION
		SIMULATION_PROPERTY->hidden = true;
		// -------------------------------------------------------------------------------- MOUNT_ON_COORDINATES_SET
		MOUNT_ON_COORDINATES_SET_PROPERTY->count = 2;
		// -------------------------------------------------------------------------------- DEVICE_PORT
		DEVICE_PORT_PROPERTY->hidden = false;
		// -------------------------------------------------------------------------------- DEVICE_PORTS
		DEVICE_PORTS_PROPERTY->hidden = false;
		// --------------------------------------------------------------------------------
		INDIGO_LOG(indigo_log("%s attached", device->name));
		return indigo_mount_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}

static indigo_result mount_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(device->device_context != NULL);
	assert(property != NULL);
	// -------------------------------------------------------------------------------- CONNECTION
	if (indigo_property_match(CONNECTION_PROPERTY, property)) {
		indigo_property_copy_values(CONNECTION_PROPERTY, property, false);
		if (CONNECTION_CONNECTED_ITEM->sw.value) {
			if (mount_open(device)) {
				CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
				GUIDER_GUIDE_DEC_PROPERTY->hidden = false;
				GUIDER_GUIDE_RA_PROPERTY->hidden = false;
			} else {
				CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
			}
		} else {
			mount_close(device);
			CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
		}
	} else if (indigo_property_match(MOUNT_PARK_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- MOUNT_PARK
		indigo_property_copy_values(MOUNT_PARK_PROPERTY, property, false);
		if (MOUNT_PARK_PARKED_ITEM->sw.value) {
			MOUNT_PARK_PROPERTY->state = INDIGO_BUSY_STATE;
			indigo_update_property(device, MOUNT_PARK_PROPERTY, "Parking...");

			// TODO PARK HERE!

			MOUNT_PARK_PROPERTY->state = INDIGO_OK_STATE;
			indigo_update_property(device, MOUNT_PARK_PROPERTY, "Parked");
			PRIVATE_DATA->parked = true;
		} else {
			MOUNT_PARK_PROPERTY->state = INDIGO_BUSY_STATE;
			indigo_update_property(device, MOUNT_PARK_PROPERTY, "Unparking...");

			// TODO UNPARK HERE!

			MOUNT_PARK_PROPERTY->state = INDIGO_OK_STATE;
			indigo_update_property(device, MOUNT_PARK_PROPERTY, "Unparked");
			PRIVATE_DATA->parked = false;
		}
		return INDIGO_OK;
	} else if (indigo_property_match(MOUNT_EQUATORIAL_COORDINATES_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- MOUNT_EQUATORIAL_COORDINATES
		indigo_property_copy_values(MOUNT_EQUATORIAL_COORDINATES_PROPERTY, property, false);
		if(mount_handle_coordinates(device)) slew_timer_callback(device);
		else MOUNT_EQUATORIAL_COORDINATES_PROPERTY->state = INDIGO_ALERT_STATE;
		return INDIGO_OK;
	} else if (indigo_property_match(MOUNT_ABORT_MOTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- MOUNT_ABORT_MOTION
		indigo_property_copy_values(MOUNT_ABORT_MOTION_PROPERTY, property, false);
		if (PRIVATE_DATA->slew_timer != NULL) {
			mount_cancel_slew(device);
			indigo_cancel_timer(device, PRIVATE_DATA->slew_timer);
			PRIVATE_DATA->slew_timer = NULL;
			MOUNT_EQUATORIAL_COORDINATES_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, MOUNT_EQUATORIAL_COORDINATES_PROPERTY, NULL);
		}
		MOUNT_ABORT_MOTION_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, MOUNT_ABORT_MOTION_PROPERTY, "Aborted");
		return INDIGO_OK;
		// --------------------------------------------------------------------------------
	}
	return indigo_mount_change_property(device, client, property);
}

static indigo_result mount_detach(indigo_device *device) {
	assert(device != NULL);
	if (CONNECTION_CONNECTED_ITEM->sw.value)
		indigo_device_disconnect(NULL, device);
	INDIGO_LOG(indigo_log("%s detached", device->name));
	return indigo_mount_detach(device);
}

// -------------------------------------------------------------------------------- INDIGO guider device implementation

static void guider_timer_callback(indigo_device *device) {
	PRIVATE_DATA->guider_timer = NULL;
	if (GUIDER_GUIDE_NORTH_ITEM->number.value != 0 || GUIDER_GUIDE_SOUTH_ITEM->number.value != 0) {
		GUIDER_GUIDE_NORTH_ITEM->number.value = 0;
		GUIDER_GUIDE_SOUTH_ITEM->number.value = 0;
		GUIDER_GUIDE_DEC_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, GUIDER_GUIDE_DEC_PROPERTY, NULL);
	}
	if (GUIDER_GUIDE_EAST_ITEM->number.value != 0 || GUIDER_GUIDE_WEST_ITEM->number.value != 0) {
		GUIDER_GUIDE_EAST_ITEM->number.value = 0;
		GUIDER_GUIDE_WEST_ITEM->number.value = 0;
		GUIDER_GUIDE_RA_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, GUIDER_GUIDE_RA_PROPERTY, NULL);
	}
}

static indigo_result guider_attach(indigo_device *device) {
	assert(device != NULL);
	assert(device->device_context != NULL);
	nexstar_private_data *private_data = device->device_context;
	device->device_context = NULL;
	if (indigo_guider_attach(device, DRIVER_VERSION) == INDIGO_OK) {
		DEVICE_CONTEXT->private_data = private_data;
		INDIGO_LOG(indigo_log("%s attached", device->name));
		return indigo_guider_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}

static indigo_result guider_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(device->device_context != NULL);
	assert(property != NULL);
	// -------------------------------------------------------------------------------- CONNECTION
	if (indigo_property_match(CONNECTION_PROPERTY, property)) {
		indigo_property_copy_values(CONNECTION_PROPERTY, property, false);
		if (CONNECTION_CONNECTED_ITEM->sw.value) {
			if (mount_open(device)) {
				CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
				GUIDER_GUIDE_DEC_PROPERTY->hidden = false;
				GUIDER_GUIDE_RA_PROPERTY->hidden = false;
			} else {
				CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
			}
		} else {
			mount_close(device);
			CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
		}
	} else if (indigo_property_match(GUIDER_GUIDE_DEC_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- GUIDER_GUIDE_DEC
		if (PRIVATE_DATA->guider_timer != NULL)
			indigo_cancel_timer(device, PRIVATE_DATA->guider_timer);
		indigo_property_copy_values(GUIDER_GUIDE_DEC_PROPERTY, property, false);
		GUIDER_GUIDE_DEC_PROPERTY->state = INDIGO_OK_STATE;
		int duration = GUIDER_GUIDE_NORTH_ITEM->number.value;
		if (duration > 0) {
			GUIDER_GUIDE_DEC_PROPERTY->state = INDIGO_BUSY_STATE;
			PRIVATE_DATA->guider_timer = indigo_set_timer(device, duration/1000.0, guider_timer_callback);
		} else {
			int duration = GUIDER_GUIDE_SOUTH_ITEM->number.value;
			if (duration > 0) {
				GUIDER_GUIDE_DEC_PROPERTY->state = INDIGO_BUSY_STATE;
				PRIVATE_DATA->guider_timer = indigo_set_timer(device, duration/1000.0, guider_timer_callback);
			}
		}
		indigo_update_property(device, GUIDER_GUIDE_DEC_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(GUIDER_GUIDE_RA_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- GUIDER_GUIDE_RA
		if (PRIVATE_DATA->guider_timer != NULL)
			indigo_cancel_timer(device, PRIVATE_DATA->guider_timer);
		indigo_property_copy_values(GUIDER_GUIDE_RA_PROPERTY, property, false);
		GUIDER_GUIDE_RA_PROPERTY->state = INDIGO_OK_STATE;
		int duration = GUIDER_GUIDE_EAST_ITEM->number.value;
		if (duration > 0) {
			GUIDER_GUIDE_RA_PROPERTY->state = INDIGO_BUSY_STATE;
			PRIVATE_DATA->guider_timer = indigo_set_timer(device, duration/1000.0, guider_timer_callback);
		} else {
			int duration = GUIDER_GUIDE_WEST_ITEM->number.value;
			if (duration > 0) {
				GUIDER_GUIDE_RA_PROPERTY->state = INDIGO_BUSY_STATE;
				PRIVATE_DATA->guider_timer = indigo_set_timer(device, duration/1000.0, guider_timer_callback);
			}
		}
		indigo_update_property(device, GUIDER_GUIDE_RA_PROPERTY, NULL);
		return INDIGO_OK;
		// --------------------------------------------------------------------------------
	}
	return indigo_guider_change_property(device, client, property);
}

static indigo_result guider_detach(indigo_device *device) {
	assert(device != NULL);
	if (CONNECTION_CONNECTED_ITEM->sw.value)
		indigo_device_disconnect(NULL, device);
	INDIGO_LOG(indigo_log("%s detached", device->name));
	return indigo_guider_detach(device);
}

// --------------------------------------------------------------------------------

static nexstar_private_data *private_data = NULL;

static indigo_device *mount = NULL;
static indigo_device *mount_guider = NULL;

indigo_result indigo_mount_nexstar(indigo_driver_action action, indigo_driver_info *info) {
	static indigo_device mount_template = {
		MOUNT_NEXSTAR_NAME, NULL, INDIGO_OK, INDIGO_VERSION_CURRENT,
		mount_attach,
		indigo_mount_enumerate_properties,
		mount_change_property,
		mount_detach
	};
	static indigo_device mount_guider_template = {
		MOUNT_NEXSTAR_GUIDER_NAME, NULL, INDIGO_OK, INDIGO_VERSION_CURRENT,
		guider_attach,
		indigo_guider_enumerate_properties,
		guider_change_property,
		guider_detach
	};

	static indigo_driver_action last_action = INDIGO_DRIVER_SHUTDOWN;

	SET_DRIVER_INFO(info, MOUNT_NEXSTAR_NAME, __FUNCTION__, DRIVER_VERSION, last_action);

	if (action == last_action)
		return INDIGO_OK;

	switch (action) {
	case INDIGO_DRIVER_INIT:
		last_action = action;
		private_data = malloc(sizeof(nexstar_private_data));
		assert(private_data != NULL);
		private_data->dev_id = -1;
		private_data->count_open = 0;
		mount = malloc(sizeof(indigo_device));
		assert(mount != NULL);
		memcpy(mount, &mount_template, sizeof(indigo_device));
		mount->device_context = private_data;
		indigo_attach_device(mount);
		mount_guider = malloc(sizeof(indigo_device));
		assert(mount_guider != NULL);
		memcpy(mount_guider, &mount_guider_template, sizeof(indigo_device));
		mount_guider->device_context = private_data;
		indigo_attach_device(mount_guider);
		break;

	case INDIGO_DRIVER_SHUTDOWN:
		last_action = action;
		if (mount != NULL) {
			indigo_detach_device(mount);
			free(mount);
			mount = NULL;
		}
		if (mount_guider != NULL) {
			indigo_detach_device(mount_guider);
			free(mount_guider);
			mount_guider = NULL;
		}
		if (private_data != NULL) {
			free(private_data);
			private_data = NULL;
		}
		break;

	case INDIGO_DRIVER_INFO:
		break;
	}

	return INDIGO_OK;
}

