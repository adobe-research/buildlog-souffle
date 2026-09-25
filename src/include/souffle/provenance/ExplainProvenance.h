/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2017, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file ExplainProvenance.h
 *
 * Abstract class for implementing an instance of the explain interface for provenance
 *
 ***********************************************************************/

#pragma once

#include "souffle/RamTypes.h"
#include "souffle/SouffleInterface.h"
#include "souffle/SymbolTable.h"
#include "souffle/utility/MiscUtil.h"
#include "souffle/utility/StreamUtil.h"
#include "souffle/utility/StringUtil.h"
#include "souffle/utility/json11.h"
#include "souffle/utility/tinyformat.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace souffle {
class TreeNode;

/** Equivalence class for variables in query command */
class Equivalence {
public:
    /** Destructor */
    ~Equivalence() = default;

    /**
     * Constructor for Equvialence class
     * @param t, type of the variable
     * @param s, symbol of the variable
     * @param idx, first occurence of the variable
     * */
    Equivalence(char t, std::string s, std::pair<std::size_t, std::size_t> idx)
            : type(t), symbol(std::move(s)) {
        indices.push_back(idx);
    }

    /** Copy constructor */
    Equivalence(const Equivalence& o) = default;

    /** Copy assignment operator */
    Equivalence& operator=(const Equivalence& o) = default;

    /** Add index at the end of indices vector */
    void push_back(std::pair<std::size_t, std::size_t> idx) {
        indices.push_back(idx);
    }

    /** Verify if elements at the indices are equivalent in the given product */
    bool verify(const std::vector<tuple>& product) const {
        for (std::size_t i = 1; i < indices.size(); ++i) {
            if (product[indices[i].first][indices[i].second] !=
                    product[indices[i - 1].first][indices[i - 1].second]) {
                return false;
            }
        }
        return true;
    }

    /** Extract index of the first occurrence of the varible */
    const std::pair<std::size_t, std::size_t>& getFirstIdx() const {
        return indices[0];
    }

    /** Get indices of equivalent variables */
    const std::vector<std::pair<std::size_t, std::size_t>>& getIndices() const {
        return indices;
    }

    /** Get type of the variable of the equivalence class,
     * 'i' for RamSigned, 's' for symbol
     * 'u' for RamUnsigned, 'f' for RamFloat
     */
    char getType() const {
        return type;
    }

    /** Get the symbol of variable */
    const std::string& getSymbol() const {
        return symbol;
    }

private:
    char type;
    std::string symbol;
    std::vector<std::pair<std::size_t, std::size_t>> indices;
};

/** Constant constraints for values in query command */
class ConstConstraint {
public:
    /** Constructor */
    ConstConstraint() = default;

    /** Destructor */
    ~ConstConstraint() = default;

    /** Add constant constraint at the end of constConstrs vector */
    void push_back(std::pair<std::pair<std::size_t, std::size_t>, RamDomain> constr) {
        constConstrs.push_back(constr);
    }

    /** Verify if the query product satisfies constant constraint */
    bool verify(const std::vector<tuple>& product) const {
        return std::all_of(constConstrs.begin(), constConstrs.end(), [&product](auto constr) {
            return product[constr.first.first][constr.first.second] == constr.second;
        });
    }

    /** Get the constant constraint vector */
    std::vector<std::pair<std::pair<std::size_t, std::size_t>, RamDomain>>& getConstraints() {
        return constConstrs;
    }

    const std::vector<std::pair<std::pair<std::size_t, std::size_t>, RamDomain>>& getConstraints() const {
        return constConstrs;
    }

private:
    std::vector<std::pair<std::pair<std::size_t, std::size_t>, RamDomain>> constConstrs;
};

/** utility function to split a string */
inline std::vector<std::string> split(const std::string& s, char delim, int times = -1) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string item;

    while ((times > 0 || times <= -1) && std::getline(ss, item, delim)) {
        v.push_back(item);
        times--;
    }

    if (ss.peek() != EOF) {
        std::string remainder;
        std::getline(ss, remainder);
        v.push_back(remainder);
    }

    return v;
}

class ExplainProvenance {
public:
    ExplainProvenance(SouffleProgram& prog) : prog(prog), symTable(prog.getSymbolTable()) {}
    virtual ~ExplainProvenance() = default;

    virtual void setup() = 0;

    virtual Own<TreeNode> explain(
            std::string relName, std::vector<std::string> tuple, std::size_t depthLimit) = 0;

    virtual Own<TreeNode> explainSubproof(std::string relName, RamDomain label, std::size_t depthLimit) = 0;

    virtual std::vector<std::string> explainNegationGetVariables(
            std::string relName, std::vector<std::string> args, std::size_t ruleNum) = 0;

    virtual Own<TreeNode> explainNegation(std::string relName, std::size_t ruleNum,
            const std::vector<std::string>& tuple, std::map<std::string, std::string>& bodyVariables) = 0;

    virtual std::string getRule(std::string relName, std::size_t ruleNum) = 0;

    virtual std::vector<std::string> getRules(const std::string& relName) = 0;

    virtual std::string measureRelation(std::string relName) = 0;

    virtual void printRulesJSON(std::ostream& os) = 0;

    /**
     * Process query with given arguments
     * @param rels, vector of relation, argument pairs
     * */
    virtual void queryProcess(const std::vector<std::pair<std::string, std::vector<std::string>>>& rels) = 0;

protected:
    SouffleProgram& prog;
    SymbolTable& symTable;

    /** Record and ADT type information, in the same format as the "types" IO parameter */
    json11::Json types;

    std::vector<RamDomain> argsToNums(
            const std::string& relName, const std::vector<std::string>& args) const {
        std::vector<RamDomain> nums;

        auto rel = prog.getRelation(relName);
        if (rel == nullptr) {
            return nums;
        }

        for (std::size_t i = 0; i < args.size(); i++) {
            nums.push_back(valueRead(rel->getAttrType(i), args[i]));
        }

        return nums;
    }

    /**
     * Decode arguments from their ram representation and return as strings.
     **/
    std::vector<std::string> decodeArguments(
            const std::string& relName, const std::vector<RamDomain>& nums) const {
        std::vector<std::string> args;

        auto rel = prog.getRelation(relName);
        if (rel == nullptr) {
            return args;
        }

        for (std::size_t i = 0; i < nums.size(); i++) {
            args.push_back(valueShow(rel->getAttrType(i), nums[i]));
        }

        return args;
    }

    /**
     * Load the record and ADT type information that the provenance translation
     * stores in the provenance.@types relation, if present.
     **/
    void setupTypes() {
        auto rel = prog.getRelation("provenance.@types");
        if (rel == nullptr) {
            return;
        }
        for (auto& tuple : *rel) {
            std::string json;
            tuple >> json;
            std::string err;
            types = json11::Json::parse(json, err);
        }
    }

    /**
     * Show a value of the given type qualifier (e.g. "i:number", "r:R", "+:A").
     **/
    std::string valueShow(const std::string& type, const RamDomain value) const {
        switch (type[0]) {
            case 'i': return tfm::format("%d", ramBitCast<RamSigned>(value));
            case 'u': return tfm::format("%d", ramBitCast<RamUnsigned>(value));
            case 'f': return tfm::format("%f", ramBitCast<RamFloat>(value));
            case 's': return tfm::format("\"%s\"", symTable.decode(value));
            case 'r': return recordShow(type, value);
            case '+': return adtShow(type, value);
            default: fatal("unhandled type attr code");
        }
    }

    std::string recordShow(const std::string& type, const RamDomain value) const {
        auto&& recordInfo = types["records"][type];
        if (recordInfo.is_null()) {
            return tfm::format("record #%d", value);
        }
        if (value == 0) {
            return "nil";
        }

        auto&& fieldTypes = recordInfo["types"].array_items();
        const RamDomain* fields = prog.getRecordTable().unpack(value, fieldTypes.size());

        std::vector<std::string> shown;
        for (std::size_t i = 0; i < fieldTypes.size(); ++i) {
            shown.push_back(valueShow(fieldTypes[i].string_value(), fields[i]));
        }
        return tfm::format("[%s]", join(shown, ", "));
    }

    std::string adtShow(const std::string& type, const RamDomain value) const {
        auto&& adtInfo = types["ADTs"][type];
        if (adtInfo.is_null()) {
            return tfm::format("adt #%d", value);
        }

        // An ADT value is encoded in one of three ways (see WriteStream::outputADT):
        //   branchID                    when the ADT is an enumeration,
        //   [branchID, arg]             when the branch has a single argument,
        //   [branchID, [branch_args]]   otherwise.
        RamDomain branchId = value;
        const RamDomain* branchArgs = nullptr;
        json11::Json branchInfo;
        json11::Json::array branchTypes;

        if (adtInfo["enum"].bool_value()) {
            branchInfo = adtInfo["branches"][branchId];
            branchTypes = branchInfo["types"].array_items();
        } else {
            const RamDomain* tuple = prog.getRecordTable().unpack(value, 2);
            branchId = tuple[0];
            branchInfo = adtInfo["branches"][branchId];
            branchTypes = branchInfo["types"].array_items();
            if (branchTypes.size() > 1) {
                branchArgs = prog.getRecordTable().unpack(tuple[1], branchTypes.size());
            } else {
                branchArgs = &tuple[1];
            }
        }

        std::string result = "$" + branchInfo["name"].string_value();
        if (branchTypes.empty()) {
            return result;
        }

        std::vector<std::string> shown;
        for (std::size_t i = 0; i < branchTypes.size(); ++i) {
            shown.push_back(valueShow(branchTypes[i].string_value(), branchArgs[i]));
        }
        return tfm::format("%s(%s)", result, join(shown, ", "));
    }

    /**
     * Read a value of the given type qualifier from its textual representation,
     * as printed by valueShow.
     **/
    RamDomain valueRead(const std::string& type, const std::string& value) const {
        switch (type[0]) {
            case 'i':
                if (!canBeParsedAsRamSigned(value)) {
                    fatal("expected a number, got `%s`", value);
                }
                return ramBitCast(RamSignedFromString(value));
            case 'u':
                if (!canBeParsedAsRamUnsigned(value)) {
                    fatal("expected an unsigned number, got `%s`", value);
                }
                return ramBitCast(RamUnsignedFromString(value));
            case 'f':
                if (!canBeParsedAsRamFloat(value)) {
                    fatal("expected a float, got `%s`", value);
                }
                return ramBitCast(RamFloatFromString(value));
            case 's':
                if (value.size() < 2 || value[0] != '"' || value.back() != '"') {
                    fatal("expected a string, got `%s`", value);
                }
                return symTable.encode(value.substr(1, value.size() - 2));
            case 'r': return recordRead(type, value);
            case '+': return adtRead(type, value);
            default: fatal("unhandled type attr code");
        }
    }

    // Note: reading a record or ADT value may add a new entry to the record table, in which
    // case no tuple contains it.
    RamDomain recordRead(const std::string& type, const std::string& value) const {
        auto&& recordInfo = types["records"][type];
        if (recordInfo.is_null()) {
            fatal("no type information for record type `%s`", type);
        }
        if (value == "nil") {
            return 0;
        }

        auto&& fieldTypes = recordInfo["types"].array_items();
        std::vector<std::string> fields;
        if (value.size() < 2 || value[0] != '[' || value.back() != ']' ||
                !splitValues(value.substr(1, value.size() - 2), fields) ||
                fields.size() != fieldTypes.size()) {
            fatal("expected a record of type `%s`, got `%s`", type, value);
        }

        std::vector<RamDomain> nums;
        for (std::size_t i = 0; i < fieldTypes.size(); ++i) {
            nums.push_back(valueRead(fieldTypes[i].string_value(), fields[i]));
        }
        return prog.getRecordTable().pack(nums.data(), nums.size());
    }

    RamDomain adtRead(const std::string& type, const std::string& value) const {
        auto&& adtInfo = types["ADTs"][type];
        if (adtInfo.is_null()) {
            fatal("no type information for ADT type `%s`", type);
        }

        // $Branch or $Branch(arg, ...)
        const auto open = value.find('(');
        std::vector<std::string> args;
        if (value.size() < 2 || value[0] != '$' ||
                (open != std::string::npos &&
                        (value.back() != ')' ||
                                !splitValues(value.substr(open + 1, value.size() - open - 2), args)))) {
            fatal("expected an ADT value of type `%s`, got `%s`", type, value);
        }
        const std::string name = value.substr(1, open == std::string::npos ? open : open - 1);

        auto&& branches = adtInfo["branches"].array_items();
        auto branch = std::find_if(branches.begin(), branches.end(),
                [&](const json11::Json& b) { return b["name"].string_value() == name; });
        if (branch == branches.end()) {
            fatal("`%s` is not a branch of ADT type `%s`", name, type);
        }
        const auto branchId = static_cast<RamDomain>(branch - branches.begin());

        auto&& branchTypes = (*branch)["types"].array_items();
        if (args.size() != branchTypes.size()) {
            fatal("branch `%s` of ADT type `%s` takes %d argument(s), got `%s`", name, type,
                    branchTypes.size(), value);
        }

        // See adtShow for the encoding.
        if (adtInfo["enum"].bool_value()) {
            return branchId;
        }
        std::vector<RamDomain> nums;
        for (std::size_t i = 0; i < branchTypes.size(); ++i) {
            nums.push_back(valueRead(branchTypes[i].string_value(), args[i]));
        }
        RamDomain payload = nums.size() == 1 ? nums[0] : prog.getRecordTable().pack(nums.data(), nums.size());
        return prog.getRecordTable().pack({branchId, payload});
    }

public:
    /**
     * Split a comma-separated list of values at the top level, i.e. not inside strings,
     * records, or ADT arguments, trimming surrounding blanks.
     * Returns false if quotes or brackets are unbalanced, or if a value is empty.
     **/
    static bool splitValues(const std::string& str, std::vector<std::string>& values) {
        auto trim = [](const std::string& s) {
            const auto begin = s.find_first_not_of(" \t");
            return begin == std::string::npos ? std::string()
                                              : s.substr(begin, s.find_last_not_of(" \t") - begin + 1);
        };

        values.clear();
        std::string current;
        int depth = 0;
        bool inString = false;
        for (char c : str) {
            if (inString) {
                inString = c != '"';
            } else if (c == '"') {
                inString = true;
            } else if (c == '[' || c == '(') {
                depth++;
            } else if ((c == ']' || c == ')') && --depth < 0) {
                return false;
            } else if (c == ',' && depth == 0) {
                values.push_back(trim(current));
                current.clear();
                continue;
            }
            current += c;
        }
        if (inString || depth != 0) {
            return false;
        }
        if (!values.empty() || !trim(current).empty()) {
            values.push_back(trim(current));
        }
        return std::none_of(values.begin(), values.end(), [](const std::string& v) { return v.empty(); });
    }
};

}  // end of namespace souffle
