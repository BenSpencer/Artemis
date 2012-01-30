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
#include "coveragetooutputstream.h"
#include <QTextStream>

namespace artemis {

    void write_coverage_report(ostream& stream, const CodeCoverage& cov) {
        foreach (int id, cov.source_ids()) {
          std::cout << "OI!!!" << std::endl;

            SourceInfo info = cov.source_info(id);
            QString src = info.source();
            QTextStream read(&src);
            stream << "Coverage for source located at URL: " << info.url().toString().toStdString() << "  line " << stdstr_to_int(info.start_line())  << endl;
            QMap<int, LineInfo> li = cov.line_info(id);
            int i = info.start_line();
            while (!read.atEnd()) {
                LineInfo curr = li[i++];
                QString prefix;
                if (curr.is_executed()) {
                    prefix = ">>>";
                } else {
                    prefix = "   ";
                }
                QString line = prefix + read.readLine() + "\n";
                stream << line.toStdString();
            }
        }
    }
}
