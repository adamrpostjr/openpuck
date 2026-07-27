// hid_reports.h — see hid_reports.c. Sized extern declarations so callers keep
// using sizeof(NAME) unchanged.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef OPK_COMMON_HID_REPORTS_H
#define OPK_COMMON_HID_REPORTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t DS3_HID_DESC[148];
extern const uint8_t report_01[64];
extern const uint8_t report_f2[64];
extern const uint8_t report_f5[64];
extern const uint8_t report_ef[64];
extern const uint8_t report_f8[64];
extern const uint8_t report_f7[64];
extern const uint8_t GYRO_HID_DESC[110];
extern const uint8_t PS5_HID_DESC[273];
extern const uint8_t SWITCH_HID_DESC[86];
extern const uint8_t SWPRO_HID_DESC[203];
extern const uint8_t SPI_IMU_CAL[24];
extern const uint8_t SPI_PARAMS1[24];
extern const uint8_t SPI_PARAMS2[18];
extern const uint8_t SPI_COLOR[13];
extern const uint8_t BT_PAIR_2[31];

extern const uint8_t PUCK_HID_DESC[372];

#ifdef __cplusplus
}
#endif

#endif // OPK_COMMON_HID_REPORTS_H
