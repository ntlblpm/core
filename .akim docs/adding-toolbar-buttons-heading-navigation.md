# Adding Toolbar Buttons to LibreOffice Writer: Heading Navigation Example

This document describes the process of adding new toolbar buttons to LibreOffice Writer, using the heading navigation feature as an example.

## Overview

We added two new toolbar buttons to allow users to quickly navigate between headings in their documents:
- **Previous Heading**: Navigate to the previous heading
- **Next Heading**: Navigate to the next heading

## Implementation Steps

### 1. Define Command IDs

First, define unique command IDs in the Writer command header file.

**File**: `/sw/inc/cmdid.h`
```cpp
// Heading navigation commands
#define FN_GOTO_NEXT_HEADING    (FN_EXTRA2 + 136)
#define FN_GOTO_PREV_HEADING    (FN_EXTRA2 + 137)
```

### 2. Create UNO Command Definitions

Define the user-visible commands with labels and tooltips.

**File**: `/officecfg/registry/data/org/openoffice/Office/UI/WriterCommands.xcu`
```xml
<node oor:name=".uno:GotoNextHeading" oor:op="replace">
  <prop oor:name="Label" oor:type="xs:string">
    <value xml:lang="en-US">Next Heading</value>
  </prop>
  <prop oor:name="ContextLabel" oor:type="xs:string">
    <value xml:lang="en-US">Next</value>
  </prop>
  <prop oor:name="TooltipLabel" oor:type="xs:string">
    <value xml:lang="en-US">Go to next heading</value>
  </prop>
</node>
<node oor:name=".uno:GotoPrevHeading" oor:op="replace">
  <prop oor:name="Label" oor:type="xs:string">
    <value xml:lang="en-US">Previous Heading</value>
  </prop>
  <prop oor:name="ContextLabel" oor:type="xs:string">
    <value xml:lang="en-US">Previous</value>
  </prop>
  <prop oor:name="TooltipLabel" oor:type="xs:string">
    <value xml:lang="en-US">Go to previous heading</value>
  </prop>
</node>
```

### 3. Define SDI (Slot Definition Interface)

Add slot definitions to register the commands with the framework.

**File**: `/sw/sdi/swriter.sdi`
```cpp
SfxVoidItem GotoNextHeading FN_GOTO_NEXT_HEADING
()
[
    AutoUpdate = FALSE,
    FastCall = FALSE,
    ReadOnlyDoc = TRUE,
    Toggle = FALSE,
    Container = FALSE,
    RecordAbsolute = FALSE,
    RecordPerSet;

    AccelConfig = TRUE,
    MenuConfig = TRUE,
    ToolBoxConfig = TRUE,
    GroupId = SfxGroupId::Navigator;
]

SfxVoidItem GotoPrevHeading FN_GOTO_PREV_HEADING
()
[
    AutoUpdate = FALSE,
    FastCall = FALSE,
    ReadOnlyDoc = TRUE,
    Toggle = FALSE,
    Container = FALSE,
    RecordAbsolute = FALSE,
    RecordPerSet;

    AccelConfig = TRUE,
    MenuConfig = TRUE,
    ToolBoxConfig = TRUE,
    GroupId = SfxGroupId::Navigator;
]
```

### 4. Add Command Handlers

Declare the commands in the text shell interface.

**File**: `/sw/sdi/_textsh.sdi`
```cpp
FN_GOTO_PREV_HEADING
[
    ExecMethod = ExecMoveMisc ;
    StateMethod = NoState ;
]
FN_GOTO_NEXT_HEADING
[
    ExecMethod = ExecMoveMisc ;
    StateMethod = NoState ;
]
```

Implement the command handlers.

**File**: `/sw/source/uibase/shells/txtcrsr.cxx`
```cpp
void SwTextShell::ExecMoveMisc(SfxRequest &rReq)
{
    // ... existing code ...
    
    case FN_GOTO_NEXT_HEADING:
        bRet = rSh.GotoNextOutline();
        break;
    case FN_GOTO_PREV_HEADING:
        bRet = rSh.GotoPrevOutline();
        break;
        
    // ... rest of function ...
}
```

### 5. Add Buttons to Toolbars

Add the buttons to the desired toolbars.

**File**: `/sw/uiconfig/swriter/toolbar/standardbar.xml`
```xml
<toolbar:toolbaritem xlink:href=".uno:SearchDialog"/>
<toolbar:toolbaritem xlink:href=".uno:Navigator" toolbar:visible="false"/>
<toolbar:toolbaritem xlink:href=".uno:GotoPrevHeading"/>
<toolbar:toolbaritem xlink:href=".uno:GotoNextHeading"/>
<toolbar:toolbaritem xlink:href=".uno:SpellingAndGrammarDialog"/>
```

**File**: `/sw/uiconfig/swriter/toolbar/textobjectbar.xml`
```xml
<toolbar:toolbaritem xlink:href=".uno:DefaultBullet"/>
<toolbar:toolbaritem xlink:href=".uno:DefaultNumbering"/>
<toolbar:toolbaritem xlink:href=".uno:SetOutline"/>
<toolbar:toolbarseparator/>
<toolbar:toolbaritem xlink:href=".uno:GotoPrevHeading"/>
<toolbar:toolbaritem xlink:href=".uno:GotoNextHeading"/>
<toolbar:toolbarseparator/>
```

### 6. Create Icons

LibreOffice automatically looks for icons based on the command name. Create icon files with the following naming convention:

- Large icons: `lc_<commandname>.png` (e.g., `lc_gotonextheading.png`)
- Small icons: `sc_<commandname>.png` (e.g., `sc_gotonextheading.png`)

Place icons in: `/icon-themes/<theme>/cmd/`

For our example, we copied existing arrow icons:
```bash
cd /icon-themes/colibre/cmd
cp lc_jumpdownthislevel.png lc_gotonextheading.png
cp lc_jumpupthislevel.png lc_gotoprevheading.png
cp sc_jumpdownthislevel.png sc_gotonextheading.png
cp sc_jumpupthislevel.png sc_gotoprevheading.png
```

## Build Process

After making these changes:

1. Build the Writer module:
   ```bash
   make sw.build
   ```

2. Update the postprocess to include new icons and configurations:
   ```bash
   make postprocess
   ```

## Key Components Explained

### Command ID (FN_*)
- Must be unique within the application
- Defined in `cmdid.h`
- Used internally to identify the command

### UNO Command Name (.uno:*)
- User-visible command identifier
- Used in toolbars, menus, and keyboard shortcuts
- Defined in `WriterCommands.xcu`

### Labels
- **Label**: Full text shown in menus and customization dialogs
- **ContextLabel**: Short text for toolbar buttons (optional)
- **TooltipLabel**: Text shown on hover

### SDI Properties
- **AutoUpdate**: Whether the UI updates automatically
- **ReadOnlyDoc**: Whether available in read-only mode
- **AccelConfig**: Can be assigned keyboard shortcuts
- **MenuConfig**: Can appear in menus
- **ToolBoxConfig**: Can appear in toolbars
- **GroupId**: Category for organization

### Visibility
- `toolbar:visible="false"`: Hidden by default (user can add via customization)
- No visibility attribute: Visible by default

## Testing

1. Run Writer:
   ```bash
   make debugrun gb_DBGARGS="--writer"
   ```

2. Check if buttons appear in the toolbar
3. Test functionality by creating a document with headings
4. Verify tooltips and labels display correctly

## Troubleshooting

If buttons show command names (e.g., ".uno:GotoNextHeading") instead of labels:
1. Ensure `WriterCommands.xcu` is properly formatted
2. Run `make postprocess` to update configurations
3. Clear user profile: `rm -rf ~/.config/libreoffice/4/user/`
4. Check that icon files exist and are properly named

## Related Files

- Command definitions: `/sw/inc/cmdid.h`
- UNO commands: `/officecfg/registry/data/org/openoffice/Office/UI/WriterCommands.xcu`
- SDI definitions: `/sw/sdi/swriter.sdi`, `/sw/sdi/_textsh.sdi`
- Command handlers: `/sw/source/uibase/shells/txtcrsr.cxx`
- Toolbar layouts: `/sw/uiconfig/swriter/toolbar/*.xml`
- Icons: `/icon-themes/*/cmd/`

## See Also

- [LibreOffice Development Documentation](https://wiki.documentfoundation.org/Development)
- Existing navigation commands: `FN_GOTO_NEXT_REGION`, `FN_GOTO_PREV_REGION`
- Related methods: `SwCursorShell::GotoNextOutline()`, `SwCursorShell::GotoPrevOutline()`