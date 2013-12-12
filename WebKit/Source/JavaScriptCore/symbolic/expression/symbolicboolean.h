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

 // AUTO GENERATED - DO NOT MODIFY

#ifndef SYMBOLIC_SYMBOLICBOOLEAN_H
#define SYMBOLIC_SYMBOLICBOOLEAN_H

#include <string>

#include "visitor.h"
#include "booleanexpression.h"
#include "symbolicsource.h"

#ifdef ARTEMIS

namespace Symbolic
{

class SymbolicBoolean : public BooleanExpression
{
public:
    explicit SymbolicBoolean(SymbolicSource source);
    void accept(Visitor* visitor);
    void accept(Visitor* visitor, void* arg);

	inline SymbolicSource getSource() {
		return m_source;
	}

private:
	SymbolicSource m_source;

};
}

#endif
#endif // SYMBOLIC_SYMBOLICBOOLEAN_H