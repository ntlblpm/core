# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Essential Build Commands

### Initial Setup
```bash
# Configure build (reads from autogen.input or autogen.lastrun)
./autogen.sh

# Configure with specific options
./autogen.sh --with-lang=en-US --enable-debug
```

### Building
```bash
# Full build
make

# Build specific module
make sc                    # Calc
make sw                    # Writer
make sd                    # Draw/Impress

# Quick rebuild after changes
make sc.build
```

### Testing
```bash
# Run all tests
make check

# Run specific test
make CppunitTest_sc_ucalc

# Run tests for a module
make sc.check

# Debug a failing test
make CppunitTest_sc_ucalc CPPUNITTRACE="gdb --args"

# Run specific UI test
make UITest_calc_tests UITEST_TEST_NAME="test_tdf12345.py"
```

### Linting and Code Quality
```bash
# Check code formatting
make clang-format-check

# Run compiler plugins
make -f compilerplugins/Makefile.mk compilerplugins
```

### Running LibreOffice
```bash
# Start in debug mode
make debugrun

# Run with arguments
make debugrun gb_DBGARGS="--calc /path/to/file.ods"
```

## High-Level Architecture

LibreOffice follows a layered architecture with these key components:

### Core Layers
- **SAL** (sal/): System Abstraction Layer - Platform-independent OS abstractions
- **VCL** (vcl/): Visual Class Library - Widget toolkit and rendering abstraction
- **Framework** (framework/): UNO-based UI framework for toolbars, menus, status bars
- **SFX2** (sfx2/): Legacy framework for document model, load/save, and application base classes

### Applications
- **Writer** (sw/): Word processor - uses SwDoc document model with SwNodes array
- **Calc** (sc/): Spreadsheet - column-oriented cache format for performance
- **Draw/Impress** (sd/): Drawing and presentation applications sharing drawing model

### Key Concepts

#### UNO Component Model
Universal Network Objects (UNO) provides language-independent component architecture:
- Language bridges for C++, Java, Python
- Remote procedure calls via UNO Remote Protocol
- API definitions in offapi/ (LibreOffice-specific) and udkapi/ (core UNO)

#### Document Model
Each application has its own document class:
- Writer: SwDoc with nested SwNodes structure
- Calc: ScDocument with spreadsheet-specific model
- Draw/Impress: SdrModel for drawing objects

#### Rendering Pipeline
DrawingLayer provides rendering abstraction:
- Primitives define what to draw
- Processors handle how to render (screen, PDF, print)
- VCL backends: Windows (Win32), macOS (Quartz), Linux (GTK3/4, Qt5/6, X11)

#### File Formats
- ODF: Primary format, implemented in xmloff/
- Microsoft: Legacy formats have native implementations, OOXML uses oox/ for import
- Various import/export filters in filter/ module

### Module Dependencies
Modules follow strict dependency rules - lower-level modules cannot depend on higher-level ones:
1. SAL (lowest level)
2. Basic tools and utilities (tools/, basegfx/, comphelper/)
3. VCL and core frameworks
4. Application frameworks (sfx2/, svx/)
5. Applications (sw/, sc/, sd/)

### Threading Model
- VCL uses SolarMutex as the "big kernel lock"
- Most code runs under this mutex
- Careful coordination needed for background threads

## Important Development Notes

### Include Directives
- Use `"..."` only for files in the same directory
- Use `<...>` for all other includes
- UNO API headers use double quotes for external API users

### Code Style
- Follow existing patterns in the module you're modifying
- Check neighboring files for framework choices and conventions
- Never assume a library is available - verify in package manifests

### Testing Requirements
- Always run relevant tests after changes
- Use `make <module>.check` to run all tests for a module
- Test logs are saved in workdir/CppunitTest/*.test.log

### Common Pitfalls
- The codebase is large (~10M LOC) with legacy code from StarOffice
- Many modules have README.md files - read them
- Some code uses integer slot IDs (SFX2) while newer code uses UNO commands
- Build times can be long - use module-specific builds when possible