#!/usr/bin/env python3
"""Test the bidi reordering algorithm (Python version of C++ code)"""

import unicodedata

def is_rtl_char(c):
    """Check if character has RTL direction"""
    bidi = unicodedata.bidirectional(c)
    return bidi in ('R', 'AL')  # Right-to-Left, Arabic Letter

def is_strong_ltr(c):
    bidi = unicodedata.bidirectional(c)
    return bidi == 'L'

def has_rtl_chars(s):
    return any(is_rtl_char(c) for c in s)

def is_primary_rtl(s):
    for c in s:
        bidi = unicodedata.bidirectional(c)
        if bidi in ('R', 'AL'):
            return True
        if bidi == 'L':
            return False
    return False

def reorder_visual_to_logical(visual):
    """Reorder from visual to logical order"""
    if len(visual) <= 1 or not has_rtl_chars(visual):
        return visual, list(range(len(visual)))
    
    primary_rtl = is_primary_rtl(visual)
    result = []
    mapping = []
    n = len(visual)
    
    if not primary_rtl:
        i = 0
        while i < n:
            while i < n and not is_rtl_char(visual[i]):
                result.append(visual[i])
                mapping.append(i)
                i += 1
            j = i
            while j < n and is_rtl_char(visual[j]):
                j += 1
            if j > i:
                for k in range(j - 1, i - 1, -1):
                    result.append(visual[k])
                    mapping.append(k)
                i = j
    else:
        i = n - 1
        while i >= 0:
            while i >= 0 and is_rtl_char(visual[i]):
                result.append(visual[i])
                mapping.append(i)
                i -= 1
            j = i
            while j >= 0 and not is_rtl_char(visual[j]):
                j -= 1
            if j < i:
                for k in range(j + 1, i + 1):
                    result.append(visual[k])
                    mapping.append(k)
                i = j
    
    return ''.join(result), mapping


def run_tests():
    tests = [
        # (name, visual_input, expected_logical, expected_mapping)
        ("English LTR", "Hello World", "Hello World", [0,1,2,3,4,5,6,7,8,9,10]),
        
        # Persian word "ketab" (کتاب): in PDF visual order = ب ا ت ک
        # Logical order = ک ت ا ب
        ("Persian word", "باتک", "کتاب", None),
        
        # Persian "salam" (سلام): in PDF visual = م ا ل س
        # Logical = س ل ا م
        ("Persian salam", "مالس", "سلام", None),
        
        # Mixed: "Hello سلام"
        # Visual: H e l l o _ س ل ا م
        # Logical: Hello _ م ا ل س
        ("Mixed EN+FA", "Hello سلام", "Hello م\u200fا\u200fل\u200fس", None),
        
        # Hebrew "shalom" (שלום): visual = ם ו ל ש
        # Logical = ש ל ו ם
        ("Hebrew shalom", "םולש", "שלום", None),
        
        # Single char
        ("Single RTL", "س", "س", [0]),
        
        # Empty
        ("Empty", "", "", []),
    ]
    
    passed = 0
    failed = 0
    
    for name, visual, expected, expected_map in tests:
        result, mapping = reorder_visual_to_logical(visual)
        
        # For mixed text, strip control chars for comparison
        clean_result = result.replace('\u200f', '').replace('\u200e', '')
        clean_expected = expected.replace('\u200f', '').replace('\u200e', '')
        
        ok = (clean_result == clean_expected)
        if ok:
            passed += 1
            print(f"PASS: {name}")
            print(f"  Visual:  {visual!r} -> Logical: {result!r}")
            print(f"  Mapping: {mapping}")
        else:
            failed += 1
            print(f"FAIL: {name}")
            print(f"  Visual:    {visual!r}")
            print(f"  Visual hex: {' '.join(f'U+{ord(c):04X}' for c in visual)}")
            print(f"  Expected:  {expected!r}")
            print(f"  Got:       {result!r}")
            print(f"  Got hex:   {' '.join(f'U+{ord(c):04X}' for c in result)}")
            print(f"  Mapping:   {mapping}")
        print()
    
    print(f"{'='*50}")
    print(f"Results: {passed}/{passed+failed} passed")
    return failed == 0


if __name__ == "__main__":
    success = run_tests()
    exit(0 if success else 1)
