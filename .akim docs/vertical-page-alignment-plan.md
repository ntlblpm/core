# Vertical Page Alignment Implementation Plan for LibreOffice Writer

## Overview

This plan outlines the implementation of vertical page alignment in LibreOffice Writer, a feature where the backend support exists but the UI has never been implemented.

## Current State Analysis

### Backend Infrastructure (Already Implemented)

- **SwPageDesc** class has `m_nVerticalAdjustment` member (drawing::TextVerticalAdjust)
- Getter/setter methods: `GetVerticalAdjustment()` / `SetVerticalAdjustment()`
- UNO API property "TextVerticalAdjust" is fully mapped
- Import/export works with MS Word documents
- Supports values: TOP (default), CENTER, BOTTOM, BLOCK

### Missing Components

1. No UI controls in Page Style dialog
2. No SfxItem for page vertical adjustment
3. Layout engine doesn't apply vertical alignment to page content

## Implementation Steps

### Phase 1: Create Infrastructure Items

1. **Define new SfxItem class** for page vertical adjustment
   - Create `SwFormatPageVertAdjust` class (similar to existing vertical orient items)
   - Add to sw/inc/format.hxx
   - Register with RES_PAGE_VERT_ADJUST in hintids.hxx

2. **Connect to SwPageDesc**
   - Add item to page format attribute set
   - Implement get/set methods that sync with m_nVerticalAdjustment

### Phase 2: Add UI Controls

1. **Modify Page Format Dialog** (cui/uiconfig/ui/pageformatpage.ui)
   - Add "Vertical alignment" label and ComboBox in Layout Settings section
   - Position after "Page numbers" before "Register-true"
   - Options: Top, Center, Bottom, Justify

2. **Update Dialog Code** (cui/source/tabpages/page.cxx)
   - Add ComboBox member variable
   - Implement in `FillItemSet()` to save selection
   - Implement in `Reset()` to load current value
   - Add item handling for RES_PAGE_VERT_ADJUST

### Phase 3: Implement Layout Engine Support

1. **Modify SwBodyFrame::Format()** (sw/source/core/layout/pagechg.cxx)
   - Calculate total content height of body
   - Get vertical adjustment from page descriptor
   - Apply vertical offset based on alignment:
     - TOP: No change (current behavior)
     - CENTER: offset = (available_height - content_height) / 2
     - BOTTOM: offset = available_height - content_height
     - BLOCK: Distribute space between paragraphs

2. **Handle Dynamic Updates**
   - Trigger relayout when vertical alignment changes
   - Account for headers/footers reducing available space
   - Handle multi-column layouts appropriately

### Phase 4: Testing and Edge Cases

1. **Create unit tests**
   - Test each alignment mode
   - Test with/without headers and footers
   - Test with different page sizes
   - Test import/export preservation

2. **Handle edge cases**
   - Content taller than page
   - Empty pages
   - Mixed page orientations
   - Footnotes and endnotes

### Phase 5: Documentation

1. Update help documentation
2. Add UI tooltips
3. Document any limitations

## Technical Challenges to Address

1. **Performance Impact**
   - Calculating content height requires full layout pass
   - May need caching mechanism

2. **Interaction with Other Features**
   - Register-true (baseline grid)
   - Column balancing
   - Widow/orphan control

3. **WYSIWYG Accuracy**
   - Ensure screen display matches print output
   - Handle zoom levels correctly

## Files to Modify

- sw/inc/format.hxx (new item class)
- sw/inc/hintids.hxx (item ID)
- cui/uiconfig/ui/pageformatpage.ui (dialog UI)
- cui/source/tabpages/page.cxx (dialog logic)
- sw/source/core/layout/pagechg.cxx (layout engine)
- sw/source/core/layout/wsfrm.cxx (frame updates)

## References

- Bug tdf#36117 - Vertical alignment of pages (Writer)
- SwPageDesc class: sw/inc/pagedesc.hxx:166
- SwBodyFrame::Format: sw/source/core/layout/pagechg.cxx:162
- Page dialog: cui/source/tabpages/page.cxx
