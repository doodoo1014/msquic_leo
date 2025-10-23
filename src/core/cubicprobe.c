// // // /*++

// // //     Copyright (c) Microsoft Corporation.
// // //     Licensed under the MIT License.

// // // Abstract:

// // //     CubicProbe congestion control algorithm. This implementation extends Ms-quic's CUBIC
// // //     with a probing mechanism, with focused printf logging for key events including simulation time.

// // //     WARNING: printf is used for temporary debugging ONLY. It will severely
// // //     degrade performance and is not suitable for production or performance testing.

// // // --*/

// // // #include "precomp.h"
// // // #include <stdio.h> // For printf
// // // #ifdef QUIC_CLOG
// // // #include "cubicprobe.c.clog.h"
// // // #endif

// // // #include "cubicprobe.h"

// // // // Constants from RFC8312 (for CUBIC)
// // // #define TEN_TIMES_BETA_CUBIC 7
// // // #define TEN_TIMES_C_CUBIC 4

// // // // Constants for CubicProbe logic (from ns-3 implementation)
// // // #define PROBE_RTT_INTERVAL 2
// // // #define PROBE_RTT_INCREASE_NUMERATOR 11
// // // #define PROBE_RTT_INCREASE_DENOMINATOR 10

// // // //
// // // // Forward declarations for all functions in the v-table and helpers
// // // //
// // // BOOLEAN CubicProbeCongestionControlCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc);
// // // void CubicProbeCongestionControlSetExemption(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint8_t NumPackets);
// // // void CubicProbeCongestionControlReset(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN FullReset);
// // // uint32_t CubicProbeCongestionControlGetSendAllowance(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint64_t TimeSinceLastSend, _In_ BOOLEAN TimeSinceLastSendValid);
// // // void CubicProbeCongestionControlOnDataSent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
// // // BOOLEAN CubicProbeCongestionControlOnDataInvalidated( _In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
// // // BOOLEAN CubicProbeCongestionControlOnDataAcknowledged(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent);
// // // void CubicProbeCongestionControlOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent);
// // // void CubicProbeCongestionControlOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent);
// // // BOOLEAN CubicProbeCongestionControlOnSpuriousCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc);
// // // void CubicProbeCongestionControlLogOutFlowStatus(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// // // uint8_t CubicProbeCongestionControlGetExemptions(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// // // uint32_t CubicProbeCongestionControlGetBytesInFlightMax(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// // // BOOLEAN CubicProbeCongestionControlIsAppLimited(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// // // void CubicProbeCongestionControlSetAppLimited(_In_ QUIC_CONGESTION_CONTROL* Cc);
// // // uint32_t CubicProbeCongestionControlGetCongestionWindow(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// // // void CubicProbeCongestionControlGetNetworkStatistics(_In_ const QUIC_CONNECTION* const Connection, _In_ const QUIC_CONGESTION_CONTROL* const Cc, _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics);
// // // static void CubicProbeCongestionControlOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn);
// // // static BOOLEAN CubicProbeCongestionControlUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState);
// // // static void CubicProbeHyStartChangeState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ QUIC_CUBIC_HYSTART_STATE NewHyStartState);
// // // static void CubicProbeHyStartResetPerRttRound(_In_ QUIC_CONGESTION_CONTROL_CUBIC* Cubic);


// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static void
// // // CubicProbeResetProbeState(
// // //     _In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe
// // //     )
// // // {
// // //     CubicProbe->ProbeState = PROBE_INACTIVE;
// // //     CubicProbe->CumulativeSuccessLevel = 0;
// // //     CubicProbe->RttCount = 0;
// // //     CubicProbe->RttAtProbeStartUs = 0;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static uint32_t
// // // CubeRoot(
// // //     uint32_t Radicand
// // //     )
// // // {
// // //     int i;
// // //     uint32_t x = 0;
// // //     uint32_t y = 0;
// // //     for (i = 30; i >= 0; i -= 3) {
// // //         x = x * 8 + ((Radicand >> i) & 7);
// // //         if ((y * 2 + 1) * (y * 2 + 1) * (y * 2 + 1) <= x) {
// // //             y = y * 2 + 1;
// // //         } else {
// // //             y = y * 2;
// // //         }
// // //     }
// // //     return y;
// // // }

// // // static void
// // // CubicProbeHyStartChangeState(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ QUIC_CUBIC_HYSTART_STATE NewHyStartState
// // //     )
// // // {
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     if (!Connection->Settings.HyStartEnabled) {
// // //         return;
// // //     }
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     switch (NewHyStartState) {
// // //     case HYSTART_ACTIVE:
// // //         break;
// // //     case HYSTART_DONE:
// // //     case HYSTART_NOT_STARTED:
// // //         Cubic->CWndSlowStartGrowthDivisor = 1;
// // //         break;
// // //     default:
// // //         CXPLAT_FRE_ASSERT(FALSE);
// // //     }
// // //     if (Cubic->HyStartState != NewHyStartState) {
// // //         Cubic->HyStartState = NewHyStartState;
// // //     }
// // // }

// // // static void
// // // CubicProbeHyStartResetPerRttRound(
// // //     _In_ QUIC_CONGESTION_CONTROL_CUBIC* Cubic
// // //     )
// // // {
// // //     Cubic->HyStartAckCount = 0;
// // //     Cubic->MinRttInLastRound = Cubic->MinRttInCurrentRound;
// // //     Cubic->MinRttInCurrentRound = UINT64_MAX;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // BOOLEAN
// // // CubicProbeCongestionControlCanSend(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void
// // // CubicProbeCongestionControlSetExemption(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ uint8_t NumPackets
// // //     )
// // // {
// // //     Cc->CubicProbe.Cubic.Exemptions = NumPackets;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void
// // // CubicProbeCongestionControlReset(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ BOOLEAN FullReset
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(&Connection->Paths[0]);

// // //     Cubic->SlowStartThreshold = UINT32_MAX;
// // //     Cubic->MinRttInCurrentRound = UINT32_MAX;
// // //     Cubic->HyStartRoundEnd = Connection->Send.NextPacketNumber;
// // //     CubicProbeHyStartResetPerRttRound(Cubic);
// // //     CubicProbeHyStartChangeState(Cc, HYSTART_NOT_STARTED);
// // //     Cubic->IsInRecovery = FALSE;
// // //     Cubic->HasHadCongestionEvent = FALSE;
// // //     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
// // //     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
// // //     Cubic->LastSendAllowance = 0;
// // //     if (FullReset) {
// // //         Cubic->BytesInFlight = 0;
// // //     }

// // //     CubicProbeResetProbeState(CubicProbe);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // uint32_t
// // // CubicProbeCongestionControlGetSendAllowance(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ uint64_t TimeSinceLastSend,
// // //     _In_ BOOLEAN TimeSinceLastSendValid
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     uint32_t SendAllowance;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     if (Cubic->BytesInFlight >= Cubic->CongestionWindow) {
// // //         SendAllowance = 0;
// // //     } else if (
// // //         !TimeSinceLastSendValid ||
// // //         !Connection->Settings.PacingEnabled ||
// // //         !Connection->Paths[0].GotFirstRttSample ||
// // //         Connection->Paths[0].SmoothedRtt < QUIC_MIN_PACING_RTT) {
// // //         SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
// // //     } else {
// // //         uint64_t EstimatedWnd;
// // //         if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
// // //             EstimatedWnd = (uint64_t)Cubic->CongestionWindow << 1;
// // //             if (EstimatedWnd > Cubic->SlowStartThreshold) {
// // //                 EstimatedWnd = Cubic->SlowStartThreshold;
// // //             }
// // //         } else {
// // //             EstimatedWnd = Cubic->CongestionWindow + (Cubic->CongestionWindow >> 2);
// // //         }
// // //         SendAllowance =
// // //             Cubic->LastSendAllowance +
// // //             (uint32_t)((EstimatedWnd * TimeSinceLastSend) / Connection->Paths[0].SmoothedRtt);
// // //         if (SendAllowance < Cubic->LastSendAllowance ||
// // //             SendAllowance > (Cubic->CongestionWindow - Cubic->BytesInFlight)) {
// // //             SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
// // //         }
// // //         Cubic->LastSendAllowance = SendAllowance;
// // //     }
// // //     return SendAllowance;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static BOOLEAN
// // // CubicProbeCongestionControlUpdateBlockedState(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ BOOLEAN PreviousCanSendState
// // //     )
// // // {
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     if (PreviousCanSendState != CubicProbeCongestionControlCanSend(Cc)) {
// // //         if (PreviousCanSendState) {
// // //             QuicConnAddOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
// // //         } else {
// // //             QuicConnRemoveOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
// // //             Connection->Send.LastFlushTime = CxPlatTimeUs64();
// // //             return TRUE;
// // //         }
// // //     }
// // //     return FALSE;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static void
// // // CubicProbeCongestionControlOnCongestionEvent(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ BOOLEAN IsPersistentCongestion,
// // //     _In_ BOOLEAN Ecn
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(&Connection->Paths[0]);

// // //     Connection->Stats.Send.CongestionCount++;
// // //     Cubic->IsInRecovery = TRUE;
// // //     Cubic->HasHadCongestionEvent = TRUE;
// // //     CubicProbeResetProbeState(CubicProbe);

// // //     if (!Ecn) {
// // //         Cubic->PrevWindowPrior = Cubic->WindowPrior;
// // //         Cubic->PrevWindowMax = Cubic->WindowMax;
// // //         Cubic->PrevWindowLastMax = Cubic->WindowLastMax;
// // //         Cubic->PrevKCubic = Cubic->KCubic;
// // //         Cubic->PrevSlowStartThreshold = Cubic->SlowStartThreshold;
// // //         Cubic->PrevCongestionWindow = Cubic->CongestionWindow;
// // //         Cubic->PrevAimdWindow = Cubic->AimdWindow;
// // //     }

// // //     uint32_t PrevCwnd = Cubic->CongestionWindow;

// // //     if (IsPersistentCongestion && !Cubic->IsInPersistentCongestion) {
// // //         Connection->Stats.Send.PersistentCongestionCount++;
// // //         Connection->Paths[0].Route.State = RouteSuspected;
// // //         Cubic->IsInPersistentCongestion = TRUE;
// // //         Cubic->WindowPrior = Cubic->WindowMax = Cubic->WindowLastMax = Cubic->SlowStartThreshold = Cubic->AimdWindow =
// // //             Cubic->CongestionWindow * TEN_TIMES_BETA_CUBIC / 10;
// // //         Cubic->CongestionWindow = DatagramPayloadLength * QUIC_PERSISTENT_CONGESTION_WINDOW_PACKETS;
// // //         Cubic->KCubic = 0;
// // //         CubicProbeHyStartChangeState(Cc, HYSTART_DONE);
// // //     } else {
// // //         Cubic->WindowPrior = Cubic->WindowMax = Cubic->CongestionWindow;
// // //         if (Cubic->WindowLastMax > Cubic->WindowMax) {
// // //             Cubic->WindowLastMax = Cubic->WindowMax;
// // //             Cubic->WindowMax = Cubic->WindowMax * (10 + TEN_TIMES_BETA_CUBIC) / 20;
// // //         } else {
// // //             Cubic->WindowLastMax = Cubic->WindowMax;
// // //         }

// // //         if (DatagramPayloadLength > 0) {
// // //             Cubic->KCubic =
// // //                 CubeRoot(
// // //                     (Cubic->WindowMax / DatagramPayloadLength * (10 - TEN_TIMES_BETA_CUBIC) << 9) /
// // //                     TEN_TIMES_C_CUBIC);
// // //             Cubic->KCubic = S_TO_MS(Cubic->KCubic);
// // //             Cubic->KCubic >>= 3;
// // //         } else {
// // //             Cubic->KCubic = 0;
// // //         }

// // //         CubicProbeHyStartChangeState(Cc, HYSTART_DONE);
// // //         uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
// // //         Cubic->SlowStartThreshold =
// // //         Cubic->CongestionWindow =
// // //         Cubic->AimdWindow =
// // //             CXPLAT_MAX(
// // //                 MinCongestionWindow,
// // //                 Cubic->CongestionWindow * TEN_TIMES_BETA_CUBIC / 10);
// // //     }

// // //     if (Connection->Stats.Timing.Start != 0) {
// // //         double ElapsedMilliseconds = (double)(CxPlatTimeUs64() - Connection->Stats.Timing.Start) / 1000.0;
// // //         printf("[CubicProbe][%p][%.3fms] CWND Update (Congestion Event): %u -> %u\n",
// // //             (void*)Connection, ElapsedMilliseconds, PrevCwnd, Cubic->CongestionWindow);
// // //     }
// // // }

// // // _IRQL_requires_max_(PASSIVE_LEVEL)
// // // void
// // // CubicProbeCongestionControlOnDataSent(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ uint32_t NumRetransmittableBytes
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// // //     Cubic->BytesInFlight += NumRetransmittableBytes;
// // //     if (Cubic->BytesInFlightMax < Cubic->BytesInFlight) {
// // //         Cubic->BytesInFlightMax = Cubic->BytesInFlight;
// // //         QuicSendBufferConnectionAdjust(QuicCongestionControlGetConnection(Cc));
// // //     }

// // //     if (NumRetransmittableBytes > Cubic->LastSendAllowance) {
// // //         Cubic->LastSendAllowance = 0;
// // //     } else {
// // //         Cubic->LastSendAllowance -= NumRetransmittableBytes;
// // //     }

// // //     if (Cubic->Exemptions > 0) {
// // //         --Cubic->Exemptions;
// // //     }

// // //     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // BOOLEAN
// // // CubicProbeCongestionControlOnDataInvalidated(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ uint32_t NumRetransmittableBytes
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// // //     CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= NumRetransmittableBytes);
// // //     Cubic->BytesInFlight -= NumRetransmittableBytes;

// // //     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // BOOLEAN
// // // CubicProbeCongestionControlOnDataAcknowledged(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ const QUIC_ACK_EVENT* AckEvent
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     const uint64_t TimeNowUs = AckEvent->TimeNow;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);
// // //     uint32_t BytesAcked = AckEvent->NumRetransmittableBytes;

// // //     CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= BytesAcked);
// // //     Cubic->BytesInFlight -= BytesAcked;

// // //     if (Cubic->IsInRecovery) {
// // //         if (AckEvent->LargestAck > Cubic->RecoverySentPacketNumber) {
// // //             Cubic->IsInRecovery = FALSE;
// // //             Cubic->IsInPersistentCongestion = FALSE;
// // //             Cubic->TimeOfCongAvoidStart = TimeNowUs;
// // //         }
// // //         goto Exit;
// // //     } else if (BytesAcked == 0) {
// // //         goto Exit;
// // //     }

// // //     if (Connection->Settings.HyStartEnabled && Cubic->HyStartState != HYSTART_DONE) {
// // //         if (AckEvent->MinRttValid) {
// // //             if (Cubic->HyStartAckCount < QUIC_HYSTART_DEFAULT_N_SAMPLING) {
// // //                 Cubic->MinRttInCurrentRound = CXPLAT_MIN(Cubic->MinRttInCurrentRound, AckEvent->MinRtt);
// // //                 Cubic->HyStartAckCount++;
// // //             } else if (Cubic->HyStartState == HYSTART_NOT_STARTED) {
// // //                 const uint64_t Eta = CXPLAT_MIN(QUIC_HYSTART_DEFAULT_MAX_ETA, CXPLAT_MAX(QUIC_HYSTART_DEFAULT_MIN_ETA, Cubic->MinRttInLastRound / 8));
// // //                 if (Cubic->MinRttInLastRound != UINT64_MAX && Cubic->MinRttInCurrentRound != UINT64_MAX && (Cubic->MinRttInCurrentRound >= Cubic->MinRttInLastRound + Eta)) {
// // //                     CubicProbeHyStartChangeState(Cc, HYSTART_ACTIVE);
// // //                     Cubic->CWndSlowStartGrowthDivisor = QUIC_CONSERVATIVE_SLOW_START_DEFAULT_GROWTH_DIVISOR;
// // //                     Cubic->ConservativeSlowStartRounds = QUIC_CONSERVATIVE_SLOW_START_DEFAULT_ROUNDS;
// // //                     Cubic->CssBaselineMinRtt = Cubic->MinRttInCurrentRound;
// // //                 }
// // //             } else {
// // //                 if (Cubic->MinRttInCurrentRound < Cubic->CssBaselineMinRtt) {
// // //                     CubicProbeHyStartChangeState(Cc, HYSTART_NOT_STARTED);
// // //                 }
// // //             }
// // //         }
// // //         if (AckEvent->LargestAck >= Cubic->HyStartRoundEnd) {
// // //             Cubic->HyStartRoundEnd = Connection->Send.NextPacketNumber;
// // //             if (Cubic->HyStartState == HYSTART_ACTIVE) {
// // //                 if (--Cubic->ConservativeSlowStartRounds == 0) {
// // //                     Cubic->SlowStartThreshold = Cubic->CongestionWindow;
// // //                     Cubic->TimeOfCongAvoidStart = TimeNowUs;
// // //                     Cubic->AimdWindow = Cubic->CongestionWindow;
// // //                     CubicProbeHyStartChangeState(Cc, HYSTART_DONE);
// // //                 }
// // //             }
// // //             CubicProbeHyStartResetPerRttRound(Cubic);
// // //         }
// // //     }

// // //     if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
// // //         uint32_t PrevCwnd = Cubic->CongestionWindow;
// // //         Cubic->CongestionWindow += (BytesAcked / Cubic->CWndSlowStartGrowthDivisor);
        
// // //         if (PrevCwnd != Cubic->CongestionWindow && Connection->Stats.Timing.Start != 0) {
// // //             double ElapsedMilliseconds = (double)(TimeNowUs - Connection->Stats.Timing.Start) / 1000.0;
// // //             printf("[CubicProbe][%p][%.3fms] CWND Update (SlowStart): %u -> %u\n",
// // //                 (void*)Connection, ElapsedMilliseconds, PrevCwnd, Cubic->CongestionWindow);
// // //         }

// // //         BytesAcked = 0;
// // //         if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
// // //             Cubic->TimeOfCongAvoidStart = TimeNowUs;
// // //             BytesAcked = Cubic->CongestionWindow - Cubic->SlowStartThreshold;
// // //             Cubic->CongestionWindow = Cubic->SlowStartThreshold;
// // //         }
// // //     }

// // //     if (BytesAcked > 0) {
// // //         CXPLAT_DBG_ASSERT(Cubic->CongestionWindow >= Cubic->SlowStartThreshold);
// // //         const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(&Connection->Paths[0]);

// // //         if (Cubic->TimeOfLastAckValid) {
// // //             const uint64_t TimeSinceLastAck = CxPlatTimeDiff64(Cubic->TimeOfLastAck, TimeNowUs);
// // //             if (TimeSinceLastAck > MS_TO_US((uint64_t)Cubic->SendIdleTimeoutMs) &&
// // //                 TimeSinceLastAck > (Connection->Paths[0].SmoothedRtt + 4 * Connection->Paths[0].RttVariance)) {
// // //                 Cubic->TimeOfCongAvoidStart += TimeSinceLastAck;
// // //                 if (CxPlatTimeAtOrBefore64(TimeNowUs, Cubic->TimeOfCongAvoidStart)) {
// // //                     Cubic->TimeOfCongAvoidStart = TimeNowUs;
// // //                 }
// // //             }
// // //         }

// // //         const uint64_t TimeInCongAvoidUs = CxPlatTimeDiff64(Cubic->TimeOfCongAvoidStart, TimeNowUs);
// // //         int64_t DeltaT = US_TO_MS((int64_t)TimeInCongAvoidUs - (int64_t)MS_TO_US(Cubic->KCubic) + (int64_t)AckEvent->SmoothedRtt);
// // //         if (DeltaT > 2500000) { DeltaT = 2500000; }

// // //         int64_t CubicWindow = ((((DeltaT * DeltaT) >> 10) * DeltaT * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20) + (int64_t)Cubic->WindowMax;
// // //         if (CubicWindow < 0) { CubicWindow = 2 * Cubic->BytesInFlightMax; }

// // //         CXPLAT_STATIC_ASSERT(TEN_TIMES_BETA_CUBIC == 7, "TEN_TIMES_BETA_CUBIC must be 7 for simplified calculation.");
// // //         if (Cubic->AimdWindow < Cubic->WindowPrior) {
// // //             Cubic->AimdAccumulator += BytesAcked / 2;
// // //         } else {
// // //             Cubic->AimdAccumulator += BytesAcked;
// // //         }
// // //         if (Cubic->AimdAccumulator > Cubic->AimdWindow) {
// // //             Cubic->AimdWindow += DatagramPayloadLength;
// // //             Cubic->AimdAccumulator -= Cubic->AimdWindow;
// // //         }

// // //         uint32_t PrevCwnd = Cubic->CongestionWindow;
// // //         if (Cubic->AimdWindow > CubicWindow) {
// // //             Cubic->CongestionWindow = Cubic->AimdWindow;
// // //         } else {
// // //             uint64_t TargetWindow = CXPLAT_MAX(Cubic->CongestionWindow, CXPLAT_MIN((uint64_t)CubicWindow, (uint64_t)Cubic->CongestionWindow + (Cubic->CongestionWindow >> 1)));
// // //             if (Cubic->CongestionWindow >= DatagramPayloadLength) {
// // //                  Cubic->CongestionWindow += (uint32_t)(((TargetWindow - Cubic->CongestionWindow) * DatagramPayloadLength) / Cubic->CongestionWindow);
// // //             } else {
// // //                  Cubic->CongestionWindow += DatagramPayloadLength;
// // //             }
// // //         }

// // //         if (PrevCwnd != Cubic->CongestionWindow && Connection->Stats.Timing.Start != 0) {
// // //             double ElapsedMilliseconds = (double)(TimeNowUs - Connection->Stats.Timing.Start) / 1000.0;
// // //             printf("[CubicProbe][%p][%.3fms] CWND Update (CUBIC): %u -> %u\n",
// // //                 (void*)Connection, ElapsedMilliseconds, PrevCwnd, Cubic->CongestionWindow);
// // //         }

// // //         if (AckEvent->MinRttValid && DatagramPayloadLength > 0) {
// // //             switch (CubicProbe->ProbeState) {
// // //                 case PROBE_INACTIVE: {
// // //                     if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow >= Cubic->WindowLastMax) {
// // //                         CubicProbe->RttCount++;
// // //                         if (CubicProbe->RttCount >= PROBE_RTT_INTERVAL) {
// // //                             CubicProbe->ProbeState = PROBE_TEST;
// // //                             CubicProbe->CumulativeSuccessLevel = 1;
// // //                             CubicProbe->RttAtProbeStartUs = AckEvent->MinRtt;
// // //                             CubicProbe->RttCount = 0;
// // //                         }
// // //                     } else {
// // //                         CubicProbe->RttCount = 0;
// // //                     }
// // //                     break;
// // //                 }
// // //                 case PROBE_TEST:
// // //                     CubicProbe->ProbeState = PROBE_JUDGMENT;
// // //                      __fallthrough;
// // //                 case PROBE_JUDGMENT: {
// // //                     if (CubicProbe->RttAtProbeStartUs == 0) {
// // //                         CubicProbeResetProbeState(CubicProbe);
// // //                         break;
// // //                     }
// // //                     if (AckEvent->MinRtt * PROBE_RTT_INCREASE_DENOMINATOR <= CubicProbe->RttAtProbeStartUs * PROBE_RTT_INCREASE_NUMERATOR) {
// // //                         CubicProbe->ProbeState = PROBE_TEST;
// // //                         CubicProbe->CumulativeSuccessLevel++;
// // //                         uint32_t GrowthInBytes = ((CubicProbe->CumulativeSuccessLevel / 2) + 1) * DatagramPayloadLength;
                        
// // //                         PrevCwnd = Cubic->CongestionWindow;
// // //                         Cubic->CongestionWindow += GrowthInBytes;

// // //                         if (Connection->Stats.Timing.Start != 0) {
// // //                             double ElapsedMilliseconds = (double)(TimeNowUs - Connection->Stats.Timing.Start) / 1000.0;
// // //                             printf("[CubicProbe][%p][%.3fms] CWND Update (Aggressive Growth Lvl %u): %u -> %u\n",
// // //                                 (void*)Connection, ElapsedMilliseconds, CubicProbe->CumulativeSuccessLevel, PrevCwnd, Cubic->CongestionWindow);
// // //                         }
// // //                     } else {
// // //                         CubicProbeResetProbeState(CubicProbe);
// // //                         Cubic->TimeOfCongAvoidStart = TimeNowUs;
// // //                     }
// // //                     break;
// // //                 }
// // //             }
// // //         }
// // //     }

// // //     if (Cubic->CongestionWindow > 2 * Cubic->BytesInFlightMax) {
// // //         Cubic->CongestionWindow = 2 * Cubic->BytesInFlightMax;
// // //     }

// // // Exit:
// // //     Cubic->TimeOfLastAck = TimeNowUs;
// // //     Cubic->TimeOfLastAckValid = TRUE;

// // //     if (Connection->Settings.NetStatsEventEnabled) {
// // //         // ...
// // //     }

// // //     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void
// // // CubicProbeCongestionControlOnDataLost(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ const QUIC_LOSS_EVENT* LossEvent
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// // //     if (Connection->Stats.Timing.Start != 0) {
// // //         double ElapsedMilliseconds = (double)(CxPlatTimeUs64() - Connection->Stats.Timing.Start) / 1000.0;
// // //         printf("[CubicProbe][%p][%.3fms] LOSS: CWnd=%u, InFlight=%u, LostBytes=%u\n",
// // //             (void*)Connection, ElapsedMilliseconds, Cubic->CongestionWindow, Cubic->BytesInFlight, LossEvent->NumRetransmittableBytes);
// // //     }

// // //     if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
// // //         Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
// // //         CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE);
// // //     }

// // //     CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= LossEvent->NumRetransmittableBytes);
// // //     Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;

// // //     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void
// // // CubicProbeCongestionControlOnEcn(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ const QUIC_ECN_EVENT* EcnEvent
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// // //     if (Connection->Stats.Timing.Start != 0) {
// // //         double ElapsedMilliseconds = (double)(CxPlatTimeUs64() - Connection->Stats.Timing.Start) / 1000.0;
// // //         printf("[CubicProbe][%p][%.3fms] ECN: CWnd=%u, InFlight=%u\n",
// // //             (void*)Connection, ElapsedMilliseconds, Cubic->CongestionWindow, Cubic->BytesInFlight);
// // //     }
    
// // //     if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
// // //         Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
// // //         QuicCongestionControlGetConnection(Cc)->Stats.Send.EcnCongestionCount++;
// // //         CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, TRUE);
// // //     }

// // //     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // BOOLEAN
// // // CubicProbeCongestionControlOnSpuriousCongestionEvent(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     if (!Cubic->IsInRecovery) {
// // //         return FALSE;
// // //     }
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// // //     UNREFERENCED_PARAMETER(Connection);

// // //     Cubic->WindowPrior = Cubic->PrevWindowPrior;
// // //     Cubic->WindowMax = Cubic->PrevWindowMax;
// // //     Cubic->WindowLastMax = Cubic->PrevWindowLastMax;
// // //     Cubic->KCubic = Cubic->PrevKCubic;
// // //     Cubic->SlowStartThreshold = Cubic->PrevSlowStartThreshold;
// // //     Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
// // //     Cubic->AimdWindow = Cubic->PrevAimdWindow;
// // //     Cubic->IsInRecovery = FALSE;
// // //     Cubic->HasHadCongestionEvent = FALSE;

// // //     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // void
// // // CubicProbeCongestionControlLogOutFlowStatus(
// // //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// // //     )
// // // {
// // //     UNREFERENCED_PARAMETER(Cc);
// // // }

// // // uint32_t
// // // CubicProbeCongestionControlGetBytesInFlightMax(
// // //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// // //     )
// // // {
// // //     return Cc->CubicProbe.Cubic.BytesInFlightMax;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // uint8_t
// // // CubicProbeCongestionControlGetExemptions(
// // //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// // //     )
// // // {
// // //     return Cc->CubicProbe.Cubic.Exemptions;
// // // }

// // // uint32_t
// // // CubicProbeCongestionControlGetCongestionWindow(
// // //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// // //     )
// // // {
// // //     return Cc->CubicProbe.Cubic.CongestionWindow;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // BOOLEAN
// // // CubicProbeCongestionControlIsAppLimited(
// // //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// // //     )
// // // {
// // //     UNREFERENCED_PARAMETER(Cc);
// // //     return FALSE;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void
// // // CubicProbeCongestionControlSetAppLimited(
// // //     _In_ struct QUIC_CONGESTION_CONTROL* Cc
// // //     )
// // // {
// // //     UNREFERENCED_PARAMETER(Cc);
// // // }

// // // void
// // // CubicProbeCongestionControlGetNetworkStatistics(
// // //     _In_ const QUIC_CONNECTION* const Connection,
// // //     _In_ const QUIC_CONGESTION_CONTROL* const Cc,
// // //     _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics
// // //     )
// // // {
// // //     const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     const QUIC_PATH* Path = &Connection->Paths[0];
// // //     NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
// // //     NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
// // //     NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
// // //     NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
// // //     NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
// // //     NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
// // // }

// // // static const QUIC_CONGESTION_CONTROL QuicCongestionControlCubicProbe = {
// // //     .Name = "CubicProbe",
// // //     .QuicCongestionControlCanSend = CubicProbeCongestionControlCanSend,
// // //     .QuicCongestionControlSetExemption = CubicProbeCongestionControlSetExemption,
// // //     .QuicCongestionControlReset = CubicProbeCongestionControlReset,
// // //     .QuicCongestionControlGetSendAllowance = CubicProbeCongestionControlGetSendAllowance,
// // //     .QuicCongestionControlOnDataSent = CubicProbeCongestionControlOnDataSent,
// // //     .QuicCongestionControlOnDataInvalidated = CubicProbeCongestionControlOnDataInvalidated,
// // //     .QuicCongestionControlOnDataAcknowledged = CubicProbeCongestionControlOnDataAcknowledged,
// // //     .QuicCongestionControlOnDataLost = CubicProbeCongestionControlOnDataLost,
// // //     .QuicCongestionControlOnEcn = CubicProbeCongestionControlOnEcn,
// // //     .QuicCongestionControlOnSpuriousCongestionEvent = CubicProbeCongestionControlOnSpuriousCongestionEvent,
// // //     .QuicCongestionControlLogOutFlowStatus = CubicProbeCongestionControlLogOutFlowStatus,
// // //     .QuicCongestionControlGetExemptions = CubicProbeCongestionControlGetExemptions,
// // //     .QuicCongestionControlGetBytesInFlightMax = CubicProbeCongestionControlGetBytesInFlightMax,
// // //     .QuicCongestionControlIsAppLimited = CubicProbeCongestionControlIsAppLimited,
// // //     .QuicCongestionControlSetAppLimited = CubicProbeCongestionControlSetAppLimited,
// // //     .QuicCongestionControlGetCongestionWindow = CubicProbeCongestionControlGetCongestionWindow,
// // //     .QuicCongestionControlGetNetworkStatistics = CubicProbeCongestionControlGetNetworkStatistics
// // // };

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void
// // // CubicProbeCongestionControlInitialize(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ const QUIC_SETTINGS_INTERNAL* Settings
// // //     )
// // // {
// // //     *Cc = QuicCongestionControlCubicProbe;

// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(&Connection->Paths[0]);

// // //     Cubic->SlowStartThreshold = UINT32_MAX;
// // //     Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
// // //     Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
// // //     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
// // //     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
// // //     Cubic->MinRttInCurrentRound = UINT64_MAX;
// // //     Cubic->HyStartRoundEnd = Connection->Send.NextPacketNumber;
// // //     Cubic->HyStartState = HYSTART_NOT_STARTED;
// // //     Cubic->CWndSlowStartGrowthDivisor = 1;
// // //     CubicProbeHyStartResetPerRttRound(Cubic);
// // //     CubicProbeResetProbeState(CubicProbe);
// // // }








// #include "precomp.h"
// #include <stdio.h>
// #include "cubicprobe.h"

// // Constants from RFC8312 (for CUBIC)
// #define TEN_TIMES_BETA_CUBIC 7
// #define TEN_TIMES_C_CUBIC 4

// // Constants for CubicProbe logic
// #define PROBE_RTT_INTERVAL 2
// #define PROBE_RTT_INCREASE_NUMERATOR 21   // 1.1x RTT threshold (수정 제안)
// #define PROBE_RTT_INCREASE_DENOMINATOR 20

// // Forward declarations
// static void CubicProbeCongestionControlOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn);
// static void CubicProbeUpdate(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _Out_ uint32_t* AckTarget);
// static void CubicProbeIncreaseWindow(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _In_ uint32_t AckTarget);
// static void CubicProbePktsAcked(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent);

// BOOLEAN CubicProbeCongestionControlCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc);
// static BOOLEAN CubicProbeCongestionControlUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState);

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static uint32_t
// CubeRoot(
//     uint32_t Radicand
//     )
// {
//     int i;
//     uint32_t x = 0;
//     uint32_t y = 0;
//     for (i = 30; i >= 0; i -= 3) {
//         x = x * 8 + ((Radicand >> i) & 7);
//         if ((y * 2 + 1) * (y * 2 + 1) * (y * 2 + 1) <= x) {
//             y = y * 2 + 1;
//         } else {
//             y = y * 2;
//         }
//     }
//     return y;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeResetProbeState(
//     _In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe
//     )
// {
//     CubicProbe->ProbeState = PROBE_INACTIVE;
//     CubicProbe->CumulativeSuccessLevel = 0;
//     CubicProbe->RttAtProbeStartUs = 0;
//     CubicProbe->ProbeTargetPacketNumber = 0;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void
// CubicProbeCongestionControlReset(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ BOOLEAN FullReset
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     Cubic->SlowStartThreshold = UINT32_MAX;
//     Cubic->IsInRecovery = FALSE;
//     Cubic->HasHadCongestionEvent = FALSE;
//     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
//     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
//     Cubic->LastSendAllowance = 0;
//     if (FullReset) {
//         Cubic->BytesInFlight = 0;
//     }
//     Cubic->WindowMax = 0;
//     Cubic->WindowLastMax = 0;

//     CubicProbeResetProbeState(CubicProbe);
//     CubicProbe->AckCountForGrowth = 0;
//     CubicProbe->MinRttUs = UINT64_MAX;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbePktsAcked(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;

//     if (!AckEvent->MinRttValid) return;

//     if (CubicProbe->MinRttUs == UINT64_MAX || CubicProbe->MinRttUs > AckEvent->MinRtt) {
//         CubicProbe->MinRttUs = AckEvent->MinRtt;
//     }

//     switch (CubicProbe->ProbeState)
//     {
//         case PROBE_INACTIVE:
//         {
//             if (Cubic->WindowMax > 0 && Cubic->CongestionWindow >= Cubic->WindowMax) {
//                     CubicProbe->ProbeState = PROBE_TEST;
//                     CubicProbe->RttAtProbeStartUs = AckEvent->MinRtt;
//                     printf("[CubicProbe] State -> PROBE_TEST. Triggering growth. RTT_Anchor=%.3fms\n", (double)AckEvent->MinRtt/1000.0);
//             }
//             break;
//         }

//         case PROBE_WAITING:
//         {
//             if (AckEvent->LargestAck >= CubicProbe->ProbeTargetPacketNumber) {
//                 CubicProbe->ProbeState = PROBE_JUDGMENT;
//                 printf("[CubicProbe] State -> PROBE_JUDGMENT. Target ACK received.\n");
//             } else {
//                 break;
//             }
//         }
//         // fall through

//         case PROBE_JUDGMENT:
//         {
//             if (AckEvent->MinRtt * PROBE_RTT_INCREASE_DENOMINATOR <= CubicProbe->RttAtProbeStartUs * PROBE_RTT_INCREASE_NUMERATOR) {
//                 CubicProbe->ProbeState = PROBE_TEST;
//                 CubicProbe->CumulativeSuccessLevel++;
//                 printf("[CubicProbe] PROBE SUCCEEDED (Lvl %u), RTT=%.3fms <= Anchor*1.xx\n", // Adjusted printf
//                     CubicProbe->CumulativeSuccessLevel, (double)AckEvent->MinRtt / 1000.0);
//             } else {
//                 printf("[CubicProbe] PROBE FAILED (RTT Spike): CWnd=%u. RTT=%.3fms > Anchor*1.xx. Treating as congestion event.\n", // Adjusted printf
//                     Cubic->CongestionWindow, (double)AckEvent->MinRtt / 1000.0);
//                 CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, FALSE);
//             }
//             break;
//         }

//         default:
//             break;
//     }
// }

// // ======================= [ START OF MODIFICATION ] =======================
// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeUpdate(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent,
//     _Out_ uint32_t* AckTarget
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     // ✅ [수정] 연속 가속 모드 (프로브 1회 이상 성공) 인 경우
//     if (CubicProbe->CumulativeSuccessLevel > 0) {
//         // W_cubic 계산 및 비교를 생략하고, 즉시 공격적인 성장 로직으로 진입합니다.
//         // RTT마다 CWND를 최소 1세그먼트씩 키우도록 DiffSegments를 1로 설정하여
//         // AckTarget이 CwndSegments가 되도록 합니다.
//         // (이후 accelerationFactor에 의해 더 작아져서 RTT보다 더 빠르게 성장)
//         uint32_t CwndSegments = Cubic->CongestionWindow / DatagramPayloadLength;
//         uint32_t DiffSegments = 1;
//         *AckTarget = CwndSegments / DiffSegments;

//     } else {
//         // 🐢 기존 로직 (첫 프로브 진입 전)
//         if (Cubic->TimeOfCongAvoidStart == 0) {
//             Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
//             if (Cubic->CongestionWindow < Cubic->WindowMax) {
//                 if (DatagramPayloadLength > 0) {
//                     uint32_t W_max_in_mss = (Cubic->WindowMax - Cubic->CongestionWindow) / DatagramPayloadLength;
//                     uint32_t radicand = (W_max_in_mss * (10) << 9) / TEN_TIMES_C_CUBIC;
//                     Cubic->KCubic = CubeRoot(radicand);
//                     Cubic->KCubic = S_TO_MS(Cubic->KCubic);
//                     Cubic->KCubic >>= 3;
//                 } else {
//                     Cubic->KCubic = 0;
//                 }
//             } else {
//                 Cubic->KCubic = 0;
//                 Cubic->WindowMax = Cubic->CongestionWindow;
//             }
//         }

//         const uint32_t W_max_bytes = Cubic->WindowMax;
//         const uint64_t t_us = CxPlatTimeDiff64(Cubic->TimeOfCongAvoidStart, AckEvent->TimeNow);
//         const uint64_t K_us = (uint64_t)Cubic->KCubic * 1000;

//         int64_t TimeDeltaUs = (int64_t)t_us - (int64_t)K_us;
//         int64_t OffsetMs = (TimeDeltaUs / 1000);
//         int64_t CubicTerm = ((((OffsetMs * OffsetMs) >> 10) * OffsetMs * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20);

//         uint32_t W_cubic_bytes;
//         if (TimeDeltaUs < 0) {
//             W_cubic_bytes = W_max_bytes - (uint32_t)(-CubicTerm);
//         } else {
//             W_cubic_bytes = W_max_bytes + (uint32_t)CubicTerm;
//         }

//         if (W_cubic_bytes > Cubic->CongestionWindow) {
//             uint32_t CwndSegments = Cubic->CongestionWindow / DatagramPayloadLength;
//             uint32_t TargetSegments = W_cubic_bytes / DatagramPayloadLength;
//             uint32_t DiffSegments = TargetSegments > CwndSegments ? TargetSegments - CwndSegments : 1;
//             *AckTarget = CwndSegments / DiffSegments;
//         } else {
//             *AckTarget = 100 * (Cubic->CongestionWindow / DatagramPayloadLength);
//         }
//     }

//     // 🚀 공통 가속 로직 (기존 코드와 동일)
//     if (CubicProbe->CumulativeSuccessLevel > 1) {
//         double accelerationFactor = 1.0 + CubicProbe->CumulativeSuccessLevel;
//         if (accelerationFactor > 1.0) {
//             *AckTarget = (uint32_t)(*AckTarget / accelerationFactor);
//         }
//     }

//     if (*AckTarget < 2) *AckTarget = 2;
// }
// // ======================== [ END OF MODIFICATION ] ========================

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeIncreaseWindow(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent,
//     _In_ uint32_t AckTarget
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     const QUIC_PATH* Path = &Connection->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     uint32_t AckedSegments = (AckEvent->NumRetransmittableBytes + DatagramPayloadLength - 1) / DatagramPayloadLength;
//     CubicProbe->AckCountForGrowth += AckedSegments;

//     if (CubicProbe->AckCountForGrowth >= AckTarget) {
//         uint32_t GrowthInSegments = 1;
//         if (CubicProbe->CumulativeSuccessLevel > 0) {
//             GrowthInSegments = CubicProbe->CumulativeSuccessLevel - 1;
//             if (GrowthInSegments < 1) {
//                 GrowthInSegments = 1;
//             }
//         }

//         uint32_t PrevCwnd = Cubic->CongestionWindow;
//         Cubic->CongestionWindow += (GrowthInSegments * DatagramPayloadLength);

//         CubicProbe->AckCountForGrowth -= AckTarget;

//         if (CubicProbe->ProbeState == PROBE_TEST) {
//             CubicProbe->ProbeState = PROBE_WAITING;
//             CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber - 1;
//             printf("[CubicProbe] CWND Grown for Probe. State -> PROBE_WAITING. TargetPkt=%lu\n",
//                 CubicProbe->ProbeTargetPacketNumber);
//         }

//         if (CubicProbe->ProbeState != PROBE_INACTIVE) {
//             printf("[CubicProbe][%p][%.3fms] CWND Update (Probe Lvl %u): %u -> %u (Target=%u, Growth=%u segs)\n",
//                 (void*)Connection, (double)AckEvent->TimeNow / 1000.0, CubicProbe->CumulativeSuccessLevel, PrevCwnd, Cubic->CongestionWindow, AckTarget, GrowthInSegments);
//         } else {
//              printf("[CubicProbe][%p][%.3fms] CWND Update (CUBIC): %u -> %u (Target=%u)\n",
//                 (void*)Connection, (double)AckEvent->TimeNow / 1000.0, PrevCwnd, Cubic->CongestionWindow, AckTarget);
//         }
//     }
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// BOOLEAN
// CubicProbeCongestionControlOnDataAcknowledged(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     Cubic->BytesInFlight -= AckEvent->NumRetransmittableBytes;

//     if (Cubic->IsInRecovery) {
//         if (AckEvent->LargestAck > Cubic->RecoverySentPacketNumber) {
//             Cubic->IsInRecovery = FALSE;
//         }
//         goto Exit;
//     }
//     if (AckEvent->NumRetransmittableBytes == 0) {
//         goto Exit;
//     }

//     if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
//         uint32_t PrevCwnd = Cubic->CongestionWindow;

//         Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;

//         printf("[CubicProbe][%p][%.3fms] CWND Update (SlowStart): %u -> %u\n",
//             (void*)Connection,
//             (double)AckEvent->TimeNow / 1000.0,
//             PrevCwnd,
//             Cubic->CongestionWindow);

//         if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
//             Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;

//             printf("[CubicProbe-SS][%p][%.3fms] SlowStart -> CongestionAvoidance (Ssthresh: %u)\n",
//                 (void*)Connection,
//                 (double)AckEvent->TimeNow / 1000.0,
//                 Cubic->SlowStartThreshold);
//         }

//     } else {
//         const QUIC_PATH* Path = &Connection->Paths[0];
//         const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
//         if (DatagramPayloadLength == 0) goto Exit;

//         CubicProbePktsAcked(Cc, AckEvent);

//         uint32_t AckTarget = 0;
//         CubicProbeUpdate(Cc, AckEvent, &AckTarget);

//         CubicProbeIncreaseWindow(Cc, AckEvent, AckTarget);
//     }

// Exit:
//     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeCongestionControlOnCongestionEvent(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ BOOLEAN IsPersistentCongestion,
//     _In_ BOOLEAN Ecn
//     )
// {
//     UNREFERENCED_PARAMETER(IsPersistentCongestion);
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     const QUIC_PATH* Path = &Connection->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
//     uint32_t PrevCwnd = Cubic->CongestionWindow;

//     CubicProbeResetProbeState(CubicProbe);
//     CubicProbe->AckCountForGrowth = 0;

//     if (!Cubic->IsInRecovery) {
//          Cubic->IsInRecovery = TRUE;
//     }
//     Cubic->HasHadCongestionEvent = TRUE;

//     if (!Ecn) {
//         Cubic->PrevCongestionWindow = Cubic->CongestionWindow;
//     }

//     Cubic->WindowLastMax = Cubic->WindowMax;
//     Cubic->WindowMax = Cubic->CongestionWindow;

//     if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow < Cubic->WindowLastMax) {
//         Cubic->WindowMax = (uint32_t)(Cubic->CongestionWindow * (10.0 + TEN_TIMES_BETA_CUBIC) / 20.0);
//     }

//     uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
//     Cubic->SlowStartThreshold =
//     Cubic->CongestionWindow =
//         CXPLAT_MAX(
//             MinCongestionWindow,
//             (uint32_t)(Cubic->CongestionWindow * ((double)TEN_TIMES_BETA_CUBIC / 10.0)));

//     Cubic->TimeOfCongAvoidStart = 0;

//     printf("[Cubic][%p][%.3fms] CWND Update (Congestion Event): %u -> %u\n",
//         (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, PrevCwnd, Cubic->CongestionWindow);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void CubicProbeCongestionControlSetExemption(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint8_t NumPackets);
// uint32_t CubicProbeCongestionControlGetSendAllowance(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint64_t TimeSinceLastSend, _In_ BOOLEAN TimeSinceLastSendValid);
// void CubicProbeCongestionControlOnDataSent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
// BOOLEAN CubicProbeCongestionControlOnDataInvalidated( _In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
// void CubicProbeCongestionControlOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent);
// void CubicProbeCongestionControlOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent);
// BOOLEAN CubicProbeCongestionControlOnSpuriousCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc);
// void CubicProbeCongestionControlLogOutFlowStatus(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// uint8_t CubicProbeCongestionControlGetExemptions(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// uint32_t CubicProbeCongestionControlGetBytesInFlightMax(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// BOOLEAN CubicProbeCongestionControlIsAppLimited(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// void CubicProbeCongestionControlSetAppLimited(_In_ QUIC_CONGESTION_CONTROL* Cc);
// uint32_t CubicProbeCongestionControlGetCongestionWindow(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// void CubicProbeCongestionControlGetNetworkStatistics(_In_ const QUIC_CONNECTION* const Connection, _In_ const QUIC_CONGESTION_CONTROL* const Cc, _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics);

// BOOLEAN
// CubicProbeCongestionControlCanSend(
//     _In_ QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
// }

// void
// CubicProbeCongestionControlSetExemption(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ uint8_t NumPackets
//     )
// {
//     Cc->CubicProbe.Cubic.Exemptions = NumPackets;
// }

// uint32_t
// CubicProbeCongestionControlGetSendAllowance(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ uint64_t TimeSinceLastSend,
//     _In_ BOOLEAN TimeSinceLastSendValid
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     uint32_t SendAllowance;

//     if (Cubic->BytesInFlight >= Cubic->CongestionWindow) {
//         SendAllowance = 0;
//     } else if (!TimeSinceLastSendValid || !Connection->Settings.PacingEnabled || !Connection->Paths[0].GotFirstRttSample || Connection->Paths[0].SmoothedRtt < QUIC_MIN_PACING_RTT) {
//         SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
//     } else {
//         uint64_t EstimatedWnd;
//         if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
//             EstimatedWnd = (uint64_t)Cubic->CongestionWindow << 1;
//             if (EstimatedWnd > Cubic->SlowStartThreshold) {
//                 EstimatedWnd = Cubic->SlowStartThreshold;
//             }
//         } else {
//             EstimatedWnd = Cubic->CongestionWindow + (Cubic->CongestionWindow >> 2);
//         }
//         SendAllowance = Cubic->LastSendAllowance + (uint32_t)((EstimatedWnd * TimeSinceLastSend) / Connection->Paths[0].SmoothedRtt);
//         if (SendAllowance < Cubic->LastSendAllowance || SendAllowance > (Cubic->CongestionWindow - Cubic->BytesInFlight)) {
//             SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
//         }
//         Cubic->LastSendAllowance = SendAllowance;
//     }
//     return SendAllowance;
// }

// void
// CubicProbeCongestionControlOnDataSent(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ uint32_t NumRetransmittableBytes
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     Cubic->BytesInFlight += NumRetransmittableBytes;
//     if (Cubic->BytesInFlightMax < Cubic->BytesInFlight) {
//         Cubic->BytesInFlightMax = Cubic->BytesInFlight;
//         QuicSendBufferConnectionAdjust(QuicCongestionControlGetConnection(Cc));
//     }
//     if (NumRetransmittableBytes > Cubic->LastSendAllowance) {
//         Cubic->LastSendAllowance = 0;
//     } else {
//         Cubic->LastSendAllowance -= NumRetransmittableBytes;
//     }
//     if (Cubic->Exemptions > 0) {
//         --Cubic->Exemptions;
//     }

//     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// BOOLEAN
// CubicProbeCongestionControlOnDataInvalidated(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ uint32_t NumRetransmittableBytes
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= NumRetransmittableBytes);
//     Cubic->BytesInFlight -= NumRetransmittableBytes;

//     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// void
// CubicProbeCongestionControlOnDataLost(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_LOSS_EVENT* LossEvent
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     printf("[Cubic][%p][%.3fms] LOSS EVENT: CWnd=%u, InFlight=%u, LostBytes=%u\n",
//         (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight, LossEvent->NumRetransmittableBytes);

//     if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
//         Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
//         CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE);
//     }

//     CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= LossEvent->NumRetransmittableBytes);
//     Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;

//     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// void
// CubicProbeCongestionControlOnEcn(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ECN_EVENT* EcnEvent
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     printf("[Cubic][%p][%.3fms] ECN EVENT: CWnd=%u, InFlight=%u\n",
//         (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight);

//     if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
//         Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
//         Connection->Stats.Send.EcnCongestionCount++;
//         CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, TRUE);
//     }

//     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// BOOLEAN
// CubicProbeCongestionControlOnSpuriousCongestionEvent(
//     _In_ QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     if (!Cubic->IsInRecovery) return FALSE;
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
//     Cubic->IsInRecovery = FALSE;
//     Cubic->HasHadCongestionEvent = FALSE;

//     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// void
// CubicProbeCongestionControlLogOutFlowStatus(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     UNREFERENCED_PARAMETER(Cc);
// }

// uint32_t
// CubicProbeCongestionControlGetBytesInFlightMax(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     return Cc->CubicProbe.Cubic.BytesInFlightMax;
// }

// uint8_t
// CubicProbeCongestionControlGetExemptions(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     return Cc->CubicProbe.Cubic.Exemptions;
// }

// uint32_t
// CubicProbeCongestionControlGetCongestionWindow(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     return Cc->CubicProbe.Cubic.CongestionWindow;
// }

// BOOLEAN
// CubicProbeCongestionControlIsAppLimited(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     UNREFERENCED_PARAMETER(Cc);
//     return FALSE;
// }

// void
// CubicProbeCongestionControlSetAppLimited(
//     _In_ struct QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     UNREFERENCED_PARAMETER(Cc);
// }

// void
// CubicProbeCongestionControlGetNetworkStatistics(
//     _In_ const QUIC_CONNECTION* const Connection,
//     _In_ const QUIC_CONGESTION_CONTROL* const Cc,
//     _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics
//     )
// {
//     const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     const QUIC_PATH* Path = &Connection->Paths[0];

//     NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
//     NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
//     NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
//     NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
//     NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
//     NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
// }

// static BOOLEAN
// CubicProbeCongestionControlUpdateBlockedState(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ BOOLEAN PreviousCanSendState
//     )
// {
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     if (PreviousCanSendState != CubicProbeCongestionControlCanSend(Cc)) {
//         if (PreviousCanSendState) {
//             QuicConnAddOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
//         } else {
//             QuicConnRemoveOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
//             Connection->Send.LastFlushTime = CxPlatTimeUs64();
//             return TRUE;
//         }
//     }
//     return FALSE;
// }

// static const QUIC_CONGESTION_CONTROL QuicCongestionControlCubicProbe = {
//     .Name = "CubicProbe",
//     .QuicCongestionControlCanSend = CubicProbeCongestionControlCanSend,
//     .QuicCongestionControlSetExemption = CubicProbeCongestionControlSetExemption,
//     .QuicCongestionControlReset = CubicProbeCongestionControlReset,
//     .QuicCongestionControlGetSendAllowance = CubicProbeCongestionControlGetSendAllowance,
//     .QuicCongestionControlOnDataSent = CubicProbeCongestionControlOnDataSent,
//     .QuicCongestionControlOnDataInvalidated = CubicProbeCongestionControlOnDataInvalidated,
//     .QuicCongestionControlOnDataAcknowledged = CubicProbeCongestionControlOnDataAcknowledged,
//     .QuicCongestionControlOnDataLost = CubicProbeCongestionControlOnDataLost,
//     .QuicCongestionControlOnEcn = CubicProbeCongestionControlOnEcn,
//     .QuicCongestionControlOnSpuriousCongestionEvent = CubicProbeCongestionControlOnSpuriousCongestionEvent,
//     .QuicCongestionControlLogOutFlowStatus = CubicProbeCongestionControlLogOutFlowStatus,
//     .QuicCongestionControlGetExemptions = CubicProbeCongestionControlGetExemptions,
//     .QuicCongestionControlGetBytesInFlightMax = CubicProbeCongestionControlGetBytesInFlightMax,
//     .QuicCongestionControlIsAppLimited = CubicProbeCongestionControlIsAppLimited,
//     .QuicCongestionControlSetAppLimited = CubicProbeCongestionControlSetAppLimited,
//     .QuicCongestionControlGetCongestionWindow = CubicProbeCongestionControlGetCongestionWindow,
//     .QuicCongestionControlGetNetworkStatistics = CubicProbeCongestionControlGetNetworkStatistics
// };

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void
// CubicProbeCongestionControlInitialize(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_SETTINGS_INTERNAL* Settings
//     )
// {
//     *Cc = QuicCongestionControlCubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     Cubic->SlowStartThreshold = UINT32_MAX;
//     Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
//     Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
//     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
//     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
//     Cubic->WindowMax = 0;
//     Cubic->WindowLastMax = 0;

//     CubicProbeResetProbeState(CubicProbe);
//     CubicProbe->AckCountForGrowth = 0;
//     CubicProbe->MinRttUs = UINT64_MAX;
// }




// 20251023까지 좋았음 코드








#include "precomp.h"
#include <stdio.h>
#include "cubicprobe.h"

// Constants from RFC8312 (for CUBIC)
#define TEN_TIMES_BETA_CUBIC 7 // [유지] 일반 손실/ECN은 0.7배 감소
#define TEN_TIMES_C_CUBIC 4

// Constants for CubicProbe logic
#define TEN_TIMES_BETA_PROBE 5 // [신규] 프로브 실패(RTT 스파이크) 시 0.5배 감소
#define PROBE_RTT_INTERVAL 2
#define PROBE_RTT_INCREASE_NUMERATOR 21   // 1.1x RTT threshold (수정 제안)
#define PROBE_RTT_INCREASE_DENOMINATOR 20

// Forward declarations
// [수정] 혼잡 이벤트 함수가 감소 계수(beta)를 인자로 받도록 변경
static void CubicProbeCongestionControlOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn, _In_ uint32_t TenTimesBeta);
static void CubicProbeUpdate(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _Out_ uint32_t* AckTarget);
static void CubicProbeIncreaseWindow(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _In_ uint32_t AckTarget);
static void CubicProbePktsAcked(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent);

BOOLEAN CubicProbeCongestionControlCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc);
static BOOLEAN CubicProbeCongestionControlUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState);

_IRQL_requires_max_(DISPATCH_LEVEL)
static uint32_t
CubeRoot(
    uint32_t Radicand
    )
{
    int i;
    uint32_t x = 0;
    uint32_t y = 0;
    for (i = 30; i >= 0; i -= 3) {
        x = x * 8 + ((Radicand >> i) & 7);
        if ((y * 2 + 1) * (y * 2 + 1) * (y * 2 + 1) <= x) {
            y = y * 2 + 1;
        } else {
            y = y * 2;
        }
    }
    return y;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeResetProbeState(
    _In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe
    )
{
    CubicProbe->ProbeState = PROBE_INACTIVE;
    CubicProbe->CumulativeSuccessLevel = 0;
    CubicProbe->RttAtProbeStartUs = 0;
    CubicProbe->ProbeTargetPacketNumber = 0;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlReset(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ BOOLEAN FullReset
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    Cubic->SlowStartThreshold = UINT32_MAX;
    Cubic->IsInRecovery = FALSE;
    Cubic->HasHadCongestionEvent = FALSE;
    Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
    Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
    Cubic->LastSendAllowance = 0;
    if (FullReset) {
        Cubic->BytesInFlight = 0;
    }
    Cubic->WindowMax = 0;
    Cubic->WindowLastMax = 0;

    CubicProbeResetProbeState(CubicProbe);
    CubicProbe->AckCountForGrowth = 0;
    CubicProbe->MinRttUs = UINT64_MAX;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbePktsAcked(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;

    if (!AckEvent->MinRttValid) return;

    if (CubicProbe->MinRttUs == UINT64_MAX || CubicProbe->MinRttUs > AckEvent->MinRtt) {
        CubicProbe->MinRttUs = AckEvent->MinRtt;
    }

    switch (CubicProbe->ProbeState)
    {
        case PROBE_INACTIVE:
        {
            if (Cubic->WindowMax > 0 && Cubic->CongestionWindow >= Cubic->WindowMax) {
                    CubicProbe->ProbeState = PROBE_TEST;
                    CubicProbe->RttAtProbeStartUs = AckEvent->MinRtt;
                    printf("[CubicProbe] State -> PROBE_TEST. Triggering growth. RTT_Anchor=%.3fms\n", (double)AckEvent->MinRtt/1000.0);
            }
            break;
        }

        case PROBE_WAITING:
        {
            if (AckEvent->LargestAck >= CubicProbe->ProbeTargetPacketNumber) {
                CubicProbe->ProbeState = PROBE_JUDGMENT;
                printf("[CubicProbe] State -> PROBE_JUDGMENT. Target ACK received.\n");
            } else {
                break;
            }
        }
        // fall through

        case PROBE_JUDGMENT:
        {
            if (AckEvent->MinRtt * PROBE_RTT_INCREASE_DENOMINATOR <= CubicProbe->RttAtProbeStartUs * PROBE_RTT_INCREASE_NUMERATOR) {
                CubicProbe->ProbeState = PROBE_TEST;
                CubicProbe->CumulativeSuccessLevel++;
                printf("[CubicProbe] PROBE SUCCEEDED (Lvl %u), RTT=%.3fms <= Anchor*1.xx\n", // Adjusted printf
                    CubicProbe->CumulativeSuccessLevel, (double)AckEvent->MinRtt / 1000.0);
            } else {
                printf("[CubicProbe] PROBE FAILED (RTT Spike): CWnd=%u. RTT=%.3fms > Anchor*1.xx. Treating as congestion event (0.5x).\n", // Adjusted printf
                    Cubic->CongestionWindow, (double)AckEvent->MinRtt / 1000.0);
                // [수정] 프로브 실패 시 0.5배 감소(TEN_TIMES_BETA_PROBE)를 사용하도록 호출
                CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, FALSE, TEN_TIMES_BETA_PROBE);
            }
            break;
        }

        default:
            break;
    }
}

// ======================= [ START OF MODIFICATION ] =======================
_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeUpdate(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent,
    _Out_ uint32_t* AckTarget
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    // ✅ [수정] 연속 가속 모드 (프로브 1회 이상 성공) 인 경우
    if (CubicProbe->CumulativeSuccessLevel > 0) {
        // W_cubic 계산 및 비교를 생략하고, 즉시 공격적인 성장 로직으로 진입합니다.
        // RTT마다 CWND를 최소 1세그먼트씩 키우도록 DiffSegments를 1로 설정하여
        // AckTarget이 CwndSegments가 되도록 합니다.
        // (이후 accelerationFactor에 의해 더 작아져서 RTT보다 더 빠르게 성장)
        uint32_t CwndSegments = Cubic->CongestionWindow / DatagramPayloadLength;
        uint32_t DiffSegments = 1;
        *AckTarget = CwndSegments / DiffSegments;

    } else {
        // 🐢 기존 로직 (첫 프로브 진입 전)
        if (Cubic->TimeOfCongAvoidStart == 0) {
            Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
            if (Cubic->CongestionWindow < Cubic->WindowMax) {
                if (DatagramPayloadLength > 0) {
                    uint32_t W_max_in_mss = (Cubic->WindowMax - Cubic->CongestionWindow) / DatagramPayloadLength;
                    uint32_t radicand = (W_max_in_mss * (10) << 9) / TEN_TIMES_C_CUBIC;
                    Cubic->KCubic = CubeRoot(radicand);
                    Cubic->KCubic = S_TO_MS(Cubic->KCubic);
                    Cubic->KCubic >>= 3;
                } else {
                    Cubic->KCubic = 0;
                }
            } else {
                Cubic->KCubic = 0;
                Cubic->WindowMax = Cubic->CongestionWindow;
            }
        }

        const uint32_t W_max_bytes = Cubic->WindowMax;
        const uint64_t t_us = CxPlatTimeDiff64(Cubic->TimeOfCongAvoidStart, AckEvent->TimeNow);
        const uint64_t K_us = (uint64_t)Cubic->KCubic * 1000;

        int64_t TimeDeltaUs = (int64_t)t_us - (int64_t)K_us;
        int64_t OffsetMs = (TimeDeltaUs / 1000);
        int64_t CubicTerm = ((((OffsetMs * OffsetMs) >> 10) * OffsetMs * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20);

        uint32_t W_cubic_bytes;
        if (TimeDeltaUs < 0) {
            W_cubic_bytes = W_max_bytes - (uint32_t)(-CubicTerm);
        } else {
            W_cubic_bytes = W_max_bytes + (uint32_t)CubicTerm;
        }

        if (W_cubic_bytes > Cubic->CongestionWindow) {
            uint32_t CwndSegments = Cubic->CongestionWindow / DatagramPayloadLength;
            uint32_t TargetSegments = W_cubic_bytes / DatagramPayloadLength;
            uint32_t DiffSegments = TargetSegments > CwndSegments ? TargetSegments - CwndSegments : 1;
            *AckTarget = CwndSegments / DiffSegments;
        } else {
            *AckTarget = 100 * (Cubic->CongestionWindow / DatagramPayloadLength);
        }
    }

    // 🚀 공통 가속 로직 (기존 코드와 동일)
    if (CubicProbe->CumulativeSuccessLevel > 1) {
        double accelerationFactor = 1.0 + CubicProbe->CumulativeSuccessLevel;
        if (accelerationFactor > 1.0) {
            *AckTarget = (uint32_t)(*AckTarget / accelerationFactor);
        }
    }

    if (*AckTarget < 2) *AckTarget = 2;
}
// ======================== [ END OF MODIFICATION ] ========================

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeIncreaseWindow(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent,
    _In_ uint32_t AckTarget
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    uint32_t AckedSegments = (AckEvent->NumRetransmittableBytes + DatagramPayloadLength - 1) / DatagramPayloadLength;
    CubicProbe->AckCountForGrowth += AckedSegments;

    if (CubicProbe->AckCountForGrowth >= AckTarget) {
        uint32_t GrowthInSegments = 1;
        if (CubicProbe->CumulativeSuccessLevel > 0) {
            GrowthInSegments = CubicProbe->CumulativeSuccessLevel - 1;
            if (GrowthInSegments < 1) {
                GrowthInSegments = 1;
            }
        }

        uint32_t PrevCwnd = Cubic->CongestionWindow;
        Cubic->CongestionWindow += (GrowthInSegments * DatagramPayloadLength);

        CubicProbe->AckCountForGrowth -= AckTarget;

        if (CubicProbe->ProbeState == PROBE_TEST) {
            CubicProbe->ProbeState = PROBE_WAITING;
            CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber - 1;
            printf("[CubicProbe] CWND Grown for Probe. State -> PROBE_WAITING. TargetPkt=%lu\n",
                CubicProbe->ProbeTargetPacketNumber);
        }

        if (CubicProbe->ProbeState != PROBE_INACTIVE) {
            printf("[CubicProbe][%p][%.3fms] CWND Update (Probe Lvl %u): %u -> %u (Target=%u, Growth=%u segs)\n",
                (void*)Connection, (double)AckEvent->TimeNow / 1000.0, CubicProbe->CumulativeSuccessLevel, PrevCwnd, Cubic->CongestionWindow, AckTarget, GrowthInSegments);
        } else {
             printf("[CubicProbe][%p][%.3fms] CWND Update (CUBIC): %u -> %u (Target=%u)\n",
                (void*)Connection, (double)AckEvent->TimeNow / 1000.0, PrevCwnd, Cubic->CongestionWindow, AckTarget);
        }
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
CubicProbeCongestionControlOnDataAcknowledged(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    Cubic->BytesInFlight -= AckEvent->NumRetransmittableBytes;

    if (Cubic->IsInRecovery) {
        if (AckEvent->LargestAck > Cubic->RecoverySentPacketNumber) {
            Cubic->IsInRecovery = FALSE;
        }
        goto Exit;
    }
    if (AckEvent->NumRetransmittableBytes == 0) {
        goto Exit;
    }

    if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
        uint32_t PrevCwnd = Cubic->CongestionWindow;

        Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;

        printf("[CubicProbe][%p][%.3fms] CWND Update (SlowStart): %u -> %u\n",
            (void*)Connection,
            (double)AckEvent->TimeNow / 1000.0,
            PrevCwnd,
            Cubic->CongestionWindow);

        if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
            Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;

            printf("[CubicProbe-SS][%p][%.3fms] SlowStart -> CongestionAvoidance (Ssthresh: %u)\n",
                (void*)Connection,
                (double)AckEvent->TimeNow / 1000.0,
                Cubic->SlowStartThreshold);
        }

    } else {
        const QUIC_PATH* Path = &Connection->Paths[0];
        const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
        if (DatagramPayloadLength == 0) goto Exit;

        CubicProbePktsAcked(Cc, AckEvent);

        uint32_t AckTarget = 0;
        CubicProbeUpdate(Cc, AckEvent, &AckTarget);

        CubicProbeIncreaseWindow(Cc, AckEvent, AckTarget);
    }

Exit:
    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeCongestionControlOnCongestionEvent(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ BOOLEAN IsPersistentCongestion,
    _In_ BOOLEAN Ecn,
    _In_ uint32_t TenTimesBeta // [수정] 감소 계수(Beta)를 인자로 받음
    )
{
    UNREFERENCED_PARAMETER(IsPersistentCongestion);
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
    uint32_t PrevCwnd = Cubic->CongestionWindow;

    CubicProbeResetProbeState(CubicProbe);
    CubicProbe->AckCountForGrowth = 0;

    if (!Cubic->IsInRecovery) {
         Cubic->IsInRecovery = TRUE;
    }
    Cubic->HasHadCongestionEvent = TRUE;

    if (!Ecn) {
        Cubic->PrevCongestionWindow = Cubic->CongestionWindow;
    }

    Cubic->WindowLastMax = Cubic->WindowMax;
    Cubic->WindowMax = Cubic->CongestionWindow;

    // [수정] 고정된 TEN_TIMES_BETA_CUBIC 대신 인자로 받은 TenTimesBeta 사용
    if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow < Cubic->WindowLastMax) {
        Cubic->WindowMax = (uint32_t)(Cubic->CongestionWindow * (10.0 + TenTimesBeta) / 20.0);
    }

    uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
    
    // [수정] 고정된 TEN_TIMES_BETA_CUBIC 대신 인자로 받은 TenTimesBeta 사용
    Cubic->SlowStartThreshold =
    Cubic->CongestionWindow =
        CXPLAT_MAX(
            MinCongestionWindow,
            (uint32_t)(Cubic->CongestionWindow * ((double)TenTimesBeta / 10.0)));

    Cubic->TimeOfCongAvoidStart = 0;

    printf("[Cubic][%p][%.3fms] CWND Update (Congestion Event, Beta=%.1f): %u -> %u\n",
        (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, (double)TenTimesBeta / 10.0, PrevCwnd, Cubic->CongestionWindow);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlSetExemption(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint8_t NumPackets);
uint32_t CubicProbeCongestionControlGetSendAllowance(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint64_t TimeSinceLastSend, _In_ BOOLEAN TimeSinceLastSendValid);
void CubicProbeCongestionControlOnDataSent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
BOOLEAN CubicProbeCongestionControlOnDataInvalidated( _In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
void CubicProbeCongestionControlOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent);
void CubicProbeCongestionControlOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent);
BOOLEAN CubicProbeCongestionControlOnSpuriousCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc);
void CubicProbeCongestionControlLogOutFlowStatus(_In_ const QUIC_CONGESTION_CONTROL* Cc);
uint8_t CubicProbeCongestionControlGetExemptions(_In_ const QUIC_CONGESTION_CONTROL* Cc);
uint32_t CubicProbeCongestionControlGetBytesInFlightMax(_In_ const QUIC_CONGESTION_CONTROL* Cc);
BOOLEAN CubicProbeCongestionControlIsAppLimited(_In_ const QUIC_CONGESTION_CONTROL* Cc);
void CubicProbeCongestionControlSetAppLimited(_In_ QUIC_CONGESTION_CONTROL* Cc);
uint32_t CubicProbeCongestionControlGetCongestionWindow(_In_ const QUIC_CONGESTION_CONTROL* Cc);
void CubicProbeCongestionControlGetNetworkStatistics(_In_ const QUIC_CONNECTION* const Connection, _In_ const QUIC_CONGESTION_CONTROL* const Cc, _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics);

BOOLEAN
CubicProbeCongestionControlCanSend(
    _In_ QUIC_CONGESTION_CONTROL* Cc
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
}

void
CubicProbeCongestionControlSetExemption(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ uint8_t NumPackets
    )
{
    Cc->CubicProbe.Cubic.Exemptions = NumPackets;
}

uint32_t
CubicProbeCongestionControlGetSendAllowance(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ uint64_t TimeSinceLastSend,
    _In_ BOOLEAN TimeSinceLastSendValid
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    uint32_t SendAllowance;

    if (Cubic->BytesInFlight >= Cubic->CongestionWindow) {
        SendAllowance = 0;
    } else if (!TimeSinceLastSendValid || !Connection->Settings.PacingEnabled || !Connection->Paths[0].GotFirstRttSample || Connection->Paths[0].SmoothedRtt < QUIC_MIN_PACING_RTT) {
        SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
    } else {
        uint64_t EstimatedWnd;
        if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
            EstimatedWnd = (uint64_t)Cubic->CongestionWindow << 1;
            if (EstimatedWnd > Cubic->SlowStartThreshold) {
                EstimatedWnd = Cubic->SlowStartThreshold;
            }
        } else {
            EstimatedWnd = Cubic->CongestionWindow + (Cubic->CongestionWindow >> 2);
        }
        SendAllowance = Cubic->LastSendAllowance + (uint32_t)((EstimatedWnd * TimeSinceLastSend) / Connection->Paths[0].SmoothedRtt);
        if (SendAllowance < Cubic->LastSendAllowance || SendAllowance > (Cubic->CongestionWindow - Cubic->BytesInFlight)) {
            SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
        }
        Cubic->LastSendAllowance = SendAllowance;
    }
    return SendAllowance;
}

void
CubicProbeCongestionControlOnDataSent(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ uint32_t NumRetransmittableBytes
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    Cubic->BytesInFlight += NumRetransmittableBytes;
    if (Cubic->BytesInFlightMax < Cubic->BytesInFlight) {
        Cubic->BytesInFlightMax = Cubic->BytesInFlight;
        QuicSendBufferConnectionAdjust(QuicCongestionControlGetConnection(Cc));
    }
    if (NumRetransmittableBytes > Cubic->LastSendAllowance) {
        Cubic->LastSendAllowance = 0;
    } else {
        Cubic->LastSendAllowance -= NumRetransmittableBytes;
    }
    if (Cubic->Exemptions > 0) {
        --Cubic->Exemptions;
    }

    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

BOOLEAN
CubicProbeCongestionControlOnDataInvalidated(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ uint32_t NumRetransmittableBytes
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= NumRetransmittableBytes);
    Cubic->BytesInFlight -= NumRetransmittableBytes;

    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

void
CubicProbeCongestionControlOnDataLost(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_LOSS_EVENT* LossEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    printf("[Cubic][%p][%.3fms] LOSS EVENT (0.7x): CWnd=%u, InFlight=%u, LostBytes=%u\n",
        (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight, LossEvent->NumRetransmittableBytes);

    if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
        Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
        // [수정] 일반 손실 시 0.7배 감소(TEN_TIMES_BETA_CUBIC)를 사용하도록 호출
        CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE, TEN_TIMES_BETA_CUBIC);
    }

    CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= LossEvent->NumRetransmittableBytes);
    Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;

    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

void
CubicProbeCongestionControlOnEcn(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ECN_EVENT* EcnEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    printf("[Cubic][%p][%.3fms] ECN EVENT (0.7x): CWnd=%u, InFlight=%u\n",
        (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight);

    if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
        Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
        Connection->Stats.Send.EcnCongestionCount++;
        // [수정] ECN 시 0.7배 감소(TEN_TIMES_BETA_CUBIC)를 사용하도록 호출
        CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, TRUE, TEN_TIMES_BETA_CUBIC);
    }

    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

BOOLEAN
CubicProbeCongestionControlOnSpuriousCongestionEvent(
    _In_ QUIC_CONGESTION_CONTROL* Cc
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    if (!Cubic->IsInRecovery) return FALSE;
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
    Cubic->IsInRecovery = FALSE;
    Cubic->HasHadCongestionEvent = FALSE;

    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

void
CubicProbeCongestionControlLogOutFlowStatus(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    UNREFERENCED_PARAMETER(Cc);
}

uint32_t
CubicProbeCongestionControlGetBytesInFlightMax(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    return Cc->CubicProbe.Cubic.BytesInFlightMax;
}

uint8_t
CubicProbeCongestionControlGetExemptions(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    return Cc->CubicProbe.Cubic.Exemptions;
}

uint32_t
CubicProbeCongestionControlGetCongestionWindow(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    return Cc->CubicProbe.Cubic.CongestionWindow;
}

BOOLEAN
CubicProbeCongestionControlIsAppLimited(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    UNREFERENCED_PARAMETER(Cc);
    return FALSE;
}

void
CubicProbeCongestionControlSetAppLimited(
    _In_ struct QUIC_CONGESTION_CONTROL* Cc
    )
{
    UNREFERENCED_PARAMETER(Cc);
}

void
CubicProbeCongestionControlGetNetworkStatistics(
    _In_ const QUIC_CONNECTION* const Connection,
    _In_ const QUIC_CONGESTION_CONTROL* const Cc,
    _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics
    )
{
    const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    const QUIC_PATH* Path = &Connection->Paths[0];

    NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
    NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
    NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
    NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
    NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
    NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
}

static BOOLEAN
CubicProbeCongestionControlUpdateBlockedState(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ BOOLEAN PreviousCanSendState
    )
{
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    if (PreviousCanSendState != CubicProbeCongestionControlCanSend(Cc)) {
        if (PreviousCanSendState) {
            QuicConnAddOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
        } else {
            QuicConnRemoveOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
            Connection->Send.LastFlushTime = CxPlatTimeUs64();
            return TRUE;
        }
    }
    return FALSE;
}

static const QUIC_CONGESTION_CONTROL QuicCongestionControlCubicProbe = {
    .Name = "CubicProbe",
    .QuicCongestionControlCanSend = CubicProbeCongestionControlCanSend,
    .QuicCongestionControlSetExemption = CubicProbeCongestionControlSetExemption,
    .QuicCongestionControlReset = CubicProbeCongestionControlReset,
    .QuicCongestionControlGetSendAllowance = CubicProbeCongestionControlGetSendAllowance,
    .QuicCongestionControlOnDataSent = CubicProbeCongestionControlOnDataSent,
    .QuicCongestionControlOnDataInvalidated = CubicProbeCongestionControlOnDataInvalidated,
    .QuicCongestionControlOnDataAcknowledged = CubicProbeCongestionControlOnDataAcknowledged,
    .QuicCongestionControlOnDataLost = CubicProbeCongestionControlOnDataLost,
    .QuicCongestionControlOnEcn = CubicProbeCongestionControlOnEcn,
    .QuicCongestionControlOnSpuriousCongestionEvent = CubicProbeCongestionControlOnSpuriousCongestionEvent,
    .QuicCongestionControlLogOutFlowStatus = CubicProbeCongestionControlLogOutFlowStatus,
    .QuicCongestionControlGetExemptions = CubicProbeCongestionControlGetExemptions,
    .QuicCongestionControlGetBytesInFlightMax = CubicProbeCongestionControlGetBytesInFlightMax,
    .QuicCongestionControlIsAppLimited = CubicProbeCongestionControlIsAppLimited,
    .QuicCongestionControlSetAppLimited = CubicProbeCongestionControlSetAppLimited,
    .QuicCongestionControlGetCongestionWindow = CubicProbeCongestionControlGetCongestionWindow,
    .QuicCongestionControlGetNetworkStatistics = CubicProbeCongestionControlGetNetworkStatistics
};

_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlInitialize(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_SETTINGS_INTERNAL* Settings
    )
{
    *Cc = QuicCongestionControlCubicProbe;
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    Cubic->SlowStartThreshold = UINT32_MAX;
    Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
    Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
    Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
    Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
    Cubic->WindowMax = 0;
    Cubic->WindowLastMax = 0;

    CubicProbeResetProbeState(CubicProbe);
    CubicProbe->AckCountForGrowth = 0;
    CubicProbe->MinRttUs = UINT64_MAX;
}




