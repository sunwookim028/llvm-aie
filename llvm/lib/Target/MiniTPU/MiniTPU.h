//===-- MiniTPU.h - MiniTPU CodeGen entry points ---------------*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MINITPU_MINITPU_H
#define LLVM_LIB_TARGET_MINITPU_MINITPU_H

#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include <memory>

namespace llvm {
class FunctionPass;
class PassRegistry;

/// Whether the post-RA scheduler keeps program order and the placed bundles
/// (gate G3: only delays change). On by default; off is reorder mode (G4).
bool miniTPUInOrder();
/// -minitpu-slat-all-readers: S ops read their SREG at issue too (M6-F2).
bool miniTPUSLatAllReaders();

/// Group each placed bundle into one issue unit and chain the units in program
/// order (in-order mode only).
std::unique_ptr<ScheduleDAGMutation> createMiniTPUInOrderBundles();
/// Reorder mode: pinned bundles (DELAY_GROUP) stay whole and in place; free
/// instructions stay between the pinned bundles that surround them.
std::unique_ptr<ScheduleDAGMutation> createMiniTPUPinnedGroups();
/// EXPERIMENTAL: a lock orders only the VMEM accesses that can meet its window.
std::unique_ptr<ScheduleDAGMutation> createMiniTPULockEdges();
/// The rules an itinerary cannot hold, as edges read from MiniTPUScheduleFacts:
/// vmatpush_i -> vmatpop_i, the weight-switch span, output-FIFO capacity.
std::unique_ptr<ScheduleDAGMutation> createMiniTPUMatrixEdges();
/// halt is a control sink: nothing after it, so no latency to the region end.
std::unique_ptr<ScheduleDAGMutation> createMiniTPUControlSinkExit();

FunctionPass *createMiniTPUFoldDelaysPass();
void initializeMiniTPUFoldDelaysPass(PassRegistry &);
} // namespace llvm
#endif
