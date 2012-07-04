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

#include "inputsequence.h"

namespace artemis
{

InputSequence::InputSequence(QObject* parent) :
    QObject(parent)
{
    mSequence.clear();
}

InputSequence::InputSequence(QObject* parent, const QList<BaseInput*>& sequencee) :
    QObject(parent)
{
    mSequence.clear();
    mSequence += sequencee;
}

void InputSequence::replaceLast(BaseInput* newLast)
{
    mSequence.removeLast();
    mSequence.append(newLast);
}

void InputSequence::extend(BaseInput* newLast)
{
    mSequence.append(newLast);
}

bool InputSequence::isEmpty() const
{
    return mSequence.empty();
}

BaseInput *InputSequence::getLast() const
{
    return mSequence.last();
}

const QList<BaseInput*> InputSequence::toList() const
{
    return mSequence;
}

InputSequence* InputSequence::copy() const
{
    return new InputSequence(parent(), mSequence);
}

}