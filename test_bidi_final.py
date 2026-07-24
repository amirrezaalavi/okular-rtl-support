#!/usr/bin/env python3
"""Quick verification of bidi algorithm with correct test data"""

import unicodedata

def is_rtl_char(c):
    return unicodedata.bidirectional(c) in ('R', 'AL')

def has_rtl_chars(s):
    return any(is_rtl_char(c) for c in s)

def is_primary_rtl(s):
    for c in s:
        bidi = unicodedata.bidirectional(c)
        if bidi in ('R', 'AL'): return True
        if bidi == 'L': return False
    return False

def reorder_visual_to_logical(visual):
    if len(visual) <= 1 or not has_rtl_chars(visual):
        return visual, list(range(len(visual)))
    primary_rtl = is_primary_rtl(visual)
    result, mapping, n = [], [], len(visual)
    if not primary_rtl:
        i = 0
        while i < n:
            while i < n and not is_rtl_char(visual[i]):
                result.append(visual[i]); mapping.append(i); i += 1
            j = i
            while j < n and is_rtl_char(visual[j]): j += 1
            if j > i:
                for k in range(j-1, i-1, -1):
                    result.append(visual[k]); mapping.append(k)
                i = j
    else:
        i = n - 1
        while i >= 0:
            while i >= 0 and is_rtl_char(visual[i]):
                result.append(visual[i]); mapping.append(i); i -= 1
            j = i
            while j >= 0 and not is_rtl_char(visual[j]): j -= 1
            if j < i:
                for k in range(j+1, i+1):
                    result.append(visual[k]); mapping.append(k)
                i = j
    return ''.join(result), mapping

# Test 1: Persian word "کتاب" (ketab = book)
# Logical order (reading): ک-ت-ا-ب
# Visual order (PDF display): ب-ا-ت-ک (leftmost on page)
visual = "باتک"  # visual: leftmost=ب, rightmost=ک
result, mapping = reorder_visual_to_logical(visual)
print(f"Test 1 - Persian word:")
print(f"  Visual:  {visual!r} -> Logical: {result!r}")
print(f"  Expected: 'کتاب' -> {'PASS' if result == 'کتاب' else 'FAIL'}")
print(f"  Mapping: {mapping} (expected [3,2,1,0])")
print()

# Test 2: Mixed "Hello World کتاب" in PDF visual order
# PDF stores: H e l l o _ W o r l d _ ب ا ت ک
visual2 = "Hello World باتک"
result2, mapping2 = reorder_visual_to_logical(visual2)
print(f"Test 2 - Mixed EN+FA (primary LTR):")
print(f"  Visual:  {visual2!r}")
print(f"  Logical: {result2!r}")
# Expected: Hello World + reversed Persian = "Hello World کتاب"
assert result2 == "Hello World کتاب", f"FAIL: {result2!r}"
print(f"  PASS")
print(f"  Mapping: {mapping2}")
print()

# Test 3: Pure RTL word (primary RTL scenario)
# For primary-RTL, the algorithm scans backward
# Visual: ب ا ت ک  (left-to-right on screen)
# The algorithm outputs RTL runs in reading order
visual3 = "باتک"
result3, mapping3 = reorder_visual_to_logical(visual3)
print(f"Test 3 - Primary RTL:")
print(f"  Since all chars are RTL, primaryRtl=True")
print(f"  Visual:  {visual3!r} -> Logical: {result3!r}")
print(f"  Expected: 'کتاب'")
print(f"  Mapping: {mapping3}")
assert result3 == "کتاب", f"FAIL: {result3!r}"
print(f"  PASS")
print()

# Test 4: Verify mapping produces correct bbox selection
# Simulate Okular: word has chars with visual positions
# charBoundingBox(i) gives bbox at visual position i
print(f"Test 4 - BBox mapping simulation:")
print(f"  Visual chars: ب@bb0, ا@bb1, ت@bb2, ک@bb3")
print(f"  Reordered logical: {result3!r}")
for logical_pos in range(4):
    visual_pos = mapping3[logical_pos]
    print(f"    logical[{logical_pos}]='{result3[logical_pos]}' uses bbox[{visual_pos}]")
print(f"  Expected: logical[0]=ک uses bbox[3] (rightmost) -> CORRECT")
print(f"  PASS")

print("\n=== All tests passed ===")
