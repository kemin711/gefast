/*
 * SCT-PJ
 *
 * Copyright (C) 2016 Robert Mueller
 *
 * TODO add licence text (e.g. GNU (Affero) General Public License)
 *
 * Contact: Robert Mueller <romueller@techfak.uni-bielefeld.de>
 * Faculty of Technology, Bielefeld University,
 * PO box 100131, DE-33501 Bielefeld, Germany
 */

#include <fstream>
#include <unordered_set>

#include "../include/Relation.hpp"
#include "../include/SegmentFilter.hpp"


namespace SCT_PJ {

SegmentFilter::Substrings SegmentFilter::selectSubstrs(const lenSeqs_t selfLen, const lenSeqs_t partnerLen, const lenSeqs_t segIndex, const lenSeqs_t t, const lenSeqs_t k) {

    lenSeqs_t p = 0;
    lenSeqs_t l = 0;

    lenSeqs_t i = segIndex + 1; //computations from PJ-paper use segment indices from 1 to t+1

    //by even-partitioning scheme
    // * first t + k - d segments: length floor(|s| / (t+l))
    // * last d segments: length floor(|s| / (t+k)) + 1
    lenSeqs_t d = partnerLen - (partnerLen / (t + k)) * (t + k);
    l = (partnerLen / (t + k)) + (i > t + k - d);
    p = 1 + (i - 1) * l - (i > t + k - d) * (t + k - d);


    //multimatch-aware substring selection boundaries
//    //calculations from paper (assume that positions p start at 1)
//    lenSeqs_t lowerL = std::max((lenSeqs_t)1, p - (i - 1));
//    lenSeqs_t upperL = std::min(s.length() - l + 1, p + (i - 1));
//
//    lenSeqs_t lowerR = std::max((lenSeqs_t)1, p + (s.length() - len) - (t + k - i));
//    lenSeqs_t upperR = std::min(s.length() - l + 1, p + (s.length() - len) + (t + k - i));
//
//    lenSeqs_t lower = std::max(lowerL, lowerR);
//    lenSeqs_t upper = std::min(upperL, upperR);


//    //problem: underflow of non-negative type lenSeqs_t
//    lenSeqs_t lower = std::max({(lenSeqs_t)1, p - (i - 1), p + (s.length() - len) - (t + k - i)});
    lenSeqs_t lower = 1;
    if (p > (i - 1)) {
        lower = p - (i - 1);
    }
    if (((p + (selfLen - partnerLen)) > (t + k - i)) && ((p + (selfLen - partnerLen) - (t + k - i)) > lower)) {
        lower = p + (selfLen - partnerLen) - (t + k - i);
    }

    //std::min okay
    lenSeqs_t upper = std::min({
                                       selfLen - l + 1,
                                       p + (i - 1),
                                       p + (selfLen - partnerLen) + (t + k - i)
                               });

    return Substrings(lower - 1, upper - 1, l); //-1 for translation to index starting at 0

}

SegmentFilter::Substrings SegmentFilter::selectSubstrsBackward(const lenSeqs_t selfLen, const lenSeqs_t partnerLen, const lenSeqs_t segIndex, const lenSeqs_t t, const lenSeqs_t k) {

    lenSeqs_t p = 0;
    lenSeqs_t l = 0;

    lenSeqs_t i = segIndex + 1; //computations from PJ-paper use segment indices from 1 to t+1

    //by even-partitioning scheme
    // * first t + k - d segments: length floor(|s| / (t+l))
    // * last d segments: length floor(|s| / (t+k)) + 1
    lenSeqs_t d = partnerLen - (partnerLen / (t + k)) * (t + k);
    l = (partnerLen / (t + k)) + (i > t + k - d);
    p = 1 + (i - 1) * l - (i > t + k - d) * (t + k - d);


    //multimatch-aware substring selection boundaries
//    //calculations from paper (assume that positions p start at 1)
//    lenSeqs_t lowerL = std::max((lenSeqs_t)1, p - (i - 1));
//    lenSeqs_t upperL = std::min(s.length() - l + 1, p + (i - 1));
//
//    lenSeqs_t lowerR = std::max((lenSeqs_t)1, p - (len - s.length()) - (t + k - i));
//    lenSeqs_t upperR = std::min(s.length() - l + 1, p + (len - s.length()) + (t + k - i));
//
//    lenSeqs_t lower = std::max(lowerL, lowerR);
//    lenSeqs_t upper = std::min(upperL, upperR);


//    //problem: underflow of non-negative type lenSeqs_t
//    lenSeqs_t lower = std::max({(lenSeqs_t)1, p - (i - 1), p + (s.length() - len) - (t + k - i)});

    lenSeqs_t lower = 1;
    if (p > (i - 1)) {
        lower = p - (i - 1);
    }
    if ((p > ((partnerLen - selfLen) + (t + k - i))) && ((p - (partnerLen - selfLen) - (t + k - i)) > lower)) {
        lower = p - (partnerLen - selfLen) - (t + k - i);
    }

    //std::min okay
    lenSeqs_t upper = std::min({
                                       selfLen - l + 1,
                                       p + (i - 1),
                                       p - (partnerLen - selfLen) + (t + k - i)
                               });

    return Substrings(lower - 1, upper - 1, l); //-1 for translation to index starting at 0

}

void SegmentFilter::selectSegments(Segments& segments, const lenSeqs_t seqLen, const lenSeqs_t t, const lenSeqs_t k) {

    lenSeqs_t d = seqLen - (seqLen / (t + k)) * (t + k);
    lenSeqs_t p = 0;


    lenSeqs_t l = (seqLen / (t + k));
    for (lenSeqs_t i = 1; i <= t + k - d; i++) {

        segments[i - 1] = std::make_pair(p, l);
        p += l;

    }


    l = (seqLen / (t + k)) + 1;
    for (lenSeqs_t i = 1; i <= d; i++) {

        segments[t + k - d + (i - 1)] = std::make_pair(p, l);
        p += l;

    }

}




void SegmentFilter::filterForward(const AmpliconCollection& ac, const Subpool& sp, RotatingBuffers<Candidate>& cands, const lenSeqs_t t, const lenSeqs_t k) {

    RollingIndices<InvertedIndex> indices(t + 1, t + k, true);

    std::vector<numSeqs_t> candIntIds;
    Substrings substrs[t + 1][t + k];
    Segments segments(t + k);

    std::unordered_map<numSeqs_t, lenSeqs_t> candCnts;
    std::vector<Candidate> candColl;

    lenSeqs_t seqLen = 0;
    numSeqs_t curIntId = sp.beginIndex;

    // process index-only amplicons
    for (; curIntId < sp.beginMatch; curIntId++) {

        if (ac[curIntId].seq.length() != seqLen) {

            seqLen = ac[curIntId].seq.length();
            indices.roll(seqLen);

            selectSegments(segments, seqLen, t, k);

        }

        for (lenSeqs_t i = 0; i < t + k; i++) {
            indices.getIndex(seqLen,i).add(ac[curIntId].seq.substr(segments[i].first, segments[i].second), curIntId);
        }

    }



    // process remaining amplicons by matching and indexing
    seqLen = 0; // ensures that the substring information are computed
    for (; curIntId < sp.end; curIntId++) { // for every amplicon in the collection...

        // on reaching new length group, open new inverted indices and discard those no longer needed ...
        if (ac[curIntId].seq.length() != seqLen) {

            seqLen = ac[curIntId].seq.length();
            indices.roll(seqLen);

            // ... and determine position information shared by all amplicons of this length
            for (lenSeqs_t lenDiff = 0; lenDiff < t + 1; lenDiff++) {
                for (lenSeqs_t segmentIndex = 0; segmentIndex < t + k; segmentIndex++) {
                    substrs[lenDiff][segmentIndex] = selectSubstrs(seqLen, seqLen - lenDiff, segmentIndex, t, k);
                }
            }
            selectSegments(segments, seqLen, t, k);

        }


        for (lenSeqs_t len = (seqLen > t) * (seqLen - t); len <= seqLen; len++) { //... consider already indexed seqs with a feasible length...

            candCnts.clear();

            for (lenSeqs_t i = 0; i < t + k; i++) { //... and apply segment filter for each segment

                Substrings& subs = substrs[seqLen - len][i];//selectSubstrs(amplIter->seq.length(), len, i, t, k);
                InvertedIndex& inv = indices.getIndex(len, i);

                for (auto substrPos = subs.first; substrPos <= subs.last; substrPos++) {

                    candIntIds = inv.getLabelsOf(std::string(ac[curIntId].seq, substrPos, subs.len));

                    for (auto candIter = candIntIds.begin(); candIter != candIntIds.end(); candIter++) {
                        candCnts[*candIter]++;
                    }

                }

            }

            // general pigeonhole principle: for being a candidate, at least k segments have to be matched
            for (auto candIter = candCnts.begin(); candIter != candCnts.end(); candIter++) {

                if (candIter->second >= k) {
                    candColl.push_back(Candidate(curIntId, candIter->first));
                }

            }

        }

        //index sequence
        for (lenSeqs_t i = 0; i < t + k; i++) {
            indices.getIndex(seqLen,i).add(ac[curIntId].seq.substr(segments[i].first, segments[i].second), curIntId);
        }

        cands.push(candColl);
        candColl = std::vector<Candidate>();

    }

}



void SegmentFilter::filterBackward(const AmpliconCollection& ac, const Subpool& sp, RotatingBuffers<Candidate>& cands, const lenSeqs_t t, const lenSeqs_t k) {

    RollingIndices<InvertedIndex> indices(t + 1, t + k, false);

    std::vector<numSeqs_t> candIntIds;
    Substrings substrs[t + 1][t + k];
    Segments segments(t + k);

    std::unordered_map<numSeqs_t, lenSeqs_t> candCnts;
    std::vector<Candidate> candColl;

    lenSeqs_t seqLen = 0;
    numSeqs_t curIntId = sp.end - 1;

    // process index-only amplicons
    for (; curIntId >= sp.beginIndex; curIntId--) {

        if (ac[curIntId].seq.length() != seqLen) {

            seqLen = ac[curIntId].seq.length();
            indices.roll(seqLen);

            selectSegments(segments, seqLen, t, k);

        }

        for (lenSeqs_t i = 0; i < t + k; i++) {
            indices.getIndex(seqLen,i).add(ac[curIntId].seq.substr(segments[i].first, segments[i].second), curIntId);
        }

    }



    // process remaining amplicons by matching and indexing
    seqLen = 0; // ensures that the substring information are computed
    curIntId++; // courtesy of do-while loop and unsigned integer type
    do { // for every remaining amplicon in the collection...

        curIntId--;

        // on reaching new length group, open new inverted indices and discard those no longer needed ...
        if (ac[curIntId].seq.length() != seqLen) {

            seqLen = ac[curIntId].seq.length();
            indices.roll(seqLen);

            // ... and determine position information shared by all amplicons of this length
            for (lenSeqs_t lenDiff = 0; lenDiff < t + 1; lenDiff++) {
                for (lenSeqs_t segmentIndex = 0; segmentIndex < t + k; segmentIndex++) {
                    substrs[lenDiff][segmentIndex] = selectSubstrsBackward(seqLen, seqLen + lenDiff, segmentIndex, t, k);
                }
            }
            selectSegments(segments, seqLen, t, k);

        }


        for (lenSeqs_t len = seqLen + t; len >= seqLen; len--) { //... consider already indexed seqs with a feasible length...

            candCnts.clear();

            for (lenSeqs_t i = 0; i < t + k; i++) { //... and apply segment filter for each segment

                Substrings& subs = substrs[len - seqLen][i]; //<-- change: swapped len and seqLen
                InvertedIndex& inv = indices.getIndex(len, i);

                for (auto substrPos = subs.first; substrPos <= subs.last; substrPos++) {

                    candIntIds = inv.getLabelsOf(std::string(ac[curIntId].seq, substrPos, subs.len));

                    for (auto candIter = candIntIds.begin(); candIter != candIntIds.end(); candIter++) {
                        candCnts[*candIter]++;
                    }

                }

            }

            // general pigeonhole principle: for being a candidate, at least k segments have to be matched
            for (auto candIter = candCnts.begin(); candIter != candCnts.end(); candIter++) {

                if (candIter->second >= k) {
                    candColl.push_back(Candidate(curIntId, candIter->first));
                }

            }

        }

        //index sequence
        for (lenSeqs_t i = 0; i < t + k; i++) {
            indices.getIndex(seqLen,i).add(ac[curIntId].seq.substr(segments[i].first, segments[i].second), curIntId);
        }

        cands.push(candColl);
        candColl = std::vector<Candidate>();

    } while (curIntId != sp.beginMatch);

}



void SegmentFilter::filterForwardBackward(const AmpliconCollection& ac, const Subpool& sp, RotatingBuffers<Candidate>& cands, const lenSeqs_t t, const lenSeqs_t k) {

    RollingIndices<InvertedIndex> indices(t + 1, t + k, true);

    std::vector<numSeqs_t> candIntIds;
    Substrings substrs[t + 1][t + k];
    Segments segments(t + k);
    std::vector<std::string> segmentStrs(t + k);

    std::unordered_map<numSeqs_t, lenSeqs_t> candCnts;
    std::vector<Candidate> candColl;

    lenSeqs_t seqLen = 0;
    numSeqs_t curIntId = sp.beginIndex;

    // process index-only amplicons
    for (; curIntId < sp.beginMatch; curIntId++) {

        if (ac[curIntId].seq.length() != seqLen) {

            seqLen = ac[curIntId].seq.length();
            indices.roll(seqLen);

            selectSegments(segments, seqLen, t, k);

        }

        for (lenSeqs_t i = 0; i < t + k; i++) {
            indices.getIndex(seqLen,i).add(ac[curIntId].seq.substr(segments[i].first, segments[i].second), curIntId);
        }

    }



    // process remaining amplicons by matching and indexing
    seqLen = 0; // ensures that the substring information are computed
    for (; curIntId < sp.end; curIntId++) { // for every amplicon in the collection...

        // on reaching new length group, open new inverted indices and discard those no longer needed ...
        if (ac[curIntId].seq.length() != seqLen) {

            seqLen = ac[curIntId].seq.length();
            indices.roll(seqLen);

            // ... and determine position information shared by all amplicons of this length
            for (lenSeqs_t lenDiff = 0; lenDiff < t + 1; lenDiff++) {
                for (lenSeqs_t segmentIndex = 0; segmentIndex < t + k; segmentIndex++) {
                    substrs[lenDiff][segmentIndex] = selectSubstrs(seqLen, seqLen - lenDiff, segmentIndex, t, k);
                }
            }
            selectSegments(segments, seqLen, t, k);

        }

        // compute actual segments of the current amplicon here once (used in pipelined filtering and indexing steps)
        for (auto i = 0; i < t + k; i++) {
            segmentStrs[i] = ac[curIntId].seq.substr(segments[i].first, segments[i].second);
        }

        for (lenSeqs_t len = (seqLen > t) * (seqLen - t); len <= seqLen; len++) { //... consider already indexed seqs with a feasible length...

            candCnts.clear();

            for (lenSeqs_t i = 0; i < t + k; i++) { //... and apply segment filter for each segment

                Substrings& subs = substrs[seqLen - len][i];
                InvertedIndex& inv = indices.getIndex(len, i);

                for (auto substrPos = subs.first; substrPos <= subs.last; substrPos++) {

                    candIntIds = inv.getLabelsOf(std::string(ac[curIntId].seq, substrPos, subs.len));

                    for (auto candIter = candIntIds.begin(); candIter != candIntIds.end(); candIter++) {
                        candCnts[*candIter]++;
                    }

                }

            }


            // general pigeonhole principle: for being a candidate, at least k segments have to be matched
            //  + pipelined backward filtering
            //      advantages of performing it directly here:
            //      (a) initial candidate set stays small (more precisely, there is no actual intermediate candidate set between the filtering steps)
            //      (b) length of candidates is known and equal for all of them -> selectSubstrs() needed only once
            //      (c) segment information of current amplicon known -> reuse
            std::string candStr;
            lenSeqs_t cnt = 0; // number of substring-segment matches for the current candidate in the second filter step

            Substrings candSubs[t + k]; // common substring information of all candidates
            for (lenSeqs_t i = 0; i < t + k; i++) {
                candSubs[i] = selectSubstrsBackward(len, seqLen, i, t, k);
            }

            for (auto candIter = candCnts.begin(); candIter != candCnts.end(); candIter++) { // 1st component = integer id of amplicon, 2nd component = number of substring-segment matches during first filter step

                if (candIter->second >= k) { // initial candidate found -> immediate backward filtering

                    candStr = ac[candIter->first].seq;

                    for (lenSeqs_t i = 0; i < t + k && cnt < k; i++) {
                        cnt += (candStr.substr(candSubs[i].first, (candSubs[i].last - candSubs[i].first) + candSubs[i].len).find(segmentStrs[i]) < std::string::npos);
                    }

                    if (cnt == k) {
                        candColl.push_back(Candidate(curIntId, candIter->first));
                    }

                    cnt = 0;


                }

            }

        }

        //index sequence
        for (lenSeqs_t i = 0; i < t + k; i++) {
            indices.getIndex(seqLen,i).add(segmentStrs[i], curIntId);
        }

        cands.push(candColl);
        candColl = std::vector<Candidate>();

    }

}



void SegmentFilter::filterBackwardForward(const AmpliconCollection& ac, const Subpool& sp, RotatingBuffers<Candidate>& cands, const lenSeqs_t t, const lenSeqs_t k) {

    RollingIndices<InvertedIndex> indices(t + 1, t + k, false);

    std::vector<numSeqs_t> candIntIds;
    Substrings substrs[t + 1][t + k];
    Segments segments(t + k);
    std::vector<std::string> segmentStrs(t + k);

    std::unordered_map<numSeqs_t, lenSeqs_t> candCnts;
    std::vector<Candidate> candColl;

    lenSeqs_t seqLen = 0;
    numSeqs_t curIntId = sp.end - 1;

    // process index-only amplicons
    for (; curIntId >= sp.beginIndex; curIntId--) {

        if (ac[curIntId].seq.length() != seqLen) {

            seqLen = ac[curIntId].seq.length();
            indices.roll(seqLen);

            selectSegments(segments, seqLen, t, k);

        }

        for (lenSeqs_t i = 0; i < t + k; i++) {
            indices.getIndex(seqLen,i).add(ac[curIntId].seq.substr(segments[i].first, segments[i].second), curIntId);
        }

    }



    // process remaining amplicons by matching and indexing
    seqLen = 0; // ensures that the substring information are computed
    curIntId++; // courtesy of do-while loop and unsigned integer type
    do { // for every remaining amplicon in the collection...

        curIntId--;

        // on reaching new length group, open new inverted indices and discard those no longer needed ...
        if (ac[curIntId].seq.length() != seqLen) {

            seqLen = ac[curIntId].seq.length();
            indices.roll(seqLen);

            // ... and determine position information shared by all amplicons of this length
            for (lenSeqs_t lenDiff = 0; lenDiff < t + 1; lenDiff++) {
                for (lenSeqs_t segmentIndex = 0; segmentIndex < t + k; segmentIndex++) {
                    substrs[lenDiff][segmentIndex] = selectSubstrsBackward(seqLen, seqLen + lenDiff, segmentIndex, t, k); //<-- change: selectSubstrsBackward, '-' to '+'
                }
            }
            selectSegments(segments, seqLen, t, k);

        }

        // compute actual segments of the current amplicon here once (used in pipelined filtering and indexing steps)
        for (auto i = 0; i < t + k; i++) {
            segmentStrs[i] = ac[curIntId].seq.substr(segments[i].first, segments[i].second);
        }

        for (lenSeqs_t len = seqLen + t; len >= seqLen; len--) { //... consider already indexed seqs with a feasible length...

            candCnts.clear();

            for (lenSeqs_t i = 0; i < t + k; i++) { //... and apply segment filter for each segment

                Substrings& subs = substrs[len - seqLen][i]; //<-- change: swapped len and seqLen
                InvertedIndex& inv = indices.getIndex(len, i);

                for (auto substrPos = subs.first; substrPos <= subs.last; substrPos++) {

                    candIntIds = inv.getLabelsOf(std::string(ac[curIntId].seq, substrPos, subs.len));

                    for (auto candIter = candIntIds.begin(); candIter != candIntIds.end(); candIter++) {
                        candCnts[*candIter]++;
                    }

                }

            }


            // general pigeonhole principle: for being a candidate, at least k segments have to be matched
            //  + pipelined forward filtering
            //      advantages of performing it directly here:
            //      (a) initial candidate set stays small (more precisely, there is no actual intermediate candidate set between the filtering steps)
            //      (b) length of candidates is known and equal for all of them -> selectSubstrs() needed only once
            //      (c) segment information of current amplicon known -> reuse
            std::string candStr;
            lenSeqs_t cnt = 0; // number of substring-segment matches for the current candidate in the second filter step

            Substrings candSubs[t + k]; // common substring information of all candidates
            for (lenSeqs_t i = 0; i < t + k; i++) {
                candSubs[i] = selectSubstrs(len, seqLen, i, t, k);
            }

            for (auto candIter = candCnts.begin(); candIter != candCnts.end(); candIter++) { // 1st component = integer id of amplicon, 2nd component = number of substring-segment matches during first filter step

                if (candIter->second >= k) { // initial candidate found -> immediate forward filtering

                    candStr = ac[candIter->first].seq;

                    for (lenSeqs_t i = 0; i < t + k && cnt < k; i++) {
                        cnt += (candStr.substr(candSubs[i].first, (candSubs[i].last - candSubs[i].first) + candSubs[i].len).find(segmentStrs[i]) < std::string::npos);
                    }

                    if (cnt == k) {
                        candColl.push_back(Candidate(curIntId, candIter->first));
                    }

                    cnt = 0;


                }

            }

        }

        //index sequence
        for (lenSeqs_t i = 0; i < t + k; i++) {
            indices.getIndex(seqLen,i).add(segmentStrs[i], curIntId);
        }

        cands.push(candColl);
        candColl = std::vector<Candidate>();

    } while (curIntId != sp.beginMatch);

}


void SegmentFilter::filter(const AmpliconCollection& ac, const Subpool& sp, RotatingBuffers<Candidate>& cands, const lenSeqs_t t, const lenSeqs_t k, const int mode) { //TODO set "best" default (see also config creation)

    switch (mode) {

        case 0:
            filterForward(ac, sp, cands, t, k);
            break;

        case 1:
            filterBackward(ac, sp, cands, t, k);
            break;

        case 2:
            filterForwardBackward(ac, sp, cands, t, k);
            break;

        case 3:
            filterBackwardForward(ac, sp, cands, t, k);
            break;

        default:
            filterForward(ac, sp, cands, t, k);

    }

}



void SegmentFilter::writeMatches(std::string oFile, AmpliconPools& pools, std::vector<Matches*>& allMatches) {

    std::ofstream oStream(oFile);

    for (lenSeqs_t i = 0; i < pools.numPools(); i++) {

        AmpliconCollection* ac = pools.get(i);
        Matches* matches = allMatches[i];

        for (numSeqs_t j = 0; j < ac->size(); j++) {

            auto m = matches->getMatchesOfAugmented(j);

            for (auto matchIter = m.begin(); matchIter != m.end(); matchIter++) {
                oStream << (*ac)[j].id << ";" << (*ac)[matchIter->first].id << ";" << (matchIter->second) << std::endl;
            }

        }

    }

    oStream.close();

}


void SegmentFilter::writeMatchesOneWay(std::string oFile, AmpliconPools& pools, std::vector<Matches*>& allMatches) {

    std::ofstream oStream(oFile);

    for (lenSeqs_t i = 0; i < pools.numPools(); i++) {

        AmpliconCollection* ac = pools.get(i);
        Matches* matches = allMatches[i];

        for (numSeqs_t j = 0; j < ac->size(); j++) {

            auto m = matches->getMatchesOfAugmented(j);

            for (auto matchIter = m.begin(); matchIter != m.end(); matchIter++) {

                if (matchIter->first <= j) continue; // to avoid writing a match in both directions; a match is written with the shorter (or, if equally long, lexicographically 'smaller') amplicon first
                oStream << (*ac)[j].id << ";" << (*ac)[matchIter->first].id << ";" << (matchIter->second) << std::endl;
            }

        }

    }

    oStream.close();

}

}