/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2021, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RecursiveClauses.cpp
 *
 * Compute the set of recursive clauses.
 *
 * A recursive clause is a clause of a rule R that depends directly or
 * transitively on that rule R.
 *
 ***********************************************************************/

#include "ast/analysis/RecursiveClauses.h"
#include "ast/Atom.h"
#include "ast/Clause.h"
#include "ast/Node.h"
#include "ast/Program.h"
#include "ast/Relation.h"
#include "ast/TranslationUnit.h"
#include "ast/utility/Utils.h"
#include "ast/utility/Visitor.h"
#include "souffle/utility/StreamUtil.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace souffle::ast::analysis {

void RecursiveClausesAnalysis::run(const TranslationUnit& translationUnit) {
    Program& program = translationUnit.getProgram();

    // Mapping from a relation to the set of relations that it depends on directly.
    //
    // It is the adjacency list of the relations dependency graph.
    std::map<uint32_t, std::set<uint32_t>> relationUseRelation;

    // Mapping from a clause to the set of relations that it depends on directly.
    std::map<const Clause*, std::set<uint32_t>> clauseUseRelation;

    // Mapping from a clause to its relations.
    std::map<const Clause*, uint32_t> clauseRelation;

    std::vector<uint32_t> relations;

    // gather dependencies
    for (const auto& qninfo : program.getRelationInfo()) {
        const uint32_t head = qninfo.first.getIndex();
        relations.emplace_back(head);
        for (const auto& clause : qninfo.second.clauses) {
            clauseRelation.emplace(clause.get(), head);
            for (const auto& atom : getBodyLiterals<Atom>(*clause)) {
                const uint32_t rhs = atom->getQualifiedName().getIndex();
                relationUseRelation[head].emplace(rhs);
                clauseUseRelation[clause.get()].emplace(rhs);
            }
        }
    }

    // A clause is recursive iff a relation it uses directly reaches (depends
    // transitively on) its head, i.e. iff the two share a strongly connected
    // component of the dependency graph. Components are found with Tarjan's
    // algorithm (iterative), linear in the graph; a transitive closure per
    // relation is quadratic or worse.
    std::map<uint32_t, uint32_t> component;
    {
        std::map<uint32_t, uint32_t> index;
        std::map<uint32_t, uint32_t> lowlink;
        std::set<uint32_t> onStack;
        std::vector<uint32_t> stack;
        uint32_t counter = 0;
        // (node, position of the next successor to visit)
        std::vector<std::pair<uint32_t, std::set<uint32_t>::const_iterator>> work;
        static const std::set<uint32_t> none;
        const auto successors = [&](uint32_t node) -> const std::set<uint32_t>& {
            auto it = relationUseRelation.find(node);
            return it == relationUseRelation.end() ? none : it->second;
        };
        const auto enter = [&](uint32_t node) {
            index[node] = lowlink[node] = counter++;
            stack.push_back(node);
            onStack.insert(node);
            work.emplace_back(node, successors(node).begin());
        };
        for (const uint32_t root : relations) {
            if (index.count(root) != 0) {
                continue;
            }
            enter(root);
            while (!work.empty()) {
                auto& [node, next] = work.back();
                if (next != successors(node).end()) {
                    const uint32_t succ = *next++;
                    if (index.count(succ) == 0) {
                        enter(succ);
                    } else if (onStack.count(succ) != 0) {
                        lowlink[node] = std::min(lowlink[node], index[succ]);
                    }
                    continue;
                }
                const uint32_t done = node;
                work.pop_back();
                if (!work.empty()) {
                    const uint32_t parent = work.back().first;
                    lowlink[parent] = std::min(lowlink[parent], lowlink[done]);
                }
                if (lowlink[done] == index[done]) {
                    uint32_t member = 0;
                    do {
                        member = stack.back();
                        stack.pop_back();
                        onStack.erase(member);
                        component[member] = index[done];
                    } while (member != done);
                }
            }
        }
    }

    for (const auto& [clause, rel] : clauseRelation) {
        for (const uint32_t used : clauseUseRelation[clause]) {
            auto it = component.find(used);
            if (it != component.end() && it->second == component.at(rel)) {
                // clause is recursive
                recursiveClauses.emplace(clause);
                break;
            }
        }
    }
}

void RecursiveClausesAnalysis::print(std::ostream& os) const {
    os << recursiveClauses << std::endl;
}

}  // namespace souffle::ast::analysis
