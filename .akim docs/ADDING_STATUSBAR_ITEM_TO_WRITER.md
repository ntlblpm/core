# How to Add a Status Bar Item to LibreOffice Writer

This guide documents the step-by-step process of adding a new status bar item to Writer, using a timer example that counts up in seconds.

## Overview

Adding a status bar item requires changes to multiple files across different modules. The process involves:

1. Defining a command ID
2. Creating a status bar control class
3. Registering the control
4. Adding UNO command mappings
5. Configuring the UI

## Step-by-Step Implementation

### Step 1: Define the Command ID

Add a new command ID to `sw/inc/cmdid.h`. This ID will be used throughout the codebase to identify your status bar item.

```cpp
// sw/inc/cmdid.h (around line 891)
#define FN_STAT_TIMER               TypedWhichId<SfxStringItem>(FN_STAT + 11)
```

### Step 2: Create the Status Bar Control Class

#### 2.1 Create the header file

Create `sw/source/uibase/inc/timerctrl.hxx`:

```cpp
#pragma once

#include <sfx2/stbitem.hxx>
#include <vcl/timer.hxx>
#include <chrono>

class SwTimerStatusBarControl final : public SfxStatusBarControl
{
private:
    Timer m_aTimer;
    std::chrono::steady_clock::time_point m_aStartTime;
    
    DECL_LINK(TimerHdl, Timer*, void);
    
public:
    SFX_DECL_STATUSBAR_CONTROL();
    
    SwTimerStatusBarControl(sal_uInt16 nSlotId, sal_uInt16 nId, StatusBar& rStb);
    virtual ~SwTimerStatusBarControl() override;
    
    virtual void StateChangedAtStatusBarControl(sal_uInt16 nSID, SfxItemState eState,
                                               const SfxPoolItem* pState) override;
};
```

#### 2.2 Create the implementation file

Create `sw/source/uibase/utlui/timerctrl.cxx`:

```cpp
#include <timerctrl.hxx>
#include <svl/stritem.hxx>
#include <vcl/status.hxx>
#include <rtl/ustrbuf.hxx>

SFX_IMPL_STATUSBAR_CONTROL(SwTimerStatusBarControl, SfxStringItem);

SwTimerStatusBarControl::SwTimerStatusBarControl(
    sal_uInt16 nSlotId, sal_uInt16 nId, StatusBar& rStb)
    : SfxStatusBarControl(nSlotId, nId, rStb)
    , m_aTimer("SwTimerStatusBarControl Timer")
{
    m_aStartTime = std::chrono::steady_clock::now();
    m_aTimer.SetInvokeHandler(LINK(this, SwTimerStatusBarControl, TimerHdl));
    m_aTimer.SetTimeout(1000); // Update every second
    m_aTimer.Start();
    
    // Set initial text
    GetStatusBar().SetItemText(GetId(), "Timer: 00:00:00");
}

SwTimerStatusBarControl::~SwTimerStatusBarControl()
{
    m_aTimer.Stop();
}

IMPL_LINK_NOARG(SwTimerStatusBarControl, TimerHdl, Timer*, void)
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_aStartTime);
    
    int hours = elapsed.count() / 3600;
    int minutes = (elapsed.count() % 3600) / 60;
    int seconds = elapsed.count() % 60;
    
    OUStringBuffer aText("Timer: ");
    if (hours < 10) aText.append("0");
    aText.append(OUString::number(hours));
    aText.append(":");
    if (minutes < 10) aText.append("0");
    aText.append(OUString::number(minutes));
    aText.append(":");
    if (seconds < 10) aText.append("0");
    aText.append(OUString::number(seconds));
    
    GetStatusBar().SetItemText(GetId(), aText.makeStringAndClear());
    
    // Keep the timer running
    m_aTimer.Start();
}

void SwTimerStatusBarControl::StateChangedAtStatusBarControl(
    sal_uInt16, SfxItemState eState, const SfxPoolItem* pState)
{
    // Timer manages its own display
}
```

### Step 3: Add to Build System

Add the new source file to `sw/Library_sw.mk` (around line 806):

```makefile
    sw/source/uibase/utlui/wordcountctrl \
    sw/source/uibase/utlui/timerctrl \
    sw/source/uibase/utlui/zoomctrl \
```

### Step 4: Register the Control

#### 4.1 Include the header

Add to `sw/source/uibase/app/swmodule.cxx` (around line 85):

```cpp
#include <wordcountctrl.hxx>
#include <timerctrl.hxx>
```

#### 4.2 Register the control

In the same file (around line 296):

```cpp
SwWordCountStatusBarControl::RegisterControl(FN_STAT_WORDCOUNT, pMod);
sw::AccessibilityStatusBarControl::RegisterControl(FN_STAT_ACCESSIBILITY_CHECK, pMod);
SwTimerStatusBarControl::RegisterControl(FN_STAT_TIMER, pMod);
```

### Step 5: Add State Handling

In `sw/source/uibase/uiview/view2.cxx`, add a case in `StateStatusLine()` (around line 1947):

```cpp
case FN_STAT_TIMER:
{
    // Timer control manages its own state, just provide an empty string
    // The actual timer display is handled by the control itself
    rSet.Put( SfxStringItem( FN_STAT_TIMER, OUString() ) );
}
break;
```

### Step 6: Configure SDI (Slot Definition Interface)

#### 6.1 Add to _viewsh.sdi

In `sw/sdi/_viewsh.sdi`, add (search for FN_STAT_WORDCOUNT for context):

```
FN_STAT_TIMER // status()
[
    ExecMethod = ExecuteStatusLine ;
    StateMethod = StateStatusLine ;
]
```

#### 6.2 Add to viewsh.sdi

In `sw/sdi/viewsh.sdi`, add:

```
FN_STAT_TIMER // status()
[
    ExecMethod = Execute ;
    StateMethod = GetState ;
]
```

#### 6.3 Add to swriter.sdi

In `sw/sdi/swriter.sdi` (around line 6267):

```
SfxStringItem StateTimer FN_STAT_TIMER

[
    AutoUpdate = FALSE,
    FastCall = FALSE,
    ReadOnlyDoc = TRUE,
    Toggle = FALSE,
    Container = FALSE,
    RecordAbsolute = FALSE,
    RecordPerSet;

    AccelConfig = FALSE,
    MenuConfig = FALSE,
    ToolBoxConfig = FALSE,
    GroupId = SfxGroupId::View;
]
```

### Step 7: Add UNO Command Mapping

In `sfx2/source/control/unoctitm.cxx`, find the `gCommandPayloadMap` and add:

```cpp
{ u"StateTimer", { PayloadType::StringPayload, true } },
```

### Step 8: Configure the UI

In `sw/uiconfig/swriter/statusbar/statusbar.xml`, add the status bar item (around line 24):

```xml
<statusbar:statusbaritem xlink:href=".uno:StateTimer" 
                        statusbar:align="center" 
                        statusbar:autosize="true" 
                        statusbar:mandatory="true" 
                        statusbar:width="120"/>
```

## Build and Test

After making all changes:

```bash
make sw.build sfx2.build
make debugrun gb_DBGARGS="--writer"
```

The timer should appear in the status bar showing "Timer: 00:00:00" and count up every second.

## Key Points to Remember

1. **Command ID**: Must be unique and follow the naming convention (FN_STAT_*)
2. **Control Class**: Must inherit from `SfxStatusBarControl` and implement required methods
3. **Registration**: Must be done in `swmodule.cxx` during module initialization
4. **SDI Files**: Required for proper command dispatch through the UNO framework
5. **UNO Mapping**: Required in `unoctitm.cxx` for the command to work with the UNO API
6. **XML Configuration**: Defines where and how the item appears in the status bar

## Troubleshooting

If your status bar item doesn't appear:

1. Check that all files are properly included in the build system
2. Verify the command ID is unique
3. Ensure all SDI entries are correct
4. Check that the UNO command mapping is added
5. Make sure `mandatory="true"` is set in the XML if you want it always visible
6. Clean and rebuild: `make sw.clean sw.build sfx2.build`

## Files Modified Summary

1. `sw/inc/cmdid.h` - Command ID definition
2. `sw/source/uibase/inc/timerctrl.hxx` - Control header
3. `sw/source/uibase/utlui/timerctrl.cxx` - Control implementation
4. `sw/Library_sw.mk` - Build system
5. `sw/source/uibase/app/swmodule.cxx` - Control registration
6. `sw/source/uibase/uiview/view2.cxx` - State handling
7. `sw/sdi/_viewsh.sdi` - SDI command mapping
8. `sw/sdi/viewsh.sdi` - SDI command mapping
9. `sw/sdi/swriter.sdi` - SDI command definition
10. `sfx2/source/control/unoctitm.cxx` - UNO payload mapping
11. `sw/uiconfig/swriter/statusbar/statusbar.xml` - UI configuration
