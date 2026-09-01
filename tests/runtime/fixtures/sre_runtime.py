import _sre


def compile_pattern(pattern, flags, code, groups=0):
    return _sre.compile(pattern, flags, code, groups, {}, [None] * (groups + 1))


assert _sre.getlower(0x41, 0) == 0x61
assert _sre.getlower(0x410, 0) == 0x410
assert _sre.getlower(0x410, 32) == 0x430
assert _sre.getlower(0x410, 36) == 0x410

cyrillic = compile_pattern(
    u"\u0410+",
    0,
    [17, 4, 0, 1, 0, 29, 6, 1, 4294967295L, 19, 1040, 1, 1],
)
cyrillic_text = u"x\u0410\u0410y"
cyrillic_match = cyrillic.search(cyrillic_text)
assert cyrillic_match.group() == u"\u0410\u0410"
assert cyrillic_match.span() == (1, 3)
assert cyrillic_match.pos == 0
assert cyrillic_match.endpos == 4
assert cyrillic.search(cyrillic_text, 2, 3).span() == (2, 3)
assert cyrillic.findall(cyrillic_text) == [u"\u0410\u0410"]
assert cyrillic.sub(u"\u0411", cyrillic_text) == u"x\u0411y"

supplementary = compile_pattern(
    u"\U0001f600+",
    0,
    [17, 4, 0, 1, 0, 29, 6, 1, 4294967295L, 19, 128512, 1, 1],
)
supplementary_match = supplementary.search(u"x\U0001f600\U0001f600y")
assert supplementary_match.group() == u"\U0001f600\U0001f600"
assert supplementary_match.span() == (1, 3)

unicode_word = compile_pattern(
    ur"\w+",
    32,
    [17, 4, 0, 1, 0, 29, 9, 1, 4294967295L, 15, 4, 9, 14, 0, 1, 1],
)
unicode_word_match = unicode_word.search(u"-\u0410\xb2\u2160-")
assert unicode_word_match.group() == u"\u0410\xb2\u2160"
assert unicode_word_match.span() == (1, 4)

unicode_digit = compile_pattern(
    ur"\d+",
    32,
    [17, 4, 0, 1, 0, 29, 9, 1, 4294967295L, 15, 4, 9, 10, 0, 1, 1],
)
assert unicode_digit.search(u"x\u0660\u0661y").group() == u"\u0660\u0661"
assert unicode_digit.search(u"x\xb2y") is None

unicode_space = compile_pattern(
    ur"\s+",
    32,
    [17, 4, 0, 1, 0, 29, 9, 1, 4294967295L, 15, 4, 9, 12, 0, 1, 1],
)
assert unicode_space.search(u"x\u2028\x85y").group() == u"\u2028\x85"

unicode_boundary = compile_pattern(
    ur"\b\u0410+\b",
    32,
    [17, 4, 0, 1, 0, 6, 10, 29, 6, 1, 4294967295L, 19, 1040, 1, 6, 10, 1],
)
assert unicode_boundary.search(u"-\u0410\u0410-").span() == (1, 3)

unicode_ignorecase = compile_pattern(
    u"\u0410+",
    34,
    [17, 4, 0, 1, 0, 29, 6, 1, 4294967295L, 20, 1072, 1, 1],
)
assert unicode_ignorecase.search(u"x\u0410\u0430y").group() == u"\u0410\u0430"

unicode_groupref = compile_pattern(
    u"(\u0410)\\1",
    0,
    [17, 8, 1, 1, 1, 1, 0, 1040, 0, 21, 0, 19, 1040, 21, 1, 12, 0, 1],
    1,
)
unicode_groupref_match = unicode_groupref.search(u"x\u0410\u0410y")
assert unicode_groupref_match.span() == (1, 3)
assert unicode_groupref_match.group(1) == u"\u0410"

unicode_lookbehind = compile_pattern(
    u"(?<=x)\u0410",
    0,
    [17, 4, 0, 1, 1, 4, 5, 1, 19, 120, 1, 19, 1040, 1],
)
assert unicode_lookbehind.search(u"\u0410x\u0410").span() == (2, 3)
