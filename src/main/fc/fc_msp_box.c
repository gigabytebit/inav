/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#include "common/streambuf.h"
#include "common/utils.h"

#include "config/feature.h"

#include "fc/config.h"
#include "fc/fc_msp_box.h"
#include "fc/runtime_config.h"
#include "flight/mixer.h"

#include "io/osd.h"

#include "drivers/pwm_output.h"

#include "sensors/diagnostics.h"
#include "sensors/sensors.h"

#include "navigation/navigation.h"

#include "telemetry/telemetry.h"

#define BOX_SUFFIX ';'
#define BOX_SUFFIX_LEN 1

static const box_t boxes[CHECKBOX_ITEM_COUNT + 1] = {
    { BOXARM, "ARM", 0 },
    { BOXANGLE, "ANGLE", 1 },
    { BOXHORIZON, "HORIZON", 2 },
    { BOXNAVALTHOLD, "NAV ALTHOLD", 3 },
    { BOXHEADINGHOLD, "HEADING HOLD", 5 },
    { BOXHEADFREE, "HEADFREE", 6 },
    { BOXHEADADJ, "HEADADJ", 7 },
    { BOXCAMSTAB, "CAMSTAB", 8 },
    { BOXNAVRTH, "NAV RTH", 10 },
    { BOXNAVPOSHOLD, "NAV POSHOLD", 11 },
    { BOXMANUAL, "MANUAL", 12 },
    { BOXBEEPERON, "BEEPER", 13 },
    { BOXLEDLOW, "LEDS OFF", 15 },
    { BOXLIGHTS, "LIGHTS", 16 },
    { BOXOSD, "OSD OFF", 19 },
    { BOXTELEMETRY, "TELEMETRY", 20 },
    { BOXAUTOTUNE, "AUTO TUNE", 21 },
    { BOXBLACKBOX, "BLACKBOX", 26 },
    { BOXFAILSAFE, "FAILSAFE", 27 },
    { BOXNAVWP, "NAV WP", 28 },
    { BOXAIRMODE, "AIR MODE", 29 },
    { BOXHOMERESET, "HOME RESET", 30 },
    { BOXGCSNAV, "GCS NAV", 31 },
    { BOXFPVANGLEMIX, "FPV ANGLE MIX", 32 },
    { BOXSURFACE, "SURFACE", 33 },
    { BOXFLAPERON, "FLAPERON", 34 },
    { BOXTURNASSIST, "TURN ASSIST", 35 },
    { BOXNAVLAUNCH, "NAV LAUNCH", 36 },
    { BOXAUTOTRIM, "SERVO AUTOTRIM", 37 },
    { BOXKILLSWITCH, "KILLSWITCH", 38 },
    { BOXCAMERA1, "CAMERA CONTROL 1", 39 },
    { BOXCAMERA2, "CAMERA CONTROL 2", 40 },
    { BOXCAMERA3, "CAMERA CONTROL 3", 41 },
    { BOXOSDALT1, "OSD ALT 1", 42 },
    { BOXOSDALT2, "OSD ALT 2", 43 },
    { BOXOSDALT3, "OSD ALT 3", 44 },
    { BOXNAVCOURSEHOLD, "NAV COURSE HOLD", 45 },
    { BOXBRAKING, "MC BRAKING", 46 },
    { BOXUSER1, "USER1", BOX_PERMANENT_ID_USER1 },
    { BOXUSER2, "USER2", BOX_PERMANENT_ID_USER2 },
    { BOXLOITERDIRCHN, "LOITER CHANGE", 49 },
    { BOXMSPRCOVERRIDE, "MSP RC OVERRIDE", 50 },
    { BOXPREARM, "PREARM", 51 },
    { BOXTURTLE, "TURTLE", 52 },
    { BOXNAVCRUISE, "NAV CRUISE", 53 },
    { BOXAUTOLEVEL, "AUTO LEVEL", 54 },
    { BOXPLANWPMISSION, "WP PLANNER", 55 },
    { BOXSOARING, "SOARING", 56 },
    { CHECKBOX_ITEM_COUNT, NULL, 0xFF }
};

// this is calculated at startup based on enabled features.
static uint8_t activeBoxIds[CHECKBOX_ITEM_COUNT];
// this is the number of filled indexes in above array
uint8_t activeBoxIdCount = 0;

#define RESET_BOX_ID_COUNT activeBoxIdCount = 0
#define ADD_ACTIVE_BOX(box) activeBoxIds[activeBoxIdCount++] = box 

const box_t *findBoxByActiveBoxId(uint8_t activeBoxId)
{
    for (uint8_t boxIndex = 0; boxIndex < sizeof(boxes) / sizeof(box_t); boxIndex++) {
        const box_t *candidate = &boxes[boxIndex];
        if (candidate->boxId == activeBoxId) {
            return candidate;
        }
    }
    return NULL;
}

const box_t *findBoxByPermanentId(uint8_t permenantId)
{
    for (uint8_t boxIndex = 0; boxIndex < sizeof(boxes) / sizeof(box_t); boxIndex++) {
        const box_t *candidate = &boxes[boxIndex];
        if (candidate->permanentId == permenantId) {
            return candidate;
        }
    }
    return NULL;
}

bool serializeBoxNamesReply(sbuf_t *dst)
{
    // First run of the loop - calculate total length of the reply
    int replyLengthTotal = 0;
    for (int i = 0; i < activeBoxIdCount; i++) {
        const box_t *box = findBoxByActiveBoxId(activeBoxIds[i]);
        if (box) {
            replyLengthTotal += strlen(box->boxName) + BOX_SUFFIX_LEN;
        }
    }

    // Check if we have enough space to send a reply
    if (sbufBytesRemaining(dst) < replyLengthTotal) {
        return false;
    }

    for (int i = 0; i < activeBoxIdCount; i++) {
        const int activeBoxId = activeBoxIds[i];
        const box_t *box = findBoxByActiveBoxId(activeBoxId);
        if (box) {
            const int len = strlen(box->boxName);
            sbufWriteData(dst, box->boxName, len);
            sbufWriteU8(dst, BOX_SUFFIX);
        }
    }

    return true;
}

void serializeBoxReply(sbuf_t *dst)
{
    for (int i = 0; i < activeBoxIdCount; i++) {
        const box_t *box = findBoxByActiveBoxId(activeBoxIds[i]);
        if (!box) {
            continue;
        }
        sbufWriteU8(dst, box->permanentId);
    }
}

void initActiveBoxIds(void)
{
    // calculate used boxes based on features and fill availableBoxes[] array
    memset(activeBoxIds, 0xFF, sizeof(activeBoxIds));

    RESET_BOX_ID_COUNT;
    ADD_ACTIVE_BOX(BOXARM);
    ADD_ACTIVE_BOX(BOXPREARM);

    if (sensors(SENSOR_ACC) && STATE(ALTITUDE_CONTROL)) {
        ADD_ACTIVE_BOX(BOXANGLE);
        ADD_ACTIVE_BOX(BOXHORIZON);
        ADD_ACTIVE_BOX(BOXTURNASSIST);
    }

    if (!feature(FEATURE_AIRMODE) && STATE(ALTITUDE_CONTROL)) {
        ADD_ACTIVE_BOX(BOXAIRMODE);
    }

    ADD_ACTIVE_BOX(BOXHEADINGHOLD);

    //Camstab mode is enabled always
    ADD_ACTIVE_BOX(BOXCAMSTAB);

    if (STATE(MULTIROTOR)) {
        if ((sensors(SENSOR_ACC) || sensors(SENSOR_MAG))) {
            ADD_ACTIVE_BOX(BOXHEADFREE);
            ADD_ACTIVE_BOX(BOXHEADADJ);
        }
        if (sensors(SENSOR_BARO) && sensors(SENSOR_RANGEFINDER) && sensors(SENSOR_OPFLOW)) {
            ADD_ACTIVE_BOX(BOXSURFACE);
        }
        ADD_ACTIVE_BOX(BOXFPVANGLEMIX);
    }

    bool navReadyAltControl = sensors(SENSOR_BARO);
#ifdef USE_GPS
    navReadyAltControl = navReadyAltControl || (feature(FEATURE_GPS) && (STATE(AIRPLANE) || positionEstimationConfig()->use_gps_no_baro));

    const bool navFlowDeadReckoning = sensors(SENSOR_OPFLOW) && sensors(SENSOR_ACC) && positionEstimationConfig()->allow_dead_reckoning;
    bool navReadyPosControl = sensors(SENSOR_ACC) && feature(FEATURE_GPS);
    if (STATE(MULTIROTOR)) {
        navReadyPosControl = navReadyPosControl && getHwCompassStatus() != HW_SENSOR_NONE;
    }

    if (STATE(ALTITUDE_CONTROL) && navReadyAltControl && (navReadyPosControl || navFlowDeadReckoning)) {
        ADD_ACTIVE_BOX(BOXNAVPOSHOLD);
        if (STATE(AIRPLANE)) {
            ADD_ACTIVE_BOX(BOXLOITERDIRCHN);
        }
    }

    if (navReadyPosControl) {
        if (!STATE(ALTITUDE_CONTROL) || (STATE(ALTITUDE_CONTROL) && navReadyAltControl)) {
            ADD_ACTIVE_BOX(BOXNAVRTH);
            ADD_ACTIVE_BOX(BOXNAVWP);
            ADD_ACTIVE_BOX(BOXHOMERESET);
            ADD_ACTIVE_BOX(BOXGCSNAV);
            ADD_ACTIVE_BOX(BOXPLANWPMISSION);
        }

        if (STATE(AIRPLANE)) {
            ADD_ACTIVE_BOX(BOXNAVCRUISE);
            ADD_ACTIVE_BOX(BOXNAVCOURSEHOLD);
            ADD_ACTIVE_BOX(BOXSOARING);
        }
    }

#ifdef USE_MR_BRAKING_MODE
    if (mixerConfig()->platformType == PLATFORM_MULTIROTOR) {
        ADD_ACTIVE_BOX(BOXBRAKING);
    }
#endif
#endif  // GPS
    if (STATE(ALTITUDE_CONTROL) && navReadyAltControl) {
        ADD_ACTIVE_BOX(BOXNAVALTHOLD);
    }

    if (STATE(AIRPLANE) || STATE(ROVER) || STATE(BOAT)) {
        ADD_ACTIVE_BOX(BOXMANUAL);
    }

    if (STATE(AIRPLANE)) {
        if (!feature(FEATURE_FW_LAUNCH)) {
           ADD_ACTIVE_BOX(BOXNAVLAUNCH);
        }

        if (!feature(FEATURE_FW_AUTOTRIM)) {
            ADD_ACTIVE_BOX(BOXAUTOTRIM);
        }

#if defined(USE_AUTOTUNE_FIXED_WING)
        ADD_ACTIVE_BOX(BOXAUTOTUNE);
#endif
        if (sensors(SENSOR_BARO)) {
            ADD_ACTIVE_BOX(BOXAUTOLEVEL);
        }
    }

    /*
     * FLAPERON mode active only in case of airplane and custom airplane. Activating on
     * flying wing can cause bad thing
     */
    if (STATE(FLAPERON_AVAILABLE)) {
        ADD_ACTIVE_BOX(BOXFLAPERON);
    }

    ADD_ACTIVE_BOX(BOXBEEPERON);

#ifdef USE_LIGHTS
    ADD_ACTIVE_BOX(BOXLIGHTS);
#endif

#ifdef USE_LED_STRIP
    if (feature(FEATURE_LED_STRIP)) {
        ADD_ACTIVE_BOX(BOXLEDLOW);
    }
#endif

    ADD_ACTIVE_BOX(BOXOSD);

#ifdef USE_TELEMETRY
    if (feature(FEATURE_TELEMETRY) && telemetryConfig()->telemetry_switch) {
        ADD_ACTIVE_BOX(BOXTELEMETRY);
    }
#endif

#ifdef USE_BLACKBOX
    if (feature(FEATURE_BLACKBOX)) {
        ADD_ACTIVE_BOX(BOXBLACKBOX);
    }
#endif

    ADD_ACTIVE_BOX(BOXKILLSWITCH);
    ADD_ACTIVE_BOX(BOXFAILSAFE);

#ifdef USE_RCDEVICE
    ADD_ACTIVE_BOX(BOXCAMERA1);
    ADD_ACTIVE_BOX(BOXCAMERA2);
    ADD_ACTIVE_BOX(BOXCAMERA3);
#endif

#ifdef USE_PINIOBOX
    // USER modes are only used for PINIO at the moment
    ADD_ACTIVE_BOX(BOXUSER1);
    ADD_ACTIVE_BOX(BOXUSER2);
#endif

#if defined(USE_OSD) && defined(OSD_LAYOUT_COUNT)
#if OSD_LAYOUT_COUNT > 0
    ADD_ACTIVE_BOX(BOXOSDALT1);
#if OSD_LAYOUT_COUNT > 1
    ADD_ACTIVE_BOX(BOXOSDALT2);
#if OSD_LAYOUT_COUNT > 2
    ADD_ACTIVE_BOX(BOXOSDALT3);
#endif
#endif
#endif
#endif

#if defined(USE_RX_MSP) && defined(USE_MSP_RC_OVERRIDE)
    ADD_ACTIVE_BOX(BOXMSPRCOVERRIDE);
#endif

#ifdef USE_DSHOT
    if(STATE(MULTIROTOR) && isMotorProtocolDshot()) {
        ADD_ACTIVE_BOX(BOXTURTLE);
    }
#endif
}

#define IS_ENABLED(mask) ((mask) == 0 ? 0 : 1)
#define CHECK_ACTIVE_BOX(condition, index)    do { if (IS_ENABLED(condition)) { activeBoxes[index] = 1; } } while(0)

void packBoxModeFlags(boxBitmask_t * mspBoxModeFlags)
{
    uint8_t activeBoxes[CHECKBOX_ITEM_COUNT];
    memset(activeBoxes, 0, sizeof(activeBoxes));

    // Serialize the flags in the order we delivered them, ignoring BOXNAMES and BOXINDEXES
    // Requires new Multiwii protocol version to fix
    // It would be preferable to setting the enabled bits based on BOXINDEX.
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(ANGLE_MODE)),               BOXANGLE);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(HORIZON_MODE)),             BOXHORIZON);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(HEADING_MODE)),             BOXHEADINGHOLD);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(HEADFREE_MODE)),            BOXHEADFREE);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXHEADADJ)),         BOXHEADADJ);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXCAMSTAB)),         BOXCAMSTAB);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXFPVANGLEMIX)),     BOXFPVANGLEMIX);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(MANUAL_MODE)),              BOXMANUAL);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXBEEPERON)),        BOXBEEPERON);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXLEDLOW)),          BOXLEDLOW);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXLIGHTS)),          BOXLIGHTS);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXOSD)),             BOXOSD);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXTELEMETRY)),       BOXTELEMETRY);
    CHECK_ACTIVE_BOX(IS_ENABLED(ARMING_FLAG(ARMED)),                    BOXARM);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXBLACKBOX)),        BOXBLACKBOX);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(FAILSAFE_MODE)),            BOXFAILSAFE);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(NAV_ALTHOLD_MODE)),         BOXNAVALTHOLD);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(NAV_POSHOLD_MODE)),         BOXNAVPOSHOLD);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(NAV_COURSE_HOLD_MODE)),     BOXNAVCOURSEHOLD);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(NAV_COURSE_HOLD_MODE)) && IS_ENABLED(FLIGHT_MODE(NAV_ALTHOLD_MODE)), BOXNAVCRUISE);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(NAV_RTH_MODE)),             BOXNAVRTH);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(NAV_WP_MODE)),              BOXNAVWP);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXAIRMODE)),         BOXAIRMODE);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXGCSNAV)),          BOXGCSNAV);
#ifdef USE_FLM_FLAPERON
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(FLAPERON)),                 BOXFLAPERON);
#endif
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(TURN_ASSISTANT)),           BOXTURNASSIST);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(NAV_LAUNCH_MODE)),          BOXNAVLAUNCH);
    CHECK_ACTIVE_BOX(IS_ENABLED(FLIGHT_MODE(AUTO_TUNE)),                BOXAUTOTUNE);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXAUTOTRIM)),        BOXAUTOTRIM);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXKILLSWITCH)),      BOXKILLSWITCH);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXHOMERESET)),       BOXHOMERESET);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXCAMERA1)),         BOXCAMERA1);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXCAMERA2)),         BOXCAMERA2);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXCAMERA3)),         BOXCAMERA3);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXOSDALT1)),         BOXOSDALT1);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXOSDALT2)),         BOXOSDALT2);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXOSDALT3)),         BOXOSDALT3);
    CHECK_ACTIVE_BOX(IS_ENABLED(navigationTerrainFollowingEnabled()),   BOXSURFACE);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXBRAKING)),         BOXBRAKING);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXUSER1)),           BOXUSER1);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXUSER2)),           BOXUSER2);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXLOITERDIRCHN)),    BOXLOITERDIRCHN);
#if defined(USE_RX_MSP) && defined(USE_MSP_RC_OVERRIDE)
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXMSPRCOVERRIDE)),   BOXMSPRCOVERRIDE);
#endif
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXAUTOLEVEL)),       BOXAUTOLEVEL);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXPLANWPMISSION)),   BOXPLANWPMISSION);
    CHECK_ACTIVE_BOX(IS_ENABLED(IS_RC_MODE_ACTIVE(BOXSOARING)),         BOXSOARING);

    memset(mspBoxModeFlags, 0, sizeof(boxBitmask_t));
    for (uint32_t i = 0; i < activeBoxIdCount; i++) {
        if (activeBoxes[activeBoxIds[i]]) {
            bitArraySet(mspBoxModeFlags->bits, i);
        }
    }
}

uint16_t packSensorStatus(void)
{
    // Sensor bits
    uint16_t sensorStatus =
            IS_ENABLED(sensors(SENSOR_ACC))         << 0 |
            IS_ENABLED(sensors(SENSOR_BARO))        << 1 |
            IS_ENABLED(sensors(SENSOR_MAG))         << 2 |
            IS_ENABLED(sensors(SENSOR_GPS))         << 3 |
            IS_ENABLED(sensors(SENSOR_RANGEFINDER)) << 4 |
            IS_ENABLED(sensors(SENSOR_OPFLOW))      << 5 |
            IS_ENABLED(sensors(SENSOR_PITOT))       << 6 |
            IS_ENABLED(sensors(SENSOR_TEMP))        << 7;

    // Hardware failure indication bit
    if (!isHardwareHealthy()) {
        sensorStatus |= 1 << 15;        // Bit 15 of sensor bit field indicates hardware failure
    }

    return sensorStatus;
}
