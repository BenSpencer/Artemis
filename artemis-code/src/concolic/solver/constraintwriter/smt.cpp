/*
 * Copyright 2013 Aarhus University
 *
 * Licensed under the GNU General Public License, Version 3 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *          http://www.gnu.org/licenses/gpl-3.0.html
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <sstream>
#include <cstdlib>
#include <math.h>

#include <QDebug>
#include <QDateTime>

#include "util/loggingutil.h"
#include "statistics/statsstorage.h"

#include "smt.h"

namespace artemis
{

SMTConstraintWriter::SMTConstraintWriter(ConcolicBenchmarkFeatures disabledFeatures)
    : mExpressionType(Symbolic::TYPEERROR)
    , mError(false)
    , mErrorClause(-1)
    , mNextTemporarySequence(0)
    , mDisabledFeatures(disabledFeatures)
{
}

void SMTConstraintWriter::preVisitPathConditionsHook(QSet<QString> varsUsed)
{
}

void SMTConstraintWriter::postVisitPathConditionsHook()
{
}

std::string SMTConstraintWriter::ifLabel()
{
    return "ite";
}

/**
 * Z3 doesn't support "_" in identifiers, thus they should be encoded before
 * writing constraints, and decoded before reading the constraints.
 */
bool SMTConstraintWriter::encodeUnderscore()
{
    return true;
}

bool SMTConstraintWriter::write(PathConditionPtr pathCondition, FormRestrictions formRestrictions, DomSnapshotStoragePtr domSnapshots, ReachablePathsConstraintSet reachablePaths, ReorderingConstraintInfoPtr reorderingInfo, std::string outputFile)
{
    std::string preVisitHookOutput;
    std::string visitorOutput;
    std::string postVisitHookOutput;
    std::string reachablePathsOutput;
    std::string linearOrderingOutput;

    mError = false;
    mCurrentClause = -1;

    mFormRestrictions = formRestrictions;
    mDomSnapshots = domSnapshots;
    mReachablePaths = reachablePaths;
    mReorderingInfo = reorderingInfo;
    if (!mReorderingInfo.isNull()) {
        mReorderingInfo->setPcIndex();
    }

    mOutput.str("");

    QSet<QString> freeVars = getFreeVariables(pathCondition);
    preVisitPathConditionsHook(freeVars);

    preVisitHookOutput = mOutput.str();
    mOutput.str("");

    mOutput << "; The PC\n";
    for (uint i = 0; i < pathCondition->size(); i++) {
        mCurrentClause = i;

        pathCondition->get(i).first->accept(this);
        if(!checkType(Symbolic::BOOL) && !checkType(Symbolic::TYPEERROR)){
            error("Writing the PC did not result in a boolean constraint");
        }

        mOutput << "(assert (= " << mExpressionBuffer;
        mOutput << (pathCondition->get(i).second ? " true" : " false");
        mOutput << "))\n";
    }
    mCurrentClause = -1;

    visitorOutput = mOutput.str();
    mOutput.str("");

    emitReachablePathsConstraints();
    reachablePathsOutput = mOutput.str();
    mOutput.str("");

    emitLinearOrderingConstraints(freeVars);
    linearOrderingOutput = mOutput.str();
    mOutput.str("");

    postVisitPathConditionsHook();
    postVisitHookOutput = mOutput.str();
    mOutput.str("");

    std::ofstream constraintFile;
    constraintFile.open(outputFile.data());

    constraintFile << preVisitHookOutput;
    constraintFile << mPreambleDefinitions.join("\n").toStdString();
    constraintFile << visitorOutput;
    constraintFile << reachablePathsOutput;
    constraintFile << linearOrderingOutput;
    constraintFile << postVisitHookOutput;

    constraintFile.close();

    if (mError) {
        return false;
    }

    for (std::map<std::string, Symbolic::Type>::iterator iter = mTypemap.begin(); iter != mTypemap.end(); iter++) {
        if (iter->second == Symbolic::TYPEERROR) {
            mErrorReason = "Artemis is unable generate constraints - a type-error was found.";
            return false;
        }
    }

    return true;
}

QSet<QString> SMTConstraintWriter::getFreeVariables(PathConditionPtr pc)
{
    QSet<QString> result = pc->freeVariables().keys().toSet();

    // If the reachable-paths constraints are being used, we must include those variables as well.
    foreach (NamedReachablePathsConstraint constraint, mReachablePaths) {
        result.unite(constraint.second->freeVariableNames());
    }

    return result;
}

void SMTConstraintWriter::emitReachablePathsConstraints()
{
    // mReachablePaths is a set of extra constraints. Each member is a pair: a name for that constraint and a
    // ReachablePathsConstraintPtr to be written. Each constraint takes the form of a tree of ITE decisions with
    // symbolic conditions and either "true" or "false" at each leaf of the tree.

    foreach (NamedReachablePathsConstraint constraint, mReachablePaths) {
        if (!mReorderingInfo.isNull()) {
            mReorderingInfo->setIndex(constraint.first.second);
        }

        mOutput << std::endl;
        mOutput << "; Reachable-paths constraint for " << constraint.first.first.toStdString() << std::endl;
        std::string exprString = reachablePathsConstraintExpression(constraint.second, 2);
        mOutput << "(assert \n" << exprString << "\n)" << std::endl;
    }
}

std::string SMTConstraintWriter::reachablePathsConstraintExpression(ReachablePathsConstraintPtr expr, int indent)
{
    std::string indentStr = std::string(indent, ' ');

    // If the expression is just true or false, we have reached a leaf.
    if (expr->isAlwaysTerminating()) {
        return indentStr + "true";
    }
    if (expr->isAlwaysAborting()) {
        return indentStr + "false";
    }

    // Otherwise, we must be at a ReachablePathsITE or ReachablePathsDisjunction node.
    // TODO: Really we should use a visitor or something and avoid this constant casting...
    QSharedPointer<ReachablePathsITE> ite = expr.dynamicCast<ReachablePathsITE>();
    if (!ite.isNull()) {

        // Run the visitor over the branch constraint.
        ite->condition->accept(this);
        if(!checkType(Symbolic::BOOL) && !checkType(Symbolic::TYPEERROR)){
            error("Writing the reachable-paths constraint did not result in a boolean constraint");
        }
        std::string conditionString = mExpressionBuffer;
        std::string thenBranch = reachablePathsConstraintExpression(ite->thenConstraint, indent+2);
        std::string elseBranch = reachablePathsConstraintExpression(ite->elseConstraint, indent+2);

        return indentStr + "(" + ifLabel() + "\n" + indentStr + "  " + conditionString + "\n" + thenBranch + "\n" + elseBranch + "\n" + indentStr + ")";

    } else {
        QSharedPointer<ReachablePathsDisjunction> disjunct = expr.dynamicCast<ReachablePathsDisjunction>();
        assert(!disjunct.isNull());

        std::string childStrings;
        foreach (ReachablePathsConstraintPtr childConstraint, disjunct->children) {
            childStrings.append(reachablePathsConstraintExpression(childConstraint, indent+2));
            childStrings.append("\n");
        }

        return indentStr + "(or\n" + childStrings + "\n" + indentStr + ")";
    }
}

void SMTConstraintWriter::emitLinearOrderingConstraints(QSet<QString> varsUsed)
{
    if (mReorderingInfo.isNull()) {
        return;
    }
    // If mReorderingInfo is set, then we will emit the reordering constraints.
    // This consists of 1) renaming the variables (in encodeIdentifier), and 2) some extra set of constraints (here).
    // The extra constraints must represent the ordering of actions, and force them to have a linear order.
    // They also enforce different (default or non-default) values for each indexed variable depending on the ordering.

    // Assume that the action indices are exactly the set 1..N.
    QMap<uint, QPair<QString, InjectionValue>> actionVariables = mReorderingInfo->getActionVariables();
    QList<uint> indices = actionVariables.keys();
    int N = indices.length();
    for (uint i=1; i<=(uint)N; i++) {
        assert(indices.contains(i));
    }

    // For each action index we create an ordering variable. Those are constrained to be distinct and in the expected
    // range, so they must form a linear order.
    mOutput << "\n; Action linear ordering constraints\n";
    if (actionVariables.size() > 1) {
        // There are multipe fields to order.
        std::string allOrderVars = "";
        foreach (uint actionIdx, actionVariables.keys()) {
            std::string orderVar = "SYM_ORDERING_" + std::to_string(actionIdx);
            recordAndEmitType(orderVar, Symbolic::INT);
            mOutput << "(assert (and (< 0 " << orderVar << " ) (<= " << orderVar << " " << N << ")))\n";
            allOrderVars.append(orderVar + " ");
        }
        mOutput << "(assert (distinct " + allOrderVars + "))\n";
    } else if (actionVariables.size() == 1) {
        // There is only a single field, just assign it a static value.
        assert(actionVariables.contains(1));
        recordAndEmitType("SYM_ORDERING_1", Symbolic::INT);
        mOutput << "(assert (= SYM_ORDERING_1 1 ))\n";
    } else {
        // There are no actions to be ordered. Skip.
    }

    // For each action pair (A, B), set the value injected at A as seen by B depending on their relative order.
    mOutput << "\n; Force values at each action to be default or not depending on the ordering\n";
    foreach (uint aIdx, indices) {
        // Emit rules only for variables which are actually used in the PC or the other constaints.
        // TODO: It would be even better if we could emit constraints only when aAsSeenByB is used, but we do not have this information conveniently available.
        ActionInfo aInfo = actionVariables[aIdx];
        std::string aMainName = aInfo.first.toStdString();
        if (!varsUsed.contains(QString::fromStdString(aMainName))) {
            //Log::debug("Skipping ordering constraint for " + aMainName);
            continue;
        }
        mOutput << "; For " << aMainName << "\n";

        Symbolic::Type aType;
        std::string aDefault;
        switch (aInfo.second.getType()) {
        case QVariant::String:
            // TODO: This is leaking some details of the type coercion that CVC4ConstraintWriter does...
            aType = getTypeUsedInPC(aMainName, Symbolic::STRING);
            if (aType == Symbolic::STRING) {
                aDefault = "\"" + aInfo.second.getString().toStdString() + "\"";
            } else if (aType == Symbolic::INT) {
                bool convertedOk;
                aDefault = std::to_string(aInfo.second.getString().toInt(&convertedOk, 10)); // If conversion is not OK, then returns 0, which is what we want.
            } else {
                Log::fatal("Unexpected coercion type found in SMTConstraintWriter::emitLinearOrderingConstraints().");
                exit(1);
            }
            break;
        case QVariant::Bool:
            aType = Symbolic::BOOL;
            aDefault = aInfo.second.getBool() ? "true" : "false";
            break;
        case QVariant::Int: // N.B. Not expected to hit this case. We do not have integer inputs, and select indices are handled separately.
        default:
            Log::fatal("Unexpected default value type found in SMTConstraintWriter::emitLinearOrderingConstraints().");
            exit(1);
        }

        foreach (uint bIdx, indices) {
            std::string aOrder = "SYM_ORDERING_" + std::to_string(aIdx);
            std::string bOrder = "SYM_ORDERING_" + std::to_string(bIdx);
            std::string aAsSeenByB = ReorderingConstraintInfo::encodeWithExplicitIndex(aInfo.first, bIdx).toStdString();
            recordAndEmitType(aMainName, aType, true);
            recordAndEmitType(aAsSeenByB, aType);
            // TODO: This will cause a type error if a variable is seen as coerced in some handlers and not coerced in other handlers. At the moment this case is not checked for or handled.

            if (aIdx == bIdx) {
                mOutput << "(assert (= " << aAsSeenByB << " " << aMainName << "))\n";
            } else {
                mOutput << "(assert (= " << aAsSeenByB << " (" << ifLabel() << " (<= " << aOrder << " " << bOrder << ") " << aMainName << " " << aDefault << " )))\n";
            }
        }

        // Also add a constraint for the variable as seen by the submit button.
        if (mReorderingInfo->getSubmitButtonIndex() != 0) {
            std::string aAsSeenByButton = ReorderingConstraintInfo::encodeWithExplicitIndex(aInfo.first, mReorderingInfo->getSubmitButtonIndex()).toStdString();
            recordAndEmitType(aAsSeenByButton, aType);
            mOutput << "(assert (= " << aAsSeenByButton << " " << aMainName << "))\n";
        }
    }

    // Emit similar constraints for selectedIndex variables, which are handled separately.
    QMap<uint, QPair<QString, InjectionValue>> actionIndexVariables = mReorderingInfo->getActionIndexVariables();
    foreach (uint aIdx, actionIndexVariables.keys()) {
        // Emit rules only for variables which are actually used in the PC or the other constaints.
        ActionInfo aInfo = actionIndexVariables[aIdx];
        std::string aMainName = aInfo.first.toStdString();
        if (!varsUsed.contains(QString::fromStdString(aMainName))) {
            //Log::debug("Skipping ordering constraint for " + aMainName);
            continue;
        }
        mOutput << "; For " << aMainName << "\n";

        // We know the type should be INT for these selectedIndex constraints.
        if (aInfo.second.getType() != QVariant::Int) {
            Log::fatal("Unexpected type found for default value of a selected index constraint.");
            exit(1);
        }
        int aDefaultInt = aInfo.second.getInt();
        std::string aDefault = (aDefaultInt > 0) ? std::to_string(aDefaultInt) : ("(- " + std::to_string(aDefaultInt*-1) + ")");

        foreach (uint bIdx, indices) {
            std::string aOrder = "SYM_ORDERING_" + std::to_string(aIdx);
            std::string bOrder = "SYM_ORDERING_" + std::to_string(bIdx);
            std::string aAsSeenByB = ReorderingConstraintInfo::encodeWithExplicitIndex(aInfo.first, bIdx).toStdString();
            recordAndEmitType(aMainName, Symbolic::INT, true);
            recordAndEmitType(aAsSeenByB, Symbolic::INT);

            if (aIdx == bIdx) {
                mOutput << "(assert (= " << aAsSeenByB << " " << aMainName << "))\n";
            } else {
                mOutput << "(assert (= " << aAsSeenByB << " (" << ifLabel() << " (<= " << aOrder << " " << bOrder << ") " << aMainName << " " << aDefault << " )))\n";
            }
        }

        // Also add a constraint for the variable as seen by the submit button.
        if (mReorderingInfo->getSubmitButtonIndex() != 0) {
            std::string aAsSeenByButton = ReorderingConstraintInfo::encodeWithExplicitIndex(aInfo.first, mReorderingInfo->getSubmitButtonIndex()).toStdString();
            recordAndEmitType(aAsSeenByButton, Symbolic::INT);
            mOutput << "(assert (= " << aAsSeenByButton << " " << aMainName << "))\n";
        }
    }

}

Symbolic::Type SMTConstraintWriter::getTypeUsedInPC(std::string variable, Symbolic::Type initialValueType)
{
    // This method is a no-op to allow overriding by CVC4ConstraintWriter.
    return initialValueType;
}


/** Symbolic Integer/String/Boolean **/


void SMTConstraintWriter::visit(Symbolic::SymbolicInteger* symbolicinteger, void* args)
{
    // Checks this symbolic value is of type INT and raises an error otherwise.
    recordAndEmitType(symbolicinteger->getSource(), Symbolic::INT);

    mExpressionBuffer = encodeIdentifier(symbolicinteger->getSource().getIdentifier());
    mExpressionType = Symbolic::INT;
}

void SMTConstraintWriter::visit(Symbolic::SymbolicString* symbolicstring, void* args)
{
    error("NO SYMBOLIC STRING SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::SymbolicBoolean* symbolicboolean, void* args)
{
    // Checks this symbolic value is of type BOOL and raises an error otherwise.
    recordAndEmitType(symbolicboolean->getSource(), Symbolic::BOOL);

    mExpressionBuffer = encodeIdentifier(symbolicboolean->getSource().getIdentifier());
    mExpressionType = Symbolic::BOOL;
}

/** Constant Integer/String/Boolean **/


void SMTConstraintWriter::visit(Symbolic::ConstantInteger* constantinteger, void* args)
{
    /**
     * Note! We convert the double into an integer in some cases since we do not support
     * writing constraints on real values right now.
     */

    std::ostringstream doubleToInt;
    if (std::isnan(constantinteger->getValue())) {
        doubleToInt << "nan";
    } else {
        doubleToInt << (int)constantinteger->getValue();
    }

    std::ostringstream strs;
    strs << doubleToInt.str();

    mExpressionBuffer = strs.str();
    mExpressionType = Symbolic::INT;

    // negative number fix, the correct syntax is (- 1) not -1
    if (mExpressionBuffer.find_first_of("-") == 0) {
        mExpressionBuffer = "(- " + mExpressionBuffer.substr(1) + ")";
    }

    if (mExpressionBuffer.find("nan") != std::string::npos) {
        error("Unsupported constraint using NaN constant");
    }
}

void SMTConstraintWriter::visit(Symbolic::ConstantString* constantstring, void* args)
{
    error("NO SYMBOLIC STRING SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::ConstantBoolean* constantboolean, void* args)
{
    std::ostringstream strs;

    strs << (constantboolean->getValue() == true ? "true" : "false");

    mExpressionBuffer = strs.str();
    mExpressionType = Symbolic::BOOL;
}

/** Coercion **/

void SMTConstraintWriter::visit(Symbolic::IntegerCoercion* integercoercion, void* args)
{
    CoercionPromise promise(Symbolic::INT);
    integercoercion->getExpression()->accept(this, &promise);

    if (!promise.isCoerced) {
        coercetype(mExpressionType, Symbolic::INT, mExpressionBuffer); // Sets mExpressionBuffer and Type.
    }
}

void SMTConstraintWriter::visit(Symbolic::StringCoercion* stringcoercion, void* args)
{
    error("NO SYMBOLIC STRING SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringCharAt* stringcharat, void* arg)
{
    error("NO SYMBOLIC STRING SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::BooleanCoercion* booleancoercion, void* args)
{
    CoercionPromise promise(Symbolic::BOOL);
    booleancoercion->getExpression()->accept(this);

    if (!promise.isCoerced) {
        coercetype(mExpressionType, Symbolic::BOOL, mExpressionBuffer); // Sets mExpressionBuffer and Type.
    }
}


/** Binary Operations **/

void SMTConstraintWriter::visit(Symbolic::IntegerBinaryOperation* integerbinaryoperation, void* args)
{
    static const char* op[] = {
        "(+ ", "(- ", "(* ", "(div ", "(= ", "(= (= ", "(<= ", "(< ", "(>= ", "(> ", "(mod ", "(= (= ", "(= "
    };

    static const char* opclose[] = {
        ")", ")", ")", ")", ")", ") false)", ")", ")", ")", ")", ")", ") false)", ")"
    };

    integerbinaryoperation->getLhs()->accept(this);
    std::string lhs = mExpressionBuffer;
    if(!checkType(Symbolic::INT)){
        error("Integer operation with incorrectly typed LHS");
        return;
    }

    integerbinaryoperation->getRhs()->accept(this);
    std::string rhs = mExpressionBuffer;
    if(!checkType(Symbolic::INT)){
        error("Integer operation with incorrectly typed RHS");
        return;
    }

    std::ostringstream strs;
    strs << op[integerbinaryoperation->getOp()] << lhs << " " << rhs << opclose[integerbinaryoperation->getOp()];
    mExpressionBuffer = strs.str();
    mExpressionType = opGetType(integerbinaryoperation->getOp());
}

void SMTConstraintWriter::visit(Symbolic::StringBinaryOperation* stringbinaryoperation, void* args)
{
    error("NO SYMBOLIC STRING SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::BooleanBinaryOperation* booleanbinaryoperation, void* args)
{
    static const char* op[] = {
        "(= ", "(= (= ", "(= ", "(! (= "
    };

    static const char* opclose[] = {
        ")", ") false)", ")", "))"
    };

    booleanbinaryoperation->getLhs()->accept(this);
    std::string lhs = mExpressionBuffer;
    if(!checkType(Symbolic::BOOL)){
        error("Boolean operation with incorrectly typed LHS");
        return;
    }

    booleanbinaryoperation->getRhs()->accept(this);
    std::string rhs = mExpressionBuffer;
    if(!checkType(Symbolic::BOOL)){
        error("Boolean operation with incorrectly typed RHS");
        return;
    }

    std::ostringstream strs;
    strs << op[booleanbinaryoperation->getOp()] << lhs << " " << rhs << opclose[booleanbinaryoperation->getOp()];
    mExpressionBuffer = strs.str();
    mExpressionType = opGetType(booleanbinaryoperation->getOp());
}

void SMTConstraintWriter::visit(Symbolic::StringRegexSubmatch* submatch, void* args)
{
    error("NO SYMBOLIC STRING REGEX SUBMATCH SUPPORT");
}



/** Other Operations **/

void SMTConstraintWriter::visit(Symbolic::StringRegexReplace* regex, void* args)
{
    error("NO SYMBOLIC REGEX SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringReplace* replace, void* args)
{
    error("NO SYMBOLIC STRING REPLACE SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringRegexSubmatchArray* exp, void* arg)
{
    error("NO SYMBOLIC STRING REGEX SUBMATCH (ARRAY) SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringRegexSubmatchArrayAt* exp, void* arg)
{
    error("NO SYMBOLIC STRING REGEX SUBMATCH (ARRAY) SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::SymbolicObjectPropertyString* obj, void* arg)
{
    error("NO SYMBOLIC NODE PROPERTY SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringSubstring* obj, void* arg)
{
    error("NO SYMBOLIC STRING SUBSTRING SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringToLowerCase *stringtolowercase, void *arg)
{
    error("NO SYMBOLIC STRING TOLOWERCASE SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringToUpperCase *stringtouppercase, void *arg)
{
    error("NO SYMBOLIC STRING TOUPPERCASE SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringRegexSubmatchArrayMatch* exp, void* arg)
{
    error("NO SYMBOLIC STRING REGEX SUBMATCH (ARRAY) SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::ConstantObject* obj, void* arg)
{
    error("NO SYMBOLIC NULL/OBJECT SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::ObjectBinaryOperation* obj, void* arg)
{
    error("NO SYMBOLIC NULL/OBJECT SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::SymbolicObject* obj, void* arg)
{
    error("NO SYMBOLIC OBJECT SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringLength* stringlength, void* args)
{
    error("NO STRING LENGTH SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::StringRegexSubmatchIndex* submatchIndex, void* args)
{
    error("NO STRING REGEX SUBMATCH-INDEX SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::IntegerMaxMin* obj, void* arg)
{

    std::list<Symbolic::Expression*> expressions = obj->getExpressions();
    std::list<Symbolic::Expression*>::iterator iter;

    std::string comp = obj->getMax() ?  ">" : "<";

    std::string last_var = "0"; // returned if people use Math.max() / Math.min(), correct behaviour would be -infty, but we can't model that

    for (iter = expressions.begin(); iter != expressions.end(); iter++) {

        Symbolic::Expression* expression = *iter;
        expression->accept(this);

        if(!checkType(Symbolic::INT)){
            error("Integer max/min operation with incorrectly typed argument");
            return;
        }

        std::string new_var = this->emitAndReturnNewTemporary(Symbolic::INT);

        if (iter == expressions.begin()) {
            // this is the first element
            mOutput << "(assert (= " << new_var << " " << mExpressionBuffer << " ))" << std::endl;
        } else {

            mOutput << "(assert (= " << new_var << " (ite (" << comp << " " << last_var << " " << mExpressionBuffer << " ) " << last_var << " " << mExpressionBuffer << " )))" << std::endl;
        }

        last_var = new_var;

    }

    mExpressionBuffer = last_var;
    mExpressionType = Symbolic::INT;

}

void SMTConstraintWriter::visit(Symbolic::StringIndexOf* stringindexof, void* arg)
{
    error("NO STRING INDEXOF SUPPORT");
}

void SMTConstraintWriter::visit(Symbolic::ObjectArrayIndexOf* obj, void* arg)
{
    error("NO OBJECT ARRAY INDEXOF SUPPORT");
}

/** Utility **/

std::string SMTConstraintWriter::stringfindreplace(const std::string& string,
                                                     const std::string& search,
                                                     const std::string& replace) {
    std::string newString = string;

    size_t start_pos = 0;
    while ((start_pos = newString.find(search, start_pos)) != std::string::npos) {
        newString.replace(start_pos, search.length(), replace);
        start_pos += replace.length();
    }

    return newString;
}

std::string SMTConstraintWriter::encodeIdentifier(const std::string& identifier, bool noRename) {
    std::string identifier_modified;
    if (noRename || mReorderingInfo.isNull()) {
        identifier_modified = identifier;
    } else {
        identifier_modified = mReorderingInfo->encode(QString::fromStdString(identifier)).toStdString();
    }

    std::string t = identifier_modified;
    if (encodeUnderscore()) {
        t = SMTConstraintWriter::stringfindreplace(t, "_" , "QQQ");
    }
    t = SMTConstraintWriter::stringfindreplace(t, "[" , "WWW");
    t = SMTConstraintWriter::stringfindreplace(t, "]" , "ZZZ");
    t = SMTConstraintWriter::stringfindreplace(t, ":" , "CCC");

    return t;
}

std::string SMTConstraintWriter::decodeIdentifier(const std::string& identifier) {

    std::string t = identifier;
    if (encodeUnderscore()) {
        t = SMTConstraintWriter::stringfindreplace(t, "QQQ", "_");
    }
    t = SMTConstraintWriter::stringfindreplace(t, "WWW", "[");
    t = SMTConstraintWriter::stringfindreplace(t, "ZZZ", "]");
    t = SMTConstraintWriter::stringfindreplace(t, "CCC", ":");

    // N.B. Do not undo the reordering renaming - leave the renamed names in the final solution.
    //if (!mReorderingInfo.isNull()) {
    //    t = mReorderingInfo->decode(QString::fromStdString(t)).toStdString();
    //}

    return t;
}

/** Error handling **/

void SMTConstraintWriter::error(std::string reason)
{
    if(!mError){
        mError = true;
        mErrorReason = reason;
        mErrorClause = mCurrentClause;
        mExpressionBuffer = "ERROR";
    }
}

/** Types **/

void SMTConstraintWriter::emitConst(const std::string& identifier, Symbolic::Type type)
{
    static const char* typeStrings[] = {
        "Int", "Bool", "String", "Int", "ERROR"
    };

    mOutput << "(declare-const " << identifier << " " << typeStrings[type] << ")\n";
}

std::string SMTConstraintWriter::emitAndReturnNewTemporary(Symbolic::Type type)
{
    stringstream t;
    t << "TMP" << mNextTemporarySequence++;

    std::string temporaryName = t.str();

    emitConst(temporaryName, type);
    return temporaryName;
}

void SMTConstraintWriter::recordAndEmitType(const Symbolic::SymbolicSource& source, Symbolic::Type type, bool noRename)
{
    recordAndEmitType(source.getIdentifier(), type, noRename);
}

void SMTConstraintWriter::recordAndEmitType(const std::string& source, Symbolic::Type type, bool noRename)
{
    std::string encodedSource = encodeIdentifier(source, noRename);

    std::map<std::string, Symbolic::Type>::iterator iter = mTypemap.find(encodedSource);

    if (iter != mTypemap.end()) {
        // type already recorded, update type info
        if(iter->second != type) {
            Log::debug("Encountered type conflict for " + source + " (" + std::to_string(iter->second) + ", " + std::to_string(type) + ")");
        }
        iter->second = iter->second == type ? type : Symbolic::TYPEERROR;
    } else {
        // type not recorded before, output definition and store type
        mTypemap.insert(std::pair<std::string, Symbolic::Type>(encodedSource, type));
        emitConst(encodedSource, type);
    }

}

bool SMTConstraintWriter::checkType(Symbolic::Type expected) {
    return mExpressionType == expected;
}

void SMTConstraintWriter::coercetype(Symbolic::Type from,
                                              Symbolic::Type to,
                                              std::string expression)
{
    mExpressionType = Symbolic::TYPEERROR;

    switch (from) {
    case Symbolic::INT:

        switch (to) {
        case Symbolic::INT:
            mExpressionBuffer = expression; // No coercion
            mExpressionType = Symbolic::INT;
            break;

        case Symbolic::STRING:
            error("Unsupported type coercion from INT to STRING");
            break;

        case Symbolic::BOOL:
            mExpressionBuffer = "(= (= " + expression + " 0) false)";
            mExpressionType = Symbolic::BOOL;
            break;

        default:
            error("Unsupported type coercion from INT to UNKNOWN");
            break;
        }

        break;


    case Symbolic::STRING:

        switch (to) {
        case Symbolic::INT:
            error("Unsupported type coercion from STRING to INT");
            break;

        case Symbolic::STRING:
            mExpressionBuffer = expression; // No coercion
            mExpressionType = Symbolic::STRING;
            break;

        case Symbolic::BOOL:
            mExpressionBuffer = "(= (= " + expression + " \"\") false)";
            mExpressionType = Symbolic::BOOL;
            break;

        default:
            error("Unsupported type coercion from STRING to UNKNOWN");
            break;
        }

        break;


    case Symbolic::BOOL:

        switch (to) {
        case Symbolic::INT:
            mExpressionBuffer = "(" + ifLabel() + " " + expression + " 1 0)";
            mExpressionType = Symbolic::INT;
            break;

        case Symbolic::STRING:
            mExpressionBuffer = "("  + ifLabel() + " " + expression + " \"true\" \"false\")";
            mExpressionType = Symbolic::STRING;
            break;

        case Symbolic::BOOL:
            mExpressionBuffer = expression; // No coercion
            mExpressionType = Symbolic::BOOL;
            break;

        default:
            error("Unsupported type coercion from BOOL to UNKNOWN");
            break;
        }

        break;

    case Symbolic::OBJECT:

        switch(to) {

        case Symbolic::STRING:
            error("Unsupported type coercion from OBJECT to STRING");
            break;

        case Symbolic::BOOL:
            mExpressionBuffer = "(not (= " + expression + " 0))"; // true if not 0 (not null/undefined)
            mExpressionType = Symbolic::BOOL;
            break;

        default:
            error("Unsupported type coercion from OBJECT to UNKNOWN");
            break;

        }

        break;

    default:
        error("Unsupported type coercion from UNKNOWN to UNKNOWN");
        break;
    }

}


}
