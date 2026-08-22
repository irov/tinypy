import _codecs


def custom_error(error):
    assert isinstance(error, (UnicodeEncodeError, UnicodeDecodeError))
    assert error.encoding == "ascii"
    assert error.start == 1
    assert error.end == 2
    return (u"<X>", error.end)


key_error = KeyError("missing")
assert str(key_error) == "'missing'"

environment_error = EnvironmentError(2, "bad", "path")
assert environment_error.args == (2, "bad")
assert environment_error.errno == 2
assert environment_error.strerror == "bad"
assert environment_error.filename == "path"
assert str(environment_error) == "[Errno 2] bad: 'path'"

system_exit = SystemExit(1, 2)
assert system_exit.args == (1, 2)
assert system_exit.code == (1, 2)

syntax_error = SyntaxError("bad syntax", ("input.py", 3, 4, "bad input"))
assert syntax_error.msg == "bad syntax"
assert syntax_error.filename == "input.py"
assert syntax_error.lineno == 3
assert syntax_error.offset == 4
assert syntax_error.text == "bad input"
assert str(syntax_error) == "bad syntax (input.py, line 3)"

encode_error = UnicodeEncodeError("ascii", u"a\u20ac", 1, 2, "ordinal not in range(128)")
assert encode_error.encoding == "ascii"
assert encode_error.object == u"a\u20ac"
assert encode_error.start == 1
assert encode_error.end == 2
assert encode_error.reason == "ordinal not in range(128)"
assert str(encode_error) == "'ascii' codec can't encode character u'\\u20ac' in position 1: ordinal not in range(128)"

decode_error = UnicodeDecodeError("ascii", "a\xff", 1, 2, "ordinal not in range(128)")
assert decode_error.encoding == "ascii"
assert decode_error.object == "a\xff"
assert decode_error.start == 1
assert decode_error.end == 2
assert decode_error.reason == "ordinal not in range(128)"
assert str(decode_error) == "'ascii' codec can't decode byte 0xff in position 1: ordinal not in range(128)"

translate_error = UnicodeTranslateError(u"a\u20ac", 1, 2, "bad")
assert translate_error.object == u"a\u20ac"
assert translate_error.start == 1
assert translate_error.end == 2
assert translate_error.reason == "bad"
assert str(translate_error) == "can't translate character u'\\u20ac' in position 1: bad"

_codecs.register_error("tinypy_test", custom_error)
assert _codecs.lookup_error("tinypy_test") is custom_error
assert _codecs.lookup_error("ignore")(encode_error) == (u"", 2)
assert _codecs.lookup_error("replace")(encode_error) == (u"?", 2)
assert _codecs.lookup_error("replace")(decode_error) == (u"\ufffd", 2)
assert _codecs.lookup_error("replace")(translate_error) == (u"\ufffd", 2)
assert _codecs.lookup_error("xmlcharrefreplace")(encode_error) == (u"&#8364;", 2)
assert _codecs.lookup_error("backslashreplace")(encode_error) == (u"\\u20ac", 2)
assert u"a\u20acb".encode("ascii", "tinypy_test") == "a<X>b"
assert "a\xffb".decode("ascii", "tinypy_test") == u"a<X>b"
assert u"a\u20ac".encode("ascii", "xmlcharrefreplace") == "a&#8364;"
assert u"a\u20ac".encode("ascii", "backslashreplace") == "a\\u20ac"
assert "ascii".decode("ascii", "STRICT") == u"ascii"
assert "ascii".decode("us-ascii") == u"ascii"
assert u"caf\xe9".encode("iso-8859-1") == "caf\xe9"
assert u"caf\xe9".encode("u8") == "caf\xc3\xa9"
try:
    "\xff".decode("ascii", "STRICT")
except LookupError:
    pass
else:
    raise AssertionError("codec error handler names were not case-sensitive")

try:
    u"a\u20ac".encode("ascii")
except UnicodeEncodeError as error:
    assert error.encoding == "ascii"
    assert error.object == u"a\u20ac"
    assert error.start == 1
    assert error.end == 2
else:
    assert False

try:
    "a\xff".decode("ascii")
except UnicodeDecodeError as error:
    assert error.encoding == "ascii"
    assert error.object == "a\xff"
    assert error.start == 1
    assert error.end == 2
else:
    assert False


def codec_encode(value, errors="strict"):
    return ("encoded:" + value, len(value))


def codec_decode(value, errors="strict"):
    return (u"decoded:" + value, len(value))


def codec_search(name):
    if name in ("tinypy-test-codec", "tinypy_test_codec"):
        return (codec_encode, codec_decode, None, None)
    return None


_codecs.register(codec_search)
ascii_codec = _codecs.lookup("ASCII")
assert len(ascii_codec) == 4
assert ascii_codec[0] is _codecs.ascii_encode
assert ascii_codec[1] is _codecs.ascii_decode
assert "value".encode("tinypy-test-codec") == "encoded:value"
assert "value".decode("tinypy-test-codec") == u"decoded:value"
assert _codecs.encode("value", "tinypy-test-codec") == "encoded:value"
assert _codecs.decode("value", "tinypy-test-codec") == u"decoded:value"
assert _codecs.encode(u"caf\xe9", "ascii", "replace") == "caf?"
assert _codecs.decode("caf\xe9", "latin-1") == u"caf\xe9"


def arbitrary_codec_encode(value, errors="strict"):
    return (7, len(value))


def invalid_result_codec_encode(value, errors="strict"):
    return ("invalid",)


def result_codec_search(name):
    if name == "tinypy-arbitrary-output":
        return (arbitrary_codec_encode, codec_decode, None, None)
    if name == "tinypy-invalid-result":
        return (invalid_result_codec_encode, codec_decode, None, None)
    return None


_codecs.register(result_codec_search)
assert _codecs.encode("value", "tinypy-arbitrary-output") == 7
try:
    "value".encode("tinypy-arbitrary-output")
except TypeError:
    pass
else:
    raise AssertionError("str.encode accepted a non-string codec result")
try:
    _codecs.encode("value", "tinypy-invalid-result")
except TypeError:
    pass
else:
    raise AssertionError("codec accepted a result tuple with the wrong size")


def invalid_codec_search(name):
    if name == "tinypy-invalid-codec":
        return (codec_encode,)
    return None


_codecs.register(invalid_codec_search)
try:
    _codecs.lookup("TINYPY INVALID-CODEC")
except TypeError:
    pass
else:
    raise AssertionError("codec registry accepted a non-four-tuple result")
