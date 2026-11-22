// Pattern generators. Each takes a TrackPattern by reference and rewrites its
// steps deterministically from a seed. No allocation: safe on any thread
// (evolve runs on the audio thread at bar lines).
#pragma once
#include "../model/Model.h"

namespace atm {

inline bool roleIsMelodic (int role) { return role == Role::Bass || role == Role::Lead || role == Role::Pad; }

// Run the generator selected in tp.gen (type, seed, params).
void runGenerator (TrackPattern& tp, int role);

void genEuclid   (TrackPattern& tp, uint32_t seed, const float* p, int role);
void genCellular (TrackPattern& tp, uint32_t seed, const float* p, int role);
void genDensity  (TrackPattern& tp, uint32_t seed, const float* p, int role);
void genMarkov   (TrackPattern& tp, uint32_t seed, const float* p, int role);
// amount 0..1; weights 0..1 for rhythm / pitch / detail (ratchets, micro-timing, probability)
bool genMutate   (TrackPattern& tp, uint32_t seed, float amount, float rhythmW, float pitchW, float detailW, int role);

// One evolve step: a gentle mutate. Returns true if anything changed.
bool evolveTrackPattern (TrackPattern& tp, float amount, uint32_t seed, int role);

// Helpers shared with the idea generator
void rotatePattern (TrackPattern& tp, int steps);
void clearSteps (TrackPattern& tp);
float metricWeight (int step, int length);

} // namespace atm
