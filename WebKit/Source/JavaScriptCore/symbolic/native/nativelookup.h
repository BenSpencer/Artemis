/*
 * Copyright 2012 Aarhus University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NATIVELOOKUP_H
#define NATIVELOOKUP_H

#ifdef ARTEMIS

#include <tr1/unordered_map>

#include "symbolic/domtraversal.h"

#include "nativefunction.h"

namespace JSC {
    class ExecState;
    class JSValue;
}

namespace Symbolic
{

class NativeLookup : public DomTraversal
{

public:
    NativeLookup();

    const NativeFunction* find(JSC::native_function_ID_t functionID);
    void buildRegistry(JSC::ExecState* callFrame);

protected:
    bool domNodeTraversalCallback(JSC::CallFrame* callFrame, std::string path, JSC::JSValue jsValue);

private:
    typedef std::tr1::unordered_map<JSC::native_function_ID_t, NativeFunction> function_map_t;
    function_map_t m_nativeFunctionMap;
};

}

#endif
#endif // NATIVELOOKUP_H
