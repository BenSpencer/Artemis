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

#include <JavaScriptCore/wtf/ExportMacros.h>

#include "JavaScriptCore/debugger/DebuggerCallFrame.h"
#include "JavaScriptCore/interpreter/Register.h"
#include "JavaScriptCore/runtime/JSObject.h"
#include "JavaScriptCore/runtime/JSValue.h"
#include "JavaScriptCore/runtime/Identifier.h"
#include "JavaScriptCore/heap/Heap.h"
#include "WebCore/xml/XMLHttpRequest.h"
#include "WebCore/xml/LazyXMLHttpRequest.h"
#include "WebCore/dom/ScriptExecutionContext.h"
#include "WebCore/page/DOMTimer.h"
#include "JavaScriptCore/parser/SourceCode.h"

#include <JavaScriptCore/parser/SourceProvider.h>

#include "qsourceregistry.h"

QSourceRegistry::QSourceRegistry()
{
}


QSource* QSourceRegistry::get(JSC::SourceProvider* sourceProvider)
{

    // Fast lookup (cache)
    // TODO

    // Normal lookup (hash lookup of sourceProvider memory location)
    // TODO

    // Slow lookup

    QString url = QString::fromStdString(sourceProvider->url().utf8().data());
    uint lineOffset = sourceProvider->startPosition().m_line.zeroBasedInt() + 1;

    uint key = qHash(new QPair<QString, uint>(url, lineOffset));

    QHash<uint, QSource*>::iterator iter = m_registry.find(key);

    if (iter == m_registry.end()) {
        QSource* source = new QSource(key, url, lineOffset);
        m_registry.insert(key, source);

        return source;
    }

    // We should not hit this case, a slow lookup of a known element
    return iter.value();
}
