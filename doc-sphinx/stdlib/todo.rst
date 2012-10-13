Standard library TODO list
==========================

Package configuration
---------------------

Similar to Python's ``pkg_resources`` module.
Features:

* System dependent operation: e.g. on Linux, guess appropriate data paths from executable and library locations;
  should be able to infer whether to use ``/usr/share/`` or ``/usr/local/share`` for example, or a package structure
  before installation.
* Remember to check for a ``setuid`` process if any user-dependent locations are searched.