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

#ifndef RESPONSEHANDLER_H
#define RESPONSEHANDLER_H

#include <QByteArray>
#include <QVariant>

#include <qhttpresponse.h>

namespace artemis
{

class ResponseHandler : public QObject
{
    Q_OBJECT

public:
    ResponseHandler() {}
    ~ResponseHandler() {}

    void sendResponse(QHttpResponse* response, QByteArray data);
    void sendResponse(QHttpResponse* response, QVariant data);

signals:
    void sigServerLog(QString data, bool direction);
};

} // namespace artemis
#endif // RESPONSEHANDLER_H
