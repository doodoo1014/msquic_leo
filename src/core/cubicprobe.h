// // /*++

// //     Copyright (c) Microsoft Corporation.
// //     Licensed under the MIT License.

// // --*/

// // //
// // // The state for the CubicProbe congestion control algorithm.
// // //
// // typedef struct QUIC_CONGESTION_CONTROL_CUBICPROBE {

// //     //
// //     // Standard CUBIC state variables.
// //     //
// //     uint32_t CongestionWindow;
// //     uint32_t SlowStartThreshold;
// //     uint32_t WindowMax;             // W_max in RFC8312
// //     uint32_t WindowLastMax;         // W_last_max in RFC812
// //     uint32_t KCubic;                // K in RFC8312
// //     uint64_t TimeOfCongAvoidStart;
// //     uint32_t AimdWindow;            // The window size according to the AIMD algorithm.
// //     uint32_t AimdAccumulator;
// //     BOOLEAN HasHadCongestionEvent;
// //     uint32_t BytesInFlight;
// //     uint32_t BytesInFlightMax;
// //     uint32_t WindowPrior;           // Window size just before the last congestion event.
// //     uint64_t TimeOfLastAck;
// //     BOOLEAN TimeOfLastAckValid;
// //     uint32_t SendIdleTimeoutMs;
// //     uint32_t InitialWindowPackets;
// //     uint64_t RecoverySentPacketNumber;
// //     BOOLEAN IsInRecovery;
// //     BOOLEAN IsInPersistentCongestion;
// //     uint32_t LastSendAllowance;
// //     uint8_t Exemptions;

// //     // HyStart state
// //     uint64_t HyStartRoundEnd;
// //     uint32_t HyStartAckCount;
// //     uint64_t MinRttInLastRound;
// //     uint64_t MinRttInCurrentRound;
// //     uint8_t HyStartState;
// //     uint32_t CWndSlowStartGrowthDivisor;
// //     uint8_t ConservativeSlowStartRounds;
// //     uint64_t CssBaselineMinRtt;

// //     //
// //     // Spurious Congestion Event State
// //     //
// //     uint32_t PrevWindowPrior;
// //     uint32_t PrevWindowMax;
// //     uint32_t PrevWindowLastMax;
// //     uint32_t PrevKCubic;
// //     uint32_t PrevSlowStartThreshold;
// //     uint32_t PrevCongestionWindow;
// //     uint32_t PrevAimdWindow;

// //     //
// //     // Custom state variables for the Probe mechanism
// //     //
// //     enum {
// //         PROBE_INACTIVE,
// //         PROBE_TEST,
// //         PROBE_JUDGMENT
// //     } ProbeState;
// //     uint32_t CumulativeSuccessLevel;
// //     uint32_t RttCount;
// //     uint64_t RttAtProbeStartUs;

// // } QUIC_CONGESTION_CONTROL_CUBICPROBE;







// #ifndef QUIC_CUBICPROBE_H
// #define QUIC_CUBICPROBE_H

// #include "cubic.h" // cubic.h를 포함하여 필요한 타입들을 가져옵니다.

// // CubicProbe의 탐색 상태 Enum
// typedef enum QUIC_PROBE_STATE {
//     PROBE_INACTIVE,
//     PROBE_TEST,
//     PROBE_JUDGMENT
// } QUIC_PROBE_STATE;

// // CubicProbe 알고리즘의 상태를 저장하는 구조체
// typedef struct QUIC_CONGESTION_CONTROL_CUBICPROBE {

//     // Base MsQuic CUBIC State.
//     // 이 구조체를 첫 멤버로 두어 코드 재사용성과 유지보수성을 높입니다.
//     QUIC_CONGESTION_CONTROL_CUBIC Cubic;

//     // CubicProbe-specific State
//     QUIC_PROBE_STATE ProbeState;
//     uint32_t CumulativeSuccessLevel;
//     uint32_t RttCount;
//     uint64_t RttAtProbeStartUs;

// } QUIC_CONGESTION_CONTROL_CUBICPROBE;

// // CubicProbe 알고리즘을 초기화하는 함수
// _IRQL_requires_max_(DISPATCH_LEVEL)
// void
// CubicProbeCongestionControlInitialize(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_SETTINGS_INTERNAL* Settings
//     );

// #endif // QUIC_CUBICPROBE_H



#ifndef QUIC_CUBICPROBE_H
#define QUIC_CUBICPROBE_H

#include "cubic.h" // cubic.h를 포함하여 필요한 타입들을 가져옵니다.

// CubicProbe의 탐색 상태 Enum
typedef enum QUIC_PROBE_STATE {
    PROBE_INACTIVE,
    PROBE_TEST,
    PROBE_JUDGMENT
} QUIC_PROBE_STATE;

// CubicProbe 알고리즘의 상태를 저장하는 구조체
typedef struct QUIC_CONGESTION_CONTROL_CUBICPROBE {

    // Base MsQuic CUBIC State.
    QUIC_CONGESTION_CONTROL_CUBIC Cubic;

    // CubicProbe-specific State
    QUIC_PROBE_STATE ProbeState;
    uint32_t CumulativeSuccessLevel;
    uint32_t RttCount;
    uint64_t RttAtProbeStartUs;

    // **ACK Counter for ns-3 style growth**
    uint32_t AckCountSinceLastGrowth; // ns-3의 m_cWndCnt 역할 (세그먼트 단위)

} QUIC_CONGESTION_CONTROL_CUBICPROBE;

// CubicProbe 알고리즘을 초기화하는 함수
_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlInitialize(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_SETTINGS_INTERNAL* Settings
    );

#endif // QUIC_CUBICPROBE_H

