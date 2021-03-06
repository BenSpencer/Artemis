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

#include <QString>
#include <QDebug>
#include <QTimer>
#include <QVariant>

#include <qjson/parser.h>

#include "util/loggingutil.h"
#include "util/delayutil.h"
#include "runtime/input/clicksimulator.h"
#include "concolic/executiontree/tracedisplay.h"
#include "concolic/executiontree/tracedisplayoverview.h"
#include "symbolic/directaccesssymbolicvalues.h"
#include "concolic/tracestatistics.h"

#include "analysisserverruntime.h"

namespace artemis
{

AnalysisServerRuntime::AnalysisServerRuntime(QObject* parent, const Options& options, const QUrl& url)
    : Runtime(parent, options, url)
    , mAnalysisServer(options.analysisServerPort, options.analysisServerLog)
    , mServerState(IDLE)
    , mIsPageLoaded(false)
    , mIsScheduledRedirection(false)
    , mFieldReadLog(mWebkitExecutor->getPage())
{
    // Connections to the server part
    QObject::connect(&mAnalysisServer, SIGNAL(sigExecuteCommand(CommandPtr)),
                     this, SLOT(slExecuteCommand(CommandPtr)));
    QObject::connect(this, SIGNAL(sigCommandFinished(QVariant)),
                     &mAnalysisServer, SLOT(slCommandFinished(QVariant)));
    QObject::connect(&mAnalysisServer, SIGNAL(sigResponseFinished()),
                     this, SLOT(slResponseFinished()));
    mAnalysisServer.logEntry(QString("    Called: %1").arg(mOptions.allArguments));

    // Connections to the browser part
    QObject::connect(mWebkitExecutor, SIGNAL(sigExecutedSequence(ExecutableConfigurationConstPtr, QSharedPointer<ExecutionResult>)),
                     this, SLOT(slExecutedSequence(ExecutableConfigurationConstPtr,QSharedPointer<ExecutionResult>)));

    QObject::connect(mWebkitExecutor->getPage().data(), SIGNAL(sigNavigationRequest(QWebFrame*,QNetworkRequest,QWebPage::NavigationType)),
                     this, SLOT(slNavigationRequest(QWebFrame*,QNetworkRequest,QWebPage::NavigationType)));

    QObject::connect(mWebkitExecutor->mWebkitListener, SIGNAL(sigPageLoadScheduled(QUrl)),
                     this, SLOT(slPageLoadScheduled(QUrl)));

    // Connections for page analysis
    QObject::connect(QWebExecutionListener::getListener(), SIGNAL(sigJavascriptSymbolicFieldRead(QString, bool)),
                     &mFieldReadLog, SLOT(slJavascriptSymbolicFieldRead(QString, bool)));

    // Browser setup
    // Set up a web view to give the page proper geometry.
    mWebView = ArtemisWebViewPtr(new ArtemisWebView());
    mWebView->setPage(mWebkitExecutor->getPage().data());
    setWindowSize(1200, 800);

    if (mOptions.analysisServerDebugView) {
        mWebView->show();
        mWebView->setEnabled(false);

        QObject::connect(mWebView.data(), SIGNAL(sigClose()),
                         this, SLOT(slDebugWindowClosed()));
    }

    // Handle async events directly.
    enableAsyncEventCapture();

    // Do not exit on a bad page load.
    mWebkitExecutor->mIgnoreCancelledPageLoad = true;

    // Concolic advice

    // The event detector for 'marker' events in the traces is created and connected here, as WebKitExecutor (where the rest are handled) does not know when these events happen.
    QSharedPointer<TraceMarkerDetector> markerDetector(new TraceMarkerDetector());
    QObject::connect(this, SIGNAL(sigNewTraceMarker(QString, QString, bool, SelectRestriction)),
                     markerDetector.data(), SLOT(slNewMarker(QString, QString, bool, SelectRestriction)));
    mWebkitExecutor->getTraceBuilder()->addDetector(markerDetector);
}

void AnalysisServerRuntime::run(const QUrl &url)
{
    concolicInit();

    Log::info("Analysis Server Runtime waiting for messages...");
}

void AnalysisServerRuntime::slExecuteCommand(CommandPtr command)
{
    Log::debug("  Analysis server runtime: recieved new command.");

    mCurrentCommand = command;

    // Recieved a new command from the AnalysisServer.
    // Execute it (calls back the appropriate execute() method).
    command->accept(this);
}

void AnalysisServerRuntime::execute(Command* command)
{
    Log::debug("  Analysis server runtime: executing a generic command (error).");
    assert(command);

    emit sigCommandFinished(errorResponse("Executing an unimplemented command."));
}

void AnalysisServerRuntime::execute(ExitCommand* command)
{
    Log::debug("  Analysis server runtime: executing an exit command.");
    assert(command);

    QVariantMap response;
    response.insert("message", "Server is shutting down");

    mServerState = EXIT;

    emit sigCommandFinished(response);
}

void AnalysisServerRuntime::execute(ErrorCommand* command)
{
    Log::debug("  Analysis server runtime: executing an error command.");
    assert(command);

    // This means there was some error in the parsing.
    // Just pass the error through and return it.
    emit sigCommandFinished(errorResponse(command->message));
}

void AnalysisServerRuntime::execute(EchoCommand* command)
{
    Log::debug("  Analysis server runtime: executing an echo command.");
    assert(command);

    QVariantMap response;
    response.insert("message", command->message);

    if (command->delay > 0) {
        DelayUtil::delay(command->delay * 1000);
    }

    emit sigCommandFinished(response);
}

void AnalysisServerRuntime::execute(PageLoadCommand* command)
{
    Log::debug("  Analysis server runtime: executing a pageload command.");
    assert(command);

    // Clear all cookies.
    // In the server mode the cookie jar will always be of type ResettableCookieJar (see options.saveCookiesForSession in artemis.cpp).
    QNetworkCookieJar* cookieJar = mWebkitExecutor->getCookieJar();
    ResettableCookieJar* resettableCookieJar = dynamic_cast<ResettableCookieJar*>(cookieJar);
    if (resettableCookieJar) {
        resettableCookieJar->reset();
    }

    mIsPageLoaded = false;

    // The whole load process is limited by the timeout field.
    if (command->timeout > 0) {
        QTimer::singleShot(command->timeout, this, SLOT(slLoadTimeoutTriggered()));
    }

    // WebkitExecutor uses the contents of the page to check for a successful load or not.
    // Therefore this check fails if we are already on a non-blank page.
    // So the first step of a page load is to load "about:blank", and then load the "real" URL.

    mServerState = PAGELOAD_BLANK;
    loadUrl(QUrl("about:blank")); // Calls back to slExecutedSequence.
}

void AnalysisServerRuntime::execute(HandlersCommand *command)
{
    Log::debug("  Analysis server runtime: executing a handlers command.");
    assert(command);

    // Retrieve the list of handlers from the saved response from the previous page load.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Handlers cannot be listed until a page is loaded."));
        return;
    }

    QList<EventHandlerDescriptorConstPtr> handlerList = mWebkitExecutor->getCurrentEventHandlers();

    // Check if a filter was used.
    if (!command->filter.isNull()) {
        QList<QWebElement> matches = mWebkitExecutor->getPage()->getElementsByXPath(command->filter).toList();
        if (matches.count() < 1) {
            emit sigCommandFinished(errorResponse("No matches were found for the filter."));
            return;
        }

        QList<EventHandlerDescriptorConstPtr> specifiedElementHandlers;
        foreach (EventHandlerDescriptorConstPtr handler, handlerList) {
            QWebElement handlerElt = handler->getDomElement()->getElement(mWebkitExecutor->getPage());
            if (matches.contains(handlerElt)) {
                specifiedElementHandlers.append(handler);
            }
        }

        // Replace the handlers list with the filtered version and continue.
        handlerList = specifiedElementHandlers;
    }

    // Group the handlers by element.
    // It is safe to use the xpaths as keys because we generate them ourselves, so they are canonical and we cannot
    // have two different xpaths referring to the same element.
    QMap<QString, QList<QVariant> > elementHandlers;
    foreach (EventHandlerDescriptorConstPtr handler, handlerList) {
        elementHandlers[handler->xPathOrTargetObject()].append(handler->getName());
    }

    QVariantList resultList;
    foreach (QString identifier, elementHandlers.keys()) {
        QVariantMap handlerObject;
        handlerObject.insert("element", identifier);
        handlerObject.insert("events", elementHandlers[identifier]);
        resultList.append(handlerObject);
    }

    QVariantMap result;
    result.insert("handlers", resultList);

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(ClickCommand *command)
{
    Log::debug("  Analysis server runtime: executing a click command.");
    assert(command);

    // Check we have loaded a page already.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Cannot execute click command until a page is loaded."));
        return;
    }

    // Look up the element
    QWebElement target = mWebkitExecutor->getPage()->getSingleElementByXPath(command->xPath);

    if (target.isNull()) {
        emit sigCommandFinished(errorResponse("Click target could not be found. The XPath either did not match or matched multiple elements."));
        return;
    }

    // Add this event to the fields-read log.
    QString eventDescription = QString("click/%1").arg(command->methodStr);
    notifyStartingEvent(eventDescription, target.xPath()); // Use a canonical XPath.

    // Execute the click.
    switch (command->method) {
    case ClickCommand::Simple:
        ClickSimulator::clickByEvent(target);
        clearAsyncEvents();
        break;

    case ClickCommand::SimulateJS:
        ClickSimulator::clickByUserEventSimulation(target);
        clearAsyncEvents();
        break;

    case ClickCommand::SimulateGUI:
        ClickSimulator::clickByGuiSimulation(target, mWebkitExecutor->getPage());
        clearAsyncEvents();
        break;

    default:
        emit sigCommandFinished(errorResponse("Unexpected simulation method for 'click' command."));
        return;
        break;
    }

    QVariantMap result;
    result.insert("click", "done");

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(PageStateCommand *command)
{
    Log::debug("  Analysis server runtime: executing a Page-State command.");
    assert(command);

    // Check we have loaded a page already.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Cannot execute page command until a page is loaded."));
        return;
    }

    QString url = mWebkitExecutor->getPage()->mainFrame()->url().toString();
    QString title = mWebkitExecutor->getPage()->mainFrame()->title();
    QString dom = mWebkitExecutor->getPage()->mainFrame()->toHtml();
    uint domEltCount = mWebkitExecutor->getPage()->mainFrame()->documentElement().findAll("*").count();
    uint domCharCount = dom.length();

    QVariantMap result;
    result.insert("url", url);
    result.insert("title", title);
    if (command->includeDom) {
        result.insert("dom", dom);
    }
    result.insert("elements", domEltCount);
    result.insert("characters", domCharCount);

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(ElementCommand *command)
{
    Log::debug("  Analysis server runtime: executing an element info command.");
    assert(command);

    // Check we have loaded a page already.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Cannot execute element command until a page is loaded."));
        return;
    }

    // Look up the element(s).
    QWebElement document = mWebkitExecutor->getPage()->currentFrame()->documentElement();
    QString escapedXPath = command->xPath;
    escapedXPath.replace('"', "\\\"");

    QString property = command->property;
    if (property.isNull()) {
        property = "outerHTML"; // By default if no property is specified, return the element's string representation.
    }
    property.replace('"', "\\\""); // Should never be used, but prevents it being completely broken in case they specify a weird property.

    QString queryJS = QString("var elems = document.evaluate(\"%1\", document, null, XPathResult.UNORDERED_NODE_SNAPSHOT_TYPE, null); var elStrs = []; for(var i=0; i < elems.snapshotLength; i++) {elStrs.push(elems.snapshotItem(i)[\"%2\"])}; elStrs;").arg(escapedXPath, property);

    QVariant elemList = document.evaluateJavaScript(queryJS, QUrl(), true);

    if(!elemList.isValid()) {
        emit sigCommandFinished(errorResponse("The XPath could not be evaluated. Is it valid?."));
        return;
    }

    QVariantMap result;
    result.insert("elements", elemList);

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(FieldsReadCommand *command)
{
    Log::debug("  Analysis server runtime: executing a Fields-Read command.");
    assert(command);

    // Check we have loaded a page already.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Cannot execute fieldsread command until a page is loaded."));
        return;
    }

    QVariantMap result;
    result.insert("fieldsread", mFieldReadLog.getLog());

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(BackButtonCommand *command)
{
    Log::debug("  Analysis server runtime: executing a Back-button command.");
    assert(command);

    // Check we have loaded a page already.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Cannot execute backbutton command until a page is loaded."));
        return;
    }

    backButtonOrError(); // Calls back to slExecutedSequence or finishes the request itself.
}

void AnalysisServerRuntime::execute(FormInputCommand *command)
{
    Log::debug("  Analysis server runtime: executing a Form-input command.");
    assert(command);

    // Check we have loaded a page already.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Cannot execute forminput command until a page is loaded."));
        return;
    }

    // Look up the element.
    QWebElement field = mWebkitExecutor->getPage()->getSingleElementByXPath(command->field);
    if (field.isNull()) {
        emit sigCommandFinished(errorResponse("Form-input field could not be found. The XPath either did not match or matched multiple elements."));
        return;
    }

    // Sanity check that this element is a form field and the injection value is an appropriate type.
    // Some of these checks are repeated in FormFieldInjector::inject, but doing them here allows us to give sensible
    // error messages, which is useful seeing as this command has quite specific requirements.
    Log::debug(QString("    Checking field \"%1\" can accept value %2").arg(field.tagName(), command->value.toString()).toStdString());
    if (field.tagName().toLower() == "input") {
        // For input fields, the allowable value type depends on the inupt type.
        // "radio" and "checkbox" inputs must have bool values, and all other input types must use "string".
        QString inputType = field.attribute("type").toLower();
        if (inputType == "checkbox" || inputType == "radio") {
            if (command->value.getType() != QVariant::Bool) {
                emit sigCommandFinished(errorResponse(QString("Only boolean values can be injected into inputs with 'checkbox' or 'radio' type.")));
                return;
            }
        } else if (command->value.getType() != QVariant::String) {
            emit sigCommandFinished(errorResponse(QString("Only string values can be injected into normal input fields.")));
            return;
        }
    } else if (field.tagName().toLower() == "select") {
        // For select boxes we support injection of string or int (as selectedIndex) but not bool.
        if (command->value.getType() != QVariant::String && command->value.getType() != QVariant::Int) {
            emit sigCommandFinished(errorResponse(QString("Could not inject '%1' into a select box. Only strings and integers (for selectedIndex) are supported.").arg(command->value.toString())));
            return;
        }
    } else {
        emit sigCommandFinished(errorResponse(QString("Could not inject into element '%1'; only 'input' or 'select' supported.").arg(field.tagName())));
        return;
    }

    // Add this event to the fields-read log.
    QString eventDescription = QString("forminput/%1").arg(command->methodStr);
    notifyStartingEvent(eventDescription, field.xPath()); // Use a canonical XPath.

    bool couldInject;

    // Inject
    switch (command->method) {
    case FormInputCommand::Inject:
        couldInject = FormFieldInjector::inject(field, command->value);
        clearAsyncEvents();
        break;

    case FormInputCommand::OnChange:
        couldInject = FormFieldInjector::injectAndTriggerChangeHandler(field, command->value);
        clearAsyncEvents();
        break;

    case FormInputCommand::SimulateJS:
        couldInject = FormFieldInjector::injectWithEventSimulation(field, command->value, command->noBlur);
        clearAsyncEvents();
        break;

    case FormInputCommand::SimulateGUI:
        emit sigCommandFinished(errorResponse("Simulation of input by GUI interaction is not yet supported."));
        return;
        break;

    default:
        emit sigCommandFinished(errorResponse("Unexpected simulation method for 'forminput' command."));
        return;
        break;
    }

    if (!couldInject) {
        // Hopefully all these cases will already be caught by the sanity checks code above...
        emit sigCommandFinished(errorResponse(QString("Failed to inject value '%1'' into field '%2'.").arg(command->value.toString(), command->field)));
        return;
    }

    // Done, nothing to return.
    QVariantMap result;
    result.insert("forminput", "done");

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(XPathCommand *command)
{
    Log::debug("  Analysis server runtime: executing an XPath evaluation command.");
    assert(command);

    // Check we have loaded a page already.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Cannot execute xpath command until a page is loaded."));
        return;
    }

    // Look up the element(s).
    QWebElement document = mWebkitExecutor->getPage()->currentFrame()->documentElement();

    QVariantList resultValueList;
    foreach (QString xPath, command->xPaths) {
        xPath.replace('"', "\\\"");

        QString evaluationJS = QString("var artemis_xpr = document.evaluate(\"%1\", document, null, XPathResult.ANY_TYPE, null);"
                                       "var artemis_result;"
                                       "if (artemis_xpr.resultType == XPathResult.NUMBER_TYPE) {"
                                       "  artemis_result = artemis_xpr.numberValue;"
                                       "} else if (artemis_xpr.resultType == XPathResult.STRING_TYPE) {"
                                       "  artemis_result = artemis_xpr.stringValue;"
                                       "} else if (artemis_xpr.resultType == XPathResult.BOOLEAN_TYPE) {"
                                       "  artemis_result = artemis_xpr.booleanValue;"
                                       "} else {"
                                       "  artemis_result = [];"
                                       "  var artemis_elt;"
                                       "  while (artemis_elt = artemis_xpr.iterateNext()) {"
                                       "    artemis_result.push(artemis_elt.outerHTML);"
                                       "  }"
                                       "};"
                                       "artemis_result;").arg(xPath);

        QVariant resultValue = document.evaluateJavaScript(evaluationJS, QUrl(), true);

        if (resultValue.isNull()) {
            emit sigCommandFinished(errorResponse("The given XPath could not be evaluated."));
            return;
        }

        // The resulting value is already in the expected response type.
        // It can be a string, number, bool, or array of strings of elements (for a node-list).
        resultValueList.append(resultValue);
    }

    // Format the result differently depending on command->formatSingleton, which shows the format of the original command (list or single query).
    QVariantMap result;
    if (command->formatSingleton) {
        assert(resultValueList.size() == 1);
        result.insert("result", resultValueList.first());
    } else {
        result.insert("result", resultValueList);
    }

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(EventTriggerCommand *command)
{
    Log::debug("  Analysis server runtime: executing an event-triggering command.");
    assert(command);

    // Check we have loaded a page already.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Cannot execute event command until a page is loaded."));
        return;
    }

    // Look up the element.
    QWebElement target = mWebkitExecutor->getPage()->getSingleElementByXPath(command->xPath);
    if (target.isNull()) {
        emit sigCommandFinished(errorResponse("Target element could not be found. The XPath either did not match or matched multiple elements."));
        return;
    }

    // Add this event to the fields-read log.
    QString eventDescription = QString("event/%1").arg(command->event);
    notifyStartingEvent(eventDescription, target.xPath()); // Use a canonical XPath.

    // Check if this is a javaScript event or a custom event (with the prefix ARTEMIS-*).
    if (!command->event.startsWith("ARTEMIS-")) {
        // Standard JavaScript event - Build and trigger the event.
        FormFieldInjector::triggerHandler(target, command->event);
        clearAsyncEvents();
    } else {
        // Cusom event - check which type and dispatch.
        QString event = command->event;
        event.remove(0, 8);

        if (event == "press-enter") {
            FormFieldInjector::guiPressEnter(target);
            clearAsyncEvents();
        } else {
            emit sigCommandFinished(errorResponse("Custom event type was not recognised."));
            return;
        }
    }

    // No result, just return.
    QVariantMap result;
    result.insert("event", "done");

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(WindowSizeCommand *command)
{
    Log::debug("  Analysis server runtime: executing a window-size command.");
    assert(command);

    setWindowSize(command->width, command->height);
    clearAsyncEvents(); // TODO: Not tested when resize events get handled. This may not capture them.

    QVariantMap result;
    result.insert("windowsize", "done");

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(ConcolicAdviceCommand* command)
{
    Log::debug("  Analysis server runtime: executing a concolic-advice command.");
    assert(command);

    // Check we have loaded a page already.
    if (!mIsPageLoaded) {
        emit sigCommandFinished(errorResponse("Cannot execute concolicadvice command until a page is loaded."));
        return;
    }

    QVariant result;
    switch (command->action) {
    case ConcolicAdviceCommand::BeginTrace:
        result = concolicBeginTrace(command->sequence, command->implicitEndTrace);
        break;
    case ConcolicAdviceCommand::EndTrace:
        result = concolicEndTrace(command->sequence);
        break;
    case ConcolicAdviceCommand::Advice:
        result = concolicAdvice(command->sequence, command->amount, command->allowDuringTrace);
        break;
    case ConcolicAdviceCommand::Statistics:
        result = concolicStatistics(command->sequence);
        break;
    default:
        emit sigCommandFinished(errorResponse("Unexpected action in concolicadvice command."));
        return;
    }

    emit sigCommandFinished(result);
}


void AnalysisServerRuntime::execute(EvaluateJsCommand *command)
{
    Log::debug("  Analysis server runtime: executing an evaluate-js command.");
    assert(command);
    // N.B. It is optional whether we have loaded a page already.

    mWebView->page()->currentFrame()->documentElement().evaluateJavaScript(command->jsString, QUrl(), false);
    // TODO: Maybe we should return the result in the response?

    clearAsyncEvents();

    QVariantMap result;
    result.insert("evaluatejs", "done");

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(SetSymbolicValuesCommand *command)
{
    Log::debug("  Analysis server runtime: executing an evaluate-js command.");
    assert(command);
    // N.B. It is optional whether we have loaded a page already.

    // Similar injection code in ConcolicStandaloneRuntime::doneConcolicIteration().
    Symbolic::DirectAccessSymbolicValues* symValueStore = Symbolic::get_direct_access_symbolic_values_store();
    if (command->reset) {
        symValueStore->reset();
    }

    foreach (QString variable, command->values.keys()) {
        QVariant value = command->values[variable];

        switch (value.type()) {
        case QVariant::String:
            symValueStore->setString(variable, value.toString());
            break;
        case QVariant::Int:
        case QVariant::UInt:
        case QVariant::LongLong:
        case QVariant::ULongLong:
            symValueStore->setInteger(variable, value.toInt());
            break;
        case QVariant::Bool:
            symValueStore->setBoolean(variable, value.toBool());
            break;
        default:
            // NOTREACHED - This is ensured by RequestHandler::setSymbolicValuesCommand().
            emit sigCommandFinished(errorResponse("Unexpected value type found in setsymbolicvalue command."));
            return;
        }
    }

    QVariantMap result;
    result.insert("setsymbolicvalues", "done");

    emit sigCommandFinished(result);
}

void AnalysisServerRuntime::execute(CoverageCommand *command)
{
    Log::debug("  Analysis server runtime: executing a coverage command.");
    assert(command);
    // N.B. It is optional whether we have loaded a page already.

    QVariantList coverage_info;

    // Grab the coverage info source-by-source and add it to the result.
    // Adapted from covergaetooutputstream.cpp writeCoverageStdout()
    CoverageListenerPtr coverage = mAppmodel->getCoverageListener();
    foreach(int sourceID, coverage->getSourceIDs()) {
        QVariantMap source_coverage_info;

        const SourceInfoPtr sourceInfo = coverage->getSourceInfo(sourceID);
        source_coverage_info["url"] = sourceInfo->getURL();
        source_coverage_info["line"] = sourceInfo->getStartLine();

        QString src = sourceInfo->getSource();
        QTextStream read(&src);

        QSet<uint> lineCoverage = sourceInfo->getLineCoverage();

        QList<uint> sortedLines = QList<uint>::fromSet(lineCoverage);
        qSort(sortedLines);
        QVariantList sortedLines_qv;
        foreach (uint val, sortedLines) {
            sortedLines_qv.append(val);
        }
        source_coverage_info["linescovered"] = sortedLines_qv;

        int lineNumber = sourceInfo->getStartLine();
        QString source_coverage_report;
        while (!read.atEnd()) {
            QString prefix = lineCoverage.contains(lineNumber) ? ">>>" : "   ";
            source_coverage_report += prefix + read.readLine() + "\n";
            lineNumber++;
        }
        source_coverage_info["coverage"] = source_coverage_report;

        coverage_info.append(source_coverage_info);
    }

    QVariantMap result;
    result.insert("coverage", coverage_info);
    emit sigCommandFinished(result);

}


QVariant AnalysisServerRuntime::errorResponse(QString message)
{
    QVariantMap response;
    response.insert("error", message);
    return response;
}

void AnalysisServerRuntime::emitTimeout()
{
    emit sigCommandFinished(errorResponse("Timed out."));
}


// Called once the response from an execute() method has been sent.
void AnalysisServerRuntime::slResponseFinished()
{
    Log::debug("  Analysis server runtime: Response is complete.");
    if (mServerState == EXIT) {
        // Hack: Wait a little so the response can be sent out (non-blocking on networking).
        DelayUtil::delay(1000);
        done();
    }

    mServerState = IDLE;
    mCurrentCommand.clear();
}


void AnalysisServerRuntime::loadUrl(QUrl url)
{
    ExecutableConfigurationPtr noInput = ExecutableConfigurationPtr(new ExecutableConfiguration(InputSequencePtr(new InputSequence()), url));
    mWebkitExecutor->executeSequence(noInput, MODE_CONCOLIC_NO_TRACE); // Calls slExecutedSequence method as callback.
}

// Use the back button.
// Either calls back to slExecutedSequence or emits the commandFinished signal with an error response.
void AnalysisServerRuntime::backButtonOrError()
{
    // Check if there is any history to go back to.
    if (mWebkitExecutor->getPage()->history()->canGoBack()) {
        mServerState = PAGELOAD_BACK_BUTTON;
        mWebkitExecutor->getPage()->history()->back(); // Calls back to slExecutedSequence.

    } else {
        emit sigCommandFinished(errorResponse("No more history to go back through."));
    }
}


void AnalysisServerRuntime::slExecutedSequence(ExecutableConfigurationConstPtr configuration, QSharedPointer<ExecutionResult> result)
{
    PageLoadCommandPtr loadCommand; // PAGELOAD_BLANK
    QVariantMap response; // PAGELOAD
    QString url; // PAGELOAD_BACK_BUTTON

    switch (mServerState) {
    case PAGELOAD_BLANK:
        // We are part way through the page-load process. Now we are ready to load the target URL.
        loadCommand = mCurrentCommand.dynamicCast<PageLoadCommand>();
        assert(loadCommand);

        mServerState = PAGELOAD;
        mIsScheduledRedirection = false;

        mFieldReadLog.clear();
        notifyStartingEvent("pageload", "");

        loadUrl(loadCommand->url); // Calls back to slExecutedSequence again.
        break;

    case PAGELOAD:
        // Successfully finished loading the real URL.

        // Check for any redirection we detected.
        if (mIsScheduledRedirection) {
            // Wait for the redirected page load to come in...
            mServerState = PAGELOAD_WAITING_REDIRECT;

        } else {
            // Send a response and finish the PAGELOAD command.
            mIsPageLoaded = true;
            clearAsyncEvents();

            response.insert("pageload", "done");
            response.insert("url", mWebkitExecutor->getPage()->currentFrame()->url().toString());

            emit sigCommandFinished(response);
        }
        break;

    case PAGELOAD_TIMEOUT:
        emitTimeout();
        // N.B. Because we do not call clearAsyncEvents() here there may be execution continuing in the background
        // after a timeout occurs. This is expected/OK as the page is still loading when we return anyway.
        break;

    case PAGELOAD_WAITING_REDIRECT:
        // Do not check for more redirects here to avoid going into a loop.

        clearAsyncEvents();
        // Send a response and finish the PAGELOAD command.
        response.insert("pageload", "done");
        response.insert("url", mWebkitExecutor->getPage()->currentFrame()->url().toString());

        emit sigCommandFinished(response);

        break;

    case PAGELOAD_BACK_BUTTON:
        // Do not support redirection when using the back button.

        // If we have reached "about:blank" then this is one of the intermediate loads between pageload commands.
        // In that case issue another back command to mask this.
        // TODO: This is a bit of a hack to pretend we do not do these intermediate loads.
        // It could be tripped up if someone intentionally loads "about:blank" and tries to go back to it.
        // It also causes some odd behaviour when going back from the initial page load (the result is "about:blank"
        // loaded and an error response).

        url = mWebkitExecutor->getPage()->currentFrame()->url().toString();
        if (url == "about:blank") {
            backButtonOrError(); // Calls back to slExecutedSequence or finishes the request itself.

        } else {
            clearAsyncEvents();

            // Send a response and finish the backbutton command.
            response.insert("backbutton", "done");
            response.insert("url", url);

            emit sigCommandFinished(response);
        }

        break;

    case IDLE:
        // This is an unexpected page load.
        // It could be caused by a page redirect.
        // Other navigation (e.g. clicking links) is supposed to be handled via slNavigationRequest and loadUrl.

        // There is nothing to do (as the server already thought the page loading was complete), so silently proceed.
        // TODO: If there was any per-page analysis state, it should be reset here as well. All we have so far is the
        // mFieldReadLog, which we want to start logging from the initial call to pageload, so this is not reset.

        Log::error("Unexpected page load in AnalysisServerRuntime.");

        clearAsyncEvents(); // Even though the load is unexpected, we don't want it to pollute the page with new events.

        break;

    case EXIT: // Fall-through
    default:
        // We do not expect any other server states to be executing event sequences.
        Log::fatal("Error: Executed an event sequence from a bad state in AnalysisServerRuntime.");
        exit(1);
        break;
    }

    concolicInitPage(result);
}

void AnalysisServerRuntime::slLoadTimeoutTriggered()
{
    mServerState = PAGELOAD_TIMEOUT;
    mWebkitExecutor->getPage()->triggerAction(QWebPage::Stop);
    // slExecutedSequence will now be called.
}

// Called when the ArtemisWebPage receives a request for navigation.
// This means there has been a page load we did not initiate (e.g. URL click, form submission, etc.).
// So we need to notify WebKitExecutor that we are starting a new trace even though we didn't call executeSequence().
void AnalysisServerRuntime::slNavigationRequest(QWebFrame *frame, const QNetworkRequest &request, QWebPage::NavigationType type)
{
    mWebkitExecutor->notifyNewSequence(true);
}

// Called when a page load is scheduled.
// This is used to detect meta-refresh redirection during a page load command.
void AnalysisServerRuntime::slPageLoadScheduled(QUrl url)
{
    mIsScheduledRedirection = true;
}

// Overrides Runtime::slAbortedExecution, called from WebkitExecutor when there is a problem with executing a sequence.
void AnalysisServerRuntime::slAbortedExecution(QString reason)
{
    if (mServerState == PAGELOAD ||
            mServerState == PAGELOAD_BLANK ||
            mServerState == PAGELOAD_WAITING_REDIRECT ||
            mServerState == PAGELOAD_BACK_BUTTON) {
        // There was an error loading this page.
        emit sigCommandFinished(errorResponse("Could not load the URL."));

    } else if (mServerState == PAGELOAD_TIMEOUT) {
        emitTimeout();

    } else {
        Runtime::slAbortedExecution(reason);
    }
}

void AnalysisServerRuntime::slDebugWindowClosed()
{
    Log::debug("Debug window closed... Exiting immediately.");
    done();
}


void AnalysisServerRuntime::setWindowSize(int width, int height)
{
    // Do not allow resizing.
    mWebView->setMinimumSize(width, height);
    mWebView->setMaximumSize(width, height);
    mWebView->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Resize, even if hidden.
    mWebView->forceResize(width, height);
}

// Called from run() to prepare the page for the analysis.
void AnalysisServerRuntime::concolicInit()
{
    mConcolicSequenceRecording.clear();
    mConcolicTrees.clear();
    mConcolicFormFields.clear();
    mConcolicTreeOutputFileNames.clear();
    mConcolicTreeOutputFileNameCounter = 1;
}

// Called on each new page load to do any preparation for analysis on that page.
void AnalysisServerRuntime::concolicInitPage(QSharedPointer<ExecutionResult> result)
{
    mConcolicFormFieldsForPage = result->getFormFields();

    // Set all form fields symbolic.
    // N.B. Doing this instead of triggering in FormFieldInjector only (as in concolic mode) is unsafe due to the
    // test-before-inject issue. It allows constraints to be generated on values which have not yet been injected.
    // This means injecting a solution to these solved constraints will not take the expected branch, which should
    // really have been concrete not symbolic to begin with.
    // However in the server mode we do this to avoid requiring a reset between each recorded trace.

    foreach (FormFieldDescriptorConstPtr element, mConcolicFormFieldsForPage) {
        element->getDomElement()->getElement(mWebkitExecutor->getPage()).evaluateJavaScript("this.symbolictrigger == \"\";", QUrl(), true);
        element->getDomElement()->getElement(mWebkitExecutor->getPage()).evaluateJavaScript("this.options.symbolictrigger == \"\";", QUrl(), true);
    }
}

QVariant AnalysisServerRuntime::concolicBeginTrace(QString sequence, bool implicitEndTrace)
{
    if (sequence.isEmpty()) {
        return errorResponse("Tried to begin recording with an invalid sequence identifier.");
    }
    if (!mConcolicSequenceRecording.isEmpty()) {
        if (implicitEndTrace) {
            Log::info("Tried to begin recording with an invalid sequence identifier. (Allowed by 'implicitendtrace' flag.)");
            concolicEndTrace(mConcolicSequenceRecording); // Ignore result.
        } else {
            return errorResponse("Tried to begin recording a trace while another was already in progress.");
        }
    }

    // If this is the first trace in a new tree, create one.
    if (!mConcolicTrees.contains(sequence)) {
        concolicCreateNewAnalysis(sequence);
    }

    mWebkitExecutor->getTraceBuilder()->beginRecording();

    mConcolicSequenceRecording = sequence;
    mConcolicTraceMarkerIdx = 0;
    return concolicResponseOk();
}

QVariant AnalysisServerRuntime::concolicEndTrace(QString sequence)
{
    if (sequence.isEmpty() || sequence != mConcolicSequenceRecording) {
        return errorResponse("Tried to end recording of a different trace than the one currently in progress.");
    }
    assert(mConcolicTrees.contains(sequence));

    // Merge the new trace into the existing tree.
    mWebkitExecutor->getTraceBuilder()->endRecording();
    TraceNodePtr trace = mWebkitExecutor->getTraceBuilder()->trace();
    // N.B. We do not classify the traces in server mode.

    // TODO: For now we pass in no exploration target. Ideally we would keep the exploration target returned from
    // nextExploration() so the analysis knows which traces belong to which exploration attempts.
    // This would mean keeping a table of them here and returning an ExplorationHandle-index from concolicAdvice
    // which could be passed back in calls to concolicBeginTrace [or end].
    mConcolicTrees[sequence]->addTrace(trace, ConcolicAnalysis::NO_EXPLORATION_TARGET);

    mConcolicSequenceRecording.clear();
    return concolicResponseOk();
}

QVariant AnalysisServerRuntime::concolicAdvice(QString sequence, uint amount, bool allowDuringTrace)
{
    if (sequence.isEmpty() || !mConcolicTrees.contains(sequence)) {
        return errorResponse("Tried to get concolic advice for a non-existent sequence ID.");
    }
    if (!mConcolicSequenceRecording.isEmpty()) {
        if (allowDuringTrace) {
            Log::info("Tried to get concolic advice while a trace was recording. (Allowed by 'allowduringtrace' flag.)");
        } else {
            return errorResponse("Tried to get concolic advice while a trace was recording.");
        }
    }

    // Get the tree form mConcolicTrees and check for new advice.
    ConcolicAnalysisPtr analysis = mConcolicTrees[sequence];
    QVariantList suggestions;

    uint i = 0;
    while (amount == 0 || i < amount) {
        ConcolicAnalysis::ExplorationResult exploration = analysis->nextExploration();
        if (!exploration.newExploration) {
            break;
        }

        assert(exploration.solution->isSolved());
        QVariantList assignment;

        foreach (QString symbol, exploration.solution->symbols()) {
            // We need to convert the variable names to elements and then to XPath expressions for the API.
            QString xPath = concolicSymbolToXPath(sequence, symbol);

            // Decode the value into a single QVariant.
            QVariant value;
            Symbolvalue internalValue = exploration.solution->findSymbol(symbol);
            switch (internalValue.kind) {
            case Symbolic::INT:
                value = QVariant(internalValue.u.integer);
                break;
            case Symbolic::BOOL:
                value = QVariant(internalValue.u.boolean);
                break;
            case Symbolic::STRING:
                value = QVariant(QString::fromStdString(internalValue.string));
                break;
            default:
                Log::fatal(QString("Unimplemented value type encountered for variable %1 (%2)").arg(symbol).arg(internalValue.kind).toStdString());
                exit(1);
            }

            QVariantMap singleAssignment;
            singleAssignment.insert("field", xPath);
            singleAssignment.insert("value", value);

            assignment.append(singleAssignment);

            // TODO: Really we should save exploration.target as well. See comments in concolicEndTrace.
        }

        suggestions.insert(suggestions.size(), assignment);

        i++;
    }

    // Some nodes will have been updated to "queued" status, so update the tree output.
    if (suggestions.length() > 0) {
        concolicOutputTree(analysis->getExecutionTree(), sequence);
    }

    QVariantMap result;
    result.insert("concolicadvice", "done");
    result.insert("sequence", sequence);
    result.insert("values", suggestions);
    return result;
}

QVariant AnalysisServerRuntime::concolicStatistics(QString sequence)
{
    if (sequence.isEmpty() || !mConcolicTrees.contains(sequence)) {
        return errorResponse("Tried to get concolic statistics for a non-existent sequence ID.");
    }

    TraceNodePtr tree = mConcolicTrees[sequence]->getExecutionTree();
    TraceStatistics stats;
    stats.processTrace(tree);

    QVariantMap result_stats;
    result_stats.insert("ConcreteBranchesTotal", stats.mNumConcreteBranches);
    result_stats.insert("ConcreteBranchesFullyExplored", stats.mNumConcreteBranchesFullyExplored);
    result_stats.insert("SymbolicBranchesTotal", stats.mNumSymBranches);
    result_stats.insert("SymbolicBranchesFullyExplored", stats.mNumSymBranchesFullyExplored);
    result_stats.insert("Alerts", stats.mNumAlerts);
    result_stats.insert("ConsoleMessages", stats.mNumConsoleMessages);
    result_stats.insert("PageLoads", stats.mNumPageLoads);
    result_stats.insert("InterestingDomModifications", stats.mNumInterestingDomModifications);
    result_stats.insert("EndSuccess", stats.mNumEndSuccess); // Can't occur in server mode.
    result_stats.insert("EndFailure", stats.mNumEndFailure); // Can't occur in server mode.
    result_stats.insert("EndUnknown", stats.mNumEndUnknown);
    result_stats.insert("Unexplored", stats.mNumUnexplored);
    result_stats.insert("UnexploredSymbolicChild", stats.mNumUnexploredSymbolicChild);
    result_stats.insert("Unsat", stats.mNumUnexploredUnsat);
    result_stats.insert("Missed", stats.mNumUnexploredMissed); // Can't occur in server mode.
    result_stats.insert("CouldNotSolve", stats.mNumUnexploredUnsolvable);
    result_stats.insert("SymbolicBranchesTotal", stats.mNumEventSequenceSymBranches);
    result_stats.insert("SymbolicBranchesFullyExplored", stats.mNumEventSequenceSymBranchesFullyExplored);
    result_stats.insert("TracesRecordedInTree", stats.mNumEndUnknown + stats.mNumEndSuccess + stats.mNumEndFailure); // TODO: I think this should be the same a DistinctTracesExplored in the main concolic runtime. In server mode it will be the same as TraceEndUnknown, as we do not use the classifier.
    result_stats.insert("Queued", stats.mNumUnexploredQueued);
    result_stats.insert("TraceDivergenceNodes", stats.mNumDivergenceNodes);
    result_stats.insert("DivergentTraces", stats.mNumDivergentTraces);

    QVariantMap result;
    result.insert("concolicadvice", "done");
    result.insert("sequence", sequence);
    result.insert("statistics", result_stats);
    return result;
}

QString AnalysisServerRuntime::concolicSymbolToXPath(QString sequence, QString symbol)
{
    // TODO: Adapted from ConcolicRuntime::findFormFieldForVariable. The common parts should be merged.

    QString varBaseName = symbol;
    varBaseName.remove(QRegExp("^SYM_IN_(INT_|BOOL_)?"));

    // Assuming the symbolic source identifier method is always ELEMENT_ID.
    // Assuming the fields are all in mConcolicFormFields and have not been added dynamically since page load.
    QSharedPointer<const FormFieldDescriptor> sourceField;
    foreach(QSharedPointer<const FormFieldDescriptor> field, mConcolicFormFields[sequence]){
        if(field->getDomElement()->getId() == varBaseName){
            sourceField = field;
            break;
        }
    }
    if(sourceField.isNull()) {
        Log::error(QString("Error: Could not identify a form field for %1.").arg(symbol).toStdString());
        return "";
    }

    return sourceField->getDomElement()->getXPath();
}

QVariant AnalysisServerRuntime::concolicResponseOk()
{
    QVariantMap result;
    result.insert("concolicadvice", "done");
    return result;
}

void AnalysisServerRuntime::concolicCreateNewAnalysis(QString sequence)
{
    assert(!mConcolicTrees.contains(sequence));

    ConcolicAnalysisPtr newAnalysis = ConcolicAnalysisPtr(new ConcolicAnalysis(mOptions, ConcolicAnalysis::QUIET));
    mConcolicTrees.insert(sequence, newAnalysis);

    // Get notifications of tree modifications so we can output the trees.
    QObject::connect(newAnalysis.data(), SIGNAL(sigExecutionTreeUpdated(TraceNodePtr, QString)),
                     this, SLOT(slExecutionTreeUpdated(TraceNodePtr, QString)));
    newAnalysis->setName(sequence);

    // Set the "base" form restrictions.
    FormRestrictions base = FormFieldRestrictedValues::getRestrictions(mConcolicFormFieldsForPage, mWebkitExecutor->getPage());
    mConcolicTrees[sequence]->setFormRestrictions(base);

    // Save the form fields for this sequence as well.
    mConcolicFormFields[sequence] = mConcolicFormFieldsForPage;
}

void AnalysisServerRuntime::concolicOutputTree(TraceNodePtr tree, QString name)
{
    // 'name' is the sequence ID for the updated tree.
    QString title = "Concolic tree for sequence:\n" + name;

    QString filename;
    QString filenameOverview;
    if (!mConcolicTreeOutputFileNames.contains(name)) {
        filename = QString("server-tree-%1.gv").arg(mConcolicTreeOutputFileNameCounter);
        filenameOverview = QString("server-tree-%1_overview.gv").arg(mConcolicTreeOutputFileNameCounter);
        mConcolicTreeOutputFileNames[name] = QPair<QString, QString>(filename, filenameOverview);
        mConcolicTreeOutputFileNameCounter++;
    } else {
        filename = mConcolicTreeOutputFileNames[name].first;
        filenameOverview = mConcolicTreeOutputFileNames[name].second;
    }

    // There is one file per concolic tree, which we overwrite each time the tree is updated.
    TraceDisplay display(mOptions.outputCoverage != NONE);
    TraceDisplayOverview displayOverview(mOptions.outputCoverage != NONE);
    display.writeGraphFile(tree, filename, false, title);
    displayOverview.writeGraphFile(tree, filenameOverview, false, title);
}

void AnalysisServerRuntime::slExecutionTreeUpdated(TraceNodePtr tree, QString name)
{
    concolicOutputTree(tree, name);
}

// Update the fields-read log and the concolic trace recorder of a new event.
void AnalysisServerRuntime::notifyStartingEvent(QString event, QString elementXPath)
{
    mFieldReadLog.beginEvent(event, elementXPath);

    // If we are recording a concolic trace, then add a new marker for this event.
    if (!mConcolicSequenceRecording.isEmpty()) {
        mConcolicTraceMarkerIdx++;
        QString eventWithElement = event + " @ " + elementXPath;

        // Check for dynamic form restrictions which should be included.
        FormRestrictions currentFormRestrictions = FormFieldRestrictedValues::getRestrictions(mConcolicFormFields[mConcolicSequenceRecording], mWebkitExecutor->getPage());
        QWebElement element = mWebkitExecutor->getPage()->getSingleElementByXPath(elementXPath);
        QPair<bool, SelectRestriction> restriction(false, SelectRestriction());
        if (!element.isNull()) {
            QString identifier = element.attribute("id"); // Select elements should all have an ID, possibly auto-generated.
            restriction = FormFieldRestrictedValues::getRelevantSelectRestriction(currentFormRestrictions, identifier);
        }

        emit sigNewTraceMarker(eventWithElement, QString::number(mConcolicTraceMarkerIdx), restriction.first, restriction.second);
    }
}



void AnalysisServerRuntime::done()
{
    mAnalysisServer.logEntry("Server stopped.");

    Runtime::done();
}

} // namespace artemis
