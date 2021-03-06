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

#ifndef GEFAST_BASE_HPP
#define GEFAST_BASE_HPP

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>


/* Preprocessor directives with program-wide effects */

#ifndef SUCCINCT
#define SUCCINCT 0
#endif

#ifndef SUCCINCT_FASTIDIOUS
#define SUCCINCT_FASTIDIOUS 0
#endif

#ifndef QGRAM_FILTER
#define QGRAM_FILTER 1
#endif
#if QGRAM_FILTER
#define QGRAMLENGTH 5
#define QGRAMVECTORBITS (1<<(2*QGRAMLENGTH))
#define QGRAMVECTORBYTES (QGRAMVECTORBITS/8)
#endif



namespace GeFaST {

// type for everything related to counts of amplicons / sequences
typedef unsigned long numSeqs_t;

// type for everything related to the length of the sequence of an amplicon
typedef unsigned long lenSeqs_t;

// type for values in the DP-matrix of (Gotoh) verification methods
typedef lenSeqs_t val_t;
//const val_t NEG_INF = INT16_MIN;
const val_t POS_INF = INT16_MAX;

#if QGRAM_FILTER
// mapping of nucleotides onto integers (a/A -> 1, c/C -> 2, g/G -> 3, t/T/u/U -> 4)
extern char acgtuMap[256];
#endif



// =====================================================
//              Data type for single amplicons
// =====================================================

/*
 * Representation of a single amplicon comprising the identifier, the sequence
 * and its length and the abundance of the amplicon.
 *
 * A q-gram vector can be added in order to speed up the candidate filtering step.
 * The handling of the q-gram vector is adapted from Swarm's findqgrams(...).
 *
 * The amplicon does NOT own the identifier and sequence strings.
 */
struct Amplicon {

    char* id; // amplicon identifier (e.g. first part of FASTA defline)
    char* seq; // amplicon sequence
    lenSeqs_t len; // length of amplicon sequence
    numSeqs_t abundance; // abundance of amplicon
#if QGRAM_FILTER
    // one bit per possible q-gram:
    // 0 (absence or even number of occurrences) and 1 (odd number of occurrences)
    unsigned char qGramVector[QGRAMVECTORBYTES];
#endif

    Amplicon() {

        id = 0;
        seq = 0;
        len = 0;
        abundance = 0;
#if QGRAM_FILTER
        memset(qGramVector, 0, QGRAMVECTORBYTES);
#endif

    }

    Amplicon(char* i, char* s, lenSeqs_t l, numSeqs_t a) {

        id = i;
        seq = s;
        len = l;
        abundance = a;
#if QGRAM_FILTER
        memset(qGramVector, 0, QGRAMVECTORBYTES);

        unsigned long qGram = 0;
        unsigned long j = 0;

        while((j < QGRAMLENGTH - 1) && (j < len)) {

            qGram = (qGram << 2) | (acgtuMap[seq[j]] - 1);
            j++;

        }

        while(j < len) {

            qGram = (qGram << 2) | (acgtuMap[seq[j]] - 1);
            qGramVector[(qGram >> 3) & (QGRAMVECTORBYTES - 1)] ^= (1 << (qGram & 7));
            j++;

        }
#endif

    }

    Amplicon& operator=(const Amplicon& other) {

        // check for self-assignment
        if (&other == this) {
            return *this;
        }

        id = other.id;
        seq = other.seq;
        len = other.len;
        abundance = other.abundance;
#if QGRAM_FILTER
        for (numSeqs_t i = 0; i < QGRAMVECTORBYTES; i++) {
            qGramVector[i] = other.qGramVector[i];
        }
#endif
        return *this;

    }

};

// comparer structures for Amplicon structures
struct AmpliconCompareAlph { // lexicographical, ascending
    bool operator()(const Amplicon& amplA, const Amplicon& amplB);
};

struct AmpliconCompareLen { // by length, ascending
    bool operator()(const Amplicon& amplA, const Amplicon& amplB);
};

struct AmpliconCompareAbund { // by abundance, ascending
    bool operator()(const Amplicon& amplA, const Amplicon& amplB);
};

struct AmpliconSeqEqual { // string equality
    bool operator()(const Amplicon& amplA, const Amplicon& amplB);
};

// describes a set of substrings chosen for comparison with a segment
struct Substrings {

    lenSeqs_t first; // start position of first substring to be checked
    lenSeqs_t last; // start position of last substring to be checked
    lenSeqs_t len; // common length of all substrings to be checked

    Substrings() {

        first = 0;
        last = 0;
        len = 0;

    }

    Substrings(lenSeqs_t fp, lenSeqs_t lp, lenSeqs_t l) {

        first = fp;
        last = lp;
        len = l;

    }

};

// describes a set of segments to be indexed as (first position, length of segment)
typedef std::vector<std::pair<lenSeqs_t, lenSeqs_t>> Segments;


// select 'substrings' (MMASS) for segment filter
Substrings selectSubstrs(const lenSeqs_t selfLen, const lenSeqs_t partnerLen, const lenSeqs_t segIndex,
                         const lenSeqs_t t, const lenSeqs_t k);
Substrings selectSubstrsBackward(const lenSeqs_t selfLen, const lenSeqs_t partnerLen, const lenSeqs_t segIndex,
                                 const lenSeqs_t t, const lenSeqs_t k);

// select 'segments' (to be stored in a parameter) for indexing step
void selectSegments(Segments& segments, const lenSeqs_t seqLen, const lenSeqs_t t, const lenSeqs_t k);


// =====================================================
//           Data types for multiple amplicons
// =====================================================

/*
 * Collection of amplicons with counts of the different occurring sequence lengths.
 *
 * The capacity of the collection and the counts are set before amplicons are added
 * and are not affected when new amplicons are added.
 */
class AmpliconCollection {

public:
    AmpliconCollection(const numSeqs_t capacity, const std::vector<std::pair<lenSeqs_t, numSeqs_t>>& counts);

    ~AmpliconCollection();

    void push_back(const Amplicon& ampl);

    Amplicon& operator[](const numSeqs_t i);

    const Amplicon& operator[](const numSeqs_t i) const;

    Amplicon& front() const;

    Amplicon& back() const;

    Amplicon* begin() const;

    Amplicon* end() const;

    numSeqs_t size() const;

    numSeqs_t numSeqsOfLen(const lenSeqs_t len) const;

    lenSeqs_t maxLen() const;

    void reserve(const numSeqs_t newCapacity);

    std::vector<lenSeqs_t> allLengths() const;

private:
    Amplicon* amplicons_; // amplicon array
    numSeqs_t size_; // number of amplicons
    numSeqs_t capacity_; // capacity of the amplicon array

    std::pair<lenSeqs_t, numSeqs_t>* counts_; // number of amplicons per (occurring) length
    lenSeqs_t numLengths_; // number of different lengths in the amplicon collection

};

/*
 * Collection of multiple amplicon collections.
 *
 * Manages a single array storing all identifier and sequence strings
 * of the comprised amplicon collections.
 */
class AmpliconPools {

public:
    AmpliconPools(std::map<lenSeqs_t, numSeqs_t>& counts, const unsigned long long capacity, const lenSeqs_t threshold);

    ~AmpliconPools();

    // adds a new amplicon to pool / amplicon collection i by storing header and sequence information
    // in the overall strings array and letting the amplicon members point there
    void add(const lenSeqs_t i, const std::string& header, const std::string& sequence, const numSeqs_t abundance);

    // return pointer to pool with the specified index (or null pointer if i is too large)
    AmpliconCollection* get(const lenSeqs_t i) const;

    // return number of pools / amplicon collections
    lenSeqs_t numPools() const;

    // return total number of amplicons in all pools
    numSeqs_t numAmplicons() const;

private:
    char* strings_; // overall strings (headers, sequences) array, each string ends with a \0
    char* nextPos_; // position at which the next string would be inserted
    unsigned long long capacity_; // capacity of strings_
    std::vector<AmpliconCollection*> pools_; // pointers to the comprised amplicon collections

};


// Description of a subset of an AmpliconCollection suitable for segment filtering
//
// Forward filtering (beginIndex <= beginMatch < end, reading left to right):
// Amplicons with an index in [beginMatch, end) make up the actual content of the subpool, i.e. these are the amplicons to be filtered.
// The amplicons with an index in [beginIndex, beginMatch) (if any) are used for indexing only,
// in order to store the same information in the inverted indices as if we would process the whole pool at once.
//
// Backward filtering (beginMatch < beginIndex <= end, reading right to left):
// Amplicons with an index in [beginIndex, end) are used for indexing only, while those with an index in [beginMatch, beginIndex)
// have to be filtered. Here, starting at index end-1 and going down to beginIndex (inclusive) we are only indexing.
// Then, starting at beginIndex-1 and going down to beginMatch (inclusive) we are filtering and indexing.
//
// Both getSubpoolBoundaries(...) and getSubpoolBoundariesBackward(...) assume that the amplicons are sorted by increasing sequence length.
struct Subpool {

    numSeqs_t beginIndex; // inclusive
    numSeqs_t beginMatch; // inclusive
    numSeqs_t end; // exclusive

    Subpool() {

        beginIndex = 0;
        beginMatch = 0;
        end = 0;

    }

    Subpool(numSeqs_t bi, numSeqs_t bm, numSeqs_t e) {

        beginIndex = bi;
        beginMatch = bm;
        end = e;

    }

};

std::vector<Subpool> getSubpoolBoundaries(const AmpliconCollection& ac, const numSeqs_t num, const lenSeqs_t threshold);


std::vector<Subpool> getSubpoolBoundariesBackward(const AmpliconCollection& ac, const numSeqs_t num, const lenSeqs_t threshold);



// =====================================================
//                          Misc
// =====================================================

// pair of amplicon 'ids', amplicons are potentially similar (have passed the filter, but not yet verified)
typedef std::pair<numSeqs_t, numSeqs_t> Candidate;

// pair of pointers (first, second) describing the string [first, last) + custom operators
typedef std::pair<const char*, const char*> StringIteratorPair;

// hash function for StringIteratorPair
struct hashStringIteratorPair {
    size_t operator()(const StringIteratorPair& p) const;
};

// comparison function for string equality
struct equalStringIteratorPair {
    bool operator()(const StringIteratorPair& lhs, const StringIteratorPair& rhs) const;
};

// comparison function for lexicographical order
struct lessStringIteratorPair {
    bool operator()(const StringIteratorPair& a, const StringIteratorPair& b) const;
};

/*
* Collection of (inverted) indices for the segment filter.
*
* The inverted indices are arranged in a grid where
* the columns correspond to segments and
* the rows correspond to sequence lengths.
*
* During the execution of the segment filter,
* 'older' rows can be removed once they correspond to
* sequences too short to be able to provide candidates for
* the current (and future) sequences.
*
*/
template<typename T>
class RollingIndices {
public:
    typedef std::vector<T> Row;


    RollingIndices(lenSeqs_t t, lenSeqs_t w, bool f, bool s = true) {

        threshold_ = t;
        width_ = w;
        forward_ = f;
        shrink_ = s;
        minLength_ = 0;
        maxLength_ = 0;

        empty_ = T();
        emptyRow_ = Row(0);

    }


    // return the indices for the specified length
    Row& getIndicesRow(const lenSeqs_t len) {

        auto iter = indices_.find(len);

        return (iter != indices_.end()) ? iter->second : emptyRow_;

    }


    // return the index corresponding to the specified length and segment
    T& getIndex(const lenSeqs_t len, const lenSeqs_t i) {

        if (i >= width_) return empty_;

        auto iter = indices_.find(len);

        return (iter != indices_.end()) ? (iter->second)[i] : empty_;

    }


    // add new row (and remove then outdated rows)
    void roll(const lenSeqs_t len) {

        if (indices_.find(len) == indices_.end()) {

            indices_[len] = Row(width_);
            minLength_ = std::min(minLength_, len);
            maxLength_ = std::max(maxLength_, len);
            if (shrink_) shrink(len);

        }


    }


    // remove outdated rows
    void shrink(const lenSeqs_t cur) {

        if (forward_) {

            for (lenSeqs_t len = minLength_; len < (cur - threshold_); len++) {
                indices_.erase(len);
            }
            for (lenSeqs_t len = cur - threshold_; len <= cur; len++) {

                if (indices_.find(len) != indices_.end()) {

                    minLength_ = len;
                    break;

                }

            }

        } else {

            for (lenSeqs_t len = cur + threshold_ + 1; len <= maxLength_; len++) {
                indices_.erase(len);
            }
            for (lenSeqs_t len = cur + threshold_; len >= cur; len--) {

                if (indices_.find(len) != indices_.end()) {

                    maxLength_ = len;
                    break;

                }

            }

        }

    }

    bool contains(const lenSeqs_t len) {
        return (indices_.find(len) != indices_.end());
    }

    lenSeqs_t minLength() const {
        return minLength_;
    }
    lenSeqs_t maxLength() const {
        return maxLength_;
    }


private:

    lenSeqs_t threshold_; // limits number of rows when applying shrink()
    lenSeqs_t width_; // number of columns / segments per row
    lenSeqs_t minLength_;
    lenSeqs_t maxLength_;

    std::unordered_map<lenSeqs_t, Row> indices_; // indices grid

    T empty_; // empty (dummy) index returned for out-of-bounds queries
    Row emptyRow_; // empty (dummy) row returned for out-of-bounds queries

    bool forward_; // flag indicating whether rolling forwards (increasingly larger lengths are 'inserted') or backwards (shorter lengths are 'inserted')
    bool shrink_; // flag indicating whether roll() automatically shrinks the index

};


}


#endif //GEFAST_BASE_HPP