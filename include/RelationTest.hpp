#ifndef GEFAST_RELATIONTEST_HPP
#define GEFAST_RELATIONTEST_HPP

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <unordered_map>
#include <StaticHybridTree.hpp>
#include <StaticBasicRectangularTree.hpp>
#include <StaticUnevenRectangularTree.hpp>

#include "Base.hpp"
#include "StaticBasicTree.hpp"
#include "StaticRowTree.hpp"
#include "StaticHybridRowTree.hpp"
#include "StaticMiniRowTree.hpp"

namespace GeFaST {

struct RelationPrecursor {//TODO mapping: map or unordered_map?

    std::vector<std::pair<numSeqs_t, numSeqs_t>> pairs;
    std::map<StringIteratorPair, numSeqs_t, lessStringIteratorPair> mapping;

    void add(const StringIteratorPair seg, const numSeqs_t id);

    void clear();

};


// maps arbitary ascending sequence of n unique (positive) integers onto [1:n]
class RankedLabels {

public:
    RankedLabels();

    RankedLabels(const numSeqs_t capacity);

    RankedLabels& operator=(const RankedLabels& other);

    ~RankedLabels();

    numSeqs_t add(const numSeqs_t lab);

    numSeqs_t unrank(const numSeqs_t r);

    bool contains(const numSeqs_t lab);

    bool containsRank(const numSeqs_t r);

    void remove(const numSeqs_t lab);

    void swap(RankedLabels& other);

    numSeqs_t size();

private:
    long long* labels_;
    numSeqs_t size_;
    numSeqs_t capa_;

};


template<typename S, typename T>
class SharingRollingIndices {

public:
    struct Row {

        S shared;
        std::vector<T> indices;

        Row() {
            // nothing to do
        }

        Row(lenSeqs_t w, numSeqs_t sharedCapacity) : shared(S(sharedCapacity)) {

//            shared = S(1000000);
            indices = std::vector<T>(w);
        }

        Row& operator=(const Row& other) {

            // check for self-assignment
            if (&other == this) {
                return *this;
            }

            shared = other.shared;
            indices = other.indices;

            return *this;

        }

    };

    SharingRollingIndices(lenSeqs_t t, lenSeqs_t w, bool f, bool s = true) {

        threshold_ = t;
        width_ = w;
        forward_ = f;
        shrink_ = s;

        empty_ = T();
        emptyRow_ = Row(0, 0);

    }


    // return the indices for the specified length
    Row& getIndicesRow(const lenSeqs_t len) {

        auto iter = rows_.find(len);

        return (iter != rows_.end()) ? iter->second : emptyRow_;

    }


    // return the index corresponding to the specified length and segment
    T& getIndex(const lenSeqs_t len, const lenSeqs_t i) {

        if (i >= width_) return empty_;

        auto iter = rows_.find(len);

        return (iter != rows_.end()) ? (iter->second).indices[i] : empty_;

    }


    // add new row (and remove then outdated rows)
    void roll(const lenSeqs_t len, const numSeqs_t sharedCapacity) {

        if (rows_.find(len) == rows_.end()) {

            rows_[len] = Row(width_, sharedCapacity);
            if (shrink_) shrink(len);

        }


    }


    // remove outdated rows
    void shrink(const lenSeqs_t cur) {

        if (forward_) {

            auto bound = rows_.lower_bound(cur - threshold_);
            rows_.erase(rows_.begin(), bound);

        } else {

            auto bound = rows_.upper_bound(cur + threshold_);
            rows_.erase(bound, rows_.end());

        }

    }

    bool contains(const lenSeqs_t len) {
        return (rows_.find(len) != rows_.end());
    }

    lenSeqs_t minLength() const {
        return rows_.begin()->first;
    }
    lenSeqs_t maxLength() const {
        return rows_.rbegin()->first;
    }


private:

    lenSeqs_t threshold_; // limits number of rows when applying shrink()
    lenSeqs_t width_; // number of columns / segments per row

    std::map<lenSeqs_t, Row> rows_; // indices grid

    T empty_; // empty (dummy) index returned for out-of-bounds queries
    Row emptyRow_; // empty (dummy) row returned for out-of-bounds queries

    bool forward_; // flag indicating whether rolling forwards (increasingly larger lengths are 'inserted') or backwards (shorter lengths are 'inserted')
    bool shrink_; // flag indicating whether roll() automatically shrinks the index

};


template<typename O, typename L, typename H = std::hash<O>, typename P = std::equal_to<O>>
class LazySimpleBinaryRelation {

public:
    LazySimpleBinaryRelation() {
        // nothing to do
    }

    ~LazySimpleBinaryRelation() {
        // nothing to do
    }

    bool areRelated(const O &obj, const L &lab) {

        auto keyIter = binRel_.find(obj);
        bool val = (keyIter != binRel_.end()) && containsLabel(lab);

        if (val) {

            auto &labels = keyIter->second;
            val = (std::find(labels.begin(), labels.end(), lab) != labels.end());

        }

        return val;

    }

    bool containsLabel(const L &lab) {
        return labels_.find(lab) != labels_.end();
    }

    bool containsObject(const O &obj) {
        return binRel_.find(obj) != binRel_.end();
    }

    std::vector<L> getLabelsOf(const O &obj) {

        std::vector<L> labels;

        if (containsObject(obj)) {

            auto &tmp = binRel_[obj];
            labels.reserve(tmp.size());

            for (auto &l : tmp) {
                if (labels_.find(l) != labels_.end()) {
                    labels.push_back(l);
                }
            }

        }

        return labels;

    }

    void add(const O &obj, const L &lab) {

        labels_.insert(lab);

        auto &row = binRel_[obj];
        if (std::find(row.begin(), row.end(), lab) == row.end()) {
            row.push_back(lab);
        }

    }

    void removeLabel(const L &lab) {
        labels_.erase(lab);
    }


private:
    std::unordered_map<O, std::vector<L>, H, P> binRel_;
    std::set<L> labels_;

};

class K2TreeBinaryRelation {

    typedef StringIteratorPair O;
    typedef numSeqs_t L;

public:
    K2TreeBinaryRelation() {
        // nothing to do
    }

    K2TreeBinaryRelation(RelationPrecursor& ir, RankedLabels& labels) {

//        binRel_ = BasicK2Tree<bool>(ir.pairs, 2);
//        binRel_ = HybridK2Tree<bool>(ir.pairs, 7, 2, 3);
//        binRel_ = KrKcTree<bool>(ir.pairs, 3, 7);
        binRel_ = UnevenKrKcTree<bool>(ir.pairs, 3, 10);
        segIdMap_.swap(ir.mapping);
        labels_ = &labels;

    }

    K2TreeBinaryRelation& operator=(const K2TreeBinaryRelation& other) {

        // check for self-assignment
        if (&other == this) {
            return *this;
        }

        binRel_ = other.binRel_;
        segIdMap_ = other.segIdMap_;
        labels_ = other.labels_;

        return *this;

    }

    ~K2TreeBinaryRelation() {
        // nothing to do
    }

    bool areRelated(const O& obj, const L& lab) {
        return containsLabel(lab) && containsObject(obj) && binRel_.areRelated(segIdMap_[obj], lab);
    }

    bool containsLabel(const L& lab) {
        return (labels_ != 0) && labels_->contains(lab);
    }

    bool containsObject(const O& obj) {
        return segIdMap_.find(obj) != segIdMap_.end();
    }

    std::vector<L> getLabelsOf(const O& obj) {

        std::vector<L> res;
        if (containsObject(obj) && labels_ != 0) {

            auto tmp = binRel_.getSuccessors(segIdMap_[obj]);
            res.reserve(tmp.size());

            for (auto& l : tmp) {
                if (labels_->containsRank(l)) {
                    res.push_back(labels_->unrank(l));
                }
            }

        }

        return res;

    }

    unsigned long countPairs() {
        return binRel_.countLinks();
    }

    unsigned long getNumRows() {
        return binRel_.getNumRows();
    }

    unsigned long getNumCols() {
        return binRel_.getNumCols();
    }

    void removeLabel(const L& lab) {
        if (labels_ != 0) {
            labels_->remove(lab);
        }
    }


private:
//    BasicK2Tree<bool> binRel_;
//    HybridK2Tree<bool> binRel_;
//    KrKcTree<bool> binRel_;
    UnevenKrKcTree<bool> binRel_;
    std::map<StringIteratorPair, numSeqs_t, lessStringIteratorPair> segIdMap_;
    RankedLabels* labels_;

};

class RowTreeBinaryRelation {

    typedef StringIteratorPair O;
    typedef numSeqs_t L;

public:
    RowTreeBinaryRelation() {
        // nothing to do
    }

    RowTreeBinaryRelation(RelationPrecursor& ir, RankedLabels& labels) {

        // TODO optimise ( = no full copy), e.g. sorting + equal_range  + sub-copy OR ctor from iterators
#if 0
        std::vector<RelationList> rows(ir.mapping.size());
        for (auto& p : ir.pairs) {
            rows[p.first].push_back(p.second);
        }
        std::vector<std::pair<numSeqs_t, numSeqs_t>>().swap(ir.pairs);

        for (auto i = 0; i < rows.size(); i++) {

            binRel_.emplace_back(rows[i], 2);
//            binRel_.emplace_back(rows[i], 3, 1, 2);
            RelationList().swap(rows[i]);

        }
#else
        std::sort(ir.pairs.begin(), ir.pairs.end());

        auto begin = ir.pairs.begin();
        for (numSeqs_t i = 0; i < ir.mapping.size(); i++) {

            auto end = std::upper_bound(begin, ir.pairs.end(), i,
                                        [](numSeqs_t val, const std::pair<numSeqs_t, numSeqs_t>& pair) {
                                            return val < pair.first;
                                        });
            binRel_.emplace_back(begin, end, 2);
//            binRel_.emplace_back(begin, end, 3, 1, 2);
            begin = end;

        }
#endif

        segIdMap_.swap(ir.mapping);
        labels_ = &labels;

    }

    RowTreeBinaryRelation& operator=(const RowTreeBinaryRelation& other) {

        // check for self-assignment
        if (&other == this) {
            return *this;
        }

        binRel_ = other.binRel_;
        segIdMap_ = other.segIdMap_;
        labels_ = other.labels_;

        return *this;

    }

    ~RowTreeBinaryRelation() {
        // nothing to do
    }

    bool areRelated(const O& obj, const L& lab) {
        return containsLabel(lab) && containsObject(obj) && binRel_[segIdMap_[obj]].isNotNull(lab);
    }

    bool containsLabel(const L& lab) {
        return (labels_ != 0) && labels_->contains(lab);
    }

    bool containsObject(const O& obj) {
        return segIdMap_.find(obj) != segIdMap_.end();
    }

    std::vector<L> getLabelsOf(const O& obj) {

        std::vector<L> res;
        if (containsObject(obj) && labels_ != 0) {

            auto tmp = binRel_[segIdMap_[obj]].getAllPositions();
            res.reserve(tmp.size());

            for (auto& l : tmp) {
                if (labels_->containsRank(l)) {
                    res.push_back(labels_->unrank(l));
                }
            }

        }

        return res;

    }

    unsigned long countPairs() {

        unsigned long sum = 0;
        for (unsigned long i = 0; i < binRel_.size(); i++) {
            sum += binRel_[i].countElements();
        }

        return sum;

    }

    unsigned long getNumRows() {
        return binRel_.size();
    }

    unsigned long getNumCols() {

        unsigned long max = 0;
        for (unsigned long i = 0; i < binRel_.size(); i++) {
            max = std::max(max, binRel_[i].getLength());
        }

        return max;

    }

    void removeLabel(const L& lab) {
        if (labels_ != 0) {
            labels_->remove(lab);
        }
    }


private:
    std::vector<BasicRowTree<bool>> binRel_;
//    std::vector<HybridRowTree<bool>> binRel_;
    std::map<StringIteratorPair, numSeqs_t, lessStringIteratorPair> segIdMap_;
    RankedLabels* labels_;

};

class MiniRowTreeBinaryRelation {

    typedef StringIteratorPair O;
    typedef numSeqs_t L;

public:
    MiniRowTreeBinaryRelation() {
        // nothing to do
    }

    MiniRowTreeBinaryRelation(RelationPrecursor& ir, RankedLabels& labels) {

        // TODO optimise ( = no full copy), e.g. sorting + equal_range  + sub-copy OR ctor from iterators
#if 0
        std::vector<RelationList> rows(ir.mapping.size());
        for (auto& p : ir.pairs) {
            rows[p.first].push_back(p.second);
        }
        std::vector<std::pair<numSeqs_t, numSeqs_t>>().swap(ir.pairs);

        for (auto i = 0; i < rows.size(); i++) {

            if (rows[i].size() > 5) {
                binRel_.push_back(new BasicRowTree<bool>(rows[i], 10));
//                binRel_.push_back(new HybridRowTree<bool>(rows[i], 7, 2, 3));
            } else {
                binRel_.push_back(new MiniRowTree<bool>(rows[i]));
            }
            RelationList().swap(rows[i]);

        }
#else
        std::sort(ir.pairs.begin(), ir.pairs.end());

        auto begin = ir.pairs.begin();
        for (numSeqs_t i = 0; i < ir.mapping.size(); i++) {

            auto end = std::upper_bound(begin, ir.pairs.end(), i,
                                        [](numSeqs_t val, const std::pair<numSeqs_t, numSeqs_t>& pair) {
                                            return val < pair.first;
                                        });

            if (end - begin > 5) {
//                binRel_.push_back(new BasicRowTree<bool>(begin, end, 5));
                binRel_.push_back(new HybridRowTree<bool>(begin, end, 5, 2, 3));
            } else {
                binRel_.push_back(new MiniRowTree<bool>(begin, end));
            }

            begin = end;

        }
#endif
        segIdMap_.swap(ir.mapping);
        labels_ = &labels;

    }

    MiniRowTreeBinaryRelation& operator=(const MiniRowTreeBinaryRelation& other) {

        // check for self-assignment
        if (&other == this) {
            return *this;
        }

        for (auto i = 0; i < binRel_.size(); i++) {
            delete binRel_[i];
        }

        binRel_.clear();
        for (auto i = 0; i < other.binRel_.size(); i++) {
            binRel_.push_back(other.binRel_[i]->clone());
        }

        segIdMap_ = other.segIdMap_;
        labels_ = other.labels_;

        return *this;

    }

    ~MiniRowTreeBinaryRelation() {

        for (auto i = 0; i < binRel_.size(); i++) {
            delete binRel_[i];
        }

    }

    bool areRelated(const O& obj, const L& lab) {
        return containsLabel(lab) && containsObject(obj) && binRel_[segIdMap_[obj]]->isNotNull(lab);
    }

    bool containsLabel(const L& lab) {
        return (labels_ != 0) && labels_->contains(lab);
    }

    bool containsObject(const O& obj) {
        return segIdMap_.find(obj) != segIdMap_.end();
    }

    std::vector<L> getLabelsOf(const O& obj) {

        std::vector<L> res;
        if (containsObject(obj) && labels_ != 0) {

            auto tmp = binRel_[segIdMap_[obj]]->getAllPositions();
            res.reserve(tmp.size());

            for (auto &l : tmp) {
                if (labels_->containsRank(l)) {
                    res.push_back(labels_->unrank(l));
                }
            }

        }

        return res;

    }

    unsigned long countPairs() {

        unsigned long sum = 0;
        for (unsigned long i = 0; i < binRel_.size(); i++) {
            sum += binRel_[i]->countElements();
        }

        return sum;

    }

    unsigned long getNumRows() {
        return binRel_.size();
    }

    unsigned long getNumCols() {

        unsigned long max = 0;
        for (unsigned long i = 0; i < binRel_.size(); i++) {
            max = std::max(max, binRel_[i]->getLength());
        }

        return max;

    }

    void removeLabel(const L& lab) {
        if (labels_ != 0) {
            labels_->remove(lab);
        }
    }


private:
    std::vector<RowTree<bool>*> binRel_;
    std::map<StringIteratorPair, numSeqs_t, lessStringIteratorPair> segIdMap_;
    RankedLabels* labels_;

};

typedef K2TreeBinaryRelation SuccinctInvertedIndex; // use with full index
//typedef RowTreeBinaryRelation SuccinctInvertedIndex; // use with full index
//typedef MiniRowTreeBinaryRelation SuccinctInvertedIndex; // use with full index


}

#endif //GEFAST_RELATIONTEST_HPP