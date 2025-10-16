/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    Declarations for the BbrResync congestion control algorithm.

--*/

#pragma once

#include "sliding_window_extremum.h"

#define kBbrDefaultFilterCapacity 3

// [수정] 이름 충돌을 피하기 위해 Resync 접두사 추가
typedef struct BBR_RESYNC_BANDWIDTH_FILTER {
    BOOLEAN AppLimited : 1;
    uint64_t AppLimitedExitTarget;
    QUIC_SLIDING_WINDOW_EXTREMUM WindowedMaxFilter;
    QUIC_SLIDING_WINDOW_EXTREMUM_ENTRY WindowedMaxFilterEntries[kBbrDefaultFilterCapacity];
} BBR_RESYNC_BANDWIDTH_FILTER;

typedef struct QUIC_CONGESTION_CONTROL_BBRRESYNC {

    //
    // Standard BBR State
    //
    BOOLEAN BtlbwFound : 1;
    BOOLEAN ExitingQuiescence : 1;
    BOOLEAN EndOfRecoveryValid : 1;
    BOOLEAN EndOfRoundTripValid : 1;
    BOOLEAN AckAggregationStartTimeValid : 1;
    BOOLEAN ProbeRttRoundValid : 1;
    BOOLEAN ProbeRttEndTimeValid : 1;
    BOOLEAN RttSampleExpired: 1;
    BOOLEAN MinRttTimestampValid: 1;
    uint32_t InitialCongestionWindowPackets;
    uint32_t CongestionWindow;
    uint32_t InitialCongestionWindow;
    uint32_t RecoveryWindow;
    uint32_t BytesInFlight;
    uint32_t BytesInFlightMax;
    uint8_t Exemptions;
    uint64_t RoundTripCounter;
    uint32_t CwndGain;
    uint32_t PacingGain;
    uint64_t SendQuantum;
    uint8_t SlowStartupRoundCounter;
    uint32_t PacingCycleIndex;
    uint64_t AckAggregationStartTime;
    uint64_t AggregatedAckBytes;
    uint32_t RecoveryState;
    uint32_t BbrState;
    uint64_t CycleStart;
    uint64_t EndOfRoundTrip;
    uint64_t EndOfRecovery;
    uint64_t LastEstimatedStartupBandwidth;
    uint64_t ProbeRttRound;
    uint64_t ProbeRttEndTime;
    QUIC_SLIDING_WINDOW_EXTREMUM MaxAckHeightFilter;
    QUIC_SLIDING_WINDOW_EXTREMUM_ENTRY MaxAckHeightFilterEntries[kBbrDefaultFilterCapacity];
    uint64_t MinRtt;
    uint64_t MinRttTimestamp;
    BBR_RESYNC_BANDWIDTH_FILTER BandwidthFilter; // [수정] 변경된 구조체 타입 사용

    //
    // BbrResync Custom Variables
    //
    BOOLEAN ForceProbeRtt;
    BOOLEAN DropDetectedInRound;
    uint64_t RoundStartCwnd;
    uint32_t RecoveryCooldownRounds;
    uint32_t DropHistory[2];
    uint8_t DropHistoryCount;

} QUIC_CONGESTION_CONTROL_BBRRESYNC;

//
// BbrResync 초기화 함수
//
_IRQL_requires_max_(DISPATCH_LEVEL)
void
BbrResyncCongestionControlInitialize(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_SETTINGS_INTERNAL* Settings
    );