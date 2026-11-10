/*
 * SPDX-FileCopyrightText: Copyright 2026 Adobe. All rights reserved. (modifications)
 * SPDX-License-Identifier: UPL-1.0 AND Apache-2.0
 * Adobe modifications are licensed under Apache-2.0.
 */

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
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace souffle {
class TreeNode;

/** Error raised when a value given to explain or query does not match its type. */
class ExplainValueError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/** A step from a record or ADT value to one of its fields or branch arguments */
struct ValueStep {
    /** 'r' for a record field, '+' for an ADT branch argument */
    char kind;
    /** Branch the ADT value must have; unused for records */
    RamDomain branchId;
    /** Number of fields of the record, or arguments of the branch */
    std::size_t arity;
    /** Index of the field or argument */
    std::size_t index;
};

/** Position in a query: an atom with variables, one of its attributes, and steps into its value */
struct QueryPosition {
    std::size_t atom;
    std::size_t attr;
    std::vector<ValueStep> steps;
};

/** Resolves a position in the current query product, or nothing if the value does not have the shape
 * the position steps through (e.g. another ADT branch, or nil) */
using QueryValueAt = std::function<std::optional<RamDomain>(const QueryPosition&)>;

/** Equivalence class for variables in query command */
class Equivalence {
public:
    /** Destructor */
    ~Equivalence() = default;

    /**
     * Constructor for Equvialence class
     * @param t, type qualifier of the variable (e.g. "i:number")
     * @param s, symbol of the variable
     * @param pos, first occurence of the variable
     * */
    Equivalence(std::string t, std::string s, QueryPosition pos) : type(std::move(t)), symbol(std::move(s)) {
        positions.push_back(std::move(pos));
    }

    /** Copy constructor */
    Equivalence(const Equivalence& o) = default;

    /** Copy assignment operator */
    Equivalence& operator=(const Equivalence& o) = default;

    /** Add position at the end of positions vector */
    void push_back(QueryPosition pos) {
        positions.push_back(std::move(pos));
    }

    /** Verify if values at the positions exist and are equivalent in the current product */
    bool verify(const QueryValueAt& valueAt) const {
        const auto first = valueAt(positions[0]);
        return first && std::all_of(positions.begin() + 1, positions.end(),
                                [&](const QueryPosition& pos) { return valueAt(pos) == first; });
    }

    /** Extract position of the first occurrence of the varible */
    const QueryPosition& getFirstPosition() const {
        return positions[0];
    }

    /** Get positions of equivalent variables */
    const std::vector<QueryPosition>& getPositions() const {
        return positions;
    }

    /** Get the type qualifier of the variable of the equivalence class, whose first character is
     * 'i' for RamSigned, 's' for symbol, 'u' for RamUnsigned, 'f' for RamFloat,
     * 'r' for records and '+' for ADTs
     */
    const std::string& getType() const {
        return type;
    }

    /** Get the symbol of variable */
    const std::string& getSymbol() const {
        return symbol;
    }

private:
    std::string type;
    std::string symbol;
    std::vector<QueryPosition> positions;
};

/** Constant constraints for values in query command */
class ConstConstraint {
public:
    /** Constructor */
    ConstConstraint() = default;

    /** Destructor */
    ~ConstConstraint() = default;

    /** Add constant constraint at the end of constConstrs vector */
    void push_back(std::pair<QueryPosition, RamDomain> constr) {
        constConstrs.push_back(std::move(constr));
    }

    /** Verify if the current query product satisfies constant constraint */
    bool verify(const QueryValueAt& valueAt) const {
        return std::all_of(constConstrs.begin(), constConstrs.end(),
                [&](const auto& constr) { return valueAt(constr.first) == constr.second; });
    }

    /** Get the constant constraint vector */
    std::vector<std::pair<QueryPosition, RamDomain>>& getConstraints() {
        return constConstrs;
    }

    const std::vector<std::pair<QueryPosition, RamDomain>>& getConstraints() const {
        return constConstrs;
    }

private:
    std::vector<std::pair<QueryPosition, RamDomain>> constConstrs;
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
            case 's': return quoteSymbol(symTable.decode(value));
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
                    throw ExplainValueError(tfm::format("expected a number, got `%s`", value));
                }
                return ramBitCast(RamSignedFromString(value));
            case 'u':
                if (!canBeParsedAsRamUnsigned(value)) {
                    throw ExplainValueError(tfm::format("expected an unsigned number, got `%s`", value));
                }
                return ramBitCast(RamUnsignedFromString(value));
            case 'f':
                if (!canBeParsedAsRamFloat(value)) {
                    throw ExplainValueError(tfm::format("expected a float, got `%s`", value));
                }
                return ramBitCast(RamFloatFromString(value));
            case 's': {
                std::string symbol;
                if (!unquoteSymbol(value, symbol)) {
                    throw ExplainValueError(tfm::format("expected a string, got `%s`", value));
                }
                return symTable.encode(symbol);
            }
            case 'r': return recordRead(type, value);
            case '+': return adtRead(type, value);
            default: fatal("unhandled type attr code");
        }
    }

    /** A record literal split into its fields, or an ADT literal split into its branch arguments */
    struct ValueParts {
        std::vector<std::string> args;
        std::vector<std::string> argTypes;
        /** ADTs only */
        RamDomain branchId = 0;
        bool isEnum = false;
    };

    /** Split a record literal other than nil into its fields, see recordRead */
    ValueParts recordParts(const std::string& type, const std::string& value) const {
        auto&& recordInfo = types["records"][type];
        if (recordInfo.is_null()) {
            fatal("no type information for record type `%s`", type);
        }

        ValueParts parts;
        for (auto&& fieldType : recordInfo["types"].array_items()) {
            parts.argTypes.push_back(fieldType.string_value());
        }
        if (value.size() < 2 || value[0] != '[' || value.back() != ']' ||
                !splitValues(value.substr(1, value.size() - 2), parts.args) ||
                parts.args.size() != parts.argTypes.size()) {
            throw ExplainValueError(tfm::format("expected a record of type `%s`, got `%s`", type, value));
        }
        return parts;
    }

    /** Split an ADT literal into its branch arguments, see adtRead */
    ValueParts adtParts(const std::string& type, const std::string& value) const {
        auto&& adtInfo = types["ADTs"][type];
        if (adtInfo.is_null()) {
            fatal("no type information for ADT type `%s`", type);
        }

        // $Branch or $Branch(arg, ...)
        ValueParts parts;
        const auto open = value.find('(');
        if (value.size() < 2 || value[0] != '$' ||
                (open != std::string::npos &&
                        (value.back() != ')' ||
                                !splitValues(value.substr(open + 1, value.size() - open - 2), parts.args)))) {
            throw ExplainValueError(tfm::format("expected an ADT value of type `%s`, got `%s`", type, value));
        }
        const std::string name = value.substr(1, open == std::string::npos ? open : open - 1);

        auto&& branches = adtInfo["branches"].array_items();
        auto branch = std::find_if(branches.begin(), branches.end(),
                [&](const json11::Json& b) { return b["name"].string_value() == name; });
        if (branch == branches.end()) {
            throw ExplainValueError(tfm::format("`%s` is not a branch of ADT type `%s`", name, type));
        }
        parts.branchId = static_cast<RamDomain>(branch - branches.begin());
        parts.isEnum = adtInfo["enum"].bool_value();

        for (auto&& argType : (*branch)["types"].array_items()) {
            parts.argTypes.push_back(argType.string_value());
        }
        if (parts.args.size() != parts.argTypes.size()) {
            throw ExplainValueError(tfm::format("branch `%s` of ADT type `%s` takes %d argument(s), got `%s`",
                    name, type, parts.argTypes.size(), value));
        }
        return parts;
    }

    // Note: reading a record or ADT value may add a new entry to the record table, in which
    // case no tuple contains it.
    RamDomain recordRead(const std::string& type, const std::string& value) const {
        if (value == "nil") {
            if (types["records"][type].is_null()) {
                fatal("no type information for record type `%s`", type);
            }
            return 0;
        }

        const ValueParts parts = recordParts(type, value);
        std::vector<RamDomain> nums;
        for (std::size_t i = 0; i < parts.args.size(); ++i) {
            nums.push_back(valueRead(parts.argTypes[i], parts.args[i]));
        }
        return prog.getRecordTable().pack(nums.data(), nums.size());
    }

    RamDomain adtRead(const std::string& type, const std::string& value) const {
        const ValueParts parts = adtParts(type, value);

        // See adtShow for the encoding.
        if (parts.isEnum) {
            return parts.branchId;
        }
        std::vector<RamDomain> nums;
        for (std::size_t i = 0; i < parts.args.size(); ++i) {
            nums.push_back(valueRead(parts.argTypes[i], parts.args[i]));
        }
        RamDomain payload = nums.size() == 1 ? nums[0] : prog.getRecordTable().pack(nums.data(), nums.size());
        return prog.getRecordTable().pack({parts.branchId, payload});
    }

    /** Follow a step into a record or ADT value, see adtShow for the ADT encoding */
    std::optional<RamDomain> valueStep(RamDomain value, const ValueStep& step) const {
        const RecordTable& recordTable = prog.getRecordTable();
        if (step.kind == 'r') {
            if (value == 0) {
                return std::nullopt;
            }
            return recordTable.unpack(value, step.arity)[step.index];
        }
        // Enumerations and branches without arguments have no steps
        const RamDomain* tuple = recordTable.unpack(value, 2);
        if (tuple[0] != step.branchId) {
            return std::nullopt;
        }
        if (step.arity == 1) {
            return tuple[1];
        }
        return recordTable.unpack(tuple[1], step.arity)[step.index];
    }

public:
    /** Show a symbol as a Datalog string literal, escaping as the parser expects */
    static std::string quoteSymbol(const std::string& symbol) {
        std::string quoted = "\"";
        for (char c : symbol) {
            switch (c) {
                case '"': quoted += "\\\""; break;
                case '\\': quoted += "\\\\"; break;
                case '\a': quoted += "\\a"; break;
                case '\b': quoted += "\\b"; break;
                case '\f': quoted += "\\f"; break;
                case '\n': quoted += "\\n"; break;
                case '\r': quoted += "\\r"; break;
                case '\t': quoted += "\\t"; break;
                case '\v': quoted += "\\v"; break;
                default: quoted += c;
            }
        }
        return quoted + "\"";
    }

    /**
     * Read a Datalog string literal, see quoteSymbol. Unknown escape sequences are kept
     * verbatim. Returns false if the value is not a string literal.
     */
    static bool unquoteSymbol(const std::string& value, std::string& symbol) {
        if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
            return false;
        }
        symbol.clear();
        for (std::size_t i = 1; i + 1 < value.size(); ++i) {
            const char c = value[i];
            if (c == '"') {
                return false;
            }
            if (c != '\\') {
                symbol += c;
                continue;
            }
            if (i + 2 >= value.size()) {
                return false;
            }
            switch (value[++i]) {
                case '"': symbol += '"'; break;
                case '\'': symbol += '\''; break;
                case '\\': symbol += '\\'; break;
                case 'a': symbol += '\a'; break;
                case 'b': symbol += '\b'; break;
                case 'f': symbol += '\f'; break;
                case 'n': symbol += '\n'; break;
                case 'r': symbol += '\r'; break;
                case 't': symbol += '\t'; break;
                case 'v': symbol += '\v'; break;
                default: symbol += '\\'; symbol += value[i];
            }
        }
        return true;
    }

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
        bool escaped = false;
        for (char c : str) {
            if (inString) {
                inString = escaped || c != '"';
                escaped = !escaped && c == '\\';
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
