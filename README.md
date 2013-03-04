
Artemis
=======

Copyright 2013 Aarhus University

Artemis is a tool that performs automated, feedback-directed testing
of JavaScript applications.

The tool is being developed by the Programming Languages group at
Aarhus University and the Program Analysis and Transformation group at
IBM Research.

Contained in this repository is, besides the source for the tool, an
instrumented version of the WebKit code-base.

For more information, see http://brics.dk/artemis/

Contributors
------------

The following people have contributed to the Artemis source code:

* Anders Møller
* Simon Holm Jensen
* Casper Svenning Jensen
* Kristoffer Just Andersen
* Christian Budde Christensen

Included Software
-----------------

This software includes components from:

* WebKit (http://www.webkit.org/)
* Google Test (https://code.google.com/p/googletest/)
* Google Mock (https://code.google.com/p/googlemock/)

Installation 
------------

See the included INSTALL file.

Usage
-----

  artemis <url-to-be-tested>

Use the help command `artemis --help` for additional options.

Modifying the Software
----------------------

Developer documentation is placed in the docs folder. To compile the documentation you need to install sphinx (http://sphinx-doc.org/) and run the command ``make html`` or ``make latex`` in the docs folder.