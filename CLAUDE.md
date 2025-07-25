# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

LibreOffice is a large-scale, cross-platform office productivity suite with approximately 10 million lines of code organized into over 200 modules. The architecture follows several key design principles:

- **Layered Architecture**: Strict dependencies from high-level application code down to low-level platform abstractions
- **Component-Based Design**: Using the Universal Network Objects (UNO) framework for language-independent components
- **Platform Independence**: Achieved through abstraction layers (SAL/VCL) that isolate platform-specific code
- **Document-Centric Model**: Each application maintains its own specialized document model

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

## System Architecture

### Layered Architecture

LibreOffice employs a strict layered architecture where dependencies flow downward:

```
┌─────────────────────────────────────────────────┐
│          Applications (sw, sc, sd)              │
├─────────────────────────────────────────────────┤
│     Application Frameworks (sfx2, svx)          │
├─────────────────────────────────────────────────┤
│    Core Frameworks (framework, svtools)         │
├─────────────────────────────────────────────────┤
│      VCL (Visual Class Library)                 │
├─────────────────────────────────────────────────┤
│   Basic Tools (tools, basegfx, comphelper)     │
├─────────────────────────────────────────────────┤
│      SAL (System Abstraction Layer)             │
└─────────────────────────────────────────────────┘
```

### Core System Layers

#### SAL (System Abstraction Layer)
- Location: `sal/`
- Purpose: Platform-independent OS abstractions
- Components:
  - `rtl`: Runtime library with platform-independent strings, memory management
  - `osl`: Operating system layer for threads, file I/O, IPC, dynamic loading
- Provides unified interface for all platform-specific operations

#### VCL (Visual Class Library)
- Location: `vcl/`
- Purpose: Widget toolkit and rendering abstraction
- Key abstractions:
  - `SalInstance`: Factory for platform-specific implementations
  - `SalFrame`: Window abstraction
  - `OutputDevice`: Rendering abstraction
- Platform backends: Windows (Win32), macOS (Quartz), Linux (GTK3/4, Qt5/6, X11)
- Threading: Uses SolarMutex as the "big kernel lock" for UI thread safety

#### Framework Layers

**UNO Framework** (`framework/`)
- Modern component architecture for UI elements
- Menu, toolbar, and statusbar management
- Command dispatching via UNO
- UI configuration loading from XML

**SFX2 (StarView Framework 2)** (`sfx2/`)
- Legacy framework for document-based applications
- Document/View/Controller base classes
- Slot-based command dispatching (using integer IDs)
- Document load/save infrastructure (`SfxMedium`)

**SVX (Shared View eXtensions)** (`svx/`)
- Shared editing components
- Drawing layer (shapes, connectors)
- Form controls and common dialogs
- Accessibility support

### Applications

#### Writer (sw/)
- Document model: `SwDoc` with `SwNodes` tree structure
- Layout engine: Frame-based layout with automatic pagination
- Specialized features: Fields, mail merge, change tracking

#### Calc (sc/)
- Document model: `ScDocument` with column-oriented storage
- Calculation engine: Multi-threaded formula evaluation
- Specialized features: Pivot tables, charts, data validation

#### Draw/Impress (sd/)
- Shared drawing model: `SdrModel` for vector graphics
- Presentation features: Slide transitions, animations
- Master pages and layouts

## Platform Abstraction

### SAL (System Abstraction Layer)

SAL provides platform-independent abstractions for fundamental OS services:

#### Core APIs
- **File System Operations**: File/directory manipulation, path handling, file locking
- **Process and Thread Management**: Process creation, thread synchronization, thread-local storage
- **Memory Management**: Memory allocation, virtual memory, memory mapping
- **Synchronization Primitives**: Mutexes, condition variables, semaphores
- **Dynamic Library Loading**: Module loading, symbol resolution, plugin architecture
- **Network Operations**: Socket management, network address resolution
- **Time and Date**: System time, high-resolution timers, time zones

#### Platform Implementations
```
sal/
├── osl/
│   ├── unx/        # Unix/Linux implementation
│   ├── w32/        # Windows implementation
│   └── all/        # Shared implementations
└── rtl/            # Platform-independent runtime
```

### VCL Platform Backends

VCL uses a plugin architecture for platform-specific UI implementations:

#### Available Backends
- **Windows** (`win/`): Win32 API, GDI/GDI+, DirectWrite, native theming
- **macOS** (`osx/`): Cocoa, Core Graphics (Quartz), Core Text
- **Linux/Unix**:
  - **GTK3/GTK4**: Native GTK widgets, Cairo rendering, Pango text
  - **Qt5/Qt6**: Qt widgets, cross-platform consistency
  - **KF5/KF6**: KDE integration with native file dialogs
  - **Generic X11**: Raw X11 protocol (fallback)
- **Headless** (`headless/`): No GUI dependencies for servers
- **Android** (`android/`): Java/JNI bridge, Android Canvas
- **iOS** (`ios/`): UIKit integration, Core Graphics

#### Backend Selection
Platform detection order:
1. Environment variable: `SAL_USE_VCLPLUGIN`
2. Desktop detection (Unix): KDE/GNOME/other
3. Fallback chain based on availability

## UNO Component Model

### Foundation
Universal Network Objects (UNO) provides:
- **Language Independence**: Components in C++, Java, Python
- **Location Transparency**: Cross-process and network communication
- **Interface-Based Design**: Clean separation of interface and implementation
- **Dynamic Service Discovery**: Runtime component instantiation

### UNO Architecture
```
┌─────────────────┐     ┌─────────────────┐
│   C++ Object    │────▶│ UNO Interface   │
└─────────────────┘     └────────┬────────┘
                                 │
                  ┌──────────────┴──────────────┐
                  │                             │
           ┌──────▼──────┐              ┌──────▼──────┐
           │ Java Bridge │              │Python Bridge│
           └─────────────┘              └─────────────┘
```

### Key Interfaces
- **XInterface**: Base interface with `queryInterface()`
- **XComponent**: Lifecycle management with `dispose()`
- **XServiceInfo**: Service identification
- **XTypeProvider**: Runtime type information

### Communication Patterns
1. **Service-Based**: Components interact through services
2. **Event-Driven**: Listener/broadcaster patterns
3. **Property-Based**: Property sets for configuration
4. **Command Dispatch**: URL-based command routing

## Data Flow and Document Architecture

### Document Models

#### Writer Document Structure
- **SwDoc**: Main document model
- **SwNodes Array**: Flat array with tree semantics containing:
  - `SwTextNode`: Paragraph text
  - `SwTableNode`: Tables
  - `SwSectionNode`: Document sections
- **Top-level Sections**: Empty, Footnotes, Headers/Footers, Change Tracking, Body

#### Calc Document Structure
- **ScDocument**: Column-oriented architecture
- **Cell Types**: Numeric (doubles), String (UTF-8), Formula (R1C1), Empty
- **Optimization**: Column-based storage for typical access patterns

### Rendering Pipeline
```
Document Model → Primitive Creation → Primitive Decomposition → Processing → Output Device
```

#### Key Components
- **Primitives**: Device-independent drawing commands
- **Processors**: Convert primitives to device-specific rendering
- **GDIMetaFile**: Intermediate vector format for recording/playback

### File Format Architecture

#### Filter Framework
```
User File → Filter Detection → Format-Specific Filter → Document Model
                                        ↓
                              Internal Representation
                                        ↓
Document Model → Export Filter → Output File
```

#### Supported Formats
- **ODF** (`xmloff/`): Primary format, ZIP-based XML
- **Microsoft Office**:
  - Legacy binary (DOC/XLS/PPT): Direct parsers
  - OOXML (DOCX/XLSX/PPTX): Token-based XML in `oox/`
- **PDF Export**: Direct primitive-to-PDF conversion
- **Others**: HTML, RTF, various import/export filters

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

## Development Workflow

### Environment Setup

#### Prerequisites
- **Linux**: GCC 12 or Clang 12 with libstdc++ 10
- **Windows**: Windows 10 + Cygwin + Visual Studio 2019 v16.10
- **macOS**: macOS 13+ with Xcode 14.3+
- **Tools**: autoconf 2.68+, pkg-config, GNU Make, git, JDK 17+, Python 3.11+

#### Initial Setup
```bash
# Clone repository
git clone https://gerrit.libreoffice.org/core libreoffice
cd libreoffice

# Configure build
./autogen.sh --with-lang=en-US --enable-debug

# For Windows/macOS, use LODE for simplified setup
git clone https://gerrit.libreoffice.org/lode
cd lode && ./setup
```

### Build System (gbuild)

The build system is Make-based with modular architecture:

#### Build Commands
```bash
# Full build
make

# Module-specific builds
make sw.build              # Quick rebuild
make sw.clean              # Clean module
make sw.check              # Run tests

# Debug builds
./autogen.sh --enable-debug
make debugrun

# Performance builds
./autogen.sh --enable-ccache --enable-pch
```

#### Build Output
- `workdir/`: Intermediate build files
- `instdir/`: Local installation
- Test logs: `workdir/CppunitTest/*.test.log`

### Testing Strategy

#### Testing Pyramid
```
        System Tests
       /            \
      UI Tests       \
     /                \
    Integration Tests  \
   /                    \
  Unit Tests             \
 /_______________________\
```

#### Running Tests
```bash
# Unit tests (CppUnit)
make check                           # All tests
make CppunitTest_sw_uwriter         # Specific test
make sw.check                       # Module tests

# Debug failing test
make CppunitTest_sw_uwriter CPPUNITTRACE="gdb --args"

# UI tests (Python)
make uicheck                        # All UI tests
make UITest_sw_findReplace         # Specific UI test

# Integration tests
make subsequentcheck
```

### Debugging Techniques

#### GDB Integration
```bash
# Run with debugger
make debugrun

# Attach to running instance
gdb -p $(pidof soffice.bin)

# Load LibreOffice pretty-printers
source solenv/gdb/autoload.py
```

#### Debug Output
```cpp
// Use SAL_INFO/SAL_WARN macros
SAL_INFO("sw.core", "Processing paragraph " << nIndex);
SAL_WARN_IF(!pDoc, "sw.core", "Document is null!");

// Enable output
export SAL_LOG="+INFO.sw.core+WARN"
```

#### Memory Debugging
```bash
# Valgrind
make debugrun VALGRIND=memcheck

# Address Sanitizer
./autogen.sh --enable-asan
export ASAN_OPTIONS=detect_leaks=0
make check
```

### Code Organization Best Practices

#### Module Structure
```
module/
├── inc/                    # Public headers
├── source/                 # Implementation
│   ├── core/              # Core functionality
│   ├── filter/            # Import/export
│   └── ui/                # User interface
├── qa/                     # Tests
│   ├── unit/              # Unit tests
│   └── uitest/            # UI tests
├── Module_*.mk            # Module definition
└── README.md              # Documentation
```

#### Naming Conventions
- Classes: `PascalCase` (e.g., `SwDoc`)
- Methods: `PascalCase` (e.g., `GetText()`)
- Variables: `camelCase` with prefixes:
  - `m_` for members
  - `n` for numbers
  - `b` for booleans
  - `p` for pointers

#### Error Handling
```cpp
// API errors use exceptions
if (!IsValid())
    throw css::uno::RuntimeException("Invalid state");

// Internal APIs may use Result types
Result<OUString, Error> LoadDocument(const OUString& rPath);
```

### Performance Optimization

#### Profiling
```bash
# CPU profiling
perf record -g make check
perf report

# Built-in profiling
export SAL_PROFILEZONE_EVENTS=1
```

#### Common Optimizations
- Use `OUStringBuffer` for string concatenation
- Prefer `std::move` for large objects
- Cache expensive calculations
- Use precompiled headers: `--enable-pch`

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

## Writer Development Focus

Since we are focusing exclusively on Writer development, here's what you need to know:

### Essential Folders for Writer Development

#### Primary Writer Module
- **sw/** - The main Writer module containing all Writer-specific code
  - source/core/ - Core Writer functionality (document model, layout, fields)
  - source/filter/ - Import/export filters for various formats
  - source/ui/ - UI dialogs and controls
  - source/uibase/ - UI base classes and controllers
  - inc/ - Writer headers

#### Core Dependencies (Required for Writer)
- **vcl/** - Visual Class Library (UI widgets and rendering)
- **sfx2/** - Application framework (document model, load/save infrastructure)
- **svx/** - Shared editing components and drawing layer
- **editeng/** - Text editing engine (paragraph, character formatting)
- **svl/** - Non-UI tools and utilities
- **svtools/** - UI-related tools
- **framework/** - UNO-based UI framework (menus, toolbars)
- **comphelper/** - Common helper classes
- **tools/** - Basic tools and utilities
- **sal/** - System Abstraction Layer (required by everything)

#### Document Format Support
- **xmloff/** - ODF XML import/export
- **oox/** - OOXML (docx) import/export
- **filter/** - General filter framework
- **package/** - ZIP package handling for ODF

#### Essential Services
- **i18npool/** - Internationalization services
- **i18nutil/** - Internationalization utilities
- **linguistic/** - Spell checking and linguistic services

### Folders You Can Safely Ignore

When working only on Writer features, you can ignore:

#### Other Applications (60%+ of codebase)
- **sc/** - Calc (spreadsheet)
- **sd/** - Draw/Impress (presentation/drawing)
- **chart2/** - Chart module
- **starmath/** - Math formula editor

#### Specialized Components
- **basic/** - Basic macro interpreter
- **forms/** - Form controls
- **reportdesign/** - Report designer
- **connectivity/**, **dbaccess/** - Database access (unless working on mail merge)
- **extensions/** - Extension framework
- **scripting/** - Scripting support

#### Platform-Specific (unless on that platform)
- **android/**, **ios/** - Mobile platforms
- **winaccessibility/** - Windows-specific accessibility
- **apple_remote/** - macOS remote control

#### Development Infrastructure
- **odk/** - SDK
- **qadevOOo/** - QA test framework
- **external/** - Third-party libraries (handled by build system)
- Language bindings: **javaunohelper/**, **pyuno/**, **cli_ure/**

### Writer-Specific Architecture Notes

#### Core Classes
- **SwDoc** - The main document model class
- **SwNodes** - Document content tree structure
- **SwTextNode** - Text paragraph nodes
- **SwFrame** - Layout objects for rendering
- **SwView** - Main view controller

#### UNO Wrappers
- **SwXTextDocument** - UNO wrapper for SwDoc
- **SwXParagraph** - UNO wrapper for paragraphs
- **SwXTextCursor** - UNO wrapper for text cursors

Most internal Writer code uses direct C++ calls between these classes. UNO is primarily used for:
- External API (extensions, macros)
- Communication with framework (menus, toolbars)
- Import/export filters

### Quick Start for Writer Changes
1. Focus your work in the **sw/** directory
2. Run `make sw.build` for quick rebuilds
3. Run `make sw.check` to test Writer-specific changes
4. Use `make debugrun gb_DBGARGS="--writer"` to test your changes