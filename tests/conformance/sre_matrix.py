"""Bounded valid SRE patterns, subjects, methods, bounds and update counts.

Programs were generated from these project-authored patterns by CPython
2.7.18's regex compiler; no malformed SRE program is included.
"""

import _sre

PATTERNS = [
    ('', 0, [17, 4, 0, 0, 0, 1], 0, {}, [None]),
    ('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1], 0, {}, [None]),
    ('a', 2, [17, 4, 0, 1, 1, 20, 97, 1], 0, {}, [None]),
    ('a*', 0, [29, 6, 0, 4294967295L, 19, 97, 1, 1], 0, {}, [None]),
    ('a+?', 0, [17, 4, 0, 1, 0, 31, 6, 1, 4294967295L, 19, 97, 1, 1], 0, {}, [None]),
    ('(a)?', 0, [28, 9, 0, 1, 21, 0, 19, 97, 21, 1, 22, 1], 1, {}, [None, None]),
    ('(a)(b)?', 0, [17, 8, 1, 1, 2, 1, 0, 97, 0, 21, 0, 19, 97, 21, 1, 28, 9, 0, 1, 21, 2, 19, 98, 21, 3, 22, 1], 2, {}, [None, None, None]),
    ('(?P<word>a+)', 0, [17, 4, 0, 1, 0, 21, 0, 29, 6, 1, 4294967295L, 19, 97, 1, 21, 1, 1], 1, {'word': 1}, [None, 'word']),
    ('(a|b)+', 0, [17, 4, 0, 1, 0, 28, 13, 1, 4294967295L, 21, 0, 15, 5, 27, 97, 98, 0, 21, 1, 22, 1], 1, {}, [None, None]),
    ('a(?=b)', 0, [17, 8, 1, 1, 1, 1, 1, 97, 0, 19, 97, 4, 5, 0, 19, 98, 1, 1], 0, {}, [None]),
    ('(?<=a)b', 0, [17, 4, 0, 1, 1, 4, 5, 1, 19, 97, 1, 19, 98, 1], 0, {}, [None]),
    ('a(?!b)', 0, [17, 8, 1, 1, 1, 1, 1, 97, 0, 19, 97, 5, 5, 0, 19, 98, 1, 1], 0, {}, [None]),
    ('^a$', 0, [17, 4, 0, 1, 1, 6, 0, 19, 97, 6, 5, 1], 0, {}, [None]),
    ('^a$', 8, [17, 4, 0, 1, 1, 6, 1, 19, 97, 6, 6, 1], 0, {}, [None]),
    ('[ab]+', 0, [17, 4, 0, 1, 0, 29, 10, 1, 4294967295L, 15, 5, 27, 97, 98, 0, 1, 1], 0, {}, [None]),
    ('[^a]+', 0, [17, 4, 0, 1, 0, 29, 6, 1, 4294967295L, 24, 97, 1, 1], 0, {}, [None]),
    ('.', 0, [17, 4, 0, 1, 1, 2, 1], 0, {}, [None]),
    ('.', 16, [17, 4, 0, 1, 1, 3, 1], 0, {}, [None]),
    ('\\w+', 0, [17, 4, 0, 1, 0, 29, 9, 1, 4294967295L, 15, 4, 9, 4, 0, 1, 1], 0, {}, [None]),
    ('\\w+', 32, [17, 4, 0, 1, 0, 29, 9, 1, 4294967295L, 15, 4, 9, 14, 0, 1, 1], 0, {}, [None]),
    ('\\d+', 32, [17, 4, 0, 1, 0, 29, 9, 1, 4294967295L, 15, 4, 9, 10, 0, 1, 1], 0, {}, [None]),
    ('\\ba\\b', 32, [17, 4, 0, 1, 1, 6, 10, 19, 97, 6, 10, 1], 0, {}, [None]),
    (u'\u0410+', 34, [17, 4, 0, 1, 0, 29, 6, 1, 4294967295L, 20, 1072, 1, 1], 0, {}, [None]),
    (u'(\U0001f600)+', 0, [17, 4, 0, 1, 0, 28, 9, 1, 4294967295L, 21, 0, 19, 128512, 21, 1, 22, 1], 1, {}, [None, None]),
    ('(ab|c)+', 0, [17, 4, 0, 1, 0, 28, 21, 1, 4294967295L, 21, 0, 7, 7, 19, 97, 19, 98, 18, 7, 5, 19, 99, 18, 2, 0, 21, 1, 22, 1], 1, {}, [None, None]),
    (r'(a)\1', 0, [17, 8, 1, 1, 1, 1, 0, 97, 0, 21, 0, 19, 97, 21, 1, 12, 0, 1], 1, {}, [None, None]),
    ('(a)?(?(1)b|c)', 0, [28, 9, 0, 1, 21, 0, 19, 97, 21, 1, 22, 13, 0, 6, 19, 98, 18, 3, 19, 99, 1], 1, {}, [None, None]),
    ('(ab)+?', 0, [17, 4, 0, 2, 0, 28, 11, 1, 4294967295L, 21, 0, 19, 97, 19, 98, 21, 1, 23, 1], 1, {}, [None, None]),
    ('(?<!a)b', 0, [17, 4, 0, 1, 1, 5, 5, 1, 19, 97, 1, 19, 98, 1], 0, {}, [None]),
    (r'([ab])\1', 2, [17, 4, 0, 1, 1, 21, 0, 16, 5, 27, 97, 98, 0, 21, 1, 14, 0, 1], 1, {}, [None, None]),
    ('a{1,3}', 0, [17, 4, 0, 1, 3, 29, 6, 1, 3, 19, 97, 1, 1], 0, {}, [None]),
    ('[acf][0-9]+', 0, [17, 14, 4, 2, 0, 10, 0, 0, 0, 74, 0, 0, 0, 0, 0, 15, 11, 10, 0, 0, 0, 74, 0, 0, 0, 0, 0, 29, 10, 1, 4294967295L, 15, 5, 27, 48, 57, 0, 1, 1], 0, {}, [None]),
]


def summarize_match(found):
    if found is None:
        return None
    group = found.group()
    return (type(group).__name__, group, found.groups(),
            sorted(found.groupdict().items()), found.regs, found.pos,
            found.endpos, found.lastindex, found.lastgroup)


def observe(identity, operation):
    try:
        result = ('value', operation())
    except Exception as error:
        result = ('error', type(error).__name__)
    print identity + '\t' + repr(result)


texts = ['', 'a', 'aa', 'ab', 'aba', 'baA', 'a\nb', ' x 12\t', '\x00a', '-', 'abac', 'c13']
subjects = []
for text in texts:
    subjects.extend([text, unicode(text), bytearray(text), buffer(text)])
subjects.extend([u'\u0416\u0410\u0430', u'x\U0001f600\U0001f600y'])
bounds = [(-2, -1), (-2, 0), (-2, 5), (0, 0), (0, 2), (0, 8),
          (1, 0), (1, 1), (1, 5), (2, 1), (2, 8), (8, 1), (8, 8)]
counts = [-2, -1, 0, 1, 2, 5, 2**80]


for pattern_index, parameters in enumerate(PATTERNS):
    pattern = _sre.compile(*parameters)
    for source_index, source in enumerate(subjects):
        for bound_index, (pos, endpos) in enumerate(bounds):
            identity = 'sre/%d/%d/%d/' % (pattern_index, source_index, bound_index)
            observe(identity + 'match', lambda: summarize_match(pattern.match(source, pos, endpos)))
            observe(identity + 'search', lambda: summarize_match(pattern.search(source, pos, endpos)))
            observe(identity + 'findall', lambda: pattern.findall(source, pos, endpos))
            observe(identity + 'finditer', lambda: [summarize_match(found) for found in pattern.finditer(source, pos, endpos)])
            observe(identity + 'scanner', lambda: summarize_match(pattern.scanner(source, pos, endpos).search()))
        for count_index, count in enumerate(counts):
            identity = 'sre-update/%d/%d/%d/' % (pattern_index, source_index, count_index)
            observe(identity + 'sub', lambda: pattern.sub('X', source, count))
            observe(identity + 'unicode-sub', lambda: pattern.sub(u'X', source, count))
            observe(identity + 'call-sub', lambda: pattern.sub(lambda found: 'X', source, count))
            observe(identity + 'none-sub', lambda: pattern.sub(lambda found: None, source, count))
            observe(identity + 'subn', lambda: pattern.subn('X', source, count))
            observe(identity + 'split', lambda: pattern.split(source, count))
