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

#ifndef ARTEMISRUNTIME_H
#define ARTEMISRUNTIME_H

#include "runtime/worklist/worklist.h"

#include "runtime/runtime.h"
#include "concolic/entrypoints.h"
#include "runtime/browser/artemiswebview.h"

namespace artemis
{

/**
 * This is the main-loop used by artemis.
 *
 * startAnalysis -> preConcreteExecution -> postConcreteExecution -> finishAnalysis
 *                              ^------------------|
 */
class ArtemisRuntime : public Runtime
{
    Q_OBJECT

public:
    ArtemisRuntime(QObject* parent, const Options& options, const QUrl& url);

    void run(const QUrl& url);

protected:
    ArtemisWebViewPtr mWebView;

    void preConcreteExecution();

    WorkListPtr mWorklist;
    QMap<QUrl, QList<int>> mUrlsSeen;

    void notifyAboutNewIteration(ExecutableConfigurationConstPtr configuration);

private:
    int mIterations;

private slots:
    void postConcreteExecution(ExecutableConfigurationConstPtr configuration, QSharedPointer<ExecutionResult> result);
    void slPageLoaded(QUrl url);

};

}

#endif // ARTEMISRUNTIME_H
