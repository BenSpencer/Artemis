/*
 * Copyright 2014 Aarhus University
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

#include "strategies/inputgenerator/targets/jquerytarget.h"

#include "jquerytargetgenerator.h"

namespace artemis {

TargetDescriptorConstPtr JqueryTargetGenerator::generateTarget(EventHandlerDescriptorConstPtr eventHandler) const
{
    return TargetDescriptorConstPtr(new JQueryTarget(eventHandler, mJQueryListener));
}

TargetDescriptorConstPtr JqueryTargetGenerator::permuteTarget(
        EventHandlerDescriptorConstPtr eventHandler,
        TargetDescriptorConstPtr,
        ExecutionResultConstPtr) const
{
    // TODO, we should check here if the model is applicable, and if not we should return NULL
    return TargetDescriptorConstPtr(new JQueryTarget(eventHandler, mJQueryListener));
}

} // END NAMESPACE
