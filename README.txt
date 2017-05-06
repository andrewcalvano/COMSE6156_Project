This repository houses instrumentation tools written for the final COMSE6156 project.

It also contains raw measurements obtained during the evaluation using the atop utility.

The measurement_data directory contains these raw measurements. For tests that completed this
directory also contains the coverage and mmap files produced.

The intelpin directory contains the basic block edge coverage tool implemented in the Intel PIN
DBI framework.

The valgrind directory contains the basic block edge coverage tool implemented in the Valgrind
DBI framework.

The dynamorio directory contains the basic block edge coverage tool implemented in the DynamoRIO
DBI framework.

The INSTALL.txt file contains instructions on how to build and use each instrumentation tool.

The EVALUATION.txt file contains instructions on how to perform the evaluation using atop.
