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
#ifndef RANDOMINPUTGENERATOR_H
#define RANDOMINPUTGENERATOR_H

#include <QList>
#include <QSharedPointer>

#include "runtime/input/dominput.h"

#include "targets/targetgenerator.h"
#include "form/forminputgenerator.h"
#include "event/eventparametergenerator.h"

#include "inputgeneratorstrategy.h"

namespace artemis
{
// TODO remove QOBJECT
class RandomInputGenerator : public InputGeneratorStrategy
{
    Q_OBJECT

public:
    RandomInputGenerator(QObject* parent,
                         FormInputGeneratorConstPtr formInputGenerator,
                         EventParameterGeneratorConstPtr eventParameterInputGenerator,
                         TargetGeneratorConstPtr targetGenerator,
                         int numberSameLength);

    QList<QSharedPointer<ExecutableConfiguration> > addNewConfigurations(QSharedPointer<const ExecutableConfiguration>, QSharedPointer<const ExecutionResult>);

private:
    FormInputGeneratorConstPtr mFormInputGenerator;
    EventParameterGeneratorConstPtr mEventParameterGenerator;

    int nextRandom();
    QList<ExecutableConfigurationPtr> insertSameLength(ExecutableConfigurationConstPtr e, ExecutionResultConstPtr result);
    QList<ExecutableConfigurationPtr> insertExtended(ExecutableConfigurationConstPtr e, ExecutionResultConstPtr result);

    TargetGeneratorConstPtr mTargetGenerator;

    int mNumberSameLength;

};

}
#endif // RANDOMINPUTGENERATOR_H
