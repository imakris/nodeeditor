Installation
============

Requirements
------------

- **Qt** 6.5 or newer
- **CMake** 3.21 or newer
- **C++ Compiler** with C++17 support (GCC 7+, Clang 5+, MSVC 2019+)
- **Catch2** (optional, for running tests)

Building from Source
--------------------

**1. Clone the repository**

.. code-block:: bash

   git clone https://github.com/paceholder/nodeeditor.git
   cd nodeeditor

**2. Create build directory and configure**

.. code-block:: bash

   mkdir build && cd build
   cmake ..

**3. Build**

.. code-block:: bash

   cmake --build .

CMake Options
-------------

.. list-table::
   :widths: 30 15 55
   :header-rows: 1

   * - Option
     - Default
     - Description
   * - ``BUILD_SHARED_LIBS``
     - ``OFF``
     - Build as a static library. Set to ``ON`` for shared.
   * - ``BUILD_TESTING``
     - ``ON``
     - Build unit tests. Requires Catch2.
   * - ``BUILD_EXAMPLES``
     - ``ON``
     - Build example applications.

**Examples:**

.. code-block:: bash

   # Build static library
   cmake .. -DBUILD_SHARED_LIBS=OFF

   # Skip tests (if Catch2 not installed)
   cmake .. -DBUILD_TESTING=OFF

Using vcpkg
-----------

If you use vcpkg for dependency management:

.. code-block:: bash

   cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

Integration into Your Project
-----------------------------

**Option 1: CMake subdirectory**

Add QtNodes as a subdirectory in your project:

.. code-block:: cmake

   add_subdirectory(external/nodeeditor)
   target_link_libraries(your_app QtNodes::QtNodes)

**Option 2: Installed library**

After running ``cmake --install .``:

.. code-block:: cmake

   find_package(QtNodes REQUIRED)
   target_link_libraries(your_app QtNodes::QtNodes)

Verifying Installation
----------------------

Run the calculator example to verify everything works:

.. code-block:: bash

   ./bin/calculator

.. image:: /_static/screenshots/calc-start.png
   :alt: Calculator example running successfully
   :width: 400px

Next Steps
----------

Continue to :doc:`quickstart` to build your first node graph.
