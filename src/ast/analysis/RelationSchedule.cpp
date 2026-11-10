// SPDX-FileCopyrightText: Copyright 2026 Adobe. All rights reserved. (modifications)
// SPDX-License-Identifier: UPL-1.0 AND Apache-2.0
// Adobe modifications are licensed under Apache-2.0.

/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2021, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RelationSchedule.cpp
 *
 * Implements method of precedence graph to build the precedence graph,
 * compute strongly connected components of the precedence graph, and
 * build the strongly connected component graph.
 *
 ***********************************************************************/

#include "ast/analysis/RelationSchedule.h"
#include "GraphUtils.h"
#include "ast/QualifiedName.h"
#include "ast/Relation.h"
#include "ast/TranslationUnit.h"
#include "ast/analysis/PrecedenceGraph.h"
#include "ast/analysis/SCCGraph.h"
#include "ast/analysis/TopologicallySortedSCCGraph.h"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <set>

namespace souffle::ast::analysis {

void RelationScheduleAnalysisStep::print(std::ostream& os) const {
    os << "computed: ";
    for (const Relation* compRel : computed()) {
        os << compRel->getQualifiedName() << ", ";
    }
    os << "\nexpired: ";
    for (const Relation* compRel : expired()) {
        os << compRel->getQualifiedName() << ", ";
    }
    os << "\n";
    if (recursive()) {
        os << "recursive";
    } else {
        os << "not recursive";
    }
    os << "\n";
}

void RelationScheduleAnalysis::run(const TranslationUnit& translationUnit) {
    topsortSCCGraphAnalysis = &translationUnit.getAnalysis<TopologicallySortedSCCGraphAnalysis>();
    precedenceGraph = &translationUnit.getAnalysis<PrecedenceGraphAnalysis>();
    sccGraph = &translationUnit.getAnalysis<SCCGraphAnalysis>();

    const std::size_t numSCCs = sccGraph->getNumberOfSCCs();
    std::vector<RelationSet> relationExpirySchedule = computeRelationExpirySchedule();

    relationSchedule.clear();
    for (std::size_t i = 0; i < numSCCs; i++) {
        const auto scc = topsortSCCGraphAnalysis->order()[i];
        const RelationSet computedRelations = sccGraph->getInternalRelations(scc);
        relationSchedule.emplace_back(
                computedRelations, relationExpirySchedule[i], sccGraph->isRecursive(scc));
    }

    topsortSCCGraphAnalysis = nullptr;
    precedenceGraph = nullptr;
    sccGraph = nullptr;
}

std::vector<RelationSet> RelationScheduleAnalysis::computeRelationExpirySchedule() {
    std::vector<RelationSet> relationExpirySchedule;
    /* Compute for each step in the reverse topological order
       of evaluating the SCC the set of alive relations. */
    std::size_t numSCCs = topsortSCCGraphAnalysis->order().size();

    /* Resize expired relations sets */
    relationExpirySchedule.resize(numSCCs);

    /* The alive set only grows from step to step, so a single set suffices: the
       relations expiring at a step are the predecessors not yet alive. (Keeping
       one alive set per step, a copy of the previous one, is quadratic.) */
    UnorderedRelationSet alive;
    for (std::size_t orderedSCC = 1; orderedSCC < numSCCs; orderedSCC++) {
        auto scc = topsortSCCGraphAnalysis->order()[numSCCs - orderedSCC];
        RelationSet& expired = relationExpirySchedule[numSCCs - orderedSCC];
        for (const Relation* r : sccGraph->getInternalRelations(scc)) {
            for (const Relation* predecessor : precedenceGraph->graph().predecessors(r)) {
                if (alive.insert(predecessor).second) {
                    expired.insert(predecessor);
                }
            }
        }
    }

    return relationExpirySchedule;
}

void RelationScheduleAnalysis::print(std::ostream& os) const {
    os << "begin schedule\n";
    for (const RelationScheduleAnalysisStep& step : relationSchedule) {
        os << step;
    }
    os << "end schedule\n";
}

}  // namespace souffle::ast::analysis
