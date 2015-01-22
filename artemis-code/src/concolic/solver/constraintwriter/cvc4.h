/*
 * Copyright 2012 Aarhus University
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

#ifndef CVC4_H
#define CVC4_H

#include <fstream>
#include <map>
#include <set>

#include <QSharedPointer>

#include "JavaScriptCore/symbolic/expr.h"
#include "JavaScriptCore/symbolic/expression/visitor.h"

#include "smt.h"
#include "abstract.h"
#include "cvc4typeanalysis.h"
#include "statistics/statsstorage.h"

namespace artemis
{

class CVC4ConstraintWriter : public SMTConstraintWriter
{
public:

    CVC4ConstraintWriter(ConcolicBenchmarkFeatures disabledFeatures);

    bool write(PathConditionPtr pathCondition, FormRestrictions formRestrictions, DomSnapshotStoragePtr domSnapshots, std::string outputFile);

protected:

    // Internal
    virtual void visit(Symbolic::StringRegexSubmatchArray* exp, void* args);

    // Returns integer values to mExpressionBuffer
    virtual void visit(Symbolic::StringRegexSubmatchIndex* submatchIndex, void* args);
    virtual void visit(Symbolic::StringLength* stringlength, void* args);
    virtual void visit(Symbolic::StringIndexOf* stringindexof, void* arg);
    virtual void visit(Symbolic::ObjectArrayIndexOf* objectarrayindexof, void* arg);

    // Returns string values to mExpressionBuffer
    virtual void visit(Symbolic::SymbolicString* symbolicstring, void* args);
    virtual void visit(Symbolic::ConstantString* constantstring, void* args);
    virtual void visit(Symbolic::StringCoercion* stringcoercion, void* args);
    virtual void visit(Symbolic::StringCharAt* stringcharat, void* arg);
    virtual void visit(Symbolic::StringRegexReplace* stringregexreplace, void* args);
    virtual void visit(Symbolic::StringReplace* stringreplace, void* args);
    virtual void visit(Symbolic::StringRegexSubmatch* submatch, void* args);
    virtual void visit(Symbolic::StringRegexSubmatchArrayAt* exp, void* args);
    virtual void visit(Symbolic::SymbolicObjectPropertyString* obj, void* arg);
    virtual void visit(Symbolic::StringSubstring* obj, void* arg);

    // Returns boolean values to mExpressionBuffer
    virtual void visit(Symbolic::StringBinaryOperation* stringbinaryoperation, void* args);

    // Returns Object values to mExpressionBuffer
    virtual void visit(Symbolic::StringRegexSubmatchArrayMatch* exp, void* args);
    virtual void visit(Symbolic::ConstantObject* obj, void* arg);
    virtual void visit(Symbolic::ObjectBinaryOperation* obj, void* arg);
    virtual void visit(Symbolic::SymbolicObject* obj, void* arg);

    virtual void preVisitPathConditionsHook(QSet<QString> varsUsed);
    virtual void postVisitPathConditionsHook();

    void emitDOMConstraints();

    virtual void coercetype(Symbolic::Type from, Symbolic::Type to, std::string expression);

    void helperRegexTest(const std::string& regex, const std::string& expression,
                                               std::string* outMatch);
    void helperRegexMatch(const std::string& regex, const std::string& expression,
                                  std::string* outIsMatch, std::string* outPre, std::string* outMatch, std::string* outPost);

    enum SelectConstraintType { VALUE_ONLY, INDEX_ONLY, VALUE_INDEX };
    void helperSelectRestriction(SelectRestriction constraint, SelectConstraintType type);
    void helperRadioRestriction(RadioRestriction constraint);

    std::set<unsigned long> m_singletonCompilations;

    CVC4TypeAnalysisPtr mTypeAnalysis;
    std::set<std::string> mSuccessfulCoercions;

    std::set<Symbolic::SymbolicObject*> mVisitedSymbolicObjects;
    std::map<Symbolic::SymbolicObject*, std::set<std::string> > mUsedSymbolicObjectProperties;
};

typedef QSharedPointer<CVC4ConstraintWriter> CVC4ConstraintWriterPtr;

}

#endif // Z3STR_H
