# How to Add a New Menu to LibreOffice Writer Menu Bar

This guide explains how to add a new menu to the LibreOffice Writer menu bar. The process involves modifying several files to integrate with LibreOffice's command dispatch system.

## Overview

Adding a new menu requires:
1. Defining the menu structure in XML
2. Creating command IDs
3. Adding slot definitions
4. Implementing command handlers
5. Creating menu labels

## Step-by-Step Instructions

### 1. Add Menu Structure to menubar.xml

Edit `sw/uiconfig/swriter/menubar/menubar.xml` to add your menu. Insert it at the desired position (e.g., after Tools menu):

```xml
<menu:menu menu:id=".uno:ProductivityMenu">
  <menu:menupopup>
    <menu:menuitem menu:id=".uno:FocusMode"/>
    <!-- Add more menu items here -->
  </menu:menupopup>
</menu:menu>
```

### 2. Define Command IDs

Add your command IDs to `sw/inc/cmdid.h`. Find an appropriate section and add:

```cpp
// Your menu commands (find the next available ID number)
#define FN_PRODUCTIVITY_MENU     (FN_EXTRA2 + 134)
#define FN_FOCUS_MODE           (FN_EXTRA2 + 135)
```

### 3. Add Slot Definitions

Add slot definitions to `sw/sdi/swriter.sdi`. This tells LibreOffice about your commands:

```cpp
SfxVoidItem ProductivityMenu FN_PRODUCTIVITY_MENU
()
[
    AutoUpdate = FALSE,
    FastCall = FALSE,
    ReadOnlyDoc = TRUE,
    Toggle = FALSE,
    Container = FALSE,
    RecordAbsolute = FALSE,
    RecordPerSet;

    AccelConfig = FALSE,
    MenuConfig = TRUE,
    ToolBoxConfig = FALSE,
    GroupId = SfxGroupId::Application;
]

SfxBoolItem FocusMode FN_FOCUS_MODE
[
    AutoUpdate = TRUE,
    FastCall = FALSE,
    ReadOnlyDoc = TRUE,
    Toggle = TRUE,
    Container = FALSE,
    RecordAbsolute = FALSE,
    RecordPerSet;

    AccelConfig = TRUE,
    MenuConfig = TRUE,
    ToolBoxConfig = TRUE,
    GroupId = SfxGroupId::View;
]
```

### 4. Implement Command Handlers

#### In sw/source/uibase/uiview/view2.cxx

Add to the `Execute` method's switch statement (before the default case):

```cpp
case FN_FOCUS_MODE:
{
    // Toggle focus mode - implement your functionality here
    // For now, this is a stub
    break;
}
```

#### In sw/source/uibase/uiview/viewstat.cxx

Add to the `GetState` method's switch statement (inside the switch, before the closing brace):

```cpp
case FN_FOCUS_MODE:
{
    // Set the state of the menu item
    rSet.Put(SfxBoolItem(FN_FOCUS_MODE, false)); // false = not checked
    break;
}
```

### 5. Define Menu Labels

#### For the menu itself
Add to `officecfg/registry/data/org/openoffice/Office/UI/GenericCommands.xcu`:

```xml
<node oor:name=".uno:ProductivityMenu" oor:op="replace">
  <prop oor:name="Label" oor:type="xs:string">
    <value xml:lang="en-US">Productivity</value>
  </prop>
</node>
```

#### For menu items
Add to `officecfg/registry/data/org/openoffice/Office/UI/WriterCommands.xcu`:

```xml
<node oor:name=".uno:FocusMode" oor:op="replace">
  <prop oor:name="Label" oor:type="xs:string">
    <value xml:lang="en-US">Focus Mode</value>
  </prop>
  <prop oor:name="Properties" oor:type="xs:int">
    <value>8</value>
  </prop>
</node>
```

## Building and Testing

After making these changes:

1. Rebuild the Writer module:
   ```bash
   make sw.build
   ```

2. Run LibreOffice Writer:
   ```bash
   make debugrun gb_DBGARGS="--writer"
   ```

## Common Issues

### Menu appears but labels are blank
- Ensure all command IDs are properly defined
- Check that slot definitions are added to .sdi file
- Verify command handlers are implemented
- Make sure menu labels are defined in .xcu files

### Build errors
- Check that case statements are inside the switch block
- Ensure command IDs don't conflict with existing ones
- Verify all files have correct syntax

### Menu doesn't appear
- Check XML syntax in menubar.xml
- Ensure the build completed successfully
- Verify all configuration files are properly formatted

## File Summary

Files you need to modify:
- `sw/uiconfig/swriter/menubar/menubar.xml` - Menu structure
- `sw/inc/cmdid.h` - Command ID definitions
- `sw/sdi/swriter.sdi` - Slot definitions
- `sw/source/uibase/uiview/view2.cxx` - Execute handler
- `sw/source/uibase/uiview/viewstat.cxx` - GetState handler
- `officecfg/registry/data/org/openoffice/Office/UI/GenericCommands.xcu` - Menu label
- `officecfg/registry/data/org/openoffice/Office/UI/WriterCommands.xcu` - Item labels

## Notes

- The `~` character in menu labels (e.g., `~Tools`) indicates the keyboard accelerator
- Toggle menu items should use `SfxBoolItem` in their slot definitions
- The `GroupId` in slot definitions affects where the command appears in customization dialogs
- Always check existing patterns in the codebase for consistency