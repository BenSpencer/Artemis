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

#include "concolic/executiontree/tracenodes.h"
#include "concolic/pathcondition.h"
#include "concolic/search/explorationdescriptor.h"

#ifndef SEARCH_H
#define SEARCH_H




namespace artemis
{



/*
 *  Abstract interface for searching for the next node to explore in the path tree.
 */


class TreeSearch : public TraceVisitor
{
public:
    // Selects an unexplored node from the tree to be explored next.
    // Returns true iff a target was found.
    virtual bool chooseNextTarget() = 0;

    // Returns the target's PC after a call to chooseNextTarget() returns true.
    virtual PathConditionPtr getTargetPC() = 0;

    // Returns the target's DOM constraints after a call to chooseNextTarget() returns true.
    virtual QSet<SelectRestriction> getTargetDomConstraints() = 0;

    // Returns a description of the target which can be looked up later.
    virtual ExplorationDescriptor getTargetDescriptor() = 0;

};

typedef QSharedPointer<TreeSearch> TreeSearchPtr;



}

#endif // SEARCH_H
