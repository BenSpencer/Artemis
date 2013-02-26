/*
 Copyright 2011 Simon Holm Jensen. All rights reserved.

 Redistribution and use in source and binary forms, with or without modification, are
 permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice, this list of
 conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice, this list
 of conditions and the following disclaimer in the documentation and/or other materials
 provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY SIMON HOLM JENSEN ``AS IS'' AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The views and conclusions contained in the software and documentation are those of the
 authors and should not be interpreted as representing official policies, either expressed
 or implied, of Simon Holm Jensen
 */

#include "executionresult.h"

using namespace std;

namespace artemis
{

ExecutionResult::ExecutionResult()
{
    mModifiedDom = false;
    mStateHash = 0;
}

QString ExecutionResult::getPageContents() const
{
    return mPageContents;
}

QList<int> ExecutionResult::getAjaxCallbackHandlers() const
{
    return mAjaxCallbackHandlers;
}

QSet<QSharedPointer<const FormField> > ExecutionResult::getFormFields() const
{
    return mFormFields;
}

QSet<QSharedPointer<AjaxRequest> > ExecutionResult::getAjaxRequests() const
{
    return mAjaxRequest;
}

QList<EventHandlerDescriptor*> ExecutionResult::getEventHandlers() const
{
    return mEventHandlers;
}

QSet<QString> ExecutionResult::getEvalStrings()
{
    return mEvaledStrings;
}

long ExecutionResult::getPageStateHash() const
{
    return mStateHash;

}

bool ExecutionResult::isDomModified() const
{
    return mModifiedDom;
}

QList<QSharedPointer<Timer> > ExecutionResult::getTimers() const
{
    return mTimers.values();
}

QSet<QString> ExecutionResult::getJavascriptConstantsObservedForLastEvent() const
{
    return mJavascriptConstantsObservedForLastEvent;
}

void ExecutionResult::addJavascriptConstantObservedForLastEvent(QString constant) {
    mJavascriptConstantsObservedForLastEvent.insert(constant);
}

QDebug operator<<(QDebug dbg, const ExecutionResult& e)
{
    dbg.nospace() << "Event handlers: " << e.mEventHandlers << "\n";
    dbg.nospace() << "Form fields   : " << e.mFormFields << "\n";
    dbg.nospace() << "Modfied dom   : " << e.mModifiedDom << "\n";
    dbg.nospace() << "Ajax requests : " << e.mAjaxRequest << "\n";
    dbg.nospace() << "Evaled strings: " << e.mEvaledStrings;

    return dbg.space();
}

}
