/*
 * GeFaST
 *
 * Copyright (C) 2016 - 2017 Robert Mueller
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Contact: Robert Mueller <romueller@techfak.uni-bielefeld.de>
 * Faculty of Technology, Bielefeld University,
 * PO box 100131, DE-33501 Bielefeld, Germany
 */

#include "../include/SIMD.hpp"
#include "../include/SwarmClustering.hpp"
#include "../include/SwarmingSegmentFilter.hpp"

#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace GeFaST {

void SwarmClustering::explorePool(const AmpliconCollection& ac, Matches& matches, std::vector<Otu*>& otus, const SwarmConfig& sc) {

    // determine order of amplicons based on abundance (descending) without invalidating the integer (position) ids of the amplicons
    std::vector<numSeqs_t> index(ac.size());
    std::iota(std::begin(index), std::end(index), 0);
    std::sort(index.begin(), index.end(), CompareIndicesAbund(ac));

    Otu* curOtu = 0;
//    numSeqs_t numOtu = 0;
    std::vector<bool> visited(ac.size(), false); // visited amplicons are already included in an OTU

    OtuEntry curSeed, newSeed;
    bool unique;
    std::unordered_set<std::string> nonUniques;
    std::vector<std::pair<numSeqs_t, lenSeqs_t>> next;
    lenSeqs_t lastGen;
    numSeqs_t pos;

    // open new OTU for the amplicon with the highest abundance that is not yet included in an OTU
    for (auto seedIter = index.begin(); seedIter != index.end(); seedIter++) {

        if (!visited[*seedIter]) {

            /* (a) Initialise new OTU with seed */
            curOtu = new Otu(/*++numOtu, */*seedIter, ac[*seedIter].abundance);

            newSeed.id = *seedIter;
            newSeed.parentId = newSeed.id;
            newSeed.parentDist = 0;
            newSeed.gen = 0;
            newSeed.rad = 0;
            curOtu->members.push_back(newSeed);

            visited[*seedIter] = true;
            nonUniques.clear();

            lastGen = 0;


            /* (b) BFS through 'match space' */
            pos = 0;
            while (pos < curOtu->members.size()) { // expand current OTU until no further similar amplicons can be added

                if (lastGen != curOtu->members[pos].gen) { // work through generation by decreasing abundance
                    std::sort(curOtu->members.begin() + pos, curOtu->members.end(), CompareOtuEntriesAbund(ac));
                }

                // get next OTU (sub)seed
                curSeed = curOtu->members[pos];

                unique = true;

                // update OTU information
                curOtu->mass += ac[curSeed.id].abundance;
                curOtu->numSingletons += (ac[curSeed.id].abundance == 1);

                if (curSeed.gen > curOtu->maxGen) curOtu->maxGen = curSeed.gen;
                if (curSeed.rad > curOtu->maxRad) curOtu->maxRad = curSeed.rad;

                // Consider yet unseen (unvisited) amplicons to continue the exploration.
                // An amplicon is marked as 'visited' as soon as it occurs the first time as matching partner
                // in order to prevent the algorithm from queueing it more than once coming from different amplicons.
                next = matches.getMatchesOfAugmented(curSeed.id);
                for (auto matchIter = next.begin(); matchIter != next.end(); matchIter++) {

                    unique &= (matchIter->second != 0);

                    if (!visited[matchIter->first] && (sc.noOtuBreaking || ac[matchIter->first].abundance <= ac[curSeed.id].abundance)) {

                        newSeed.id = matchIter->first;
                        newSeed.parentId = curSeed.id;
                        newSeed.parentDist = matchIter->second;
                        newSeed.gen = curSeed.gen + 1;
                        newSeed.rad = curSeed.rad + matchIter->second;
                        curOtu->members.push_back(newSeed);
                        visited[matchIter->first] = true;

                    }
                }

                // unique sequences contribute when they occur, non-unique sequences only at their first occurrence
                // and when dereplicating each contributes (numUniqueSequences used to count the multiplicity of the sequence)
                unique = unique || sc.dereplicate || nonUniques.insert(ac[curSeed.id].seq).second;
                curOtu->numUniqueSequences += unique;

                lastGen = curSeed.gen;
                pos++;

            }

            /* (c) Close the no longer extendable OTU */
            otus.push_back(curOtu);

        }

    }

}


void SwarmClustering::fastidiousIndexOtu(RollingIndices<InvertedIndexFastidious>& indices, std::unordered_map<lenSeqs_t, SegmentFilter::Segments>& segmentsArchive, const AmpliconCollection& ac, Otu& otu, std::vector<GraftCandidate>& graftCands, const SwarmConfig& sc) {

    lenSeqs_t seqLen;

    for (auto memberIter = otu.members.begin(); memberIter != otu.members.end(); memberIter++) {

        seqLen = ac[memberIter->id].seq.length();
        SegmentFilter::Segments& segments = segmentsArchive[seqLen];

        if (segments.size() == 0) {

            indices.roll(seqLen);
            segments = SegmentFilter::Segments(sc.fastidiousThreshold + sc.extraSegs);
            SegmentFilter::selectSegments(segments, seqLen, sc.fastidiousThreshold, sc.extraSegs);

        }

        for (lenSeqs_t i = 0; i < sc.fastidiousThreshold + sc.extraSegs; i++) {
            indices.getIndex(seqLen, i).add(ac[memberIter->id].seq.substr(segments[i].first, segments[i].second), &(*memberIter));
        }

        graftCands[memberIter->id].childOtu = &otu;
        graftCands[memberIter->id].childMember = &(*memberIter);

    }

}

inline bool compareCandidates(const Amplicon& newCand, const Amplicon& oldCand) {
    return (oldCand.abundance < newCand.abundance)
#if INPUT_RANK
           || ((oldCand.abundance == newCand.abundance) && (oldCand.rank > newCand.rank))
#endif
            ;
}

void SwarmClustering::verifyFastidious(const AmpliconPools& pools, const AmpliconCollection& acOtus, const AmpliconCollection& acIndices, std::vector<GraftCandidate>& graftCands, Buffer<CandidateFastidious>& buf, const lenSeqs_t width, const lenSeqs_t t, std::mutex& mtx) {

    CandidateFastidious c;
    Buffer<CandidateFastidious> localBuffer;
    lenSeqs_t M[width]; // reusable DP-matrix (wide enough for all possible calculations for this AmpliconCollection)

    while (!buf.isClosed() || buf.syncSize() > 0) {

        buf.syncSwapContents(localBuffer);

        while (localBuffer.size() > 0) {

            c = localBuffer.pop();

            for (auto childIter = c.children.begin(); childIter != c.children.end(); childIter++) {

                std::unique_lock<std::mutex> lock(mtx);
                if ((graftCands[*childIter].parentOtu == 0) || compareCandidates(acOtus[c.parent], (*pools.get(graftCands[*childIter].parentOtu->poolId))[graftCands[*childIter].parentMember->id])) {

                    lock.unlock();
                    if (Verification::computeLengthAwareRow(acOtus[c.parent].seq, acIndices[*childIter].seq, t, M) <= t) {

                        lock.lock();
                        if ((graftCands[*childIter].parentOtu == 0) || compareCandidates(acOtus[c.parent], (*pools.get(graftCands[*childIter].parentOtu->poolId))[graftCands[*childIter].parentMember->id])) {

                            graftCands[*childIter].parentOtu = c.parentOtu;
                            graftCands[*childIter].parentMember = c.parentMember;

                        }
                        lock.unlock();

                    }

                }

            }

        }

    }

}

void SwarmClustering::verifyGotohFastidious(const AmpliconPools& pools, const AmpliconCollection& acOtus, const AmpliconCollection& acIndices, std::vector<GraftCandidate>& graftCands, Buffer<CandidateFastidious>& buf, const lenSeqs_t width, const lenSeqs_t t, const Verification::Scoring& scoring, std::mutex& mtx) {

    CandidateFastidious c;
    Buffer<CandidateFastidious> localBuffer;

    // reusable DP-matrices (wide enough for all possible calculations for this AmpliconCollection)
    val_t D[width];
    val_t P[width];
    lenSeqs_t cntDiffs[width];
    lenSeqs_t cntDiffsP[width];

    while (!buf.isClosed() || buf.syncSize() > 0) {

        buf.syncSwapContents(localBuffer);

        while (localBuffer.size() > 0) {

            c = localBuffer.pop();

            for (auto childIter = c.children.begin(); childIter != c.children.end(); childIter++) {

                std::unique_lock<std::mutex> lock(mtx);
                if ((graftCands[*childIter].parentOtu == 0) || compareCandidates(acOtus[c.parent], (*pools.get(graftCands[*childIter].parentOtu->poolId))[graftCands[*childIter].parentMember->id])) {

                    lock.unlock();
                    if (Verification::computeGotohLengthAwareEarlyRow8(acOtus[c.parent].seq, acIndices[*childIter].seq, t, scoring, D, P, cntDiffs, cntDiffsP) <= t) {

                        lock.lock();
                        if ((graftCands[*childIter].parentOtu == 0) || compareCandidates(acOtus[c.parent], (*pools.get(graftCands[*childIter].parentOtu->poolId))[graftCands[*childIter].parentMember->id])) {

                            graftCands[*childIter].parentOtu = c.parentOtu;
                            graftCands[*childIter].parentMember = c.parentMember;

                        }
                        lock.unlock();

                    }

                }

            }

        }

    }

}

void SwarmClustering::fastidiousCheckOtus(RotatingBuffers<CandidateFastidious>& cbs, const std::vector<Otu*>& otus, const AmpliconCollection& acOtus, RollingIndices<InvertedIndexFastidious>& indices, const AmpliconCollection& acIndices, std::vector<GraftCandidate>& graftCands, const SwarmConfig& sc) {

    std::unordered_map<lenSeqs_t, std::unordered_map<lenSeqs_t, std::vector<SegmentFilter::Substrings>>> substrsArchive;
    std::vector<OtuEntry*> candMembers;
    std::unordered_map<numSeqs_t, lenSeqs_t> candCnts;
    lenSeqs_t seqLen;

    std::vector<CandidateFastidious> localCands;

    for (auto otuIter = otus.begin(); otuIter != otus.end(); otuIter++) {

        if ((*otuIter)->mass >= sc.boundary) { // for each heavy OTU of the pool ...

            for (auto memberIter = (*otuIter)->members.begin(); memberIter != (*otuIter)->members.end(); memberIter++) { // ... consider every amplicon in the OTU and ...

                seqLen = acOtus[memberIter->id].seq.length();

                std::unordered_map<lenSeqs_t, std::vector<SegmentFilter::Substrings>>& substrs = substrsArchive[seqLen];

                // on reaching new length group, open new inverted indices
                if (substrs.empty()) {

                    // ... and determine position information shared by all amplicons of this length
                    for (lenSeqs_t partnerLen = (seqLen > sc.fastidiousThreshold) * (seqLen - sc.fastidiousThreshold); partnerLen <= seqLen + sc.fastidiousThreshold; partnerLen++) {

                        std::vector<SegmentFilter::Substrings>& vec = substrs[partnerLen];
                        for (lenSeqs_t segmentIndex = 0; segmentIndex < sc.fastidiousThreshold + sc.extraSegs; segmentIndex++) {
                            if (partnerLen <= seqLen) {
                                vec.push_back(SegmentFilter::selectSubstrs(seqLen, partnerLen, segmentIndex, sc.fastidiousThreshold, sc.extraSegs));
                            } else {
                                vec.push_back(SegmentFilter::selectSubstrsBackward(seqLen, partnerLen, segmentIndex, sc.fastidiousThreshold, sc.extraSegs));
                            }
                        }

                    }

                }

                localCands.push_back(CandidateFastidious(memberIter->id, *otuIter, &(*memberIter)));

                for (lenSeqs_t len = (seqLen > sc.fastidiousThreshold) * (seqLen - sc.fastidiousThreshold); len <= seqLen + sc.fastidiousThreshold; len++) { // ... search for graft candidates among the amplicons in light OTUs

                    for (lenSeqs_t i = 0; i < sc.fastidiousThreshold + sc.extraSegs; i++) { // ... and apply segment filter for each segment

                        SegmentFilter::Substrings& subs = substrs[len][i];
                        InvertedIndexFastidious& inv = indices.getIndex(len, i);

                        for (auto substrPos = subs.first; substrPos <= subs.last; substrPos++) {

                            candMembers = inv.getLabelsOf(std::string(acOtus[memberIter->id].seq, substrPos, subs.len));

                            for (auto candIter = candMembers.begin(); candIter != candMembers.end(); candIter++) {
                                candCnts[(*candIter)->id]++;
                            }

                        }

                    }

                    // general pigeonhole principle: for being a candidate, at least sc.extraSegs segments have to be matched
                    for (auto candIter = candCnts.begin(); candIter != candCnts.end(); candIter++) {

#if QGRAM_FILTER
                        if ((candIter->second >= sc.extraSegs) && (qgram_diff(acOtus[memberIter->id], acIndices[candIter->first]) <= sc.threshold)) {
#else
                        if (candIter->second >= sc.extraSegs) {
#endif
                            localCands.back().children.push_back(candIter->first);
                        }

                    }

                    candCnts = std::unordered_map<numSeqs_t, lenSeqs_t>();

                }

                cbs.push(localCands);
                localCands = std::vector<CandidateFastidious>();

            }

        }

    }

}

#if SIMD_VERIFICATION
void SwarmClustering::fastidiousCheckOtusDirectly(const AmpliconPools& pools, const std::vector<Otu*>& otus, const AmpliconCollection& acOtus, RollingIndices<InvertedIndexFastidious>& indices, const AmpliconCollection& acIndices, std::vector<GraftCandidate>& graftCands, const lenSeqs_t width, std::mutex& graftCandsMtx, const SwarmConfig& sc) {

    std::unordered_map<lenSeqs_t, std::unordered_map<lenSeqs_t, std::vector<SegmentFilter::Substrings>>> substrsArchive;
    std::vector<OtuEntry*> candMembers;
    std::unordered_map<numSeqs_t, lenSeqs_t> candCnts;
    lenSeqs_t seqLen;

    lenSeqs_t M[sc.useScore ? 1 : width];
    val_t D[sc.useScore? width : 1];
    val_t P[sc.useScore? width : 1];
    lenSeqs_t cntDiffs[sc.useScore? width : 1];
    lenSeqs_t cntDiffsP[sc.useScore? width : 1];

    for (auto otuIter = otus.begin(); otuIter != otus.end(); otuIter++) {

        if ((*otuIter)->mass >= sc.boundary) { // for each heavy OTU of the pool ...

            for (auto memberIter = (*otuIter)->members.begin(); memberIter != (*otuIter)->members.end(); memberIter++) { // ... consider every amplicon in the OTU and ...

                seqLen = acOtus[memberIter->id].seq.length();

                std::vector<numSeqs_t> cands;
                std::unordered_map<lenSeqs_t, std::vector<SegmentFilter::Substrings>>& substrs = substrsArchive[seqLen];

                // on reaching new length group, open new inverted indices
                if (substrs.empty()) {

                    // ... and determine position information shared by all amplicons of this length
                    for (lenSeqs_t partnerLen = (seqLen > sc.fastidiousThreshold) * (seqLen - sc.fastidiousThreshold); partnerLen <= seqLen + sc.fastidiousThreshold; partnerLen++) {

                        std::vector<SegmentFilter::Substrings>& vec = substrs[partnerLen];
                        for (lenSeqs_t segmentIndex = 0; segmentIndex < sc.fastidiousThreshold + sc.extraSegs; segmentIndex++) {
                            if (partnerLen <= seqLen) {
                                vec.push_back(SegmentFilter::selectSubstrs(seqLen, partnerLen, segmentIndex, sc.fastidiousThreshold, sc.extraSegs));
                            } else {
                                vec.push_back(SegmentFilter::selectSubstrsBackward(seqLen, partnerLen, segmentIndex, sc.fastidiousThreshold, sc.extraSegs));
                            }
                        }

                    }

                }


                for (lenSeqs_t len = (seqLen > sc.fastidiousThreshold) * (seqLen - sc.fastidiousThreshold); len <= seqLen + sc.fastidiousThreshold; len++) { // ... search for graft candidates among the amplicons in light OTUs

                    for (lenSeqs_t i = 0; i < sc.fastidiousThreshold + sc.extraSegs; i++) { // ... and apply segment filter for each segment

                        SegmentFilter::Substrings& subs = substrs[len][i];
                        InvertedIndexFastidious& inv = indices.getIndex(len, i);

                        for (auto substrPos = subs.first; substrPos <= subs.last; substrPos++) {

                            candMembers = inv.getLabelsOf(std::string(acOtus[memberIter->id].seq, substrPos, subs.len));

                            for (auto candIter = candMembers.begin(); candIter != candMembers.end(); candIter++) {
                                candCnts[(*candIter)->id]++;
                            }

                        }

                    }

                    // general pigeonhole principle: for being a candidate, at least sc.extraSegs segments have to be matched
                    for (auto candIter = candCnts.begin(); candIter != candCnts.end(); candIter++) {

//                        if ((candIter->second >= sc.extraSegs)
//                                &&((graftCands[candIter->first].parentOtu == 0) || compareCandidates(acOtus[memberIter->id], (*pools.get(graftCands[candIter->first].parentOtu->poolId))[graftCands[candIter->first].parentMember->id]))
//                                && ((useScore ?
//                                        Verification::computeGotohLengthAwareEarlyRow8(acOtus[memberIter->id].seq, acIndices[candIter->first].seq, sc.fastidiousThreshold, sc.scoring, D, P, cntDiffs, cntDiffsP)
//                                      : Verification::computeLengthAwareRow(acOtus[memberIter->id].seq, acIndices[candIter->first].seq, sc.fastidiousThreshold, M)) <= sc.fastidiousThreshold)) {
//
//                                    graftCands[candIter->first].parentOtu = *otuIter;
//                                    graftCands[candIter->first].parentMember = &(*memberIter);
//
//                        }

                        std::unique_lock<std::mutex> lock(graftCandsMtx);
#if QGRAM_FILTER
                        if ((candIter->second >= sc.extraSegs) && ((graftCands[candIter->first].parentOtu == 0) || compareCandidates(acOtus[memberIter->id], (*pools.get(graftCands[candIter->first].parentOtu->poolId))[graftCands[candIter->first].parentMember->id])) && (qgram_diff(acOtus[memberIter->id], acIndices[candIter->first]) <= sc.threshold)) {
#else
                        if ((candIter->second >= sc.extraSegs) && ((graftCands[candIter->first].parentOtu == 0) || compareCandidates(acOtus[memberIter->id], (*pools.get(graftCands[candIter->first].parentOtu->poolId))[graftCands[candIter->first].parentMember->id]))) {
#endif
                            cands.push_back(candIter->first);
                        }

                    }

                    candCnts = std::unordered_map<numSeqs_t, lenSeqs_t>();

                }

                if (cands.size() > 0) {

                    auto verifiedCands = SimdVerification::computeDiffsReduce((AmpliconCollection&) acIndices, (Amplicon&) acOtus[memberIter->id], cands, sc.fastidiousThreshold);

                    std::unique_lock<std::mutex> lock(graftCandsMtx);

                    for (auto& c : verifiedCands) {
                        if (((graftCands[c.first].parentOtu == 0) || compareCandidates(acOtus[memberIter->id], (*pools.get(graftCands[c.first].parentOtu->poolId))[graftCands[c.first].parentMember->id]))) {

                            graftCands[c.first].parentOtu = *otuIter;
                            graftCands[c.first].parentMember = &(*memberIter);

                        }
                    }

                }

            }

        }

    }

}

#else

void SwarmClustering::fastidiousCheckOtusDirectly(const AmpliconPools& pools, const std::vector<Otu*>& otus, const AmpliconCollection& acOtus, RollingIndices<InvertedIndexFastidious>& indices, const AmpliconCollection& acIndices, std::vector<GraftCandidate>& graftCands, const lenSeqs_t width, std::mutex& graftCandsMtx, const SwarmConfig& sc) {

    std::unordered_map<lenSeqs_t, std::unordered_map<lenSeqs_t, std::vector<SegmentFilter::Substrings>>> substrsArchive;
    std::vector<OtuEntry*> candMembers;
    std::unordered_map<numSeqs_t, lenSeqs_t> candCnts;
    lenSeqs_t seqLen;

    lenSeqs_t M[sc.useScore ? 1 : width];
    val_t D[sc.useScore? width : 1];
    val_t P[sc.useScore? width : 1];
    lenSeqs_t cntDiffs[sc.useScore? width : 1];
    lenSeqs_t cntDiffsP[sc.useScore? width : 1];

    for (auto otuIter = otus.begin(); otuIter != otus.end(); otuIter++) {

        if ((*otuIter)->mass >= sc.boundary) { // for each heavy OTU of the pool ...

            for (auto memberIter = (*otuIter)->members.begin(); memberIter != (*otuIter)->members.end(); memberIter++) { // ... consider every amplicon in the OTU and ...

                seqLen = acOtus[memberIter->id].seq.length();

                std::unordered_map<lenSeqs_t, std::vector<SegmentFilter::Substrings>>& substrs = substrsArchive[seqLen];

                // on reaching new length group, open new inverted indices
                if (substrs.empty()) {

                    // ... and determine position information shared by all amplicons of this length
                    for (lenSeqs_t partnerLen = (seqLen > sc.fastidiousThreshold) * (seqLen - sc.fastidiousThreshold); partnerLen <= seqLen + sc.fastidiousThreshold; partnerLen++) {

                        std::vector<SegmentFilter::Substrings>& vec = substrs[partnerLen];
                        for (lenSeqs_t segmentIndex = 0; segmentIndex < sc.fastidiousThreshold + sc.extraSegs; segmentIndex++) {
                            if (partnerLen <= seqLen) {
                                vec.push_back(SegmentFilter::selectSubstrs(seqLen, partnerLen, segmentIndex, sc.fastidiousThreshold, sc.extraSegs));
                            } else {
                                vec.push_back(SegmentFilter::selectSubstrsBackward(seqLen, partnerLen, segmentIndex, sc.fastidiousThreshold, sc.extraSegs));
                            }
                        }

                    }

                }


                for (lenSeqs_t len = (seqLen > sc.fastidiousThreshold) * (seqLen - sc.fastidiousThreshold); len <= seqLen + sc.fastidiousThreshold; len++) { // ... search for graft candidates among the amplicons in light OTUs

                    for (lenSeqs_t i = 0; i < sc.fastidiousThreshold + sc.extraSegs; i++) { // ... and apply segment filter for each segment

                        SegmentFilter::Substrings& subs = substrs[len][i];
                        InvertedIndexFastidious& inv = indices.getIndex(len, i);

                        for (auto substrPos = subs.first; substrPos <= subs.last; substrPos++) {

                            candMembers = inv.getLabelsOf(std::string(acOtus[memberIter->id].seq, substrPos, subs.len));

                            for (auto candIter = candMembers.begin(); candIter != candMembers.end(); candIter++) {
                                candCnts[(*candIter)->id]++;
                            }

                        }

                    }

                    // general pigeonhole principle: for being a candidate, at least sc.extraSegs segments have to be matched
                    for (auto candIter = candCnts.begin(); candIter != candCnts.end(); candIter++) {

//                        if ((candIter->second >= sc.extraSegs)
//                                &&((graftCands[candIter->first].parentOtu == 0) || compareCandidates(acOtus[memberIter->id], (*pools.get(graftCands[candIter->first].parentOtu->poolId))[graftCands[candIter->first].parentMember->id]))
//                                && ((useScore ?
//                                        Verification::computeGotohLengthAwareEarlyRow8(acOtus[memberIter->id].seq, acIndices[candIter->first].seq, sc.fastidiousThreshold, sc.scoring, D, P, cntDiffs, cntDiffsP)
//                                      : Verification::computeLengthAwareRow(acOtus[memberIter->id].seq, acIndices[candIter->first].seq, sc.fastidiousThreshold, M)) <= sc.fastidiousThreshold)) {
//
//                                    graftCands[candIter->first].parentOtu = *otuIter;
//                                    graftCands[candIter->first].parentMember = &(*memberIter);
//
//                        }

                        std::unique_lock<std::mutex> lock(graftCandsMtx);
#if QGRAM_FILTER
                        if ((candIter->second >= sc.extraSegs) && ((graftCands[candIter->first].parentOtu == 0) || compareCandidates(acOtus[memberIter->id], (*pools.get(graftCands[candIter->first].parentOtu->poolId))[graftCands[candIter->first].parentMember->id])) && (qgram_diff(acOtus[memberIter->id], acIndices[candIter->first]) <= sc.threshold)) {
#else
                        if ((candIter->second >= sc.extraSegs) && ((graftCands[candIter->first].parentOtu == 0) || compareCandidates(acOtus[memberIter->id], (*pools.get(graftCands[candIter->first].parentOtu->poolId))[graftCands[candIter->first].parentMember->id]))) {
#endif

                            lock.unlock();
                            if ((sc.useScore ?
                                  Verification::computeGotohLengthAwareEarlyRow8(acOtus[memberIter->id].seq, acIndices[candIter->first].seq, sc.fastidiousThreshold, sc.scoring, D, P, cntDiffs, cntDiffsP)
                                : Verification::computeLengthAwareRow(acOtus[memberIter->id].seq, acIndices[candIter->first].seq, sc.fastidiousThreshold, M)) <= sc.fastidiousThreshold) {

                                lock.lock();
                                if (((graftCands[candIter->first].parentOtu == 0) || compareCandidates(acOtus[memberIter->id], (*pools.get(graftCands[candIter->first].parentOtu->poolId))[graftCands[candIter->first].parentMember->id]))) {

                                    graftCands[candIter->first].parentOtu = *otuIter;
                                    graftCands[candIter->first].parentMember = &(*memberIter);

                                }
                                lock.unlock();

                            }

                        }

                    }

                    candCnts = std::unordered_map<numSeqs_t, lenSeqs_t>();

                }

            }

        }

    }

}
#endif

void SwarmClustering::checkAndVerify(const AmpliconPools& pools, const std::vector<Otu*>& otus, const AmpliconCollection& acOtus, RollingIndices<InvertedIndexFastidious>& indices, const AmpliconCollection& acIndices, std::vector<GraftCandidate>& graftCands, const lenSeqs_t width, std::mutex& graftCandsMtx, const SwarmConfig& sc) {

#if SIMD_VERIFICATION

    fastidiousCheckOtusDirectly(pools, otus, acOtus, indices, acIndices, graftCands, width, graftCandsMtx, sc);

#else

    if (sc.numThreadsPerCheck == 1) {

#if 0

        RotatingBuffers<CandidateFastidious> cbs = RotatingBuffers<CandidateFastidious>(1);
        fastidiousCheckOtus(cbs, otus, acOtus, indices, acIndices, graftCands, sc);
        cbs.close();

        if (sc.useScore) {
            verifyGotohFastidious(pools, acOtus, acIndices, graftCands, cbs.getBuffer(0), width, sc.fastidiousThreshold, sc.scoring, graftCandsMtx);
        } else {
            verifyFastidious(pools, acOtus, acIndices, graftCands, cbs.getBuffer(0), width, sc.fastidiousThreshold, graftCandsMtx);
        }

#else

        fastidiousCheckOtusDirectly(pools, otus, acOtus, indices, acIndices, graftCands, width, graftCandsMtx, sc);

#endif

    } else {

        RotatingBuffers<CandidateFastidious> cbs = RotatingBuffers<CandidateFastidious>(sc.numThreadsPerCheck);
        std::thread verifierThreads[sc.numThreadsPerCheck];

        for (unsigned long v = 0; v < sc.numThreadsPerCheck; v++) {
            verifierThreads[v] = sc.useScore ?
                                   std::thread(&SwarmClustering::verifyGotohFastidious, std::ref(pools), std::ref(acOtus), std::ref(acIndices), std::ref(graftCands), std::ref(cbs.getBuffer(v)), width, sc.fastidiousThreshold, std::ref(sc.scoring), std::ref(graftCandsMtx))
                                 : std::thread(&SwarmClustering::verifyFastidious, std::ref(pools), std::ref(acOtus), std::ref(acIndices), std::ref(graftCands), std::ref(cbs.getBuffer(v)), width, sc.fastidiousThreshold, std::ref(graftCandsMtx));
        }

        fastidiousCheckOtus(cbs, otus, acOtus, indices, acIndices, graftCands, sc);
        cbs.close();

        for (unsigned long v = 0; v < sc.numThreadsPerCheck; v++) {
            verifierThreads[v].join();
        }

    }

#endif

}

void SwarmClustering::determineGrafts(const AmpliconPools& pools, const std::vector<std::vector<Otu*>>& otus, std::vector<GraftCandidate>& allGraftCands, const numSeqs_t p, std::mutex& allGraftCandsMtx, const SwarmConfig& sc) {

    AmpliconCollection* ac = pools.get(p);
    RollingIndices<InvertedIndexFastidious> indices = RollingIndices<InvertedIndexFastidious>(2 * sc.fastidiousThreshold + 1, sc.fastidiousThreshold + sc.extraSegs, true, false);
    std::vector<GraftCandidate> graftCands = std::vector<GraftCandidate>(ac->size()); // initially, graft candidates for all amplicons of the pool are "empty"

    // a) Index amplicons of all light OTUs of the current pool
    {
        std::unordered_map<lenSeqs_t, SegmentFilter::Segments> segmentsArchive = std::unordered_map<lenSeqs_t, SegmentFilter::Segments>();
        for (auto otuIter = otus[p].begin(); otuIter != otus[p].end(); otuIter++) {

            if ((*otuIter)->mass < sc.boundary) {
                fastidiousIndexOtu(indices, segmentsArchive, *ac, *(*otuIter), graftCands, sc);
            }

        }
    }

    // determine maximum sequence length to adjust data structures in subsequently called methods
    lenSeqs_t maxLen = 0;
    for (auto iter = ac->begin(); iter != ac->end(); iter++) {
        maxLen = std::max(maxLen, iter->seq.length());
    }

    // b) Search with amplicons of all heavy OTUs of current and neighbouring pools
    std::mutex graftCandsMtx;
    lenSeqs_t halfRange = sc.fastidiousThreshold / (sc.threshold + 1);
    lenSeqs_t minP = (p > halfRange) ? (p - halfRange) : 0;
    lenSeqs_t maxP = std::min(p + halfRange, pools.numPools() - 1);
#if FASTIDIOUS_PARALLEL_CHECK

    switch (sc.fastidiousCheckingMode) {

        case 0: {

            for (lenSeqs_t q = minP; q < p; q++) {
                checkAndVerify(pools, otus[q], *(pools.get(q)), indices, *ac, graftCands, maxLen + 1, graftCandsMtx, sc);
            }

            checkAndVerify(pools, otus[p], *ac, indices, *ac, graftCands, maxLen + 1, graftCandsMtx, sc);

            for (lenSeqs_t q = p + 1; q <= maxP; q++) {

                AmpliconCollection* succAc = pools.get(q);

                // adjust maxLen as successor amplicon collection contains longer sequences
                for (auto iter = succAc->begin(); iter != succAc->end(); iter++) {
                    maxLen = std::max(maxLen, iter->seq.length());
                }

                checkAndVerify(pools, otus[q], *succAc, indices, *ac, graftCands, maxLen + 1, graftCandsMtx, sc);

            }

            break;

        }

        case 1: {

            std::thread t(&SwarmClustering::checkAndVerify, std::ref(pools), std::ref(otus[p]), std::ref(*ac), std::ref(indices), std::ref(*ac), std::ref(graftCands), maxLen + 1, std::ref(graftCandsMtx), std::ref(sc));

            for (lenSeqs_t q = minP; q < p; q++) {
                checkAndVerify(pools, otus[q], *(pools.get(q)), indices, *ac, graftCands, maxLen + 1, graftCandsMtx, sc);
            }

            for (lenSeqs_t q = p + 1; q <= maxP; q++) {

                AmpliconCollection* succAc = pools.get(q);

                // adjust maxLen as successor amplicon collection contains longer sequences
                for (auto iter = succAc->begin(); iter != succAc->end(); iter++) {
                    maxLen = std::max(maxLen, iter->seq.length());
                }

                checkAndVerify(pools, otus[q], *succAc, indices, *ac, graftCands, maxLen + 1, graftCandsMtx, sc);

            }

            t.join();

            break;

        }

        default: {

            std::thread pred, succ;

            std::thread self(&SwarmClustering::checkAndVerify, std::ref(pools), std::ref(otus[p]), std::ref(*ac), std::ref(indices), std::ref(*ac), std::ref(graftCands), maxLen + 1, std::ref(graftCandsMtx), std::ref(sc));

            for (lenSeqs_t d = 1; d <= halfRange; d++) {

                if (d <= p - minP) {
                    pred = std::thread(&SwarmClustering::checkAndVerify, std::ref(pools), std::ref(otus[p - d]), std::ref(*(pools.get(p - d))), std::ref(indices), std::ref(*ac), std::ref(graftCands), maxLen + 1, std::ref(graftCandsMtx), std::ref(sc));
                }

                if (d <= maxP - p) {

                    AmpliconCollection* succAc = pools.get(p + d);

                    // adjust maxLen as successor amplicon collection contains longer sequences
                    for (auto iter = succAc->begin(); iter != succAc->end(); iter++) {
                        maxLen = std::max(maxLen, iter->seq.length());
                    }

                    succ = std::thread(&SwarmClustering::checkAndVerify, std::ref(pools), std::ref(otus[p + d]), std::ref(*succAc), std::ref(indices), std::ref(*ac), std::ref(graftCands), maxLen + 1, std::ref(graftCandsMtx), std::ref(sc));

                }

                if (d <= p - minP) {
                    pred.join();
                }

                if (d <= maxP - p) {
                    succ.join();
                }

            }

            self.join();

            break;

        }

    }

#else

    if (p > 0) {
        checkAndVerify(pools, otus[p - 1], *(pools.get(p - 1)), indices, *ac, graftCands, maxLen + 1, graftCandsMtx, sc);
    }

    checkAndVerify(pools, otus[p], *ac, indices, *ac, graftCands, maxLen + 1, graftCandsMtx, sc);

    if (p < pools.numPools() - 1) {

        AmpliconCollection* succAc = pools.get(p + 1);

        // adjust maxLen as successor amplicon collection contains longer sequences
        for (auto iter = succAc->begin(); iter != succAc->end(); iter++) {
            maxLen = std::max(maxLen, iter->seq.length());
        }

        checkAndVerify(pools, otus[p + 1], *succAc, indices, *ac, graftCands, maxLen + 1, graftCandsMtx, sc);

    }

#endif

    // c) Collect the (actual = non-empty) graft candidates for the current pool
    auto newEnd = std::remove_if(
            graftCands.begin(),
            graftCands.end(),
            [](GraftCandidate& gc) {
                return gc.parentOtu == 0;
            });

    std::lock_guard<std::mutex> lock(allGraftCandsMtx);
    allGraftCands.reserve(allGraftCands.size() + std::distance(graftCands.begin(), newEnd));
    std::move(graftCands.begin(), newEnd, std::back_inserter(allGraftCands));

}

void SwarmClustering::graftOtus(numSeqs_t& maxSize, numSeqs_t& numOtus, const AmpliconPools& pools, const std::vector<std::vector<Otu*>>& otus, const SwarmConfig& sc) {

    std::vector<GraftCandidate> allGraftCands;
    std::mutex allGraftCandsMtx;

#if FASTIDIOUS_PARALLEL_POOL

    std::thread grafters[sc.numGrafters];
    unsigned long r = 0;
    for (; r + sc.numGrafters <= pools.numPools(); r += sc.numGrafters) {

        for (unsigned long g = 0; g < sc.numGrafters; g++) {
            grafters[g] = std::thread(&SwarmClustering::determineGrafts, std::ref(pools), std::ref(otus), std::ref(allGraftCands), r + g, std::ref(allGraftCandsMtx), std::ref(sc));
        }
        for (unsigned long g = 0; g < sc.numGrafters; g++) {
            grafters[g].join();
        }

    }

    for (unsigned long g = 0; g < pools.numPools() % sc.numGrafters; g++) {
        grafters[g] = std::thread(&SwarmClustering::determineGrafts, std::ref(pools), std::ref(otus), std::ref(allGraftCands), r + g, std::ref(allGraftCandsMtx), std::ref(sc));
    }
    for (unsigned long g = 0; g < pools.numPools() % sc.numGrafters; g++) {
        grafters[g].join();
    }

#else

    for (numSeqs_t p = 0; p < pools.numPools(); p++) {
        determineGrafts(pools, otus, allGraftCands, p, allGraftCandsMtx, sc);
    }

#endif

    // Sort all graft candidates and perform actual grafting
    std::cout << "Got " << allGraftCands.size() << " graft candidates." << std::endl;
    std::sort(allGraftCands.begin(), allGraftCands.end(), CompareGraftCandidatesAbund(pools));
    Otu* parentOtu = 0;
    Otu* childOtu = 0;
    numSeqs_t numGrafts = 0;
    for (auto graftIter = allGraftCands.begin(); graftIter != allGraftCands.end(); graftIter++) {

        if (!(graftIter->childOtu->attached)) {

            parentOtu = graftIter->parentOtu;
            childOtu = graftIter->childOtu;

            // "attach the seed of the light swarm to the tail of the heavy swarm"
            // OTU entries are moved unchanged (entry of seed of attached swarm stays 'incomplete', grafting 'link' is not recorded)
            parentOtu->members.reserve(parentOtu->members.size() + childOtu->members.size());
            std::move(std::begin(childOtu->members), std::end(childOtu->members), std::back_inserter(parentOtu->members));
            childOtu->members.clear();

            // update stats
            if (parentOtu->members.size() > maxSize) {
                maxSize = parentOtu->members.size();
            }

            parentOtu->numUniqueSequences += childOtu->numUniqueSequences;
            parentOtu->numSingletons += childOtu->numSingletons;
            parentOtu->mass += childOtu->mass;
            // maximum generation / radius are untouched

            childOtu->attached = true;
            numGrafts++;
            numOtus--;

        }

    }

    std::cout << "Made " << numGrafts << " grafts." << std::endl;

}

#if 0
void SwarmClustering::prepareGraftInfos(const numSeqs_t poolSize, const std::vector<Otu*>& otus, std::vector<GraftCandidate>& curGraftCands, std::vector<GraftCandidate>& nextGraftCands, const SwarmConfig& sc) {

    nextGraftCands = std::vector<GraftCandidate>(0);
    curGraftCands = std::vector<GraftCandidate>(poolSize);

    for (auto otuIter = otus.begin(); otuIter != otus.end(); otuIter++) {

        for (auto memberIter = (*otuIter)->members.begin(); memberIter != (*otuIter)->members.end(); memberIter++) {

            curGraftCands[memberIter->id].childOtu = *otuIter;
            curGraftCands[memberIter->id].childMember = &(*memberIter);

        }

    }

}

AmpliconCollection::iterator SwarmClustering::shiftIndexWindow(RollingIndices<InvertedIndexFastidious2>& indices, const AmpliconPools& pools, const std::vector<std::vector<Otu*>>& otus, const numSeqs_t poolIndex, const AmpliconCollection::iterator indexIter, std::vector<GraftCandidate>& curGraftCands, std::vector<GraftCandidate>& nextGraftCands, const lenSeqs_t len, const bool forerunning, const SwarmConfig& sc) {

    // Advance indexIter in the same pool and update the collection of inverted indices
    AmpliconCollection* ac = pools.get(poolIndex + forerunning);
    auto newIter = indexIter;
    auto a = std::distance(ac->begin(), newIter);
    lenSeqs_t seqLen = 0;
    SegmentFilter::Segments segments(sc.fastidiousThreshold + sc.extraSegs);

    for (; newIter != ac->end() && newIter->seq.length() <= len + sc.fastidiousThreshold; newIter++, a++) {

        if (curGraftCands[a].childOtu->mass >= sc.boundary) {

            if (newIter->seq.length() != seqLen) {

                seqLen = newIter->seq.length();
                indices.roll(seqLen);

                SegmentFilter::selectSegments(segments, seqLen, sc.fastidiousThreshold, sc.extraSegs);

            }

            for (lenSeqs_t i = 0; i < sc.fastidiousThreshold + sc.extraSegs; i++) {
                indices.getIndex(seqLen, i).add(newIter->seq.substr(segments[i].first, segments[i].second), std::make_pair(curGraftCands[a].childOtu, curGraftCands[a].childMember));
            }

        }

    }


    // Move indexIter to the next pool, if ...
    // ... the end position of indexIter (= first amplicon which is too long) in the current shifting round is in the next pool
    // ... and such a next pool exists
    // ... and indexIter would not advance two pools ahead of the amplicons currently filtered.
    // If then possible, advance indexIter and update collection of inverted indices as above.
    if (newIter == ac->end() && (poolIndex + 1 < pools.numPools()) && !forerunning) {

        ac = pools.get(poolIndex + 1);
        prepareGraftInfos(ac->size(), otus[poolIndex + 1], nextGraftCands, nextGraftCands, sc);

        for (newIter = ac->begin(), a = 0; newIter != ac->end() && newIter->seq.length() <= len + sc.fastidiousThreshold; newIter++, a++) {

            if (nextGraftCands[a].childOtu->mass >= sc.boundary) {

                if (newIter->seq.length() != seqLen) {

                    seqLen = newIter->seq.length();
                    indices.roll(seqLen);

                    SegmentFilter::selectSegments(segments, seqLen, sc.fastidiousThreshold, sc.extraSegs);

                }

                for (lenSeqs_t i = 0; i < sc.fastidiousThreshold + sc.extraSegs; i++) {
                    indices.getIndex(seqLen, i).add(newIter->seq.substr(segments[i].first, segments[i].second), std::make_pair(nextGraftCands[a].childOtu, nextGraftCands[a].childMember));
                }

            }

        }

    }

    return newIter;

}

void SwarmClustering::graftOtus2(numSeqs_t& maxSize, numSeqs_t& numOtus, const AmpliconPools& pools, const std::vector<std::vector<Otu*>>& otus, const SwarmConfig& sc) {

    RollingIndices<InvertedIndexFastidious2> indices(2 * sc.fastidiousThreshold + 1, sc.fastidiousThreshold + sc.extraSegs, true, false);
    std::unordered_map<lenSeqs_t, std::vector<SegmentFilter::Substrings>> substrs; //TODO? RollingIndices?
    std::vector<std::pair<Otu*, OtuEntry*>> candMembers;
    std::unordered_map<Otu*, std::unordered_map<OtuEntry*, lenSeqs_t>> candCnts;
    std::vector<GraftCandidate> curGraftCands, nextGraftCands, allGraftCands;
    lenSeqs_t M[pools.get(pools.numPools() - 1)->back().seq.length() + 1];

    // iterate over all amplicons in order of increasing length (as if they were stored in one big pool)
    numSeqs_t a = 0;
    numSeqs_t p = 0;
    AmpliconCollection* ac = pools.get(0);
    AmpliconCollection* last = pools.get(pools.numPools() - 1);
    lenSeqs_t seqLen = 0;
    std::vector<SegmentFilter::Substrings> tmp(sc.fastidiousThreshold + sc.extraSegs);

    prepareGraftInfos(ac->size(), otus[0], curGraftCands, nextGraftCands, sc);
    auto indexIter = ac->begin();
    nextGraftCands.resize(0); //TODO? unnecessary?

    for (auto amplIter = ac->begin(); amplIter != last->end();) {

        if (curGraftCands[a].childOtu->mass < sc.boundary) {

            // arriving at a new length, continue indexing
            if (amplIter->seq.length() != seqLen) {

                seqLen = amplIter->seq.length();

                if (nextGraftCands.size() == 0) { // indexIter still in the same pool
                    indexIter = shiftIndexWindow(indices, pools, otus, p, indexIter, curGraftCands, nextGraftCands, seqLen, false, sc);
                } else { // indexIter already in the next pool
                    indexIter = shiftIndexWindow(indices, pools, otus, p, indexIter, nextGraftCands, nextGraftCands, seqLen, true, sc);
                }

                // determine substring information for this length
                for (lenSeqs_t partnerLen = (seqLen > sc.fastidiousThreshold) * (seqLen - sc.fastidiousThreshold); partnerLen <= seqLen + sc.fastidiousThreshold; partnerLen++) {

                    std::vector<SegmentFilter::Substrings>& vec = substrs[partnerLen];

                    for (lenSeqs_t segmentIndex = 0; segmentIndex < sc.fastidiousThreshold + sc.extraSegs; segmentIndex++) {
                        if (partnerLen <= seqLen) {
                            tmp[segmentIndex] = SegmentFilter::selectSubstrs(seqLen, partnerLen, segmentIndex, sc.fastidiousThreshold, sc.extraSegs);
                        } else {
                            tmp[segmentIndex] = SegmentFilter::selectSubstrsBackward(seqLen, partnerLen, segmentIndex, sc.fastidiousThreshold, sc.extraSegs);
                        }
                    }

                    vec.swap(tmp);
                    tmp.reserve(sc.fastidiousThreshold + sc.extraSegs);

                }

            }

            // search for partners and verify them directly
            GraftCandidate& gc = curGraftCands[a];
            for (lenSeqs_t len = (seqLen > sc.fastidiousThreshold) * (seqLen - sc.fastidiousThreshold); len <= seqLen + sc.fastidiousThreshold; len++) { // ... search for grafting candidates among the indexed amplicons

                for (lenSeqs_t i = 0; i < sc.fastidiousThreshold + sc.extraSegs; i++) { // ... and apply segment filter for each segment

                    SegmentFilter::Substrings& subs = substrs[len][i];
                    InvertedIndexFastidious2& inv = indices.getIndex(len, i);

                    for (auto substrPos = subs.first; substrPos <= subs.last; substrPos++) {

                        candMembers = inv.getLabelsOf(std::string(amplIter->seq, substrPos, subs.len));

                        for (auto candIter = candMembers.begin(); candIter != candMembers.end(); candIter++) {
                            candCnts[candIter->first][candIter->second]++;
                        }

                    }

                }

                // general pigeonhole principle: for being a candidate, at least sc.extraSegs segments have to be matched
                for (auto otuIter = candCnts.begin(); otuIter != candCnts.end(); otuIter++) {

                    for (auto candIter = otuIter->second.begin(); candIter != otuIter->second.end(); candIter++) {

                        if (candIter->second >= sc.extraSegs) {

                            Amplicon& cand = (*pools.get(otuIter->first->poolId))[candIter->first->id];

                            if ((gc.parentOtu == 0 || compareCandidates(cand, (*pools.get(gc.parentOtu->poolId))[gc.parentMember->id]))
                                    && (Verification::computeBoundedRow(amplIter->seq, cand.seq, sc.fastidiousThreshold, M) <= sc.fastidiousThreshold)) {

                                gc.parentOtu = otuIter->first;
                                gc.parentMember = candIter->first;

                            }

                        }

                    }

                }

                candCnts.clear();

            }

        }

        // iterator handling (includes jumping to next pool)
        amplIter++;
        a++;

        if (amplIter == ac->end() && ac != last) {

            // collect and sort all (actual = non-empty) graft candidates for the current pool
            auto candEnd = std::remove_if(
                    curGraftCands.begin(),
                    curGraftCands.end(),
                    [](GraftCandidate& gc) {
                        return gc.parentOtu == 0;
                    });

            allGraftCands.reserve(allGraftCands.size() + std::distance(curGraftCands.begin(), candEnd));
            std::move(curGraftCands.begin(), candEnd, std::back_inserter(allGraftCands));

            ac = pools.get(++p);
            amplIter = ac->begin();
            a = 0;

            if (nextGraftCands.size() != 0) {

                curGraftCands.swap(nextGraftCands);
                nextGraftCands = std::vector<GraftCandidate>(0);

            } else {
                prepareGraftInfos(ac->size(), otus[p], curGraftCands, nextGraftCands, sc);
            }

        }
    }


    // collect and sort all (actual = non-empty) graft candidates for the last pool
    auto candEnd = std::remove_if(
            curGraftCands.begin(),
            curGraftCands.end(),
            [](GraftCandidate& gc) {
                return gc.parentOtu == 0;
            });

    allGraftCands.reserve(allGraftCands.size() + std::distance(curGraftCands.begin(), candEnd));
    std::move(curGraftCands.begin(), candEnd, std::back_inserter(allGraftCands));


    // perform actual grafting
    std::cout << "Got " << allGraftCands.size() << " graft candidates" << std::endl;
    std::sort(allGraftCands.begin(), allGraftCands.end(), CompareGraftCandidatesAbund(pools));

    numSeqs_t numGrafts = 0;
    Otu* parentOtu = 0;
    Otu* childOtu = 0;
    for (auto graftIter = allGraftCands.begin(); graftIter != allGraftCands.end(); graftIter++) {

        if (!graftIter->childOtu->attached) {

            parentOtu = graftIter->parentOtu;
            childOtu = graftIter->childOtu;

            // "attach the see of the light swarm to the tail of the heavy swarm"
            // OTU entries are moved unchanged (entry of seed of attached swarm stays 'incomplete', grafting 'link' is not recorded)
            parentOtu->members.reserve(parentOtu->members.size() + childOtu->members.size());
            std::move(std::begin(childOtu->members), std::end(childOtu->members), std::back_inserter(parentOtu->members));
            childOtu->members.clear();

            // update stats
            if (parentOtu->members.size() > maxSize) {
                maxSize = parentOtu->members.size();
            }

            parentOtu->numUniqueSequences += childOtu->numUniqueSequences;
            parentOtu->numSingletons += childOtu->numSingletons;
            parentOtu->mass += childOtu->mass;
            // maximum generation / radius are untouched

            childOtu->attached = true;
            numGrafts++;
            numOtus--;

        }

    }

    std::cout << "Made " << numGrafts << " grafts" << std::endl;

}
#endif

void SwarmClustering::processOtus(const AmpliconPools& pools, std::vector<std::vector<Otu*>>& otus, const SwarmConfig& sc) {

    // make OTU IDs unique over all pools (so far IDs start at 1 in each pool) (currently commented out)
    // add pool IDs and determine some overall statistics
    numSeqs_t numOtus = 0;
    numSeqs_t numOtusAdjusted = 0;
    numSeqs_t numAmplicons = 0;
    numSeqs_t maxSize = 0;
    numSeqs_t maxGen = 0;

    for (numSeqs_t p = 0; p < pools.numPools(); p++) {

        for (auto otuIter = otus[p].begin(); otuIter != otus[p].end(); otuIter++) {

//            (*otuIter)->id += numOtus;
            // set also parent id of seed to pool id (after fastidious clustering, there can be amplicons from
            // different pools in one OTU; OTU members with gen = 0 (seeds) denote a context (amplicon pool) switch)
            (*otuIter)->poolId = p;
            (*otuIter)->members[0].parentId = p;

            if ((*otuIter)->members.size() > maxSize){
                maxSize = (*otuIter)->members.size();
            }

            if ((*otuIter)->maxGen > maxGen){
                maxGen = (*otuIter)->maxGen;
            }

        }

        numOtus += otus[p].size();
        numAmplicons += pools.get(p)->size();

    }

    numOtusAdjusted = numOtus;

#if !PRINT_INTERNAL_MODIFIED
    if (!sc.dereplicate && sc.outInternals) {

        std::vector<Otu*> flattened;
        flattened.reserve(numOtus);

        for (auto p = 0; p < pools.numPools(); p++) {
            flattened.insert(flattened.end(), otus[p].begin(), otus[p].end());
        }

        std::sort(flattened.begin(), flattened.end(), CompareOtusSeedAbund(pools));
        outputInternalStructures(sc.oFileInternals, pools, flattened, sc.sepInternals);

    }
#endif

    /* (b) Optional (second) clustering phase of swarm */
    if (sc.fastidious) {

        std::cout << "Results before fastidious processing: " << std::endl;
        std::cout << "Number of swarms: " << numOtus << std::endl;
        std::cout << "Largest swarms: " << maxSize << std::endl;

        std::cout << "Counting amplicons in heavy and light swarms..." << std::endl;
        numSeqs_t numLightOtus = 0;
        numSeqs_t numAmplLightOtus = 0;
        for (numSeqs_t p = 0; p < pools.numPools(); p++) {
            for (auto otuIter = otus[p].begin(); otuIter != otus[p].end(); otuIter++) {

                numLightOtus += ((*otuIter)->mass < sc.boundary);
                numAmplLightOtus += ((*otuIter)->mass < sc.boundary) * (*otuIter)->members.size();

            }
        }
        std::cout << "Heavy swarms: " << (numOtus - numLightOtus) << ", with " << (numAmplicons - numAmplLightOtus) << " amplicons" << std::endl;
        std::cout << "Light swarms: " << numLightOtus << ", with " << numAmplLightOtus << " amplicons" << std::endl;

        if ((numLightOtus == 0) || (numLightOtus == numOtus)) {
            std::cout << "Fastidious: Only light or only heavy OTUs. No further action." << std::endl;
        } else {
            graftOtus(maxSize, numOtusAdjusted, pools, otus, sc);
        }

    }

    /* (c) Generating results */
    std::vector<Otu*> flattened(numOtus);
    auto iter = flattened.begin();
    for (auto p = 0; p < pools.numPools(); p++) {

        iter = std::move(otus[p].begin(), otus[p].end(), iter);
        otus[p] = std::vector<Otu*>();

    }

    if (sc.dereplicate) {

        std::sort(flattened.begin(), flattened.end(), CompareOtusMass(pools));

        outputDereplicate(pools, flattened, sc);

    } else {

        std::sort(flattened.begin(), flattened.end(), CompareOtusSeedAbund(pools));

#if PRINT_INTERNAL_MODIFIED
        if (sc.outInternals) outputInternalStructures(sc.oFileInternals, pools, flattened, sc.sepInternals);
#endif
        if (sc.outOtus) {
            (sc.outMothur) ?
              outputOtusMothur(sc.oFileOtus, pools, flattened, sc.threshold, numOtusAdjusted, sc.sepMothur, sc.sepMothurOtu, sc.sepAbundance)
            : outputOtus(sc.oFileOtus, pools, flattened, sc.sepOtus, sc.sepAbundance);
        }
        if (sc.outStatistics) outputStatistics(sc.oFileStatistics, pools, flattened, sc.sepStatistics);
        if (sc.outSeeds) outputSeeds(sc.oFileSeeds, pools, flattened, sc.sepAbundance);
        if (sc.outUclust) outputUclust(sc.oFileUclust, pools, flattened, sc);

    }

    std::cout << "Number of swarms: " << numOtusAdjusted << std::endl;
    std::cout << "Largest swarm: " << maxSize << std::endl;
    std::cout << "Max generations: " << maxGen << std::endl;


    /* (d) Cleaning up */
    for (auto otuIter = flattened.begin(); otuIter != flattened.end(); otuIter++) {
        delete *otuIter;
    }

}

void SwarmClustering::cluster(const AmpliconPools& pools, std::vector<Matches*>& allMatches, const SwarmConfig& sc) {

    /* (a) Mandatory (first) clustering phase of swarm */
    // determine OTUs by exploring all pools
    std::vector<std::vector<Otu*>> otus(pools.numPools());
    std::thread explorers[sc.numExplorers];
    unsigned long r = 0;
    for (; r + sc.numExplorers <= pools.numPools(); r += sc.numExplorers) {

        for (unsigned long e = 0; e < sc.numExplorers; e++) {
            explorers[e] = std::thread(&SwarmClustering::explorePool, std::ref(*(pools.get(r + e))), std::ref(*(allMatches[r + e])), std::ref(otus[r + e]), std::ref(sc));
        }
        for (unsigned long e = 0; e < sc.numExplorers; e++) {
            explorers[e].join();
        }

    }

    for (unsigned long e = 0; e < pools.numPools() % sc.numExplorers; e++) {
        explorers[e] = std::thread(&SwarmClustering::explorePool, std::ref(*(pools.get(r + e))), std::ref(*(allMatches[r + e])), std::ref(otus[r + e]), std::ref(sc));
    }
    for (unsigned long e = 0; e < pools.numPools() % sc.numExplorers; e++) {
        explorers[e].join();
    }

    processOtus(pools, otus, sc);

}

void SwarmClustering::cluster(const AmpliconPools& pools, const SwarmConfig& sc) {

    /* (a) Mandatory (first) clustering phase of swarm */
    // determine OTUs by exploring all pools
    std::vector<std::vector<Otu*>> otus(pools.numPools());
    std::thread explorers[sc.numExplorers];
    auto fun = (sc.numThreadsPerExplorer == 1) ? &SegmentFilter::swarmFilterDirectly : &SegmentFilter::swarmFilter;
    unsigned long r = 0;
    for (; r + sc.numExplorers <= pools.numPools(); r += sc.numExplorers) {

        for (unsigned long e = 0; e < sc.numExplorers; e++) {
            explorers[e] = std::thread(fun, std::ref(*(pools.get(r + e))), std::ref(otus[r + e]), std::ref(sc));
        }
        for (unsigned long e = 0; e < sc.numExplorers; e++) {
            explorers[e].join();
        }

    }

    for (unsigned long e = 0; e < pools.numPools() % sc.numExplorers; e++) {
        explorers[e] = std::thread(fun, std::ref(*(pools.get(r + e))), std::ref(otus[r + e]), std::ref(sc));
    }
    for (unsigned long e = 0; e < pools.numPools() % sc.numExplorers; e++) {
        explorers[e].join();
    }

    processOtus(pools, otus, sc);

}


#if SIMD_VERIFICATION
    std::string mapBack(std::string& s) {

    std::string res(s);
    for (auto i = 0; i < res.length(); i++) {
        res[i] = SimdVerification::sym_nt[res[i]];
    }

    return res;

}
#endif

void SwarmClustering::outputInternalStructures(const std::string oFile, const AmpliconPools& pools, const std::vector<Otu*>& otus, const char sep) {

    std::ofstream oStream(oFile);
    std::stringstream sStream;

    AmpliconCollection* ac = 0;
    Otu* otu = 0;

    for (auto i = 0; i < otus.size(); i++) {

        otu = otus[i];

        if (!otu->attached) {

            for (auto memberIter = otu->members.begin(); memberIter != otu->members.end(); memberIter++) {

                if (memberIter->gen == 0) {
                    ac = pools.get(memberIter->parentId);
                } else {

                    sStream << (*ac)[memberIter->parentId].id << sep << (*ac)[memberIter->id].id << sep << memberIter->parentDist << sep << (i + 1) << sep << memberIter->gen << std::endl;
                    oStream << sStream.rdbuf();
                    sStream.str(std::string());

                }

            }

        }

    }

    oStream.close();

}

void SwarmClustering::outputOtus(const std::string oFile, const AmpliconPools& pools, const std::vector<Otu*>& otus, const char sep, const std::string sepAbundance) {

    std::ofstream oStream(oFile);
    std::stringstream sStream;

    AmpliconCollection* ac = 0;
    Otu* otu = 0;

    for (auto i = 0; i < otus.size(); i++) {

        otu = otus[i];

        if (!otu->attached) {

            ac = pools.get(otu->poolId);
            sStream << (*ac)[otu->seedId].id << sepAbundance << (*ac)[otu->seedId].abundance;

            for (auto memberIter = otu->members.begin() + 1; memberIter != otu->members.end(); memberIter++) {

                if (memberIter->gen == 0) {
                    ac = pools.get(memberIter->parentId);
                }

                sStream << sep << (*ac)[memberIter->id].id << sepAbundance << (*ac)[memberIter->id].abundance;
            }

            sStream << std::endl;
            oStream << sStream.rdbuf();
            sStream.str(std::string());

        }

    }

    oStream.close();

}

void SwarmClustering::outputOtusMothur(const std::string oFile, const AmpliconPools& pools, const std::vector<Otu*>& otus, const lenSeqs_t threshold, const numSeqs_t numOtusAdjusted, const char sep, const std::string sepOtu, const std::string sepAbundance) {

    std::ofstream oStream(oFile);
    std::stringstream sStream;

    AmpliconCollection* ac = 0;
    Otu* otu = 0;

    oStream << "swarm_" << threshold << "\t" << numOtusAdjusted;

    for (auto i = 0; i < otus.size(); i++) {

        otu = otus[i];

        if (!otu->attached) {

            ac = pools.get(otu->poolId);
            sStream << sepOtu << (*ac)[otu->seedId].id << sepAbundance << (*ac)[otu->seedId].abundance;

            for (auto memberIter = otu->members.begin() + 1; memberIter != otu->members.end(); memberIter++) {

                if (memberIter->gen == 0) {
                    ac = pools.get(memberIter->parentId);
                }

                sStream << sep << (*ac)[memberIter->id].id << sepAbundance << (*ac)[memberIter->id].abundance;
            }

            oStream << sStream.rdbuf();
            sStream.str(std::string());

        }

    }

    oStream << std::endl;

    oStream.close();

}

void SwarmClustering::outputStatistics(const std::string oFile, const AmpliconPools& pools, const std::vector<Otu*>& otus, const char sep) {

    std::ofstream oStream(oFile);
    std::stringstream sStream;

    AmpliconCollection* ac = 0;
    Otu* otu = 0;

    for (auto i = 0; i < otus.size(); i++) {

        otu = otus[i];
        ac = pools.get(otu->poolId);

        if (!otu->attached) {

            sStream << otu->numUniqueSequences << sep << otu->mass << sep << (*ac)[otu->seedId].id << sep << otu->seedAbundance << sep << otu->numSingletons << sep << otu->maxGen << sep << otu->maxRad << std::endl;
            oStream << sStream.rdbuf();
            sStream.str(std::string());

        }

    }

    oStream.close();

}

void SwarmClustering::outputSeeds(const std::string oFile, const AmpliconPools& pools, const std::vector<Otu*>& otus, const std::string sepAbundance) {

    std::ofstream oStream(oFile);
    std::stringstream sStream;

    AmpliconCollection* ac = 0;
    Otu* otu = 0;

    for (auto i = 0; i < otus.size(); i++) {

        otu = otus[i];
        ac = pools.get(otu->poolId);

        if (!otu->attached) {

#if SIMD_VERIFICATION
            sStream << ">" << (*ac)[otu->seedId].id << sepAbundance << otu->mass << std::endl << mapBack((*ac)[otu->seedId].seq) << std::endl;
#else
            sStream << ">" << (*ac)[otu->seedId].id << sepAbundance << otu->mass << std::endl << (*ac)[otu->seedId].seq << std::endl;
#endif
            oStream << sStream.rdbuf();
            sStream.str(std::string());

        }

    }

    oStream.close();

}

void SwarmClustering::outputUclust(const std::string oFile, const AmpliconPools& pools, const std::vector<Otu*>& otus, const SwarmConfig& sc) {

    std::ofstream oStream(oFile);
    std::stringstream sStream;
    sStream << std::fixed << std::setprecision(1);

    AmpliconCollection* ac = 0;
    Otu* otu = 0;

    ac = pools.get(pools.numPools() - 1);
    lenSeqs_t maxLen = 0;
    for (auto iter = ac->begin(); iter != ac->end(); iter++) {
        maxLen = std::max(maxLen, iter->seq.length());
    }
    val_t D[maxLen + 1];
    val_t P[maxLen + 1];
    char BT[(maxLen + 1) * (maxLen + 1)];

    for (auto i = 0; i < otus.size(); i++) {

        otu = otus[i];

        if (!otu->attached) {

            ac = pools.get(otu->poolId);
            auto& seed = (*ac)[otu->seedId];

            sStream << 'C' << sc.sepUclust << i << sc.sepUclust << otu->members.size() << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust
                    << seed.id << sc.sepAbundance << seed.abundance << sc.sepUclust << '*' << '\n';
            sStream << 'S' << sc.sepUclust << i << sc.sepUclust << seed.seq.length() << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust
                    << seed.id << sc.sepAbundance << seed.abundance << sc.sepUclust << '*' << '\n';
            oStream << sStream.rdbuf();
            sStream.str(std::string());

            for (auto memberIter = otu->members.begin() + 1; memberIter != otu->members.end(); memberIter++) {

                if (memberIter->gen == 0) {
                    ac = pools.get(memberIter->parentId);
                }

                auto& member = (*ac)[memberIter->id];
                auto ai = Verification::computeGotohCigarRow1(seed.seq, member.seq, sc.scoring, D, P, BT);

                sStream << 'H' << sc.sepUclust << i << sc.sepUclust << member.seq.length() << sc.sepUclust << (100.0 * (ai.length - ai.numDiffs) / ai.length) << sc.sepUclust << '+' << sc.sepUclust << '0' << sc.sepUclust << '0' << sc.sepUclust << ((ai.numDiffs == 0) ? "=" : ai.cigar) << sc.sepUclust
                        << member.id << sc.sepAbundance << member.abundance << sc.sepUclust
                        << seed.id << sc.sepAbundance << seed.abundance << '\n';
                oStream << sStream.rdbuf();
                sStream.str(std::string());

            }

        }

    }

    oStream.close();

}

void SwarmClustering::outputDereplicate(const AmpliconPools& pools, const std::vector<Otu*>& otus, const SwarmConfig& sc) {

    std::ofstream oStreamInternals, oStreamOtus, oStreamStatistics, oStreamSeeds, oStreamUclust;
    std::stringstream sStreamInternals, sStreamOtus, sStreamStatistics, sStreamSeeds, sStreamUclust;
    sStreamUclust << std::fixed << std::setprecision(1);

    if (sc.outInternals) oStreamInternals.open(sc.oFileInternals);
    if (sc.outOtus) oStreamOtus.open(sc.oFileOtus);
    if (sc.outStatistics) oStreamStatistics.open(sc.oFileStatistics);
    if (sc.outSeeds) oStreamSeeds.open(sc.oFileSeeds);
    if (sc.outUclust) oStreamUclust.open(sc.oFileUclust);

    if (sc.outOtus && sc.outMothur) oStreamOtus << "swarm_" << sc.threshold << "\t" << otus.size();

    for (auto i = 0; i < otus.size(); i++) {

        Otu& otu = *(otus[i]);
        AmpliconCollection& ac = *(pools.get(otu.poolId));

        if (sc.outInternals) {

            for (auto memberIter = otu.members.begin() + 1; memberIter != otu.members.end(); memberIter++) {

                sStreamInternals << ac[otu.seedId].id << sc.sepInternals << ac[memberIter->id].id << sc.sepInternals << 0 << sc.sepInternals << (i + 1) << sc.sepInternals << 0 << std::endl;
                oStreamInternals << sStreamInternals.rdbuf();
                sStreamInternals.str(std::string());

            }

        }

        if (sc.outOtus) {

            if (sc.outMothur) {

                sStreamOtus << sc.sepMothurOtu << ac[otu.seedId].id << sc.sepAbundance << ac[otu.seedId].abundance;

                for (auto memberIter = otu.members.begin() + 1; memberIter != otu.members.end(); memberIter++) {
                    sStreamOtus << sc.sepMothur << ac[memberIter->id].id << sc.sepAbundance << ac[memberIter->id].abundance;
                }

                oStreamOtus << sStreamOtus.rdbuf();
                sStreamOtus.str(std::string());

            } else {

                sStreamOtus << ac[otu.seedId].id << sc.sepAbundance << ac[otu.seedId].abundance;

                for (auto memberIter = otu.members.begin() + 1; memberIter != otu.members.end(); memberIter++) {
                    sStreamOtus << sc.sepOtus << ac[memberIter->id].id << sc.sepAbundance << ac[memberIter->id].abundance;
                }

                sStreamOtus << std::endl;
                oStreamOtus << sStreamOtus.rdbuf();
                sStreamOtus.str(std::string());

            }

        }

        if (sc.outStatistics) {

            sStreamStatistics << otu.numUniqueSequences << sc.sepStatistics << otu.mass << sc.sepStatistics << ac[otu.seedId].id << sc.sepStatistics << otu.seedAbundance << sc.sepStatistics << otu.numSingletons << sc.sepStatistics << 0 << sc.sepStatistics << 0 << std::endl;
            oStreamStatistics << sStreamStatistics.rdbuf();
            sStreamStatistics.str(std::string());

        }

        if (sc.outSeeds) {

            sStreamSeeds << ">" << ac[otu.seedId].id << sc.sepAbundance << otu.mass << std::endl << ac[otu.seedId].seq << std::endl;
            oStreamSeeds << sStreamSeeds.rdbuf();
            sStreamSeeds.str(std::string());

        }

        if (sc.outUclust) {

            auto& seed = ac[otu.seedId];

            sStreamUclust << 'C' << sc.sepUclust << i << sc.sepUclust << otu.members.size() << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust
                          << seed.id << sc.sepAbundance << seed.abundance << sc.sepUclust << '*' << '\n';
            sStreamUclust << 'S' << sc.sepUclust << i << sc.sepUclust << seed.seq.length() << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust << '*' << sc.sepUclust
                          << seed.id << sc.sepAbundance << seed.abundance << sc.sepUclust << '*' << '\n';
            oStreamUclust << sStreamUclust.rdbuf();
            sStreamUclust.str(std::string());

            for (auto memberIter = otu.members.begin() + 1; memberIter != otu.members.end(); memberIter++) {
                sStreamOtus << 'H' << sc.sepUclust << i << sc.sepUclust << ac[memberIter->id].seq.length() << sc.sepUclust << "100.0" << sc.sepUclust << '+' << sc.sepUclust << '0' << sc.sepUclust << '0' << sc.sepUclust << '=' << sc.sepUclust
                            << ac[memberIter->id].id << sc.sepAbundance << ac[memberIter->id].abundance << sc.sepUclust
                            << seed.id << sc.sepAbundance << seed.abundance << '\n';
            }
            oStreamUclust << sStreamUclust.rdbuf();
            sStreamUclust.str(std::string());

        }

    }

    if (sc.outOtus && sc.outMothur) oStreamOtus << std::endl;

    if (sc.outInternals) oStreamInternals.close();
    if (sc.outOtus) oStreamOtus.close();
    if (sc.outStatistics) oStreamStatistics.close();
    if (sc.outSeeds) oStreamSeeds.close();
    if (sc.outUclust) oStreamUclust.close();

}

}