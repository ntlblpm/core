# How to Add a New Menu Item to LibreOffice Writer Menu Bar

This guide explains how to add a new menu item to the LibreOffice Writer menu bar. The process involves modifying several files to integrate with LibreOffice's command dispatch system.

## Overview

Adding a new menu item requires:
1. Defining the command ID
2. Adding slot definitions in SDI files
3. Registering the command in shell interfaces
4. Implementing command handlers
5. Creating menu labels in configuration
6. Adding the menu item to the menu structure
7. Building with proper steps

## Step-by-Step Instructions

### 1. Define Command IDs

Add your command IDs to `sw/inc/cmdid.h`. Find an appropriate section and add:

```cpp
// Your menu commands (find the next available ID number)
#define FN_TEXT_TO_SPEECH       (FN_EXTRA2 + 138)
```

### 2. Add Menu Item to menubar.xml

Edit `sw/uiconfig/swriter/menubar/menubar.xml` to add your menu item. Find the appropriate menu (e.g., Tools menu) and add:

```xml
<menu:menu menu:id=".uno:ToolsMenu">
  <menu:menupopup>
    <!-- ... existing items ... -->
    <menu:menuitem menu:id=".uno:Translate"/>
    <menu:menuitem menu:id=".uno:TextToSpeech"/>  <!-- Add your item here -->
    <menu:menuseparator/>
    <!-- ... more items ... -->
  </menu:menupopup>
</menu:menu>
```

### 3. Add Slot Definitions

Add slot definitions to `sw/sdi/swriter.sdi`. This tells LibreOffice about your commands:

```cpp
SfxVoidItem TextToSpeech FN_TEXT_TO_SPEECH
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
    GroupId = SfxGroupId::Application;
]
```

### 4. Register Command in Shell Interface Files

This is a critical step that is often missed. You need to register your command in the appropriate shell interface files:

#### 4.1 Add to _viewsh.sdi

Edit `sw/sdi/_viewsh.sdi` and add your command (typically near the end, before the closing brace):

```
FN_TEXT_TO_SPEECH
[
    ExecMethod = Execute ;
    StateMethod = GetState ;
]
```

#### 4.2 Add to _textsh.sdi (for Tools menu items)

Edit `sw/sdi/_textsh.sdi` and add your command:

```
FN_TEXT_TO_SPEECH
[
    ExecMethod = Execute;
]
```

### 5. Implement Command Handlers

#### 5.1 In sw/source/uibase/uiview/view2.cxx

Add to the `Execute` method's switch statement (before the default case):

```cpp
case FN_TEXT_TO_SPEECH:
{
    // Show popup window for Text to Speech
    std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                  VclMessageType::Info, VclButtonsType::Ok,
                                                  u"Text to Speech"_ustr));
    xInfoBox->set_secondary_text(u"Text to Speech functionality will be implemented here."_ustr);
    xInfoBox->run();
    break;
}
```

#### 5.2 In sw/source/uibase/uiview/viewstat.cxx

Add to the `GetState` method's switch statement (inside the switch, before the closing brace):

```cpp
case FN_TEXT_TO_SPEECH:
{
    // Text to speech is always available
    // Nothing special to set, just enable the menu item
    break;
}
```

#### 5.3 In sw/source/uibase/shells/textsh1.cxx

For Tools menu items, you also need to add a handler in the text shell's Execute method:

```cpp
case FN_TEXT_TO_SPEECH:
{
    // Forward to view's Execute method
    rReq.SetSlot(FN_TEXT_TO_SPEECH);
    GetView().Execute(rReq);
}
break;
```

### 6. Define Menu Labels

Add to `officecfg/registry/data/org/openoffice/Office/UI/WriterCommands.xcu`:

```xml
<node oor:name=".uno:TextToSpeech" oor:op="replace">
  <prop oor:name="Label" oor:type="xs:string">
    <value xml:lang="en-US">Text to speech</value>
  </prop>
  <prop oor:name="Properties" oor:type="xs:int">
    <value>1</value>
  </prop>
</node>
```

**Important**: The `Properties` attribute is required. Without it, the label may not display correctly.

## Building and Testing

After making these changes, you need to build in the correct order:

1. Rebuild the SDI files and Writer module:
   ```bash
   make sw.build
   ```

2. **Important**: Process the configuration files:
   ```bash
   make postprocess
   ```

3. Run LibreOffice Writer:
   ```bash
   make debugrun gb_DBGARGS="--writer"
   ```

## Common Issues and Troubleshooting

### Menu item appears but label is blank

This is the most common issue when adding menu items. The button may be clickable but shows no text. To fix:

1. **Ensure the Properties attribute is set** in WriterCommands.xcu:
   ```xml
   <prop oor:name="Properties" oor:type="xs:int">
     <value>1</value>
   </prop>
   ```

2. **Run `make postprocess`** after making configuration changes. This step processes the .xcu files and is often missed.

3. **Clear the user profile** if the label still doesn't appear:
   ```bash
   rm -rf ~/.config/libreoffice/4/user/
   ```
   Or run with a clean profile:
   ```bash
   make debugrun gb_DBGARGS="--writer -env:UserInstallation=file:///tmp/test-profile"
   ```

4. **Check the shell registration** - Ensure the command is registered in both `_viewsh.sdi` and `_textsh.sdi` for Tools menu items.

### Build errors
- Check that case statements are inside the switch block
- Ensure command IDs don't conflict with existing ones
- Verify all files have correct syntax
- Make sure all required headers are included

### Menu item doesn't appear
- Check XML syntax in menubar.xml
- Ensure the build completed successfully
- Verify the menu ID uses the correct format (`.uno:CommandName`)
- Check that the command is registered in the appropriate shell files

## File Summary

Files you need to modify:
1. `sw/inc/cmdid.h` - Command ID definition
2. `sw/uiconfig/swriter/menubar/menubar.xml` - Menu structure
3. `sw/sdi/swriter.sdi` - Slot definition
4. `sw/sdi/_viewsh.sdi` - View shell command registration
5. `sw/sdi/_textsh.sdi` - Text shell command registration (for Tools menu)
6. `sw/source/uibase/uiview/view2.cxx` - Execute handler
7. `sw/source/uibase/uiview/viewstat.cxx` - GetState handler
8. `sw/source/uibase/shells/textsh1.cxx` - Text shell Execute handler
9. `officecfg/registry/data/org/openoffice/Office/UI/WriterCommands.xcu` - Command labels

## Key Points to Remember

1. **Shell Registration is Critical**: Commands must be registered in the appropriate shell interface files (_viewsh.sdi, _textsh.sdi) or they won't work properly.

2. **Properties Attribute**: The `Properties` attribute in WriterCommands.xcu is required for labels to display correctly.

3. **Build Order Matters**: Always run `make postprocess` after changing configuration files.

4. **Tools Menu Items**: Items in the Tools menu need handlers in both view2.cxx and textsh1.cxx.

5. **Clean Profile Testing**: When debugging label issues, test with a clean user profile to eliminate caching problems.

## Notes

- The `~` character in menu labels (e.g., `~Tools`) indicates the keyboard accelerator
- Toggle menu items should use `SfxBoolItem` in their slot definitions
- The `GroupId` in slot definitions affects where the command appears in customization dialogs
- Always check existing patterns in the codebase for consistency
- The UNO command name (e.g., `.uno:TextToSpeech`) must match between menubar.xml and WriterCommands.xcu